#pragma once
// Last good Miniserver structure (the parsed controls, not the raw LoxAPP3.json):
// packed into one NVS blob so the panel can show the controls at boot while the
// Miniserver is unreachable. Only names, rooms and ids from LoxAPP3 (no credentials).
#include <stddef.h>
#include <stdint.h>

#include "loxone_types.h"

constexpr size_t kLoxCacheMax = 4096;  // NVS has 24 KB in total: never more than this

// Pure (host-tested). pack returns the length, 0 if it does not fit into cap.
size_t lox_cache_pack(const LoxoneControl* c, size_t n, const char* server, uint8_t* out, size_t cap);
// Returns the number of controls (0 = invalid blob).
size_t lox_cache_unpack(const uint8_t* in, size_t len, LoxoneControl* out, size_t cap, char* server, size_t server_n);

#ifndef LOX_CACHE_HOST
// NVS namespace "loxcache". save writes only if the blob changed (flash wear).
bool lox_cache_save(const LoxoneControl* c, size_t n, const char* server);
size_t lox_cache_load(LoxoneControl* out, size_t cap, char* server, size_t server_n);
#endif
