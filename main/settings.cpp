#include "settings.h"
#include "waste_window.h"

#include <stdio.h>
#include <string.h>

#include "config_load.h"
#include "info.h"
#include <ArduinoJson.h>
#include "esp_heap_caps.h"
#include "nvs.h"
#include "sdkconfig.h"
#include "settings_schema.h"
#include "sys.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace {
constexpr const char* kNs = "panel";
const char* const TAG = "settings";

// The single write path: every NVS write of namespace "panel" goes through a
// Tx. Values are checked against the schema first; only if all are valid they
// are written and committed under one mutex (UI and net task both save).
// Values must stay alive until commit(). Logs key names only, never values.
SemaphoreHandle_t write_mutex() {
  static StaticSemaphore_t buf;
  static SemaphoreHandle_t m = xSemaphoreCreateRecursiveMutexStatic(&buf);
  return m;
}

// Bulk writers that are validated elsewhere (restore, migration, build seed,
// test harness) take the same mutex for their whole run.
struct WriteLock {
  WriteLock() { xSemaphoreTakeRecursive(write_mutex(), portMAX_DELAY); }
  ~WriteLock() { xSemaphoreGiveRecursive(write_mutex()); }
};

class Tx {
 public:
  bool u8(const char* key, uint8_t v) { return add(key, Op::U8, &v, v); }
  bool u16(const char* key, uint16_t v) { return add(key, Op::U16, nullptr, v); }
  bool str(const char* key, const char* v) { return add(key, Op::Str, v, 0); }
  bool blob(const char* key, const void* v, size_t n) { return add(key, Op::Blob, v, n); }
  bool erase(const char* key) { return add(key, Op::Erase, nullptr, 0); }
  bool commit() {
    if (!ok_ || n_ == 0) return ok_;
    xSemaphoreTakeRecursive(write_mutex(), portMAX_DELAY);
    nvs_handle_t h;
    bool ok = nvs_open(kNs, NVS_READWRITE, &h) == ESP_OK;
    for (int i = 0; ok && i < n_; ++i) {
      const Item& it = items_[i];
      esp_err_t e = ESP_OK;
      switch (it.op) {
        case Op::U8: e = nvs_set_u8(h, it.key, static_cast<uint8_t>(it.num)); break;
        case Op::U16: e = nvs_set_u16(h, it.key, static_cast<uint16_t>(it.num)); break;
        case Op::Str: e = nvs_set_str(h, it.key, static_cast<const char*>(it.ptr)); break;
        case Op::Blob: e = nvs_set_blob(h, it.key, it.ptr, it.num); break;
        case Op::Erase:
          e = nvs_erase_key(h, it.key);
          if (e == ESP_ERR_NVS_NOT_FOUND) e = ESP_OK;
          break;
      }
      if (e != ESP_OK) {
        ESP_LOGE(TAG, "write %s: %s", it.key, esp_err_to_name(e));
        ok = false;
      }
    }
    if (ok) ok = nvs_commit(h) == ESP_OK;
    nvs_close(h);
    xSemaphoreGiveRecursive(write_mutex());
    return ok;
  }

 private:
  enum class Op : uint8_t { U8, U16, Str, Blob, Erase };
  struct Item {
    const char* key;
    Op op;
    const void* ptr;
    size_t num;
  };
  bool add(const char* key, Op op, const void* ptr, size_t num) {
    if (!ok_) return false;
    const KeySpec* ks = schema_find(key);
    const char* why = "nicht im Schema";
    bool valid = ks != nullptr && n_ < kMax;
    if (valid) {
      switch (op) {
        case Op::U8: valid = ks->type == KeyType::U8 && schema_check_int(*ks, static_cast<long long>(num), &why); ptr = nullptr; break;
        case Op::U16: valid = ks->type == KeyType::U16 && schema_check_int(*ks, static_cast<long long>(num), &why); break;
        case Op::Str: valid = ks->type == KeyType::Str && ptr != nullptr && schema_check_str(*ks, static_cast<const char*>(ptr), &why); break;
        case Op::Blob: valid = ks->type == KeyType::Blob && schema_check_blob(*ks, static_cast<const uint8_t*>(ptr), num, &why); break;
        case Op::Erase: break;
      }
    }
    if (!valid) {
      ESP_LOGW(TAG, "%s rejected: %s", key, why);
      ok_ = false;
      return false;
    }
    items_[n_++] = {key, op, ptr, num};
    return true;
  }
  static constexpr int kMax = 12;
  Item items_[kMax];
  int n_ = 0;
  bool ok_ = true;
};

bool get_str(nvs_handle_t h, const char* key, char* out, size_t n) {
  size_t len = n;
  return nvs_get_str(h, key, out, &len) == ESP_OK;
}
}  // namespace

Location settings_default_location() {
  Location l = {};
  snprintf(l.name, sizeof(l.name), "Buchs SG");
  l.lat = 47.1674f;
  l.lon = 9.4779f;
  return l;
}

bool settings_load_wifi(WifiCreds* out) {
  memset(out, 0, sizeof(*out));
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READONLY, &h) == ESP_OK) {
    bool ok = get_str(h, "wifi_ssid", out->ssid, sizeof(out->ssid)) && out->ssid[0] != '\0';
    if (ok) get_str(h, "wifi_pass", out->pass, sizeof(out->pass));
    uint8_t seeded = 0;
    nvs_get_u8(h, "wifi_seeded", &seeded);
    nvs_close(h);
    if (ok) return true;
    // Once NVS owns the Wi-Fi settings (seeded or set by the user), an empty
    // SSID means "not configured": the build default never comes back.
    if (seeded) return false;
  }
  snprintf(out->ssid, sizeof(out->ssid), "%s", WIFI_SSID);
  snprintf(out->pass, sizeof(out->pass), "%s", WIFI_PASSWORD);
  return out->ssid[0] != '\0';
}

bool settings_save_wifi(const WifiCreds& c) {
  Tx tx;
  if (c.ssid[0] == '\0') tx.erase("wifi_ssid");
  else tx.str("wifi_ssid", c.ssid);
  tx.str("wifi_pass", c.pass);
  tx.u8("wifi_seeded", 1);
  return tx.commit();
}

Location settings_load_location() {
  Location l = settings_default_location();
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READONLY, &h) != ESP_OK) return l;
  Location n = {};
  size_t sz = sizeof(float);
  float lat = 0, lon = 0;
  bool ok = get_str(h, "loc_name", n.name, sizeof(n.name)) && nvs_get_blob(h, "loc_lat", &lat, &sz) == ESP_OK &&
            (sz = sizeof(float), nvs_get_blob(h, "loc_lon", &lon, &sz) == ESP_OK);
  nvs_close(h);
  if (!ok) return l;
  n.lat = lat;
  n.lon = lon;
  return n;
}

bool settings_save_location(const Location& l) {
  Tx tx;
  tx.str("loc_name", l.name);
  tx.blob("loc_lat", &l.lat, sizeof(float));
  tx.blob("loc_lon", &l.lon, sizeof(float));
  return tx.commit();
}

bool settings_reset_location() {
  Tx tx;
  tx.erase("loc_name");
  tx.erase("loc_lat");
  tx.erase("loc_lon");
  return tx.commit();
}

DisplaySettings settings_load_display() {
  DisplaySettings d = {120, true, kSaverBlack};
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READONLY, &h) != ESP_OK) return d;
  uint16_t t = 0;
  uint8_t hint = 0;
  if (nvs_get_u16(h, "scr_to", &t) == ESP_OK) d.timeout_s = t;
  if (nvs_get_u8(h, "scr_hint", &hint) == ESP_OK) d.hint = hint != 0;
  uint8_t saver = 0;
  if (nvs_get_u8(h, "scr_saver", &saver) == ESP_OK && saver <= kSaverAnalog) d.saver = saver;
  nvs_close(h);
  return d;
}

bool settings_save_display(const DisplaySettings& d) {
  Tx tx;
  tx.u16("scr_to", d.timeout_s);
  tx.u8("scr_hint", d.hint ? 1 : 0);
  tx.u8("scr_saver", d.saver);
  return tx.commit();
}

TimeSettings settings_default_time() {
  TimeSettings t = {};
  snprintf(t.server, sizeof(t.server), "ch.pool.ntp.org");
  snprintf(t.tz_name, sizeof(t.tz_name), "Europe/Zurich");
  snprintf(t.tz, sizeof(t.tz), "CET-1CEST,M3.5.0,M10.5.0/3");
  return t;
}

TimeSettings settings_load_time() {
  TimeSettings t = settings_default_time();
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READONLY, &h) != ESP_OK) return t;
  TimeSettings n = {};
  bool ok = get_str(h, "ntp_srv", n.server, sizeof(n.server)) && n.server[0] != '\0' &&
            get_str(h, "tz_name", n.tz_name, sizeof(n.tz_name)) && get_str(h, "tz_posix", n.tz, sizeof(n.tz)) &&
            n.tz[0] != '\0';
  nvs_close(h);
  return ok ? n : t;
}

bool settings_save_time(const TimeSettings& t) {
  Tx tx;
  tx.str("ntp_srv", t.server);
  tx.str("tz_name", t.tz_name);
  tx.str("tz_posix", t.tz);
  return tx.commit();
}

namespace {
const Webcam kDefaultCams[] = {
    {1292514749, "Grabs \xC2\xB7 Nordwesten: Altmann"},
    {1670606347, "Buchs"},
    {1697029384, "Triesenberg: Gemeinde"},
    {1499936290, "Wartau \xC2\xB7 Westen: Seilbahn Palfries - Churfirsten"},
    {1697035782, "Wildhaus-Alt St. Johann: Wildhaus: Berggasthaus"},
};
}  // namespace

// NVS holds only the IDs: blob "cam_ids" (uint32_t[n], n = 0 stored as "cams_n").
int settings_load_cams(Webcam* out, int cap) {
  memset(out, 0, sizeof(Webcam) * cap);
  const int nd = static_cast<int>(sizeof(kDefaultCams) / sizeof(kDefaultCams[0]));
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READONLY, &h) == ESP_OK) {
    uint32_t ids[kMaxCams];
    size_t sz = sizeof(ids);
    uint8_t n8 = 0;
    bool ok = nvs_get_u8(h, "cams_n", &n8) == ESP_OK && n8 <= kMaxCams;
    if (ok && n8 > 0) ok = nvs_get_blob(h, "cam_ids", ids, &sz) == ESP_OK && sz == n8 * sizeof(uint32_t);
    nvs_close(h);
    if (ok) {
      int n = n8 > cap ? cap : n8;
      for (int i = 0; i < n; ++i) {
        out[i].id = ids[i];
        for (int k = 0; k < nd; ++k) {
          if (kDefaultCams[k].id == ids[i]) snprintf(out[i].title, sizeof(out[i].title), "%s", kDefaultCams[k].title);
        }
      }
      return n;
    }
  }
  int n = nd > cap ? cap : nd;
  memcpy(out, kDefaultCams, sizeof(Webcam) * n);
  return n;
}

bool settings_save_cams(const Webcam* cams, int n) {
  if (n < 0 || n > kMaxCams) return false;
  uint32_t ids[kMaxCams];
  for (int i = 0; i < n; ++i) ids[i] = cams[i].id;
  Tx tx;
  if (n > 0) tx.blob("cam_ids", ids, n * sizeof(uint32_t));
  else tx.erase("cam_ids");
  tx.u8("cams_n", static_cast<uint8_t>(n));
  return tx.commit();
}

bool settings_load_windy_key(char* out, size_t n, bool* own) {
  if (own != nullptr) *own = false;
  out[0] = '\0';
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READONLY, &h) == ESP_OK) {
    bool ok = get_str(h, "windy_key", out, n) && out[0] != '\0';
    nvs_close(h);
    if (ok) {
      if (own != nullptr) *own = true;
      return true;
    }
  }
  snprintf(out, n, "%s", CONFIG_PANEL_WINDY_API_KEY);
  if (out[0] == '\0' && nvs_open(kNs, NVS_READONLY, &h) == ESP_OK) {
    if (!get_str(h, "windy_dflt", out, n)) out[0] = '\0';  // seeded from an earlier build
    nvs_close(h);
  }
  return out[0] != '\0';
}

bool settings_save_windy_key(const char* key) {
  Tx tx;
  if (key == nullptr || key[0] == '\0') tx.erase("windy_key");
  else tx.str("windy_key", key);
  return tx.commit();
}

void settings_mask_key(const char* key, char* out, size_t n) {
  size_t len = strlen(key);
  if (len == 0) {
    snprintf(out, n, "-");
  } else if (len <= 8) {
    snprintf(out, n, "********");
  } else {
    snprintf(out, n, "%.4s****%s", key, key + len - 4);
  }
}

void settings_load_stations(Station* out) {
  static const Station kDefault[kStationN] = {{"8509404", "Buchs SG"}, {"8587407", "R\xC3\xA4" "fis, Schulhaus"}};
  nvs_handle_t h;
  bool open = nvs_open(kNs, NVS_READONLY, &h) == ESP_OK;
  for (int i = 0; i < kStationN; ++i) {
    out[i] = kDefault[i];
    if (!open) continue;
    char kid[8], kname[10];
    snprintf(kid, sizeof(kid), "st%d_id", i);
    snprintf(kname, sizeof(kname), "st%d_name", i);
    Station s = {};
    if (get_str(h, kid, s.id, sizeof(s.id)) && s.id[0] != '\0' && get_str(h, kname, s.name, sizeof(s.name))) out[i] = s;
  }
  if (open) nvs_close(h);
}

bool settings_save_station(int i, const Station& st) {
  if (i < 0 || i >= kStationN) return false;
  char kid[8], kname[10];
  snprintf(kid, sizeof(kid), "st%d_id", i);
  snprintf(kname, sizeof(kname), "st%d_name", i);
  Tx tx;
  if (st.id[0] == '\0') {
    tx.erase(kid);
    tx.erase(kname);
  } else {
    tx.str(kid, st.id);
    tx.str(kname, st.name);
  }
  return tx.commit();
}

namespace {
const char* const kTextKey[kTextSettingN] = {"ical_url", "waste_api", "ota_url",  "meteo_plz", "waste_bfs",
                                             "waste_zone", "en_host",  "en_flat",  "en_ht",     "en_nt",
                                             "en_feed",    "wa_price", "en_htwin", "en_switch",
                                             "pv_gw",      "pv_gwport", "pv_unit"};
// Legacy (Grott stick, cfg_ver 5): "pv_ip" / "pv_port" stay in NVS and the backup schema, unused.
}  // namespace

const char* settings_text_default(TextSetting which) {
  switch (which) {
    case kUrlIcal: return CONFIG_PANEL_ICAL_URL;
    case kUrlWaste: return CONFIG_PANEL_WASTE_API;
    case kTextWasteBfs: return CONFIG_PANEL_WASTE_BFS;
    case kUrlOta: return CONFIG_PANEL_OTA_URL;
    case kTextPlz: return CONFIG_PANEL_METEO_PLZ;
    case kTextEnFlat: return "27.27";
    case kTextEnHt: return "30.00";
    case kTextEnNt: return "25.67";
    case kTextEnFeed: return "10.04";
    case kTextWaPrice: return "2.44";
    case kTextEnHtWin: return "06:00-10:00,17:00-21:00";
    case kTextEnSwitch: return "2027-01-01";
    case kTextPvGw: return "192.168.1.244";
    case kTextPvGwPort: return "502";
    case kTextPvUnit: return "1";
    default: return "";
  }
}

bool settings_load_text(TextSetting which, char* out, size_t n, bool* own) {
  if (own != nullptr) *own = false;
  out[0] = '\0';
  if (which >= kTextSettingN) return false;
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READONLY, &h) == ESP_OK) {
    bool ok = get_str(h, kTextKey[which], out, n) && out[0] != '\0';
    nvs_close(h);
    if (ok) {
      if (own != nullptr) *own = true;
      return true;
    }
  }
  snprintf(out, n, "%s", settings_text_default(which));
  if (out[0] == '\0' && which == kUrlIcal && nvs_open(kNs, NVS_READONLY, &h) == ESP_OK) {
    if (!get_str(h, "ical_dflt", out, n)) out[0] = '\0';  // seeded from an earlier build
    nvs_close(h);
  }
  return out[0] != '\0';
}

bool settings_save_text(TextSetting which, const char* value) {
  if (which >= kTextSettingN) return false;
  Tx tx;
  if (value == nullptr || value[0] == '\0') tx.erase(kTextKey[which]);
  else tx.str(kTextKey[which], value);
  return tx.commit();
}

// {"format":"esp_display-settings","version":1,"cfg_ver":1,"secrets":false,
//  "keys":{"loc_name":{"t":"str","v":"Buchs SG"},"scr_to":{"t":"u16","v":120},
//          "loc_lat":{"t":"blob","v":"<hex>"}}}
// Only keys of the schema (settings_schema.cpp) are exported; secrets only on request.
size_t settings_export_json(char* out, size_t n, bool secrets) {
  DynamicJsonDocument doc(16384);
  doc["format"] = "esp_display-settings";
  doc["version"] = 1;
  doc["cfg_ver"] = kCfgVersion;
  doc["secrets"] = secrets;
  JsonObject keys = doc.createNestedObject("keys");
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READONLY, &h) != ESP_OK) {
    return serializeJson(doc, out, n);
  }
  const KeySpec* spec = schema_begin();
  for (size_t i = 0; i < schema_count(); ++i) {
    const KeySpec& ks = spec[i];
    if (ks.flags & kKeyInternal) continue;
    if (!secrets && (ks.flags & kKeySecret)) continue;
    switch (ks.type) {
      case KeyType::U8: {
        uint8_t v;
        if (nvs_get_u8(h, ks.key, &v) != ESP_OK) break;
        JsonObject k = keys.createNestedObject(ks.key);
        k["t"] = "u8";
        k["v"] = v;
        break;
      }
      case KeyType::U16: {
        uint16_t v;
        if (nvs_get_u16(h, ks.key, &v) != ESP_OK) break;
        JsonObject k = keys.createNestedObject(ks.key);
        k["t"] = "u16";
        k["v"] = v;
        break;
      }
      case KeyType::Str: {
        char v[300];
        size_t len = sizeof(v);
        if (nvs_get_str(h, ks.key, v, &len) != ESP_OK) break;
        JsonObject k = keys.createNestedObject(ks.key);
        k["t"] = "str";
        k["v"] = v;  // copied by ArduinoJson
        memset(v, 0, sizeof(v));
        break;
      }
      case KeyType::Blob: {
        uint8_t v[256];
        size_t len = sizeof(v);
        if (nvs_get_blob(h, ks.key, v, &len) != ESP_OK) break;
        char hex[sizeof(v) * 2 + 1];
        for (size_t b = 0; b < len; ++b) snprintf(hex + b * 2, 3, "%02x", v[b]);
        hex[len * 2] = '\0';
        JsonObject k = keys.createNestedObject(ks.key);
        k["t"] = "blob";
        k["v"] = hex;
        break;
      }
    }
  }
  nvs_close(h);
  if (measureJson(doc) + 1 > n) return 0;
  return serializeJson(doc, out, n);
}

namespace {

bool parse_hex(const char* hex, uint8_t* out, size_t cap, size_t* n) {
  size_t len = strlen(hex);
  if (len % 2 != 0 || len / 2 > cap) return false;
  for (size_t i = 0; i < len / 2; ++i) {
    unsigned v = 0;
    for (int k = 0; k < 2; ++k) {
      char c = hex[i * 2 + k];
      int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
      if (d < 0) return false;
      v = v * 16 + static_cast<unsigned>(d);
    }
    out[i] = static_cast<uint8_t>(v);
  }
  *n = len / 2;
  return true;
}

const char* type_name(KeyType t) {
  switch (t) {
    case KeyType::U8: return "u8";
    case KeyType::U16: return "u16";
    case KeyType::Str: return "str";
    default: return "blob";
  }
}

}  // namespace

// Two passes: validate every key against the schema, then write. Any invalid
// known key rejects the whole file (nothing written); unknown keys (e.g. from a
// newer firmware) are skipped and listed.
int settings_import_json(const char* json, size_t len, char* report, size_t rn) {
  WriteLock lock;
  if (report != nullptr && rn > 0) report[0] = '\0';
  auto note = [&](const char* fmt, const char* a, const char* b) {
    if (report == nullptr || rn == 0) return;
    size_t used = strlen(report);
    if (used + 1 < rn) snprintf(report + used, rn - used, fmt, a, b);
  };
  DynamicJsonDocument doc(16384);
  if (deserializeJson(doc, json, len)) return -1;
  if (strcmp(doc["format"] | "", "esp_display-settings") != 0) return -1;
  int ver = doc["cfg_ver"] | 1;
  if (ver > kCfgVersion) {
    return -2;
  }
  JsonObject keys = doc["keys"].as<JsonObject>();
  if (keys.isNull()) return -1;
  int bad = 0, skipped = 0;
  for (int pass = 0; pass < 2; ++pass) {
    nvs_handle_t h = 0;
    if (pass == 1) {
      if (bad > 0) return -3;
      if (nvs_open(kNs, NVS_READWRITE, &h) != ESP_OK) return -1;
    }
    int written = 0;
    for (JsonPair kv : keys) {
      const char* key = kv.key().c_str();
      const KeySpec* ks = schema_find(key);
      if (ks == nullptr || (ks->flags & kKeyInternal)) {
        if (pass == 0) {
          ++skipped;
          note("unbekannt: %s%s; ", key, "");
        }
        continue;
      }
      const char* t = kv.value()["t"] | "";
      JsonVariant v = kv.value()["v"];
      const char* why = "falscher Typ";
      bool ok = strcmp(t, type_name(ks->type)) == 0;
      uint8_t blob[256];
      size_t bl = 0;
      if (ok) {
        switch (ks->type) {
          case KeyType::U8:
          case KeyType::U16:
            ok = v.is<long long>() && schema_check_int(*ks, v.as<long long>(), &why);
            break;
          case KeyType::Str:
            ok = v.is<const char*>() && schema_check_str(*ks, v.as<const char*>(), &why);
            break;
          case KeyType::Blob:
            ok = v.is<const char*>() && parse_hex(v.as<const char*>(), blob, sizeof(blob), &bl);
            if (!ok) why = "kein Hex";
            ok = ok && schema_check_blob(*ks, blob, bl, &why);
            break;
        }
      }
      if (pass == 0) {
        if (!ok) {
          ++bad;
          note("%s: %s; ", key, why);
        }
        continue;
      }
      esp_err_t e = ESP_FAIL;
      switch (ks->type) {
        case KeyType::U8: e = nvs_set_u8(h, key, v.as<uint8_t>()); break;
        case KeyType::U16: e = nvs_set_u16(h, key, v.as<uint16_t>()); break;
        case KeyType::Str: e = nvs_set_str(h, key, v.as<const char*>()); break;
        case KeyType::Blob: e = nvs_set_blob(h, key, blob, bl); break;
      }
      if (e == ESP_OK) ++written;
    }
    if (pass == 1) {
      nvs_set_u8(h, "cfg_ver", static_cast<uint8_t>(ver));  // older backups: converted below
      nvs_commit(h);
      nvs_close(h);
      (void)skipped;
      settings_migrate();
      return written;
    }
  }
  return -1;
}

void settings_migrate() {
  WriteLock lock;
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READWRITE, &h) != ESP_OK) return;
  uint8_t v = 0;
  if (nvs_get_u8(h, "cfg_ver", &v) != ESP_OK) v = 0;
  if (v < kCfgVersion) {
    // 0 -> 1: the layout of v0.7.5 is version 1; nothing to convert yet.
    // 1 -> 2: waste reminders use "Am Vortag ab" (wst_from) instead of the lead
    // preset rem_lwst: "1 Tag" (day before 07:00) -> 07:00, all others -> 18:00.
    uint16_t from = 0;
    if (v < 2 && nvs_get_u16(h, "wst_from", &from) != ESP_OK) {
      uint8_t lead = 4;
      nvs_get_u8(h, "rem_lwst", &lead);
      from = lead == 3 ? 420 : kWasteFromDefault;
      nvs_set_u16(h, "wst_from", from);
      console_printf("Settings: waste reminder lead %u -> from %02u:%02u on the day before\n", lead, from / 60,
                     from % 60);
    }
    // 2 -> 3: family calendar reminders split by type. Old preset rem_lcal:
    // timed lead 15 min / 1 h / 3 h kept, others -> 1 h; all-day "1 Tag" (day
    // before 07:00) -> 07:00 on the day before, others -> 18:00.
    uint16_t cl = 0, cf = 0;
    if (v < 3 && nvs_get_u16(h, "cal_lead", &cl) != ESP_OK && nvs_get_u16(h, "cal_from", &cf) != ESP_OK) {
      uint8_t lead = 1;
      nvs_get_u8(h, "rem_lcal", &lead);
      cl = lead == 0 ? 15 : lead == 2 ? 180 : kCalLeadDefault;
      cf = lead == 3 ? 420 : kCalFromDefault;
      nvs_set_u16(h, "cal_lead", cl);
      nvs_set_u16(h, "cal_from", cf);
      console_printf("Settings: calendar lead %u -> timed %u min, all-day from %02u:%02u\n", lead, cl, cf / 60,
                     cf % 60);
    }
    // 3 -> 4: Energie settings (en_*, wa_*) are new; defaults apply, nothing to convert.
    // 4 -> 5: PV (pv_on, pv_ip, pv_port) are new; defaults apply, nothing to convert.
    // 5 -> 6: PV via Modbus TCP gateway (pv_gw, pv_gwport, pv_unit) replaces the Grott stick
    //         server; defaults apply, pv_on is kept, the old pv_ip/pv_port stay (unused).
    nvs_set_u8(h, "cfg_ver", kCfgVersion);
    nvs_commit(h);
    console_printf("Settings: config version %u -> %u\n", v, kCfgVersion);
  }
  nvs_close(h);
}

// --- Energie ---------------------------------------------------------------------

EnergySettings settings_load_energy() {
  EnergySettings e = {};
  e.on = true;
  e.poll_s = 10;
  e.model = kTariffAuto;
  e.show_exp = true;
  e.water_on = true;
  e.water_bus = 2;
  tariff_defaults(&e.tariff);
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READONLY, &h) == ESP_OK) {
    uint8_t v = 0;
    uint16_t w = 0;
    if (nvs_get_u8(h, "en_on", &v) == ESP_OK) e.on = v != 0;
    if (nvs_get_u16(h, "en_poll", &w) == ESP_OK && w >= 5 && w <= 300) e.poll_s = w;
    if (nvs_get_u8(h, "en_model", &v) == ESP_OK && v <= kTariffHtNt) e.model = v;
    if (nvs_get_u8(h, "en_exp", &v) == ESP_OK) e.show_exp = v != 0;
    if (nvs_get_u8(h, "wa_on", &v) == ESP_OK) e.water_on = v != 0;
    if (nvs_get_u8(h, "wa_bus", &v) == ESP_OK && v >= 1 && v <= 4) e.water_bus = v;
    nvs_close(h);
  }
  e.tariff.model = e.model;
  char t[64];
  settings_load_text(kTextEnHost, e.host, sizeof(e.host), nullptr);
  uint16_t p;
  if (settings_load_text(kTextEnFlat, t, sizeof(t), nullptr) && energy_parse_price(t, &p)) e.tariff.flat = p;
  if (settings_load_text(kTextEnHt, t, sizeof(t), nullptr) && energy_parse_price(t, &p)) e.tariff.ht = p;
  if (settings_load_text(kTextEnNt, t, sizeof(t), nullptr) && energy_parse_price(t, &p)) e.tariff.nt = p;
  if (settings_load_text(kTextEnFeed, t, sizeof(t), nullptr) && energy_parse_price(t, &p)) e.tariff.feed = p;
  if (settings_load_text(kTextWaPrice, t, sizeof(t), nullptr) && energy_parse_price(t, &p)) e.tariff.water = p;
  if (settings_load_text(kTextEnHtWin, t, sizeof(t), nullptr)) energy_parse_windows(t, e.tariff.win);
  int32_t ymd;
  if (settings_load_text(kTextEnSwitch, t, sizeof(t), nullptr) && energy_parse_date(t, &ymd)) e.tariff.switch_ymd = ymd;
  return e;
}

bool settings_save_energy_flags(const EnergySettings& e) {
  Tx tx;
  tx.u8("en_on", e.on ? 1 : 0);
  tx.u16("en_poll", e.poll_s);
  tx.u8("en_model", e.model);
  tx.u8("en_exp", e.show_exp ? 1 : 0);
  tx.u8("wa_on", e.water_on ? 1 : 0);
  tx.u8("wa_bus", e.water_bus);
  return tx.commit();
}

bool settings_load_pv_on() {
  uint8_t v = 1;
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READONLY, &h) == ESP_OK) {
    nvs_get_u8(h, "pv_on", &v);
    nvs_close(h);
  }
  return v != 0;
}

bool settings_save_pv_on(bool on) {
  Tx tx;
  tx.u8("pv_on", on ? 1 : 0);
  return tx.commit();
}

bool settings_load_energy_hist(EnergyHist* hist) {
  energy_hist_clear(hist);
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READONLY, &h) != ESP_OK) return false;
  size_t sz = sizeof(*hist);
  bool ok = nvs_get_blob(h, "en_hist", hist, &sz) == ESP_OK && sz == sizeof(*hist) && hist->magic == kEnergyHistMagic;
  nvs_close(h);
  if (!ok) energy_hist_clear(hist);
  return ok;
}

bool settings_save_energy_hist(const EnergyHist& hist) {
  Tx tx;
  tx.blob("en_hist", &hist, sizeof(hist));
  return tx.commit();
}

bool settings_load_pv_hist(PvHist* hist) {
  pv_hist_clear(hist);
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READONLY, &h) != ESP_OK) return false;
  size_t sz = sizeof(*hist);
  bool ok = nvs_get_blob(h, "pv_hist", hist, &sz) == ESP_OK && sz == sizeof(*hist) && hist->magic == kPvHistMagic;
  nvs_close(h);
  if (!ok) pv_hist_clear(hist);
  return ok;
}

bool settings_save_pv_hist(const PvHist& hist) {
  Tx tx;
  tx.blob("pv_hist", &hist, sizeof(hist));
  return tx.commit();
}

bool settings_load_pv_hours(PvHours* hours) {
  pv_hours_clear(hours);
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READONLY, &h) != ESP_OK) return false;
  size_t sz = sizeof(*hours);
  bool ok = nvs_get_blob(h, "pv_hours", hours, &sz) == ESP_OK && sz == sizeof(*hours) && hours->magic == kPvHoursMagic;
  nvs_close(h);
  if (!ok) pv_hours_clear(hours);
  return ok;
}

bool settings_save_pv_hours(const PvHours& hours) {
  Tx tx;
  tx.blob("pv_hours", &hours, sizeof(hours));
  return tx.commit();
}

// --- Termin-Erinnerungen ---------------------------------------------------------

ReminderSettings settings_load_reminders() {
  ReminderSettings r = {true, true, true, 1, 4, kCalLeadDefault, kCalFromDefault};
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READONLY, &h) != ESP_OK) return r;
  uint8_t v = 0;
  if (nvs_get_u8(h, "rem_on", &v) == ESP_OK) r.on = v != 0;
  if (nvs_get_u8(h, "rem_src", &v) == ESP_OK) {
    r.cal = (v & 1) != 0;
    r.waste = (v & 2) != 0;
  }
  if (nvs_get_u8(h, "rem_lcal", &v) == ESP_OK && v < kLeadN) r.lead_cal = v;
  if (nvs_get_u8(h, "rem_lwst", &v) == ESP_OK && v < kLeadN) r.lead_waste = v;
  uint16_t m = 0;
  if (nvs_get_u16(h, "cal_lead", &m) == ESP_OK && m >= 1 && m <= 1440) r.cal_lead_min = m;
  if (nvs_get_u16(h, "cal_from", &m) == ESP_OK && m <= 1439) r.cal_from_min = m;
  nvs_close(h);
  return r;
}

uint16_t settings_load_waste_until() {
  uint16_t v = kWasteUntilDefault;
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READONLY, &h) != ESP_OK) return v;
  uint16_t t = 0;
  if (nvs_get_u16(h, "wst_until", &t) == ESP_OK && t <= 1440) v = t;
  nvs_close(h);
  return v;
}

uint16_t settings_load_waste_from() {
  uint16_t v = kWasteFromDefault;
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READONLY, &h) != ESP_OK) return v;
  uint16_t t = 0;
  if (nvs_get_u16(h, "wst_from", &t) == ESP_OK && t <= 1439) v = t;
  nvs_close(h);
  return v;
}

bool settings_save_waste_from(uint16_t minutes) {
  Tx tx;
  tx.u16("wst_from", minutes);
  return tx.commit();
}

bool settings_save_waste_until(uint16_t minutes) {
  Tx tx;
  tx.u16("wst_until", minutes);
  return tx.commit();
}

bool settings_save_reminders(const ReminderSettings& r) {
  uint8_t src = (r.cal ? 1 : 0) | (r.waste ? 2 : 0);
  Tx tx;
  tx.u8("rem_on", r.on ? 1 : 0);
  tx.u8("rem_src", src);
  tx.u8("rem_lcal", r.lead_cal);
  tx.u8("rem_lwst", r.lead_waste);
  tx.u16("cal_lead", r.cal_lead_min);
  tx.u16("cal_from", r.cal_from_min);
  return tx.commit();
}

namespace {
constexpr int kAckN = 48;
struct AckRing {
  uint32_t key[kAckN];
  uint8_t next;
};
static_assert(sizeof(AckRing) == 48 * 4 + 4, "rem_ack size must match the schema (kAckBytes)");
AckRing ack_ring;
bool ack_loaded = false;

void ack_load() {
  if (ack_loaded) return;
  ack_loaded = true;
  memset(&ack_ring, 0, sizeof(ack_ring));
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READONLY, &h) != ESP_OK) return;
  size_t sz = sizeof(ack_ring);
  if (nvs_get_blob(h, "rem_ack", &ack_ring, &sz) != ESP_OK || sz != sizeof(ack_ring)) memset(&ack_ring, 0, sizeof(ack_ring));
  nvs_close(h);
  if (ack_ring.next >= kAckN) ack_ring.next = 0;
}
}  // namespace

bool settings_reminder_acked(uint32_t key) {
  ack_load();
  for (uint32_t k : ack_ring.key) {
    if (k == key && key != 0) return true;
  }
  return false;
}

bool settings_reminder_ack(uint32_t key) {
  if (settings_reminder_acked(key)) return true;
  ack_ring.key[ack_ring.next] = key;
  ack_ring.next = static_cast<uint8_t>((ack_ring.next + 1) % kAckN);
  Tx tx;
  tx.blob("rem_ack", &ack_ring, sizeof(ack_ring));
  return tx.commit();
}

size_t settings_reminder_acks_size() { return sizeof(ack_ring); }

void settings_reminder_acks_get(uint8_t* out) {
  ack_load();
  memcpy(out, &ack_ring, sizeof(ack_ring));
}

bool settings_reminder_acks_set(const uint8_t* in) {
  memcpy(&ack_ring, in, sizeof(ack_ring));
  if (ack_ring.next >= kAckN) ack_ring.next = 0;
  ack_loaded = true;
  Tx tx;
  tx.blob("rem_ack", &ack_ring, sizeof(ack_ring));
  return tx.commit();
}

void settings_reminder_clear_acks() {
  memset(&ack_ring, 0, sizeof(ack_ring));
  ack_loaded = true;
  Tx tx;
  tx.erase("rem_ack");
  tx.commit();
}

// --- Seeding build-time secrets into NVS ---------------------------------------------

namespace {
// Writes value under key when the key is missing (or, with update, differs).
void seed(nvs_handle_t h, const char* key, const char* value, bool update, int* written) {
  if (value == nullptr || value[0] == '\0') return;
  char cur[256];
  bool have = get_str(h, key, cur, sizeof(cur));
  if (have && (!update || strcmp(cur, value) == 0)) return;
  if (nvs_set_str(h, key, value) == ESP_OK) ++*written;
}
}  // namespace

void settings_seed_from_build() {
  WriteLock lock;
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READWRITE, &h) != ESP_OK) return;
  int n = 0;
  // Wi-Fi and Loxone: only when NVS has no value yet (the user may have changed them).
  // The build Wi-Fi is copied once ("wifi_seeded"); afterwards a network the
  // user picked (or cleared) is never overridden by the firmware default.
  char ssid[33];
  uint8_t seeded = 0;
  nvs_get_u8(h, "wifi_seeded", &seeded);
  if (!seeded) {
    if (!get_str(h, "wifi_ssid", ssid, sizeof(ssid)) || ssid[0] == '\0') {
      seed(h, "wifi_ssid", WIFI_SSID, true, &n);
      seed(h, "wifi_pass", WIFI_PASSWORD, true, &n);
    }
    if (get_str(h, "wifi_ssid", ssid, sizeof(ssid)) && ssid[0] != '\0') {
      nvs_set_u8(h, "wifi_seeded", 1);
      ++n;
    }
  }
  char host[64];
  if (!get_str(h, "lox_host", host, sizeof(host)) || host[0] == '\0') {
    seed(h, "lox_host", LOXONE_HOST, true, &n);
    seed(h, "lox_user", LOXONE_USER, true, &n);
    seed(h, "lox_pass", LOXONE_PASSWORD, true, &n);
    seed(h, "lox_auth", LOXONE_AUTH, true, &n);
    if (LOXONE_HOST[0] != '\0') {
      nvs_set_u16(h, "lox_port", static_cast<uint16_t>(LOXONE_PORT));
    }
  }
  // Build defaults for the Windy key and iCal URL: kept as a separate "default"
  // copy (the user's own value stays in windy_key / ical_url).
  seed(h, "windy_dflt", CONFIG_PANEL_WINDY_API_KEY, true, &n);
  seed(h, "ical_dflt", CONFIG_PANEL_ICAL_URL, true, &n);
  if (n > 0) nvs_commit(h);
  nvs_close(h);
  printf("Settings: %d build values copied to NVS\n", n);
}

#ifdef CONFIG_PANEL_TEST_HARNESS
// Test only: forget the Wi-Fi (as after a user reset), set a password, or
// restore the build credentials. Values are never logged.
bool settings_test_wifi(int op, const char* arg) {
  WriteLock lock;
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READWRITE, &h) != ESP_OK) return false;
  bool ok = true;
  if (op == 0) {
    nvs_erase_key(h, "wifi_ssid");
    nvs_erase_key(h, "wifi_pass");
    nvs_set_u8(h, "wifi_seeded", 1);
  } else if (op == 1) {
    ok = nvs_set_str(h, "wifi_pass", arg) == ESP_OK;
  } else {
    ok = nvs_set_str(h, "wifi_ssid", WIFI_SSID) == ESP_OK && nvs_set_str(h, "wifi_pass", WIFI_PASSWORD) == ESP_OK;
    nvs_set_u8(h, "wifi_seeded", 1);
  }
  ok = ok && nvs_commit(h) == ESP_OK;
  nvs_close(h);
  return ok;
}
#endif

LoxoneConfig settings_load_loxone() {
  LoxoneConfig c = {};
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READONLY, &h) == ESP_OK) {
    bool ok = get_str(h, "lox_host", c.host, sizeof(c.host)) && c.host[0] != '\0';
    if (ok) {
      get_str(h, "lox_user", c.user, sizeof(c.user));
      get_str(h, "lox_pass", c.pass, sizeof(c.pass));
      if (!get_str(h, "lox_auth", c.auth, sizeof(c.auth)) || c.auth[0] == '\0') snprintf(c.auth, sizeof(c.auth), "basic");
      c.port = 80;
      nvs_get_u16(h, "lox_port", &c.port);
    }
    nvs_close(h);
    if (ok) return c;
  }
  snprintf(c.host, sizeof(c.host), "%s", LOXONE_HOST);
  c.port = static_cast<uint16_t>(LOXONE_PORT);
  snprintf(c.user, sizeof(c.user), "%s", LOXONE_USER);
  snprintf(c.pass, sizeof(c.pass), "%s", LOXONE_PASSWORD);
  snprintf(c.auth, sizeof(c.auth), "%s", LOXONE_AUTH);
  return c;
}

const LoxoneConfig& settings_loxone() {
  static LoxoneConfig c = settings_load_loxone();
  return c;
}
