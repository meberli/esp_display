#pragma once

#include <stddef.h>
#include <stdint.h>

#include "loxone_auth.h"
#include "loxone_types.h"

struct LoxoneHttpResult {
  bool ok;
  int status;
  char detail[120];
};

// Local Miniserver HTTP. Paths are absolute ("/data/LoxAPP3.json").
LoxoneHttpResult loxone_get(LoxoneAuthorizer& auth, const char* path, uint8_t* body, size_t cap,
                            size_t* out_len);

// GET /jdev/sps/io/<action>/<command>
// action is a Loxone uuidAction. command is On, Off, Pulse, FullUp, or FullDown.
LoxoneHttpResult loxone_send_command(LoxoneAuthorizer& auth, const char* action, const char* command);

// Live value for one control. Tries /jdev/sps/io/<action>/all first, because that
// uses the same uuid as On/Off, then /jdev/sps/io/<state>/state.
// On success, value is numeric text ("0", "1", "0.42", "-1", ...).
LoxoneHttpResult loxone_read_state(LoxoneAuthorizer& auth, const char* action, const char* state,
                                   LoxoneKind kind, char* value, size_t value_n);
