#pragma once
// Wartung: status values, OTA updates (URL or browser upload) and the small
// maintenance web server (status, settings backup/restore, firmware upload).

#include <stddef.h>
#include <stdint.h>

struct MaintStatus {
  bool wifi;
  int rssi;            // dBm
  char ssid[33];
  char ip[16];
  uint32_t uptime_s;
  uint32_t free_internal;
  uint32_t largest_internal;
  uint32_t free_psram;
  char version[32];    // esp_app_desc version (git describe)
  char built[24];      // date + time of the build
  char idf[16];
  char partition[17];  // running app partition label
  bool ntp_synced;
  uint32_t ntp_age_s;  // seconds since the last sync (0 = never)
  bool web;            // maintenance web server running
};
void maint_collect(MaintStatus* out);  // any task (no LVGL)

// Marks the running image valid (rollback support) - call once the UI runs.
void maint_mark_valid();
// UI task, every loop: a new OTA image that has not reached Wi-Fi (not marked
// valid) within kVerify time is rolled back to the previous slot.
void maint_verify_tick();
// Test harness: boot the other OTA slot (as after an update); with skip_valid
// that boot never marks itself valid, so it must roll back on its own.
bool maint_test_boot_other(bool skip_valid);

// Network task: download and install url, then the caller restarts.
// Progress 0..100 in maint_ota_progress() while it runs.
bool maint_ota_from_url(const char* url, char* err, size_t n);
int maint_ota_progress();

// Maintenance web server on port 80 (core 0). Off by default and after every
// reboot; Einstellungen > Wartung switches it on.
bool maint_web_start();
void maint_web_stop();
bool maint_web_running();
// Access PIN (6 digits, new on every start); shown on the panel only.
const char* maint_web_pin();
// UI task, once a second: switches the server off after 15 min without an
// authenticated request. true if it just stopped.
bool maint_web_tick();
// Secrets export: the browser asked, the panel must confirm (one download
// within 60 s after "allow").
void maint_web_set_idle_ms(uint32_t idle_ms);  // test harness; 0 = default 15 min
bool maint_secrets_requested();
void maint_secrets_answer(bool allow);
// true once an upload installed a new image (the UI shows it and restarts).
bool maint_restart_pending();
