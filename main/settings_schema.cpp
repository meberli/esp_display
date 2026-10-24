#include "settings_schema.h"

#include <ctype.h>
#include <string.h>

#include "energy.h"
#include "info.h"
#include "settings.h"

namespace {

// Ranges match the structs in settings.h / info.h (buffer size - 1).
constexpr int kAckBytes = 48 * 4 + 4;  // AckRing (uint32_t[48] + next, padded)

const KeySpec kSchema[] = {
    {"cfg_ver", KeyType::U8, 0, 255, kKeyInternal, 0},
    {"wifi_seeded", KeyType::U8, 0, 1, kKeyInternal, 0},
    {"wifi_ssid", KeyType::Str, 1, 32, 0, 0},
    {"wifi_pass", KeyType::Str, 0, 64, kKeySecret, 0},
    {"loc_name", KeyType::Str, 1, 47, 0, 0},
    {"loc_lat", KeyType::Blob, -90, 90, kKeyFloat, 0},
    {"loc_lon", KeyType::Blob, -180, 180, kKeyFloat, 0},
    {"ntp_srv", KeyType::Str, 1, 63, kKeyHost, 0},
    {"tz_name", KeyType::Str, 1, 31, 0, 0},
    {"tz_posix", KeyType::Str, 1, 63, 0, 0},
    {"windy_key", KeyType::Str, 0, 63, kKeySecret, 0},
    {"windy_dflt", KeyType::Str, 0, 63, kKeySecret, 0},
    {"cams_n", KeyType::U8, 0, kMaxCams, 0, 0},
    {"cam_ids", KeyType::Blob, 0, kMaxCams * 4, 0, 4},
    {"st0_id", KeyType::Str, 1, 11, kKeyDigits, 0},
    {"st0_name", KeyType::Str, 1, 47, 0, 0},
    {"st1_id", KeyType::Str, 1, 11, kKeyDigits, 0},
    {"st1_name", KeyType::Str, 1, 47, 0, 0},
    {"scr_to", KeyType::U16, 0, 3600, 0, 0},
    {"scr_hint", KeyType::U8, 0, 1, 0, 0},
    {"scr_saver", KeyType::U8, 0, kSaverAnalog, 0, 0},
    {"ical_url", KeyType::Str, 8, 255, kKeySecret | kKeyUrl, 0},
    {"ical_dflt", KeyType::Str, 0, 255, kKeySecret | kKeyUrl, 0},
    {"waste_url", KeyType::Str, 8, 255, kKeyUrl, 0},  // unused since 0.8.2 (old buchs-sg.ch page); kept for old backups
    {"waste_api", KeyType::Str, 9, 255, kKeyUrl | kKeyHttpsOnly, 0},
    {"waste_bfs", KeyType::Str, 4, 4, kKeyDigits, 0},
    {"waste_zone", KeyType::Str, 1, 95, 0, 0},
    {"ota_url", KeyType::Str, 9, 255, kKeyUrl | kKeyHttpsOnly, 0},
    {"meteo_plz", KeyType::Str, 4, 4, kKeyDigits, 0},
    {"rem_on", KeyType::U8, 0, 1, 0, 0},
    {"rem_src", KeyType::U8, 0, 3, 0, 0},
    {"rem_lcal", KeyType::U8, 0, kLeadN - 1, 0, 0},
    {"rem_lwst", KeyType::U8, 0, kLeadN - 1, 0, 0},
    {"cal_lead", KeyType::U16, 1, 1440, 0, 0},
    {"cal_from", KeyType::U16, 0, 1439, 0, 0},
    {"rem_ack", KeyType::Blob, kAckBytes, kAckBytes, 0, 0},
    {"wst_until", KeyType::U16, 0, 1440, 0, 0},
    {"wst_from", KeyType::U16, 0, 1439, 0, 0},
    {"lox_host", KeyType::Str, 1, 63, kKeyHost, 0},
    {"lox_port", KeyType::U16, 1, 65535, 0, 0},
    {"lox_user", KeyType::Str, 0, 32, 0, 0},
    {"lox_pass", KeyType::Str, 0, 64, kKeySecret, 0},
    {"lox_auth", KeyType::Str, 1, 7, 0, 0},
    {"en_on", KeyType::U8, 0, 1, 0, 0},
    {"en_poll", KeyType::U16, 5, 300, 0, 0},
    {"en_model", KeyType::U8, 0, 2, 0, 0},
    {"en_exp", KeyType::U8, 0, 1, 0, 0},
    {"wa_on", KeyType::U8, 0, 1, 0, 0},
    {"wa_bus", KeyType::U8, 1, 4, 0, 0},
    {"en_host", KeyType::Str, 1, 63, kKeyHost, 0},
    {"en_flat", KeyType::Str, 1, 7, 0, 0},
    {"en_ht", KeyType::Str, 1, 7, 0, 0},
    {"en_nt", KeyType::Str, 1, 7, 0, 0},
    {"en_feed", KeyType::Str, 1, 7, 0, 0},
    {"wa_price", KeyType::Str, 1, 7, 0, 0},
    {"en_htwin", KeyType::Str, 1, 31, 0, 0},
    {"en_switch", KeyType::Str, 10, 10, 0, 0},
    {"pv_on", KeyType::U8, 0, 1, 0, 0},
    {"pv_ip", KeyType::Str, 7, 15, kKeyHost, 0},
    {"pv_port", KeyType::Str, 1, 5, kKeyDigits, 0},  // legacy (Grott stick), unused since cfg_ver 6
    {"pv_gw", KeyType::Str, 7, 15, kKeyHost, 0},
    {"pv_gwport", KeyType::Str, 1, 5, kKeyDigits, 0},
    {"pv_unit", KeyType::Str, 1, 3, kKeyDigits, 0},
    {"pv_hours", KeyType::Blob, sizeof(PvHours), sizeof(PvHours), kKeyInternal, 0},  // PV hours today: never exported
    {"pv_hist", KeyType::Blob, sizeof(PvHist), sizeof(PvHist), kKeyInternal, 0},  // PV day history: never exported
    {"en_hist", KeyType::Blob, sizeof(EnergyHist), sizeof(EnergyHist), kKeyInternal, 0},  // history: never exported
};

bool printable(const char* v) {
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(v); *p; ++p) {
    if (*p < 0x20 || *p == 0x7F) return false;  // control characters (UTF-8 bytes >= 0x80 are fine)
  }
  return true;
}

bool host_ok(const char* v) {
  for (const char* p = v; *p; ++p) {
    if (!(isalnum(static_cast<unsigned char>(*p)) || *p == '.' || *p == '-')) return false;
  }
  return true;
}

}  // namespace

const KeySpec* schema_begin() { return kSchema; }
size_t schema_count() { return sizeof(kSchema) / sizeof(kSchema[0]); }

const KeySpec* schema_find(const char* key) {
  for (const KeySpec& s : kSchema) {
    if (strcmp(s.key, key) == 0) return &s;
  }
  return nullptr;
}

bool schema_check_int(const KeySpec& s, long long v, const char** why) {
  if (s.type != KeyType::U8 && s.type != KeyType::U16) {
    *why = "falscher Typ";
    return false;
  }
  if (v < s.min || v > s.max) {
    *why = "ausserhalb des erlaubten Bereichs";
    return false;
  }
  return true;
}

bool schema_check_str(const KeySpec& s, const char* v, const char** why) {
  if (s.type != KeyType::Str) {
    *why = "falscher Typ";
    return false;
  }
  size_t n = strlen(v);
  if (static_cast<int32_t>(n) < s.min || static_cast<int32_t>(n) > s.max) {
    *why = "falsche Laenge";
    return false;
  }
  if (!printable(v)) {
    *why = "Steuerzeichen";
    return false;
  }
  if (n == 0) return true;  // empty = default where allowed (min 0)
  if ((s.flags & kKeyHttpsOnly) && strncmp(v, "https://", 8) != 0) {
    *why = "nur https:// erlaubt";
    return false;
  }
  if ((s.flags & kKeyUrl) && strncmp(v, "https://", 8) != 0 && strncmp(v, "http://", 7) != 0) {
    *why = "keine http(s)-Adresse";
    return false;
  }
  if ((s.flags & kKeyUrl) && strpbrk(v, " \"'<>\\") != nullptr) {
    *why = "ungueltige Zeichen in der Adresse";
    return false;
  }
  if ((s.flags & kKeyHost) && !host_ok(v)) {
    *why = "kein Hostname";
    return false;
  }
  if (s.flags & kKeyDigits) {
    for (const char* p = v; *p; ++p) {
      if (!isdigit(static_cast<unsigned char>(*p))) {
        *why = "nur Ziffern erlaubt";
        return false;
      }
    }
  }
  return true;
}

bool schema_check_blob(const KeySpec& s, const uint8_t* v, size_t n, const char** why) {
  if (s.type != KeyType::Blob) {
    *why = "falscher Typ";
    return false;
  }
  if (s.flags & kKeyFloat) {
    float f = 0;
    if (n != sizeof(float)) {
      *why = "falsche Laenge";
      return false;
    }
    memcpy(&f, v, sizeof(f));
    if (!(f >= s.min && f <= s.max)) {  // also rejects NaN
      *why = "ausserhalb des erlaubten Bereichs";
      return false;
    }
    return true;
  }
  if (static_cast<int32_t>(n) < s.min || static_cast<int32_t>(n) > s.max || (s.blob_step && n % s.blob_step)) {
    *why = "falsche Laenge";
    return false;
  }
  return true;
}
