#pragma once
// Co4 Energie: login (Keycloak Device Authorization Grant), token refresh, zone
// list and data fetch on a core-0 task. The UI (core 1) reads a mutex-guarded
// snapshot and is told about changes through a FreeRTOS queue (co4_ui_take).
// Tokens never leave this module: the access token lives in RAM only, the
// offline refresh token in NVS ("co4_rt"); DIAG and the UI only see set/unset.

#include <stddef.h>
#include <stdint.h>

#include "co4_data.h"

enum class Co4State : uint8_t {
  NoLogin,   // no token: "Co4 nicht verbunden"
  Starting,  // discovery + device authorization request running
  Code,      // QR code / user_code shown, polling the token endpoint
  LoggedIn,  // offline token present
  Expired,   // invalid_grant on refresh: "Co4-Anmeldung abgelaufen"
};
enum class Co4Fetch : uint8_t { Idle, Loading, Ok, Offline };
enum class Co4ZonesState : uint8_t { Idle, Loading, Ok, Error };
enum class Co4Event : uint8_t { State, Zones, Data };
enum class Co4Mode : uint8_t { Live = 0, Fixture = 1, Demo = 2 };  // Demo: example data without login

struct Co4View {
  Co4State state;
  Co4Mode mode;
  char msg[64];  // last login problem (German), "" if none
  bool token_set;
  char user_code[16];
  char verify_uri[128];
  char verify_complete[200];
  uint32_t code_until_ms;  // now_ms() deadline of the device code
  bool zone_set;
  char zone_id[40];
  char zone_name[48];
  char site_niid[20];  // ZEV site of the zone ("" unknown), not secret but not shown
  Co4ZonesState zones_state;
  char zones_err[48];
  Co4Fetch fetch;
  bool have_data;
  int64_t data_ms;  // wall clock of the shown data (ms since epoch)
  char data_err[48];
};

void co4_start();
// UI: non-blocking; true while events are queued.
bool co4_ui_take(Co4Event* ev);
void co4_get(Co4View* out);
// Copies the zone list (only meaningful when zones_state == Ok).
void co4_get_zones(Co4ZoneList* out);
// Copies the data block; false if none yet.
bool co4_get_data(Co4Data* out);

void co4_login();
void co4_login_cancel();
void co4_logout();
void co4_zones_load();
void co4_zone_choose(const char* id, const char* name, const char* site);
void co4_refresh_now();
void co4_set_mode(Co4Mode m);
// One-line DIAG summary: never contains a token, only set/unset.
void co4_diag(char* out, size_t n);
