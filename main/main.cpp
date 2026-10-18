#include "esp_attr.h"
#include <ArduinoJson.h>

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "lvgl.h"
#include "net.h"
#include "settings.h"
#include "energy_task.h"
#include "pv_task.h"
#include "diag.h"
#include "clock_sync.h"
#include "sdkconfig.h"
#include "sys.h"

#include <stdio.h>
#include <string.h>

#include "config_load.h"
#include "loxone_auth.h"
#include "loxone_client.h"
#include "loxone_structure.h"
#include "panel_hw.h"
#include "panel_ui.h"
#include "weather_derive.h"
#include "webcams.h"
#include "info.h"
#include "maint.h"
#include "ota_check.h"
#include "board_pins.h"
#include "test_harness.h"
#include <esp_log.h>
#include "freertos/idf_additions.h"
#include "strings_de.h"

static const char* const TAG = "app";
static TaskHandle_t ui_handle = nullptr;

#ifndef SERIAL_BAUD
#define SERIAL_BAUD CONFIG_PANEL_SERIAL_BAUD
#endif

namespace {

enum class Phase { NotConfigured, Wifi, Loading, Ready, Failed };

Phase phase = Phase::NotConfigured;
bool load_painted = false;
uint32_t wifi_started_ms = 0;
constexpr uint32_t kWifiTimeoutMs = 30000;  // boot: then open the WLAN selection
constexpr uint32_t kWifiSwitchMs = 20000;   // settings: then revert to the previous network
constexpr int kWifiAuthFails = 3;
constexpr int kWifiNoApFails = 3;
bool wifi_setup_auto = false;  // WLAN selection opened by the firmware (boot failure / no Wi-Fi)
constexpr size_t kStructureCap = 768 * 1024;

LoxoneTokenAuthorizer token_auth;
LoxoneAuthorizer* auth = nullptr;
uint32_t next_weather_ms = 0;

// Buchs, St. Gallen. Open-Meteo needs no key. TLS is checked against the
// IDF certificate bundle.
// One request: current conditions, 7 daily summaries and hourly values (the
// hourly time array is filtered out; hour i of day d is index d*24+i).
// The parsed document (~15 KB) is allocated from the heap, which puts large
// blocks in PSRAM on this board.
EXT_RAM_BSS_ATTR static WeatherDetailDay week[kWxDays];
// Weather location; written only by the network task (and before it starts).
Location g_loc;
WifiAp g_aps[kMaxAps];
GeoPlace g_places[kMaxPlaces];

void url_encode(const char* in, char* out, size_t n) {
  static const char* hex = "0123456789ABCDEF";
  size_t o = 0;
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(in); *p != 0 && o + 4 < n; ++p) {
    if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '-' ||
        *p == '.' || *p == '_') {
      out[o++] = static_cast<char>(*p);
    } else {
      out[o++] = '%';
      out[o++] = hex[*p >> 4];
      out[o++] = hex[*p & 15];
    }
  }
  out[o] = '\0';
}

// Open-Meteo geocoding: place name -> up to kMaxPlaces results (German names).
int geocode(const char* query, GeoPlace* out) {
  char q[160];
  url_encode(query, q, sizeof(q));
  char url[300];
  snprintf(url, sizeof(url), "https://geocoding-api.open-meteo.com/v1/search?name=%s&language=de&count=%d&format=json",
           q, kMaxPlaces);
  constexpr size_t kCap = 160 * 1024;  // results carry long postcode lists
  uint8_t* body = static_cast<uint8_t*>(heap_caps_malloc(kCap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (body == nullptr) return -1;
  HttpGetResult http = http_get(url, nullptr, nullptr, body, kCap, 10000);
  if (!http.ok || http.status != 200) {
    ESP_LOGW(TAG, "Geocode HTTP %d %s", http.status, http.error);
    heap_caps_free(body);
    return -1;
  }
  StaticJsonDocument<256> filter;
  for (const char* k : {"name", "admin1", "country_code", "latitude", "longitude"}) filter["results"][0][k] = true;
  DynamicJsonDocument doc(8192);
  DeserializationError err = deserializeJson(doc, reinterpret_cast<const char*>(body), http.length,
                                             DeserializationOption::Filter(filter));  // const: copies strings
  heap_caps_free(body);
  if (err) {
    ESP_LOGW(TAG, "Geocode JSON %s", err.c_str());
    return -1;
  }
  int n = 0;
  for (JsonObject r : doc["results"].as<JsonArray>()) {
    if (n >= kMaxPlaces) break;
    GeoPlace& p = out[n];
    snprintf(p.name, sizeof(p.name), "%s", r["name"] | "?");
    const char* a1 = r["admin1"] | "";
    const char* cc = r["country_code"] | "";
    snprintf(p.region, sizeof(p.region), "%s%s%s", a1, (a1[0] != '\0' && cc[0] != '\0') ? ", " : "", cc);
    p.lat = r["latitude"] | 0.0f;
    p.lon = r["longitude"] | 0.0f;
    n++;
  }
  console_printf("Geocode \"%s\" -> %d results (%u bytes)\n", query, n, static_cast<unsigned>(http.length));
  return n;
}

bool fetch_buchs_weather(int* temp_c, int* code, WeatherDay* days, int* week_count) {
  constexpr size_t kCap = 64 * 1024;
  uint8_t* payload = static_cast<uint8_t*>(heap_caps_malloc(kCap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (payload == nullptr) {
    return false;
  }
  char url[512];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&current=temperature_2m,weather_code"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_sum,"
           "sunrise,sunset,wind_speed_10m_max,precipitation_probability_max"
           "&hourly=temperature_2m,precipitation,precipitation_probability,weather_code,wind_speed_10m,cloud_cover"
           "&timezone=Europe%%2FZurich&forecast_days=7",
           g_loc.lat, g_loc.lon);
  HttpGetResult http = http_get(url, nullptr, nullptr, payload, kCap, 10000);
  if (!http.ok || http.status != 200) {
    ESP_LOGW(TAG, "Weather HTTP %d %s", http.status, http.error);
    heap_caps_free(payload);
    return false;
  }
  StaticJsonDocument<768> filter;
  filter["current"]["temperature_2m"] = true;
  filter["current"]["weather_code"] = true;
  const char* dkeys[] = {"time", "weather_code", "temperature_2m_max", "temperature_2m_min", "precipitation_sum",
                         "sunrise", "sunset", "wind_speed_10m_max", "precipitation_probability_max"};
  for (const char* k : dkeys) filter["daily"][k] = true;
  const char* hkeys[] = {"temperature_2m", "precipitation", "precipitation_probability", "weather_code",
                         "wind_speed_10m", "cloud_cover"};
  for (const char* k : hkeys) filter["hourly"][k] = true;
  DynamicJsonDocument doc(24576);
  DeserializationError err =
      deserializeJson(doc, reinterpret_cast<const char*>(payload), http.length,
                      DeserializationOption::Filter(filter));  // const input: strings are copied, payload can be freed
  heap_caps_free(payload);
  if (err) {
    ESP_LOGW(TAG, "Weather JSON %s (%u bytes)", err.c_str(), static_cast<unsigned>(http.length));
    return false;
  }
  JsonVariant temp = doc["current"]["temperature_2m"];
  JsonVariant wcode = doc["current"]["weather_code"];
  if (temp.isNull() || wcode.isNull()) {
    return false;
  }
  float value = temp.as<float>();
  *temp_c = static_cast<int>(value >= 0.f ? value + 0.5f : value - 0.5f);
  *code = wcode.as<int>();
  JsonObject daily = doc["daily"];
  JsonObject hourly = doc["hourly"];
  int n = 0;
  for (int d = 0; d < kWxDays; ++d) {
    if (daily["weather_code"][d].isNull() || daily["temperature_2m_max"][d].isNull() ||
        daily["temperature_2m_min"][d].isNull()) {
      break;
    }
    WeatherDetailDay& w = week[d];
    memset(&w, 0, sizeof(w));
    snprintf(w.date, sizeof(w.date), "%s", daily["time"][d] | "");
    const char* sr = daily["sunrise"][d] | "";  // "2026-10-12T07:38"
    const char* ss = daily["sunset"][d] | "";
    snprintf(w.sunrise, sizeof(w.sunrise), "%s", strlen(sr) >= 16 ? sr + 11 : "--:--");
    snprintf(w.sunset, sizeof(w.sunset), "%s", strlen(ss) >= 16 ? ss + 11 : "--:--");
    w.code = daily["weather_code"][d].as<int>();
    w.t_max = daily["temperature_2m_max"][d].as<float>();
    w.t_min = daily["temperature_2m_min"][d].as<float>();
    w.precip = daily["precipitation_sum"][d] | 0.0f;
    w.prob_max = daily["precipitation_probability_max"][d] | 0;
    w.wind_max = daily["wind_speed_10m_max"][d] | 0.0f;
    WxHour hrs[24];
    for (int i = 0; i < 24; ++i) {
      int k = d * 24 + i;
      hrs[i].code = hourly["weather_code"][k] | 0;
      hrs[i].precip = hourly["precipitation"][k] | 0.0f;
      hrs[i].temp = hourly["temperature_2m"][k] | 0.0f;
      hrs[i].cloud = hourly["cloud_cover"][k] | 50.0f;
    }
    // Daytime window: sunrise hour .. sunset hour, else 06-21.
    int h_rise = strlen(sr) >= 13 ? atoi(sr + 11) : 6;
    int h_set = strlen(ss) >= 13 ? atoi(ss + 11) : 21;
    if (h_rise < 0 || h_rise > 23 || h_set < h_rise || h_set > 23) {
      h_rise = 6;
      h_set = 21;
    }
    w.kind = static_cast<int>(derive_day(hrs + h_rise, h_set - h_rise + 1, w.precip));
    for (int s = 0; s < kWxSlots; ++s) {
      int h0 = d * 24 + s * 3;
      WeatherSlot& sl = w.slot[s];
      sl.temp = hourly["temperature_2m"][h0] | 0.0f;
      sl.code = derive_slot_code(hrs + s * 3, 3);
      for (int k = 0; k < 3; ++k) {
        sl.precip += hourly["precipitation"][h0 + k] | 0.0f;
        int pr = hourly["precipitation_probability"][h0 + k] | 0;
        if (pr > sl.prob) sl.prob = pr;
        float wi = hourly["wind_speed_10m"][h0 + k] | 0.0f;
        if (wi > sl.wind) sl.wind = wi;
      }
    }
    if (d < 2) {
      days[d].code = w.code;
      days[d].t_max = w.t_max;
      days[d].t_min = w.t_min;
      days[d].precip = w.precip;
      days[d].kind = w.kind;
      console_printf("Forecast %s %s daily code %d -> %s, max %.1f min %.1f rain %.1f mm\n",
                    d == 0 ? "today" : "tomorrow", w.date, w.code, day_kind_name(static_cast<DayKind>(w.kind)),
                    w.t_max, w.t_min, w.precip);
    }
    n++;
  }
  *week_count = n;
  console_printf("Weather week %d days, JSON %u bytes, doc %u bytes\n", n, static_cast<unsigned>(http.length),
                static_cast<unsigned>(doc.memoryUsage()));
  return n >= 2;
}



// ---------------------------------------------------------------------------
// Tasks
//   net   (core 0): Wi-Fi, Miniserver HTTP, Open-Meteo, JSON parsing (yielding).
//   ui    (core 1): LVGL, touch, test harness. The only task that calls LVGL.
// The UI posts one NetRequest at a time to net_q; the network task answers on
// res_q. Big results (controls, week) live in static storage that only the
// network task writes before posting and only the UI task reads afterwards.

enum class NetOp : uint8_t { Connect, Load, Command, State, Weather, Settings, Camera, Transit, Calendar, Meteo, Ota, TestBusy };

struct NetRequest {
  NetOp op;
  LoxoneCommandRequest command;
  LoxoneStateRequest state;
  SettingsRequest settings;
  CamRequest cam;
  int station;      // Transit: board index
  char url[256];    // Ota
  uint32_t busy_ms; // TestBusy (harness): keep the net task busy
  bool ota_check;   // Ota: only check for a newer version
  bool ota_own;     // Ota check: url set by the user
};

struct NetResult {
  NetOp op;
  bool ok;
  int index;
  char title[40];
  char detail[120];
  char value[64];
  char command[16];
  // Load
  size_t shown;
  size_t supported;
  char server[48];
  // Weather
  int temp_c;
  int code;
  WeatherDay days[2];
  int week_count;
  // Settings
  uint8_t settings_kind;
  int count;
  Location location;
  // Camera / CamLookup
  int status;
  CamInfo cam;
  uint16_t* frame;
};

constexpr int kMaxFound = 20;
CamInfo* g_found = nullptr;  // PSRAM, kMaxFound entries; CamNearby results (network task writes, UI reads after the result)

// Windy key for this request (own NVS key, else the build default).
bool windy_key(char* key, size_t n) {
  bool own = false;
  return settings_load_windy_key(key, n, &own);
}

// Results too big for the queue: written by the network task before it posts
// the result, read by the UI task in handle_result (one request in flight).
Board g_board;
OtaCheck g_ota;
EXT_RAM_BSS_ATTR Calendar g_cal;
EXT_RAM_BSS_ATTR MeteoNow g_meteo;
Station g_hits[kMaxStationHits];

QueueHandle_t net_q = nullptr;
QueueHandle_t res_q = nullptr;
// Loxone commands/state reads have their own task and queue (core 0, above
// the net task), so an OTA download, camera fetch or Wi-Fi switch on the net
// task never delays a light switch. One Loxone request in flight.
QueueHandle_t lox_q = nullptr;
bool net_busy = false;
bool lox_busy = false;
EXT_RAM_BSS_ATTR LoxoneControl controls[kLoxoneControlCap];

void net_post(const NetRequest& r) {
  net_busy = true;
  xQueueSend(net_q, &r, portMAX_DELAY);
}

void lox_post(const NetRequest& r) {
  lox_busy = true;
  xQueueSend(lox_q, &r, portMAX_DELAY);
}

void net_load(NetResult* res) {
  uint8_t* buf = static_cast<uint8_t*>(heap_caps_malloc(kStructureCap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (buf == nullptr) {
    snprintf(res->title, sizeof(res->title), "%s", str::kOutOfMemoryTitle);
    snprintf(res->detail, sizeof(res->detail), "%s", str::kOutOfMemoryBody);
    return;
  }
  size_t length = 0;
  LoxoneHttpResult http = loxone_get(*auth, "/data/LoxAPP3.json", buf, kStructureCap - 1, &length);
  if (!http.ok) {
    heap_caps_free(buf);
    snprintf(res->title, sizeof(res->title), "%s", str::kUnreachableTitle);
    snprintf(res->detail, sizeof(res->detail), "%s", http.detail);
    return;
  }
  bool parsed = loxone_parse_structure(buf, length, controls, kLoxoneControlCap, &res->shown, &res->supported,
                                       res->server, sizeof(res->server), res->detail, sizeof(res->detail));
  heap_caps_free(buf);
  if (!parsed) {
    snprintf(res->title, sizeof(res->title), "%s", str::kLoadFailedTitle);
    return;
  }
  console_printf("LoxAPP3 %u bytes, %u controls shown, %u supported\n", static_cast<unsigned>(length),
                 static_cast<unsigned>(res->shown), static_cast<unsigned>(res->supported));
  res->ok = true;
}

void net_settings(const SettingsRequest& s, NetResult* res) {
  res->settings_kind = s.kind;
  switch (s.kind) {
    case SettingsRequest::Scan:
      res->count = wifi_scan(g_aps, kMaxAps);
      res->ok = res->count > 0;
      console_printf("Wi-Fi scan %d networks\n", res->count);
      break;
    case SettingsRequest::Connect: {
      WifiCreds old;
      bool have_old = settings_load_wifi(&old);
      console_printf("Wi-Fi switching to \"%s\"\n", s.creds.ssid);
      wifi_start(s.creds.ssid, s.creds.pass, "loxone-panel");
      // Give up after 20 s, or earlier on repeated hard failures. A single
      // rejection is not enough: some WPA3 routers refuse the first SAE attempt.
      uint32_t t0 = now_ms();
      for (;;) {
        uint32_t el = now_ms() - t0;
        if (wifi_connected() || el > kWifiSwitchMs) break;
        if (el > 8000 && wifi_auth_fails() >= kWifiAuthFails) break;
        if (el > 10000 && wifi_noap_fails() >= kWifiNoApFails) break;
        wifi_wait(1000);  // wakes on got-IP or disconnect, re-checks the counters
      }
      res->ok = wifi_connected();
      snprintf(res->detail, sizeof(res->detail), "%s", s.creds.ssid);
      // status: 0 ok, 1 password/auth, 2 network not found, 3 timeout
      res->status = res->ok ? 0 : wifi_auth_fails() > 0 ? 1 : wifi_noap_fails() > 0 ? 2 : 3;
      if (res->ok) {
        settings_save_wifi(s.creds);  // only a network that really got an IP is stored
        console_printf("Wi-Fi \"%s\" connected and saved\n", s.creds.ssid);
      } else if (have_old) {
        ESP_LOGW(TAG, "Wi-Fi \"%s\" failed (status %d, reason %d), back to \"%s\"", s.creds.ssid, res->status,
                       wifi_last_reason(), old.ssid);
        snprintf(res->title, sizeof(res->title), "%s", old.ssid);
        wifi_start(old.ssid, old.pass, "loxone-panel");
        t0 = now_ms();
        while (!wifi_connected() && now_ms() - t0 < kWifiTimeoutMs) wifi_wait(kWifiTimeoutMs - (now_ms() - t0));
        res->index = wifi_connected() ? 1 : 0;  // reverted and connected again
        console_printf("Wi-Fi back on \"%s\": %s\n", old.ssid, wifi_connected() ? "connected" : "still trying");
      } else {
        ESP_LOGW(TAG, "Wi-Fi \"%s\" failed (status %d, reason %d), no previous network", s.creds.ssid,
                       res->status, wifi_last_reason());
      }
      memset(&old, 0, sizeof(old));
      break;
    }
    case SettingsRequest::Geocode:
      res->count = geocode(s.query, g_places);
      res->ok = res->count >= 0;
      if (res->count < 0) res->count = 0;
      break;
    case SettingsRequest::SetLocation:
      g_loc = s.location;
      res->ok = settings_save_location(g_loc);
      res->location = g_loc;
      console_printf("Location %s %.4f %.4f\n", g_loc.name, g_loc.lat, g_loc.lon);
      break;
    case SettingsRequest::SetTime:
      res->ok = settings_save_time(s.time);
      clock_sntp_start(s.time.server);
      console_printf("Time server %s, zone %s (%s) saved=%d\n", s.time.server, s.time.tz_name, s.time.tz, res->ok ? 1 : 0);
      break;
    case SettingsRequest::CamLookup: {
      char key[72];
      if (!windy_key(key, sizeof(key))) {
        res->status = 401;
        break;
      }
      res->ok = windy_cam_info(key, s.cam_id, &res->cam, &res->status);
      memset(res->cam.url, 0, sizeof(res->cam.url));
      console_printf("Webcam lookup %lu -> %s (HTTP %d)\n", static_cast<unsigned long>(s.cam_id),
                     res->ok ? res->cam.title : "-", res->status);
      break;
    }
    case SettingsRequest::CamNearby: {
      char key[72];
      if (!windy_key(key, sizeof(key))) {
        res->status = 401;
        break;
      }
      if (g_found == nullptr) break;
      int places = geocode(s.query, g_places);
      if (places <= 0) {
        res->status = places == 0 ? 404 : 0;
        snprintf(res->detail, sizeof(res->detail), "%s", s.query);
        break;
      }
      snprintf(res->detail, sizeof(res->detail), "%s", g_places[0].name);
      res->count = windy_nearby(key, g_places[0].lat, g_places[0].lon, 25, g_found, kMaxFound, &res->status);
      res->ok = res->count >= 0;
      if (res->count < 0) res->count = 0;
      break;
    }
    case SettingsRequest::SetCams:
      res->ok = settings_save_cams(s.cams, s.cam_n);
      console_printf("Webcams saved: %d (ok=%d)\n", s.cam_n, res->ok ? 1 : 0);
      break;
    case SettingsRequest::SetWindyKey:
      res->ok = settings_save_windy_key(s.key);
      console_printf("Windy key %s (ok=%d)\n", s.key[0] != '\0' ? "saved" : "reset to default", res->ok ? 1 : 0);
      break;
    case SettingsRequest::StationSearch:
      res->count = transit_search(s.query, g_hits, kMaxStationHits);
      res->ok = res->count >= 0;
      if (res->count < 0) res->count = 0;
      break;
    case SettingsRequest::SetStation: {
      Station st = {};
      snprintf(st.id, sizeof(st.id), "%s", s.station_id);
      snprintf(st.name, sizeof(st.name), "%s", s.station_name);
      res->ok = settings_save_station(s.station_idx, st);
      res->index = s.station_idx;
      console_printf("Station %d = %s (%s) saved=%d\n", s.station_idx, st.name, st.id, res->ok ? 1 : 0);
      break;
    }
    case SettingsRequest::SetText:
      res->ok = settings_save_text(static_cast<TextSetting>(s.text_which), s.text);
      res->index = s.text_which;
      console_printf("Setting text %d %s (ok=%d)\n", s.text_which, s.text[0] != '\0' ? "saved" : "reset to default",
                     res->ok ? 1 : 0);  // never log the value (private calendar URL)
      break;
    case SettingsRequest::ResetLocation:
      settings_reset_location();
      g_loc = settings_default_location();
      res->ok = true;
      res->location = g_loc;
      console_printf("Location reset to %s\n", g_loc.name);
      break;
  }
}

void lox_task(void*) {
  for (;;) {
    NetRequest req;
    if (xQueueReceive(lox_q, &req, portMAX_DELAY) != pdTRUE) continue;
    static NetResult res;
    memset(&res, 0, sizeof(res));
    res.op = req.op;
    switch (req.op) {
      case NetOp::Command: {
        LoxoneHttpResult r = loxone_send_command(*auth, req.command.action, req.command.command);
        res.ok = r.ok;
        res.index = req.command.index;
        snprintf(res.command, sizeof(res.command), "%s", req.command.command);
        snprintf(res.detail, sizeof(res.detail), "%s", r.detail);
        console_printf("Command %s -> %s\n", req.command.command, r.ok ? "ok" : r.detail);
        break;
      }
      case NetOp::State: {
        LoxoneHttpResult r = loxone_read_state(*auth, req.state.action, req.state.state, req.state.kind,
                                               res.value, sizeof(res.value));
        res.ok = r.ok;
        res.index = req.state.index;
        console_printf("State %d -> %s (%s)\n", req.state.index, r.ok ? res.value : "-", r.ok ? "ok" : r.detail);
        break;
      }
      default:
        break;
    }
    xQueueSend(res_q, &res, portMAX_DELAY);
    if (ui_handle != nullptr) xTaskNotifyGive(ui_handle);
  }
}

void net_task(void*) {
  for (;;) {
    NetRequest req;
    if (xQueueReceive(net_q, &req, portMAX_DELAY) != pdTRUE) {
      continue;
    }
    static NetResult res;
    memset(&res, 0, sizeof(res));
    res.op = req.op;
    switch (req.op) {
      case NetOp::Connect: {
        WifiCreds creds;
        settings_load_wifi(&creds);
        wifi_start(creds.ssid, creds.pass, "loxone-panel");
        clock_sntp_start(settings_load_time().server);  // lwIP retries until the link is up
        res.ok = true;
        break;
      }
      case NetOp::Settings:
        net_settings(req.settings, &res);
        break;
      case NetOp::Load:
        net_load(&res);
        break;
      case NetOp::Command:
      case NetOp::State:
        break;  // handled by lox_task
      case NetOp::TestBusy:
        console_printf("Net busy test %lu ms\n", (unsigned long)req.busy_ms);
        vTaskDelay(pdMS_TO_TICKS(req.busy_ms));
        res.ok = true;
        break;
      case NetOp::Camera: {
        // Fresh details first: the image URL token expires (10 min, free tier).
        char key[72];
        res.cam.id = req.cam.id;
        res.frame = req.cam.frame;
        if (!windy_key(key, sizeof(key))) {
          res.status = 401;
          snprintf(res.detail, sizeof(res.detail), "Kein Windy API-Key");
          break;
        }
        if (!windy_cam_info(key, req.cam.id, &res.cam, &res.status)) {
          snprintf(res.detail, sizeof(res.detail), "Windy HTTP %d", res.status);
          res.cam.id = req.cam.id;
          break;
        }
        res.ok = windy_cam_image(res.cam.url, req.cam.frame, res.detail, sizeof(res.detail));
        memset(res.cam.url, 0, sizeof(res.cam.url));  // tokenised URL: used once, never kept
        console_printf("Webcam %lu %s (internal free %u, largest %u)\n", static_cast<unsigned long>(req.cam.id),
                       res.ok ? "ok" : res.detail, static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                       static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
        break;
      }
      case NetOp::Transit: {
        Station st[kStationN];
        settings_load_stations(st);
        int i = req.station >= 0 && req.station < kStationN ? req.station : 0;
        res.index = i;
        res.ok = transit_board(st[i], &g_board);
        break;
      }
      case NetOp::Calendar: {
        char* ical = static_cast<char*>(heap_caps_malloc(640, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (ical == nullptr) break;
        char* waste = ical + 256;
        char* bfs = ical + 512;
        char* zone = ical + 528;
        settings_load_text(kUrlIcal, ical, 256, nullptr);
        settings_load_text(kUrlWaste, waste, 256, nullptr);
        settings_load_text(kTextWasteBfs, bfs, 16, nullptr);
        settings_load_text(kTextWasteZone, zone, 112, nullptr);
        res.ok = calendar_fetch(ical, waste, bfs, zone, &g_cal);
        memset(ical, 0, 640);
        heap_caps_free(ical);
        break;
      }
      case NetOp::Meteo: {
        char plz[16];
        settings_load_text(kTextPlz, plz, sizeof(plz), nullptr);
        res.ok = meteo_fetch(plz, &g_meteo);
        break;
      }
      case NetOp::Ota:
        if (req.ota_check) {
          res.index = 1;  // marks a check result
          res.ok = ota_check(req.url, req.ota_own, &g_ota);
          break;
        }
        console_printf("OTA from %s\n", req.url);
        res.ok = maint_ota_from_url(req.url, res.detail, sizeof(res.detail));
        console_printf("OTA %s %s\n", res.ok ? "installed" : "failed", res.ok ? "" : res.detail);
        break;
      case NetOp::Weather:
        res.ok = fetch_buchs_weather(&res.temp_c, &res.code, res.days, &res.week_count);
        console_printf("Weather %s\n", res.ok ? "ok" : "unavailable");
        break;
    }
    xQueueSend(res_q, &res, portMAX_DELAY);
    if (ui_handle != nullptr) xTaskNotifyGive(ui_handle);  // wake the UI loop for the result
  }
  vTaskDelete(nullptr);  // not reached
}

// ---------------------------------------------------------------------------
// UI side (runs in the UI task only)

void start_wifi() {
  NetRequest r = {};
  r.op = NetOp::Connect;
  net_post(r);
  wifi_started_ms = now_ms();
}

void handle_result(const NetResult& res) {
  if (res.op != NetOp::Command && res.op != NetOp::State) net_busy = false;
  switch (res.op) {
    case NetOp::Connect:
      break;
    case NetOp::Load:
      if (!res.ok) {
        phase = Phase::Failed;
        ui_show_message(res.title, res.detail, true);
        break;
      }
      phase = Phase::Ready;
      ui_show_controls(controls, res.shown, res.supported, res.server[0] != '\0' ? res.server : "Loxone");
      if (!panel_touch_ok()) {
        ui_set_status(str::kStatusNoTouch);
      }
      break;
    case NetOp::Command:
      lox_busy = false;
      ui_command_finished(res.index, res.ok, res.command, res.detail);
      break;
    case NetOp::State:
      lox_busy = false;
      ui_state_finished(res.index, res.ok, res.ok ? res.value : nullptr);
      break;
    case NetOp::Settings:
      switch (res.settings_kind) {
        case SettingsRequest::Scan:
          ui_wifi_scan_done(g_aps, res.count);
          break;
        case SettingsRequest::Connect:
          ui_wifi_connect_done(res.ok, res.detail, wifi_ssid(), res.status, res.title, res.index == 1);
          if (res.ok && phase == Phase::Failed) {  // changed from the Wi-Fi failed screen
            phase = Phase::Wifi;
            wifi_started_ms = now_ms();
          }
          break;
        case SettingsRequest::Geocode:
          ui_geocode_done(res.ok, g_places, res.count);
          break;
        case SettingsRequest::SetTime:
          ui_time_done(res.ok);
          break;
        case SettingsRequest::CamLookup:
          ui_cam_lookup_done(res.ok, res.status, res.cam);
          break;
        case SettingsRequest::CamNearby:
          ui_cam_nearby_done(res.ok, res.status, g_found, res.count, res.detail);
          break;
        case SettingsRequest::SetCams:
          ui_cams_saved(res.ok);
          break;
        case SettingsRequest::SetWindyKey:
          ui_windy_key_done(res.ok);
          break;
        case SettingsRequest::StationSearch:
          ui_station_search_done(res.ok, g_hits, res.count);
          break;
        case SettingsRequest::SetStation:
          ui_station_saved(res.ok, res.index);
          break;
        case SettingsRequest::SetText:
          ui_text_saved(res.ok, static_cast<uint8_t>(res.index));
          break;
        default:
          ui_location_done(res.ok, res.location.name);
          next_weather_ms = now_ms();  // refresh the weather right away
          break;
      }
      break;
    case NetOp::Camera:
      ui_cam_done(res.ok, res.status, res.cam, res.frame, res.detail);
      break;
    case NetOp::Transit:
      ui_transit_done(res.index, res.ok, g_board);
      break;
    case NetOp::Calendar:
      ui_calendar_done(res.ok, g_cal);
      break;
    case NetOp::Meteo:
      ui_meteo_done(res.ok, g_meteo);
      break;
    case NetOp::Ota:
      if (res.index == 1) ui_ota_checked(g_ota);
      else ui_ota_done(res.ok, res.detail);
      break;
    case NetOp::TestBusy:
      console_puts("Net busy test done");
      break;
    case NetOp::Weather:
      ui_set_weather(res.ok, res.temp_c, res.code);
      ui_set_forecast(res.ok, res.days, 2);
      if (res.ok) {
        ui_set_week(week, res.week_count);
      }
      // Every 30 min; retry after 1 min on errors (last forecast stays).
      next_weather_ms = now_ms() + (res.ok ? 1800000UL : 60000UL);
      break;
  }
}

void poll() {
  maint_verify_tick();
  NetResult res;
  while (xQueueReceive(res_q, &res, 0) == pdTRUE) {
    handle_result(res);
  }
  // Loxone path first and independent of the net task.
  if (phase == Phase::Ready && !lox_busy && wifi_connected()) {
    NetRequest r = {};
    if (ui_take_command(&r.command)) {
      r.op = NetOp::Command;
      lox_post(r);
    } else if (ui_take_state(&r.state)) {
      r.op = NetOp::State;
      lox_post(r);
    }
  }
  if (net_busy) {
    return;
  }
  switch (phase) {
    case Phase::NotConfigured:
      break;
    case Phase::Wifi:
      if (wifi_connected()) {
        console_printf("Wi-Fi connected, Miniserver %s:%d auth %s\n", settings_loxone().host, settings_loxone().port, auth->name());
        phase = Phase::Loading;
        load_painted = false;
        clock_sntp_kick();
        maint_mark_valid();  // UI runs and Wi-Fi works: keep this image after an OTA update
        ui_set_wifi_name(wifi_ssid());
        ui_set_status(str::kStatusConnected);
      } else if (now_ms() - wifi_started_ms > kWifiTimeoutMs) {
        // Not on the first rejection (WPA3 routers often refuse the first SAE
        // attempt): only after the timeout. The saved network stays in NVS and
        // keeps being retried in the background.
        phase = Phase::Failed;
        wifi_setup_auto = true;
        console_printf("Wi-Fi \"%s\" not reachable after %u s (auth %d, no AP %d, reason %d): WLAN selection\n",
                       wifi_ssid(), static_cast<unsigned>(kWifiTimeoutMs / 1000), wifi_auth_fails(), wifi_noap_fails(),
                       wifi_last_reason());
        ui_wifi_setup_open(wifi_ssid(), wifi_auth_fails() > 0 ? 1 : wifi_noap_fails() > 0 ? 2 : 3);
      }
      break;
    case Phase::Loading: {
      if (!load_painted) {
        ui_show_message(str::kLoadingTitle, str::kLoadingBody, false);
        load_painted = true;
        break;
      }
      if (lox_busy) break;  // the structure load and Loxone requests share the client
      NetRequest r = {};
      r.op = NetOp::Load;
      net_post(r);
      break;
    }
    case Phase::Ready: {
      if (!wifi_connected()) {
        ui_set_status(str::kStatusDisconnected);
        break;
      }
      NetRequest r = {};
      if (ui_take_settings(&r.settings)) {
        r.op = NetOp::Settings;
        net_post(r);
      } else if (ui_take_cam(&r.cam)) {
        r.op = NetOp::Camera;
        net_post(r);
      } else if (ui_take_ota(r.url, sizeof(r.url), &r.ota_check, &r.ota_own)) {
        r.op = NetOp::Ota;
        net_post(r);
      } else if (ui_take_transit(&r.station)) {
        r.op = NetOp::Transit;
        net_post(r);
      } else if (static_cast<int32_t>(now_ms() - next_weather_ms) >= 0) {
        r.op = NetOp::Weather;
        net_post(r);
      } else if (ui_take_meteo()) {
        r.op = NetOp::Meteo;
        net_post(r);
      } else if (ui_take_calendar()) {
        r.op = NetOp::Calendar;
        net_post(r);
      }
      break;
    }
    case Phase::Failed: {
      NetRequest r = {};
      if (ui_take_settings(&r.settings)) {  // Wi-Fi can be changed while offline
        r.op = NetOp::Settings;
        net_post(r);
        break;
      }
      if (wifi_setup_auto && wifi_connected() && !ui_wifi_busy()) {
        // The saved network came back while the WLAN selection was open.
        console_printf("Wi-Fi \"%s\" is back: closing the WLAN selection\n", wifi_ssid());
        wifi_setup_auto = false;
        ui_wifi_setup_close();
        phase = Phase::Wifi;
        wifi_started_ms = now_ms();
        break;
      }
      if (ui_take_retry()) {
        start_wifi();
        phase = Phase::Wifi;
        ui_show_message(str::kConnectingTitle, str::kConnectingBody, false);
        ui_set_status(str::kStatusConnecting);
      }
      break;
    }
  }
}

void halt(const char* why) {
  console_puts(why);
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void ui_task(void*) {
#ifdef CONFIG_PANEL_TEST_HARNESS
  console_begin(SERIAL_BAUD, 1024, 16384);
#else
  console_begin(SERIAL_BAUD, 1024, 4096);
#endif
  vTaskDelay(pdMS_TO_TICKS(200));
  console_puts("ESP32-4848S040C_I_Y_1 Loxone panel (ESP-IDF)");
  console_printf("Flash %u bytes, PSRAM %u bytes\n", static_cast<unsigned>(sys_flash_size()),
                 static_cast<unsigned>(sys_psram_size()));
  panel_relay_begin();
  console_printf("Relay 1 GPIO%d off (level %d). GPIO%d and GPIO%d left unconfigured\n", kPinRelay1,
                 kRelayOffLevel, kPinRelay2, kPinRelay3);
  if (!panel_display_begin()) halt("Display init failed");
  panel_touch_begin();
  if (!panel_lvgl_start()) halt("LVGL init failed");
  ui_init();
  ui_set_place(g_loc.name);

  const LoxoneConfig& lox = settings_loxone();
  static LoxoneBasicAuthorizer basic_auth(lox.user, lox.pass);
  WifiCreds wc;
  bool have_wifi = settings_load_wifi(&wc);
  bool configured = lox.host[0] != '\0' && lox.user[0] != '\0';
  memset(&wc, 0, sizeof(wc));
  if (strcmp(lox.auth, "basic") == 0) {
    auth = &basic_auth;
  } else if (strcmp(lox.auth, "token") == 0) {
    auth = &token_auth;
  }

  if (!configured) {
    phase = Phase::NotConfigured;
    console_puts(PANEL_CONFIG_PRESENT ? "panel_config.h is missing Wi-Fi, host, or user"
                                      : "panel_config.h not found; not configured");
    ui_show_message(str::kNotConfiguredTitle, str::kNotConfiguredBody, false);
    ui_set_status(str::kStatusRelayOff);
  } else if (!have_wifi) {
    // Miniserver known, but no Wi-Fi saved: straight to the WLAN selection.
    phase = Phase::Failed;
    wifi_setup_auto = true;
    console_puts("No Wi-Fi configured: WLAN selection");
    wifi_start("", "", "loxone-panel");  // station up for scanning, no network
    ui_wifi_setup_open("", 0);
  } else if (auth == nullptr) {
    phase = Phase::Failed;
    ui_show_message(str::kNotConfiguredTitle, str::kBadAuthBody, false);
  } else {
    console_printf("Auth scheme %s\n", auth->name());
    start_wifi();
    phase = Phase::Wifi;
    ui_show_message(str::kConnectingTitle, str::kConnectingBody, false);
    ui_set_status(str::kStatusConnecting);
  }

  diag_start();
  uint32_t last_ms = now_ms();
  for (;;) {
    uint32_t now = now_ms();
    lv_tick_inc(now - last_ms);
    last_ms = now;
    int64_t t0 = esp_timer_get_time();
    lv_timer_handler();
    int64_t t1 = esp_timer_get_time();
    harness_poll();
    poll();
    int64_t t2 = esp_timer_get_time();
    uint32_t next_ms = lv_timer_handler();
    int64_t t3 = esp_timer_get_time();
    diag_ui_loop(static_cast<uint32_t>((t1 - t0) > (t3 - t2) ? (t1 - t0) : (t3 - t2)), static_cast<uint32_t>(t2 - t1),
                 static_cast<uint32_t>(t3 - t0));
    if (phase == Phase::Ready) diag_ui_first_interactive();
    // Sleep until LVGL's next timer is due (max 10 ms for console/harness
    // polling) or the network task notifies a result.
    uint32_t wait = next_ms < 1 ? 1 : (next_ms > 10 ? 10 : next_ms);
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait));
  }
  vTaskDelete(nullptr);  // not reached
}

}  // namespace

#ifdef CONFIG_PANEL_TEST_HARNESS
// Test harness (UI task): occupy the net task for ms, e.g. to check that
// Loxone requests still run meanwhile.
bool app_test_net_busy(uint32_t ms) {
  if (net_busy) return false;
  NetRequest r = {};
  r.op = NetOp::TestBusy;
  r.busy_ms = ms;
  net_post(r);
  return true;
}
#endif

void app_start() {
  settings_migrate();
  settings_seed_from_build();
  g_loc = settings_load_location();
  g_found = static_cast<CamInfo*>(heap_caps_calloc(kMaxFound, sizeof(CamInfo), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  net_q = xQueueCreate(2, sizeof(NetRequest));
  res_q = xQueueCreate(3, sizeof(NetResult));
  lox_q = xQueueCreate(1, sizeof(NetRequest));
  // Network and protocol work on core 0 (with the Wi-Fi stack), UI on core 1.
  xTaskCreatePinnedToCore(net_task, "net", 12 * 1024, nullptr, 4, nullptr, 0);
  // Stack in PSRAM (8 KB, ~5.2 KB used): internal RAM is scarce, and with
  // SPIRAM_XIP_FROM_PSRAM the PSRAM stays usable during flash operations.
  xTaskCreatePinnedToCoreWithCaps(lox_task, "lox", 8 * 1024, nullptr, 5, nullptr, 0, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  xTaskCreatePinnedToCore(ui_task, "ui", 16 * 1024, nullptr, 5, &ui_handle, 1);
  energy_start();  // whatwatt Go polling (core 0, below net and Loxone)
  pv_start();      // Growatt ShineLan listener (core 0, lowest)
}
