// Host test for main/ota_github.h:
//   g++ -std=c++17 -Wall -I main tools/test_ota_github.cpp -o t && ./t
#include <cstdio>
#include <cstring>

#include "ota_github.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

static bool err_is(int status, bool ok, const char* transport, const char* want) {
  char b[96];
  ota_github::list_http_err(status, ok, transport, b, sizeof(b));
  return strcmp(b, want) == 0;
}

int main() {
  char api[192];
  CHECK(ota_github::list_api(
      "https://github.com/meberli/esp_display/releases/latest/download/loxone_panel.bin", api, sizeof(api)));
  CHECK(strcmp(api, "https://api.github.com/repos/meberli/esp_display/releases?per_page=8") == 0);
  CHECK(ota_github::list_api(
      "https://github.com/meberli/esp_display/releases/download/v0.9.15-beta.1/loxone_panel_panlee.bin", api,
      sizeof(api), 3));
  CHECK(strcmp(api, "https://api.github.com/repos/meberli/esp_display/releases?per_page=3") == 0);
  CHECK(!ota_github::list_api("https://example.com/releases/latest", api, sizeof(api)));
  CHECK(!ota_github::list_api("http://github.com/meberli/esp_display/releases/latest", api, sizeof(api)));
  CHECK(!ota_github::list_api("", api, sizeof(api)));

  char empty[96];
  empty[0] = 'x';
  ota_github::list_http_err(200, true, "", empty, sizeof(empty));
  CHECK(empty[0] == '\0');

  // The 0.9.14 bug: HTTP 200 + body over the cap was shown as "GitHub-Fehler HTTP 200".
  CHECK(err_is(200, false, "Response is 172691 bytes (limit 98304)", "GitHub-Antwort zu gross"));
  CHECK(err_is(200, false, "Response exceeded 98304 bytes", "GitHub-Antwort zu gross"));
  CHECK(err_is(200, false, "body timeout", "Zeit\xC3\xBC" "berschreitung bei GitHub"));
  CHECK(err_is(403, false, "", "GitHub-Limit erreicht, sp\xC3\xA4ter erneut pr\xC3\xBC" "fen"));
  CHECK(err_is(429, true, "", "GitHub-Limit erreicht, sp\xC3\xA4ter erneut pr\xC3\xBC" "fen"));
  CHECK(err_is(404, false, "", "Kein Release gefunden"));
  CHECK(err_is(500, true, "", "GitHub-Fehler HTTP 500"));
  CHECK(err_is(0, false, "ESP_ERR_HTTP_CONNECT", "Keine Verbindung zu GitHub (ESP_ERR_HTTP_CONNECT)"));
  CHECK(err_is(200, false, "read error", "GitHub-Antwort unbrauchbar"));
  // Never claim HTTP 200 is the cause.
  char b[96];
  ota_github::list_http_err(200, false, "Response is 172691 bytes (limit 98304)", b, sizeof(b));
  CHECK(strstr(b, "200") == nullptr);

  printf("%d failures\n", fails);
  return fails;
}
