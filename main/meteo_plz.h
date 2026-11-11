#pragma once
// MeteoSchweiz location: the user enters and the panel stores the normal
// 4-digit Swiss PLZ ("9470" = Buchs SG). The MeteoSchweiz app API wants the
// PLZ plus two digits ("947000"); meteo_plz_query() appends "00".
// Pure functions, host tested in tools/test_meteo_plz.cpp.

#include <stddef.h>

// True for exactly 4 digits in 1000..9999.
bool meteo_plz_valid(const char* plz);

// Normalises a stored or legacy value to the 4-digit PLZ: "9470" stays,
// "947000" (old build default / old NVS / old backup: PLZ + "00") becomes
// "9470". Leading/trailing blanks are ignored. out needs >= 5 bytes.
// Returns false (out = "") for anything else.
bool meteo_plz_normalize(const char* in, char* out, size_t n);

// Query value for the API: "9470" -> "947000". Normalises first (legacy
// 6-digit values work too). Returns false (out = "") if not a valid PLZ.
bool meteo_plz_query(const char* plz, char* out, size_t n);
