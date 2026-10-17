// Host test for main/settings_schema.cpp:
//   g++ -std=c++17 -Itools/host_stub -Imain tools/test_settings_schema.cpp main/settings_schema.cpp -o /tmp/t && /tmp/t
#include <stdio.h>
#include <string.h>

#include "settings_schema.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

static bool str(const char* key, const char* v) {
  const char* why = "";
  const KeySpec* s = schema_find(key);
  return s != nullptr && schema_check_str(*s, v, &why);
}
static bool num(const char* key, long long v) {
  const char* why = "";
  const KeySpec* s = schema_find(key);
  return s != nullptr && schema_check_int(*s, v, &why);
}

int main() {
  CHECK(schema_find("nope") == nullptr);
  CHECK(schema_find("wifi_pass")->flags & kKeySecret);
  CHECK(schema_find("lox_pass")->flags & kKeySecret);
  CHECK(schema_find("ical_url")->flags & kKeySecret);
  CHECK(schema_find("cfg_ver")->flags & kKeyInternal);
  // URLs
  CHECK(str("ota_url", "https://example.com/fw.bin"));
  CHECK(!str("ota_url", "http://example.com/fw.bin"));
  CHECK(str("waste_url", "http://example.com/x"));
  CHECK(!str("waste_url", "ftp://example.com/x"));
  CHECK(!str("waste_url", "https://a b"));
  CHECK(str("waste_api", "https://abfall-sg.hispeed.workers.dev"));
  CHECK(!str("waste_api", "http://abfall-sg.hispeed.workers.dev"));
  CHECK(schema_find("wst_from") != nullptr);
  CHECK(schema_find("cal_lead") != nullptr);
  CHECK(schema_find("cal_from") != nullptr);
  CHECK(schema_find("en_host") != nullptr && (schema_find("en_host")->flags & kKeyHost));
  CHECK(schema_find("en_hist") != nullptr && (schema_find("en_hist")->flags & kKeyInternal));
  CHECK(schema_find("wa_bus") != nullptr && schema_find("en_switch") != nullptr);
  CHECK(str("waste_bfs", "3271"));
  CHECK(!str("waste_bfs", "327"));
  CHECK(!str("waste_bfs", "32a1"));
  CHECK(str("waste_zone", "Nesslau, Neu St. Johann"));
  // hosts, digits
  CHECK(str("lox_host", "192.168.1.5"));
  CHECK(!str("lox_host", "http://x"));
  CHECK(str("meteo_plz", "9470"));
  CHECK(!str("meteo_plz", "94a0"));
  CHECK(!str("st0_id", ""));
  // lengths and control characters
  CHECK(!str("wifi_ssid", ""));
  CHECK(!str("loc_name", "a\nb"));
  char longs[80];
  memset(longs, 'x', sizeof(longs) - 1);
  longs[79] = 0;
  CHECK(!str("wifi_pass", longs));
  // ranges
  CHECK(num("scr_saver", 2) && !num("scr_saver", 3));
  CHECK(num("wst_until", 1440) && !num("wst_until", 1441));
  CHECK(!num("lox_port", 0) && num("lox_port", 80));
  CHECK(!num("rem_lcal", -1));
  // float blob
  const KeySpec* lat = schema_find("loc_lat");
  float f = 47.1f, bad = 123.0f;
  const char* why = "";
  CHECK(schema_check_blob(*lat, reinterpret_cast<uint8_t*>(&f), 4, &why));
  CHECK(!schema_check_blob(*lat, reinterpret_cast<uint8_t*>(&bad), 4, &why));
  uint8_t ids[8] = {};
  CHECK(schema_check_blob(*schema_find("cam_ids"), ids, 8, &why));
  CHECK(!schema_check_blob(*schema_find("cam_ids"), ids, 6, &why));
  // unique keys
  for (size_t i = 0; i < schema_count(); ++i)
    for (size_t j = i + 1; j < schema_count(); ++j) CHECK(strcmp(schema_begin()[i].key, schema_begin()[j].key) != 0);
  printf("%d failures\n", fails);
  return fails != 0;
}
