// Host test for main/meteo_plz.cpp:
//   g++ -std=c++17 -Wall -I main tools/test_meteo_plz.cpp main/meteo_plz.cpp -o /tmp/tmp && /tmp/tmp
#include <stdio.h>
#include <string.h>

#include "meteo_plz.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

static bool norm(const char* in, const char* want) {
  char out[8] = "x";
  bool ok = meteo_plz_normalize(in, out, sizeof(out));
  return want == nullptr ? (!ok && out[0] == '\0') : (ok && strcmp(out, want) == 0);
}
static bool query(const char* in, const char* want) {
  char out[8] = "x";
  bool ok = meteo_plz_query(in, out, sizeof(out));
  return want == nullptr ? (!ok && out[0] == '\0') : (ok && strcmp(out, want) == 0);
}

int main() {
  CHECK(meteo_plz_valid("9470"));
  CHECK(meteo_plz_valid("1000"));
  CHECK(meteo_plz_valid("9999"));
  CHECK(!meteo_plz_valid("0999"));
  CHECK(!meteo_plz_valid("947"));
  CHECK(!meteo_plz_valid("94700"));
  CHECK(!meteo_plz_valid("947000"));  // input field: 4 digits only
  CHECK(!meteo_plz_valid("94a0"));
  CHECK(!meteo_plz_valid(""));
  CHECK(!meteo_plz_valid(nullptr));
  CHECK(norm("9470", "9470"));
  CHECK(norm("947000", "9470"));  // old build default / NVS / backup
  CHECK(norm("800100", "8001"));
  CHECK(norm(" 9470 ", "9470"));
  CHECK(norm("947001", nullptr));  // 6 digits not ending in 00
  CHECK(norm("94700", nullptr));
  CHECK(norm("0470", nullptr));
  CHECK(norm("047000", nullptr));
  CHECK(norm("", nullptr));
  CHECK(norm(nullptr, nullptr));
  CHECK(norm("9470000", nullptr));
  char small[4] = "x";
  CHECK(!meteo_plz_normalize("9470", small, sizeof(small)));
  CHECK(query("9470", "947000"));
  CHECK(query("947000", "947000"));
  CHECK(query("1000", "100000"));
  CHECK(query("12", nullptr));
  char q6[6] = "x";
  CHECK(!meteo_plz_query("9470", q6, sizeof(q6)));
  printf("%d failures\n", fails);
  return fails;
}
