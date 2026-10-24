#include "weather_derive.h"

bool wmo_is_thunder(int c) { return c >= 95; }
bool wmo_is_snow(int c) { return (c >= 71 && c <= 77) || c == 85 || c == 86; }
bool wmo_is_fog(int c) { return c == 45 || c == 48; }
bool wmo_is_precip(int c) { return (c >= 51 && c <= 67) || (c >= 80 && c <= 82) || wmo_is_snow(c) || wmo_is_thunder(c); }

namespace {
int cloud_code(float cloud) {
  if (cloud < 25.f) return 0;
  if (cloud < 60.f) return 1;
  if (cloud < 85.f) return 2;
  return 3;
}
}  // namespace

DayKind derive_day(const WxHour* h, int n, float day_precip) {
  if (n <= 0) return DayKind::Overcast;
  int wet = 0, thunder = 0, thunder_wet = 0, fog = 0;
  float cloud = 0.f, snow_mm = 0.f, sum = 0.f;
  for (int i = 0; i < n; ++i) {
    cloud += h[i].cloud;
    sum += h[i].precip;
    bool is_wet = h[i].precip >= kWetHourMm;
    if (is_wet) wet++;
    if (wmo_is_thunder(h[i].code)) {
      thunder++;
      if (h[i].precip > 0.f) thunder_wet++;
    }
    if (wmo_is_fog(h[i].code)) fog++;
    if (h[i].precip > 0.f && (wmo_is_snow(h[i].code) || h[i].temp <= 1.0f)) snow_mm += h[i].precip;
  }
  cloud /= n;
  // Only daytime precipitation decides: rain at 02:00 does not make the day
  // rainy. day_precip (whole day) is a cap for rounding in the hourly data.
  float total = sum;
  (void)day_precip;
  bool precip = total >= kDayRainMm || (total >= kDayRainSmallMm && wet >= 2);
  if (precip) {
    if (thunder >= 2 || thunder_wet >= 1) return DayKind::Thunder;
    bool few = wet <= kShowerMaxWetHours && cloud < kShowerMaxCloud;
    if (sum > 0.f && snow_mm > 0.5f * sum) return few ? DayKind::SnowShowers : DayKind::Snow;
    return few ? DayKind::Showers : DayKind::Rain;
  }
  if (fog * 2 >= n) return DayKind::Fog;
  switch (cloud_code(cloud)) {
    case 0: return DayKind::Sunny;
    case 1: return DayKind::Partly;
    case 2: return DayKind::MostlyCloudy;
    default: return DayKind::Overcast;
  }
}

int derive_slot_code(const WxHour* h, int n) {
  int best = 0, best_w = -1;
  float sum = 0.f, cloud = 0.f;
  for (int i = 0; i < n; ++i) {
    sum += h[i].precip;
    cloud += h[i].cloud;
  }
  for (int i = 0; i < n; ++i) {
    int w = 0;
    for (int j = 0; j < n; ++j) {
      if (h[j].code == h[i].code) w += h[j].precip >= kWetHourMm ? 3 : 1;
    }
    if (w > best_w || (w == best_w && h[i].code > best)) {
      best_w = w;
      best = h[i].code;
    }
  }
  if (n > 0 && wmo_is_precip(best) && sum < kWetHourMm) {
    return cloud_code(cloud / n);
  }
  return best;
}

const char* day_kind_name(DayKind k) {
  switch (k) {
    case DayKind::Sunny: return "Sonnig";
    case DayKind::Partly: return "Teilweise bew\xC3\xB6lkt";
    case DayKind::MostlyCloudy: return "\xC3\x9C" "berwiegend bew\xC3\xB6lkt";
    case DayKind::Overcast: return "Bedeckt";
    case DayKind::Fog: return "Nebel";
    case DayKind::Showers: return "Regenschauer";
    case DayKind::Rain: return "Regen";
    case DayKind::SnowShowers: return "Schneeschauer";
    case DayKind::Snow: return "Schnee";
    case DayKind::Thunder: return "Gewitter";
  }
  return "";
}
