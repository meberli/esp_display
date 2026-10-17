#pragma once
// Settings schema: the single list of NVS keys in namespace "panel" with type,
// limits and flags. Backup (export), restore (import) and the stored config
// version use it; keys that are not listed are never exported or imported.

#include <stddef.h>
#include <stdint.h>

// Bumped when a key changes meaning, type or range; settings_migrate() converts.
constexpr uint8_t kCfgVersion = 4;  // 2: wst_from replaces rem_lwst; 3: cal_lead/cal_from replace rem_lcal; 4: Energie

enum class KeyType : uint8_t { U8, U16, Str, Blob };

enum KeyFlag : uint8_t {
  kKeySecret = 1,     // exported only with secrets (on-panel confirmation)
  kKeyInternal = 2,   // bookkeeping: neither exported nor imported
  kKeyUrl = 4,        // http:// or https:// URL
  kKeyHttpsOnly = 8,  // https:// URL only (firmware updates)
  kKeyHost = 16,      // host name or IPv4, no scheme or path
  kKeyDigits = 32,    // decimal digits only
  kKeyFloat = 64,     // blob = one float; min/max are the allowed range (degrees)
};

struct KeySpec {
  const char* key;
  KeyType type;
  int32_t min;  // U8/U16: value range; Str/Blob: length range (bytes)
  int32_t max;
  uint8_t flags;
  uint8_t blob_step;  // Blob: length must be a multiple (0 = any)
};

const KeySpec* schema_find(const char* key);
const KeySpec* schema_begin();
size_t schema_count();

// Validation of a value for spec. why gets a short German reason on failure.
bool schema_check_int(const KeySpec& s, long long v, const char** why);
bool schema_check_str(const KeySpec& s, const char* v, const char** why);
bool schema_check_blob(const KeySpec& s, const uint8_t* v, size_t n, const char** why);
