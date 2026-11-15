#pragma once

#include <stddef.h>
#include <stdint.h>

#include "loxone_auth.h"
#include "loxone_types.h"

struct LoxoneConfig;

struct LoxoneHttpResult {
  bool ok;
  int status;
  char detail[120];
};

// Local Miniserver HTTP. Paths are absolute ("/data/LoxAPP3.json").
LoxoneHttpResult loxone_get(LoxoneAuthorizer& auth, const char* path, uint8_t* body, size_t cap,
                            size_t* out_len);

// GET /jdev/sps/io/<action>/<command> for a simple named command (On, Off, Pulse,
// FullUp, FullDown, stopOverride). startOverride uses loxone_send_io instead.
LoxoneHttpResult loxone_send_command(LoxoneAuthorizer& auth, const char* action, const char* command);

// Builds a validated path (named command, startOverride/{level}/{seconds}, or
// stopOverride) and issues the GET. command is one of the short names above or
// "startOverride" / "stopOverride".
LoxoneHttpResult loxone_send_io(LoxoneAuthorizer& auth, const char* action, const char* command,
                                int level, uint32_t seconds);

// Live value for one control. Tries /jdev/sps/io/<action>/all first, because that
// uses the same uuid as On/Off, then /jdev/sps/io/<state>/state.
// On success, value is numeric text ("0", "1", "0.42", "-1", ...).
LoxoneHttpResult loxone_read_state(LoxoneAuthorizer& auth, const char* action, const char* state,
                                   LoxoneKind kind, char* value, size_t value_n,
                                   int32_t* override_s = nullptr);

// On-device setup: tries a login against c (not the saved config) with
// GET /jdev/sps/LoxAPPversion3. ok on HTTP 200; status 401 = login rejected.
LoxoneHttpResult loxone_test(const LoxoneConfig& c);
