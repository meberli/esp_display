#pragma once

#include "energy.h"
#include "pv.h"
// User settings kept in NVS (namespace "panel"): Wi-Fi credentials and the
// weather location. Types shared between the UI and the network task.

#include <stddef.h>
#include <stdint.h>

#include "sdkconfig.h"

struct WifiCreds {
  char ssid[33];
  char pass[65];
};

struct Location {
  char name[48];
  float lat;
  float lon;
};

struct WifiAp {
  char ssid[33];
  int8_t rssi;
  bool secure;
};

struct GeoPlace {
  char name[48];    // "Zürich"
  char region[48];  // "Zürich, CH"
  float lat;
  float lon;
};

constexpr int kMaxAps = 16;
constexpr int kMaxPlaces = 10;

// Default location: Buchs SG (Open-Meteo geocoding: 47.1674 N, 9.4779 E).
Location settings_default_location();

// Wi-Fi: NVS first, else panel_config.h. false if neither has an SSID.
bool settings_load_wifi(WifiCreds* out);
bool settings_save_wifi(const WifiCreds& creds);

// Location: NVS first, else the default.
Location settings_load_location();
bool settings_save_location(const Location& loc);
bool settings_reset_location();  // back to the default (erases the NVS keys)

// Display: screen timeout in seconds (0 = never) and the off-mode hint.
// saver: what the screen shows after the timeout.
enum Saver : uint8_t { kSaverBlack = 0, kSaverDigital = 1, kSaverAnalog = 2 };
struct DisplaySettings {
  uint16_t timeout_s;
  bool hint;      // kSaverBlack only: dim "wake me up" instead of a dark panel
  uint8_t saver;  // Saver
};
constexpr uint16_t kTimeoutChoices[] = {0, 30, 60, 120, 300, 600};
constexpr int kTimeoutChoiceN = 6;
DisplaySettings settings_load_display();  // default 120 s, hint on, black
bool settings_save_display(const DisplaySettings& d);

// Design (UiDesign in ui_theme.h): 0 Dark Glass (default), 1 Co4. Read at boot.
uint8_t settings_load_design();
bool settings_save_design(uint8_t design);

// Time: SNTP server and time zone (POSIX TZ string; name is for display).
struct TimeSettings {
  char server[64];
  char tz_name[32];
  char tz[64];
};
// Default: ch.pool.ntp.org, Europe/Zurich (CET-1CEST,M3.5.0,M10.5.0/3).
TimeSettings settings_default_time();
TimeSettings settings_load_time();  // NVS first, else the default
bool settings_save_time(const TimeSettings& t);

// Webcams (Kameras): only the Windy webcam IDs are stored in NVS. title is
// RAM only (built-in names for the defaults, else filled from the API).
struct Webcam {
  uint32_t id;
  char title[64];
};
constexpr int kMaxCams = 12;
// NVS (IDs) first, else the five default cameras around Buchs SG. Returns the count.
int settings_load_cams(Webcam* out, int cap);
bool settings_save_cams(const Webcam* cams, int n);

// Windy Webcams API key: the user's own key from NVS, else the build default
// (CONFIG_PANEL_WINDY_API_KEY from the gitignored sdkconfig.defaults.local).
// own = true when the key came from NVS. Returns false if there is no key.
bool settings_load_windy_key(char* out, size_t n, bool* own);
bool settings_save_windy_key(const char* key);  // "" erases the own key
// "abcd••••wxyz" for display (never the whole key).
void settings_mask_key(const char* key, char* out, size_t n);

// --- ÖV stations (two departure boards) ---
// NVS "st<i>_id"/"st<i>_name", else Buchs SG (8509404) and Räfis, Schulhaus (8587407).
struct Station;
void settings_load_stations(Station* out);  // kStationN entries
bool settings_save_station(int i, const Station& st);

// --- Text settings with a build default (URLs) ---
// kUrlIcal: family calendar (private: NVS "ical_url", else CONFIG_PANEL_ICAL_URL
//           from the gitignored sdkconfig.defaults.local)
// kUrlWaste: Abfallkalender-API base URL (https only)
// kUrlOta: firmware image for "Update aus dem Internet"
// kTextPlz: MeteoSchweiz location (PLZ + 2 digits, "947000" = 9470 Buchs SG)
// kTextWasteBfs: Gemeinde (BFS number) for the Abfallkalender-API
// kTextWasteZone: zone / tour key of the API answer (multi-tour Gemeinden; empty = all)
enum TextSetting : uint8_t {
  kUrlIcal = 0,
  kUrlWaste = 1,  // Abfallkalender-API base URL (https)
  kUrlOta = 2,
  kTextPlz = 3,
  kTextWasteBfs = 4,
  kTextWasteZone = 5,
  // Einstellungen > Energie (whatwatt Go); prices as decimal text ("27.27")
  kTextEnHost = 6,     // host name or IPv4 of the whatwatt Go
  kTextEnFlat = 7,     // Einheitstarif Rp./kWh
  kTextEnHt = 8,       // Hochtarif Rp./kWh
  kTextEnNt = 9,       // Niedertarif Rp./kWh
  kTextEnFeed = 10,    // Einspeisevergütung Rp./kWh
  kTextWaPrice = 11,   // Wasser CHF/m3 (incl. Abwasser)
  kTextEnHtWin = 12,   // HT-Zeiten Mo-Fr "06:00-10:00,17:00-21:00"
  kTextEnSwitch = 13,  // Wechsel auf HT/NT ab "2027-01-01"
  kTextPvGw = 14,      // PV: IPv4 of the Modbus TCP gateway (RS485 -> Growatt), "pv_gw"
  kTextPvGwPort = 15,  // PV: Modbus TCP port (502), "pv_gwport"
  kTextPvUnit = 16,    // PV: Modbus unit id of the inverter (1), "pv_unit"
  kTextSettingN = 17
};
// own = true when the value came from NVS. Returns false if empty.
bool settings_load_text(TextSetting which, char* out, size_t n, bool* own);
bool settings_save_text(TextSetting which, const char* value);  // "" = back to the build default
const char* settings_text_default(TextSetting which);

// --- Energie (whatwatt Go) ---
struct EnergySettings {
  bool on;            // "en_on" (default on; polls only with a host)
  uint16_t poll_s;    // "en_poll" 5..300 s (default 10)
  uint8_t model;      // "en_model" TariffModel (default automatic)
  bool show_exp;      // "en_exp" show export / feed-in
  bool water_on;      // "wa_on"
  uint8_t water_bus;  // "wa_bus" M-Bus number of the water meter (default 2)
  char host[64];      // text kTextEnHost
  Tariff tariff;      // text prices / windows / switch date (defaults: ewb Buchs 2026/2027 incl. VAT)
};
EnergySettings settings_load_energy();
bool settings_save_energy_flags(const EnergySettings& e);  // on, poll, model, exp, water on/bus
// PV (Growatt via Modbus TCP gateway, read only): "pv_on" (default on).
bool settings_load_pv_on();
bool settings_save_pv_on(bool on);
// History (internal, excluded from backups): one blob, written at most hourly.
bool settings_load_energy_hist(EnergyHist* h);
bool settings_load_pv_hist(PvHist* h);       // "pv_hist" blob (daily PV kWh)
bool settings_save_pv_hist(const PvHist& h);
bool settings_load_pv_hours(PvHours* h);     // "pv_hours" blob (today per hour / quarter)
bool settings_save_pv_hours(const PvHours& h);
bool settings_save_energy_hist(const EnergyHist& h);

// --- Termin-Erinnerungen ---
// Lead presets (index): 15 min, 1 h, 3 h, 1 Tag, Vorabend 18:00, am Tag 07:00.
// All-day events (waste) count from 07:00 on their day for the duration presets.
constexpr int kLeadN = 6;
struct ReminderSettings {
  bool on;
  bool cal;            // family calendar
  bool waste;          // Abfallkalender
  uint8_t lead_cal;    // unused since config version 3 (see cal_lead_min / cal_from_min), kept for old backups
  uint8_t lead_waste;  // unused since config version 2 (see wst_from), kept for old backups
  uint16_t cal_lead_min;  // timed events: minutes before the start (NVS "cal_lead", default 60)
  uint16_t cal_from_min;  // all-day events: from this time on the day before (NVS "cal_from", default 18:00)
};
// Abfallkalender: on the collection day the home hint (chip, reminder card)
// shows until this local time, minutes after midnight (default 08:00 = 480,
// 1440 = the whole day). NVS "wst_until".
constexpr uint16_t kWasteUntilDefault = 480;
uint16_t settings_load_waste_until();
bool settings_save_waste_until(uint16_t minutes);
// Abfallkalender reminder: from this local time on the day before the
// collection (minutes after midnight, default 18:00 = 1080). NVS "wst_from";
// replaces the lead preset rem_lwst (config version 2).
uint16_t settings_load_waste_from();
bool settings_save_waste_from(uint16_t minutes);
ReminderSettings settings_load_reminders();  // default on, cal 1 h, waste Vorabend 18:00
bool settings_save_reminders(const ReminderSettings& r);
// Acknowledged reminders (key = hash of event UID + start), ring of the last 48 in NVS "rem_ack".
bool settings_reminder_acked(uint32_t key);
bool settings_reminder_ack(uint32_t key);
void settings_reminder_clear_acks();  // test harness
// Test harness: raw copy of the ack ring (save/restore around tests). n = settings_reminder_acks_size().
size_t settings_reminder_acks_size();
void settings_reminder_acks_get(uint8_t* out);
bool settings_reminder_acks_set(const uint8_t* in);

// --- Secrets that a firmware without local config must keep ---
// Copies build-time values (Wi-Fi, Loxone, Windy key, iCal URL) into NVS when
// NVS lacks them, so an OTA update to a public, secret-free build keeps working.
// Call once at boot before anything loads settings.
void settings_seed_from_build();
#ifdef CONFIG_PANEL_TEST_HARNESS
bool settings_test_wifi(int op, const char* arg);  // 0 forget, 1 set password, 2 restore build creds
#endif

// Loxone Miniserver: NVS ("lox_host", "lox_port", "lox_user", "lox_pass", "lox_auth"),
// else panel_config.h.
struct LoxoneConfig {
  char host[64];
  uint16_t port;
  char user[33];
  char pass[65];
  char auth[8];
};
LoxoneConfig settings_load_loxone();
const LoxoneConfig& settings_loxone();  // loaded once (after settings_seed_from_build)

// --- Backup / restore of the whole "panel" NVS namespace as JSON ---
// Secret keys (Wi-Fi password, Windy key, iCal URL) only with secrets = true.
// Returns the JSON length written to out (truncated output returns 0).
size_t settings_export_json(char* out, size_t n, bool secrets);
// Restores a settings_export_json() document, validated against the schema
// (settings_schema.h). Returns keys written; -1 not a settings file, -2 file
// from a newer config version, -3 invalid values (nothing written). report
// lists rejected and unknown keys (never values).
int settings_import_json(const char* json, size_t len, char* report, size_t report_n);
// Boot: stores/upgrades the config version ("cfg_ver"), converting old keys.
void settings_migrate();

// Requests from the settings screens to the network task.
struct SettingsRequest {
  enum Kind : uint8_t {
    Scan, Connect, Geocode, SetLocation, ResetLocation, SetTime,
    CamLookup,    // cam_id -> title (verifies the ID)
    CamNearby,    // query: place name -> geocode -> webcams nearby
    SetCams,      // cams/cam_n -> NVS
    SetWindyKey,  // key ("" = back to the build default) -> NVS
    StationSearch,  // query -> stations (transport.opendata.ch)
    SetStation,     // station_idx + station -> NVS
    SetText,        // text_which + text ("" = default) -> NVS
  } kind;
  uint32_t scan_gen;  // Scan: generation (UI cancel / stale-result check)
  WifiCreds creds;
  char query[48];
  Location location;
  TimeSettings time;
  uint32_t cam_id;
  int cam_n;
  Webcam cams[kMaxCams];
  char key[72];
  int station_idx;
  char station_id[12];
  char station_name[48];
  uint8_t text_which;
  char text[256];
};
