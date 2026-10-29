#pragma once
// (see also ui_scroll_check below)

#include <stddef.h>

#include "loxone_types.h"
#include "lvgl.h"
#include "settings.h"
#include "webcams.h"
#include "info.h"
#include "maint.h"
#include "ota_check.h"

struct LoxoneCommandRequest {
  char action[40];
  char command[16];
  int index;
};

struct LoxoneStateRequest {
  int index;
  char action[40];
  char state[40];
  LoxoneKind kind;
};

void ui_init();
extern size_t g_ui_build_int, g_ui_build_psram;  // heap taken by building the UI (bytes)
// Writes pending display choices to NVS now (UI task only).
void ui_flush_settings();
// Every touch sample (real or simulated), before LVGL sees it. Decides tap vs
// swipe for the whole touch and runs page/back/home swipes.
void ui_touch_sample(bool pressed, int x, int y);
// "home", "message" or "category <Name> <page>/<pages>" (test harness STATE).
void ui_describe(char* out, size_t n);
void ui_show_message(const char* title, const char* body, bool retry);
void ui_show_controls(const LoxoneControl* items, size_t count, size_t supported, const char* server_name);
void ui_set_status(const char* text);
bool ui_take_command(LoxoneCommandRequest* out);
bool ui_take_retry();
void ui_command_finished(int index, bool ok, const char* command, const char* detail);

// One control per call. The visible page is read first, including a fresh
// read each time that page is opened.
bool ui_take_state(LoxoneStateRequest* out);
void ui_state_finished(int index, bool ok, const char* value);

// Home-screen weather for Buchs SG. code is a WMO weather code.
void ui_set_weather(bool ok, int temp_c, int code);

// Daily forecast for the home card: [0] today, [1] tomorrow.
struct WeatherDay {
  int code;       // WMO weather code
  float t_max;    // deg C
  float t_min;    // deg C
  float precip;   // mm
  int kind;       // DayKind from weather_derive.h (icon + text)
};
// ok=false keeps the last good forecast on screen (or shows "--" if none).
void ui_set_forecast(bool ok, const WeatherDay* days, int count);

// 7-day forecast for the day detail screen (tap Heute/Morgen on home).
constexpr int kWxDays = 7;
constexpr int kWxSlots = 8;  // 00, 03, ... 21 h
struct WeatherSlot {
  int code;       // representative WMO code of the 3 hours (derive_slot_code)
  float temp;     // deg C at the slot hour
  float precip;   // mm over the 3 hours
  int prob;       // max precipitation probability % over the 3 hours
  float wind;     // max wind km/h over the 3 hours
};
struct WeatherDetailDay {
  char date[11];    // YYYY-MM-DD
  char sunrise[6];  // HH:MM
  char sunset[6];
  int code;
  float t_max;
  float t_min;
  float precip;
  int prob_max;
  int kind;  // DayKind (derived from daytime hourly data)
  float wind_max;
  WeatherSlot slot[kWxSlots];
  float hprecip[24];  // mm per hour, local 00..23 (Open-Meteo hourly)
  uint8_t hprob[24]; // precipitation probability % per hour
};
// days must stay valid only for the call; the UI copies what it needs.
void ui_set_week(const WeatherDetailDay* days, int count);

// --- Settings (Einstellungen) ---
bool ui_take_settings(SettingsRequest* out);
void ui_wifi_scan_done(const WifiAp* aps, int n, uint32_t gen);
// current = SSID the panel uses now (the new one, or the old one after a failure).
void ui_wifi_connect_done(bool ok, const char* ssid, const char* current, int status, const char* old_ssid,
                          bool reverted);
bool ui_wifi_busy();                                // a connect is queued or running
void ui_wifi_setup_open(const char* saved, int why);  // boot: WLAN selection instead of a dead end
void ui_wifi_setup_close();                         // the saved network is back
void ui_geocode_done(bool ok, const GeoPlace* places, int n);
void ui_location_done(bool ok, const char* name);
void ui_set_place(const char* name);
void ui_set_wifi_name(const char* ssid);
void ui_time_done(bool ok);  // SetTime saved (NVS) and SNTP restarted

// Test harness helpers.
bool ui_find(const char* name, int* x, int* y);  // visible named widget centre
void ui_list_dump();                             // ITEM lines for the open list
bool ui_type(const char* text);                  // into the keyboard's text area
bool ui_enter();                                 // keyboard OK
void ui_reset_location();                        // back to Buchs SG (NVS erase)

// Screen timeout. Called for every touch sample before LVGL; false = swallow
// (the touch that wakes the screen, until its release).
bool ui_power_touch(bool pressed, int x, int y);
void ui_power_describe(char* out, size_t n);       // "scr=on|off bl=N to=S saver=K"
void ui_set_screen_timeout(uint16_t seconds);      // runtime only (harness)
lv_obj_t* ui_overlay();                            // screensaver screen while off (SHOT)

// --- Kameras (Windy webcams) ---
// The UI owns two PSRAM frames (480x480 RGB565); a request names the one that
// is not on screen and the network task fills it.
struct CamRequest {
  uint32_t id;
  uint16_t* frame;
};
bool ui_take_cam(CamRequest* out);
void ui_cam_done(bool ok, int status, const CamInfo& info, uint16_t* frame, const char* err);
void ui_cam_lookup_done(bool ok, int status, const CamInfo& info);
void ui_cam_nearby_done(bool ok, int status, const CamInfo* found, int n, const char* place);
void ui_cams_saved(bool ok);
void ui_windy_key_done(bool ok);

// --- ÖV, Kalender, MeteoSchweiz, Wartung (panel_ui_info.inc) ---
// The UI decides when to refresh (visible screen, intervals); the network
// task fetches and answers with the *_done calls.
bool ui_take_transit(int* station);  // board index to refresh now
void ui_transit_done(int station, bool ok, const Board& board);
void ui_station_search_done(bool ok, const Station* stations, int n);
void ui_station_saved(bool ok, int station);
void ui_text_saved(bool ok, uint8_t which);  // TextSetting
bool ui_take_calendar();
void ui_calendar_done(bool ok, const Calendar& cal);
bool ui_take_meteo();
void ui_meteo_done(bool ok, const MeteoNow& m);
bool ui_take_ota(char* url, size_t n, bool* check, bool* own);  // check or install request
void ui_ota_checked(const OtaCheck& c);
void ui_ota_done(bool ok, const char* err);

// Termin-Erinnerungen (test harness): synthetic event in `minutes` (kind 1 = all-day waste),
// clear queue and acknowledged list, pending count.
void ui_rem_test(int minutes, int kind);  // kind 0 timed, 1 waste (all-day), 2 all-day family event
void ui_rem_drop_tests();
void ui_rem_clear();
void ui_set_waste_until(uint16_t minutes);  // persists (NVS), refreshes chip and reminders
uint16_t ui_waste_until();
void ui_set_waste_from(uint16_t minutes);  // "Am Vortag ab" (minutes), persists, refreshes reminders
uint16_t ui_waste_from();
void ui_set_cal_lead(uint16_t minutes);  // timed events: lead (minutes), persists
uint16_t ui_cal_lead();
void ui_set_cal_from(uint16_t minutes);  // all-day events: "Am Vortag ab" (minutes), persists
uint16_t ui_cal_from();
bool ui_rain_test(bool on);  // test harness: synthetic rain in the 24 h forecast
bool ui_waste_test();  // test harness: inject a waste collection today
int ui_rem_pending();
void ui_saver_describe(char* out, size_t n);  // screensaver state for DIAGFB
void ui_saver_runtime(int saver);              // DIAGSAVER: not saved to NVS

// Test harness: scroll every container of the visible page to its end and check
// that the bottom-most clickable widget is visible and tappable.
bool ui_scroll_check(char* out, size_t n);

// DIAGCO4: shape of the drawn day chart per series (valid points, runs, isolated
// dots, segments, steepest segment in px) - never values.
void ui_co4_chart_shape(char* out, size_t n);
