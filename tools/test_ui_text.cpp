// Host test for main/ui_text.h: g++ -std=c++17 -I main tools/test_ui_text.cpp -o t && ./t
#include <cstdio>
#include <cstring>
#include "ui_text.h"
static int fails = 0;
static void eq(const char* got, const char* want) {
  if (strcmp(got, want) != 0) { printf("FAIL got '%s' want '%s'\n", got, want); ++fails; }
}
int main() {
  char b[96];
  const char* p1[] = {"net", "", "-78 dBm"};               ui_text::join(b, sizeof b, p1, 3); eq(b, "net \xC2\xB7 -78 dBm");
  const char* p2[] = {"net", " ", "-78 dBm", nullptr};     ui_text::join(b, sizeof b, p2, 4); eq(b, "net \xC2\xB7 -78 dBm");
  const char* p3[] = {"", "net"};                          ui_text::join(b, sizeof b, p3, 2); eq(b, "net");
  const char* p4[] = {"net", "\xC2\xB7", "-50 dBm"};       ui_text::join(b, sizeof b, p4, 3); eq(b, "net \xC2\xB7 -50 dBm");
  const char* p7[] = {"net \xC2\xB7 ", "-78 dBm"};         ui_text::join(b, sizeof b, p7, 2); eq(b, "net \xC2\xB7 -78 dBm");
  const char* p5[] = {"", ""};                             ui_text::join(b, sizeof b, p5, 2); eq(b, "");
  char s[8]; const char* p6[] = {"abcdef", "ghij"};        ui_text::join(s, sizeof s, p6, 2); if (strlen(s) >= sizeof s) ++fails;
  ui_text::url_short(b, sizeof b, "https://github.com/meberli/esp_display/releases/latest/download/x.bin");
  eq(b, "github.com/meberli/esp_display/\xE2\x80\xA6");
  ui_text::url_short(b, sizeof b, "https://github.com/meberli/esp_display"); eq(b, "github.com/meberli/esp_display");
  ui_text::url_short(b, sizeof b, "https://github.com/meberli/esp_display/"); eq(b, "github.com/meberli/esp_display");
  ui_text::url_short(b, sizeof b, "http://www.example.org/fw.bin?x=1"); eq(b, "example.org/fw.bin\xE2\x80\xA6");
  ui_text::url_short(b, sizeof b, "example.org"); eq(b, "example.org");
  printf("%d failures\n", fails);
  return fails;
}
