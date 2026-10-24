#pragma once

#include <stddef.h>
#include <stdint.h>

#include "loxone_types.h"

// Parse a LoxAPP3.json body into switch, pushbutton, and shutter controls.
// Copies strings out of the document, including the state uuid used to read
// the live value. Does not keep the JSON.
bool loxone_parse_structure(const uint8_t* json, size_t length, LoxoneControl* out, size_t cap,
                            size_t* shown, size_t* supported, char* server, size_t server_n, char* error,
                            size_t error_n);
