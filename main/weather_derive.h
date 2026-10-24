#pragma once
// Daily / 3-hour weather summary from hourly Open-Meteo data.
// Pure C++ (no Arduino), so it is unit-tested on the host:
//   tools/test_weather_derive.cpp
//
// Open-Meteo's daily weather_code is the most severe code of the whole day,
// so one drizzle hour at 03:00 turns a sunny day into "rain". The day is
// summarised here from the hourly values over daytime instead.

struct WxHour {
  int code;      // WMO weather code
  float precip;  // mm in this hour
  float temp;    // deg C
  float cloud;   // cloud cover %
};

enum class DayKind : int {
  Sunny = 0,      // mean daytime cloud < 25 %
  Partly,         // 25-60 %
  MostlyCloudy,   // 60-85 %
  Overcast,       // > 85 %
  Fog,            // fog codes in at least half the daytime hours
  Showers,        // rain in a few hours, sun in between
  Rain,           // rain over many hours or heavy cloud
  SnowShowers,
  Snow,
  Thunder,
};

// Thresholds (also documented in the README / commit).
constexpr float kWetHourMm = 0.1f;   // an hour counts as wet from 0.1 mm
constexpr float kDayRainMm = 1.0f;   // a day is wet from 1.0 mm ...
constexpr float kDayRainSmallMm = 0.2f;  // ... or 0.2 mm with >= 2 wet hours
constexpr int kShowerMaxWetHours = 4;    // <= 4 wet daytime hours = showers
constexpr float kShowerMaxCloud = 85.f;  // ... if mean cloud stays below this

// hours: the daytime hours (sunrise..sunset hour). Only their precipitation
// decides wet vs dry (night rain does not make the day rainy); day_precip is
// the whole-day sum, kept for callers and future use.
DayKind derive_day(const WxHour* hours, int n, float day_precip);

// Representative WMO code for a 3-hour slot (hours[0..n-1]): codes weighted by
// how often they occur, wet hours (>= 0.1 mm) count triple; a precipitation
// code without any measurable precipitation in the slot falls back to the
// cloud-cover code (0 / 1 / 2 / 3).
int derive_slot_code(const WxHour* hours, int n);

const char* day_kind_name(DayKind k);  // German, matches the icon

bool wmo_is_precip(int code);
bool wmo_is_snow(int code);
bool wmo_is_thunder(int code);
bool wmo_is_fog(int code);
