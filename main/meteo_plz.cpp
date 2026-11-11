#include "meteo_plz.h"

#include <stdio.h>
#include <string.h>

namespace {
bool digits(const char* p, size_t len) {
  for (size_t i = 0; i < len; ++i)
    if (p[i] < '0' || p[i] > '9') return false;
  return true;
}
}  // namespace

bool meteo_plz_valid(const char* plz) {
  return plz != nullptr && strlen(plz) == 4 && digits(plz, 4) && plz[0] != '0';
}

bool meteo_plz_normalize(const char* in, char* out, size_t n) {
  if (out == nullptr || n == 0) return false;
  out[0] = '\0';
  if (in == nullptr || n < 5) return false;
  while (*in == ' ') ++in;
  size_t len = strlen(in);
  while (len > 0 && in[len - 1] == ' ') --len;
  if (len == 6 && in[4] == '0' && in[5] == '0') len = 4;  // legacy "PLZ + 00"
  if (len != 4 || !digits(in, 4) || in[0] == '0') return false;
  memcpy(out, in, 4);
  out[4] = '\0';
  return true;
}

bool meteo_plz_query(const char* plz, char* out, size_t n) {
  char p[5];
  if (out == nullptr || n == 0) return false;
  out[0] = '\0';
  if (n < 7 || !meteo_plz_normalize(plz, p, sizeof(p))) return false;
  snprintf(out, n, "%s00", p);
  return true;
}
