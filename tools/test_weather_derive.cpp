// Host unit test for main/weather_derive.cpp.
//   g++ -std=c++17 -I main tools/test_weather_derive.cpp main/weather_derive.cpp -o t && ./t [days.txt]
// days.txt (from tools/openmeteo_days.py): per day one line
//   "DAY <date> <sunrise_h> <sunset_h> <precip_sum> <daily_code>"
// followed by 24 lines "code precip temp cloud".
#include <cstdio>
#include <cstring>
#include "weather_derive.h"

static int fails = 0, passes = 0;
static void expect(const char* name, DayKind got, DayKind want) {
  bool ok = got == want;
  (ok ? passes : fails)++;
  printf("%s %-42s got %-22s want %s\n", ok ? "PASS" : "FAIL", name, day_kind_name(got), day_kind_name(want));
}
static void expect_i(const char* name, int got, int want) {
  bool ok = got == want;
  (ok ? passes : fails)++;
  printf("%s %-42s got %d want %d\n", ok ? "PASS" : "FAIL", name, got, want);
}

static void fill(WxHour* h, int n, int code, float p, float t, float c) {
  for (int i = 0; i < n; ++i) h[i] = {code, p, t, c};
}

int main(int argc, char** argv) {
  WxHour d[16];  // 06..21
  // Sunny day with one 0.1 mm hour -> sunny
  fill(d, 16, 0, 0, 15, 10);
  d[3] = {51, 0.1f, 14, 30};
  expect("sunny + one 0.1 mm hour", derive_day(d, 16, 0.1f), DayKind::Sunny);
  // Rainy afternoon: 3 wet hours, moderate cloud -> showers
  fill(d, 16, 1, 0, 15, 40);
  d[9] = {80, 1.2f, 13, 80}; d[10] = {81, 2.5f, 12, 90}; d[11] = {80, 0.6f, 12, 80};
  expect("rainy afternoon (3 wet h)", derive_day(d, 16, 4.3f), DayKind::Showers);
  // All-day rain -> rain
  fill(d, 16, 63, 1.0f, 9, 100);
  expect("all-day rain", derive_day(d, 16, 16.f), DayKind::Rain);
  // Overcast dry, partly, mostly, fog
  fill(d, 16, 3, 0, 10, 95);
  expect("overcast dry", derive_day(d, 16, 0), DayKind::Overcast);
  fill(d, 16, 2, 0, 10, 45);
  expect("partly cloudy", derive_day(d, 16, 0), DayKind::Partly);
  fill(d, 16, 3, 0, 10, 70);
  expect("mostly cloudy", derive_day(d, 16, 0), DayKind::MostlyCloudy);
  fill(d, 16, 45, 0, 5, 100);
  for (int i = 10; i < 16; ++i) d[i] = {3, 0, 8, 90};
  expect("fog most of the day", derive_day(d, 16, 0), DayKind::Fog);
  fill(d, 16, 45, 0, 5, 30);
  for (int i = 3; i < 16; ++i) d[i] = {1, 0, 12, 20};
  expect("morning fog only (3 h)", derive_day(d, 16, 0), DayKind::Sunny);
  // Thunder: single dry thunder code -> not thunder; wet thunder -> thunder
  fill(d, 16, 1, 0, 25, 30);
  d[8] = {95, 0, 24, 60};
  expect("one dry thunder hour, no rain", derive_day(d, 16, 0), DayKind::Partly);
  d[8] = {95, 3.0f, 22, 90}; d[9] = {80, 0.5f, 21, 70};
  expect("wet thunderstorm", derive_day(d, 16, 3.5f), DayKind::Thunder);
  // Snow
  fill(d, 16, 73, 0.8f, -2, 100);
  expect("all-day snow", derive_day(d, 16, 12.8f), DayKind::Snow);
  fill(d, 16, 2, 0, 0, 50);
  d[4] = {85, 0.6f, 0.5f, 70}; d[5] = {85, 0.8f, 0.5f, 70};
  expect("snow showers", derive_day(d, 16, 1.4f), DayKind::SnowShowers);
  // small but repeated drizzle: 0.3 mm in 3 hours -> showers
  fill(d, 16, 2, 0, 12, 50);
  d[2] = {51, 0.1f, 12, 70}; d[3] = {51, 0.1f, 12, 70}; d[4] = {51, 0.1f, 12, 70};
  // rain only at night is outside the daytime window -> the day stays dry
  fill(d, 16, 1, 0, 12, 20);
  expect("dry daytime, 5 mm overnight", derive_day(d, 16, 5.0f), DayKind::Sunny);
  fill(d, 16, 2, 0, 12, 50);
  d[2] = {51, 0.1f, 12, 70}; d[3] = {51, 0.1f, 12, 70}; d[4] = {51, 0.1f, 12, 70};
  expect("0.3 mm drizzle over 3 h", derive_day(d, 16, 0.3f), DayKind::Showers);
  // Slots
  WxHour s[3] = {{61, 0.0f, 10, 80}, {3, 0.0f, 10, 80}, {3, 0.0f, 10, 80}};
  expect_i("slot: rain code but 0 mm -> cloud code", derive_slot_code(s, 3), 3);
  WxHour s2[3] = {{3, 0.0f, 10, 80}, {61, 0.5f, 10, 90}, {3, 0.0f, 10, 80}};
  expect_i("slot: one wet rain hour wins", derive_slot_code(s2, 3), 61);
  WxHour s3[3] = {{61, 0.0f, 10, 10}, {0, 0.0f, 10, 10}, {0, 0.0f, 10, 10}};
  expect_i("slot: mostly clear", derive_slot_code(s3, 3), 0);

  if (argc > 1) {
    FILE* f = fopen(argv[1], "r");
    char date[16];
    int sr, ss, dcode;
    float psum;
    while (f && fscanf(f, " DAY %15s %d %d %f %d", date, &sr, &ss, &psum, &dcode) == 5) {
      WxHour h[24];
      for (int i = 0; i < 24; ++i) fscanf(f, "%d %f %f %f", &h[i].code, &h[i].precip, &h[i].temp, &h[i].cloud);
      DayKind k = derive_day(h + sr, ss - sr + 1, psum);
      printf("DATA %s daytime %02d-%02d sum %.1f mm, daily code %d -> %s | slots:", date, sr, ss, psum, dcode,
             day_kind_name(k));
      for (int sl = 0; sl < 8; ++sl) printf(" %d", derive_slot_code(h + sl * 3, 3));
      printf("\n");
    }
    if (f) fclose(f);
  }
  printf("%d passed, %d failed\n", passes, fails);
  return fails ? 1 : 0;
}
