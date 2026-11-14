#pragma once
// "Firmware prüfen": find the newest firmware and compare it with the running
// version. Network task only (blocking HTTP).

#include <stddef.h>

struct OtaCheck {
  bool ok;            // the check itself worked
  int cmp;            // offered vs running: 1 newer, 0 same, -1 older
  char version[32];   // offered version, without "v"
  char notes[96];     // first line of the release notes (ASCII), may be empty
  char url[256];      // image to install (exact release asset); empty: no image for this board
  char err[96];       // message for the panel when !ok
};

// cfg_url: configured update URL; own: user-set (not the build default).
// prereleases: when true (Beta-Kanal), GitHub prereleases are considered too.
// skip: comma-separated versions to ignore (failed boots), may be empty/nullptr.
// Default GitHub URL: GitHub API /releases list, picked by semver and channel.
// Other URLs: the version is read from the image header (HTTP Range request).
bool ota_check(const char* cfg_url, bool own, bool prereleases, const char* skip, OtaCheck* out);

// Semantic version compare (semver: pre-release < final of the same version). <0, 0, >0.
int ota_ver_cmp(const char* a, const char* b);

// Running version (esp_app_desc), or a diagnostic override for tests.
const char* ota_running_version();
void ota_set_version_override(const char* v);  // nullptr/"" clears
