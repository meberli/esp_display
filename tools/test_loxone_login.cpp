// Host test for main/loxone_host.h and main/loxone_err.h:
//   g++ -std=c++17 -Wall -I main tools/test_loxone_login.cpp -o t && ./t
#include <cstdio>
#include <cstring>

#include "loxone_err.h"
#include "loxone_host.h"

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

static bool parse_is(const char* in, const char* want_host, uint16_t want_port) {
  char host[64], err[96];
  uint16_t port = 0;
  bool ok = loxone_host::parse(in, host, sizeof(host), &port, err, sizeof(err));
  return ok && port == want_port && strcmp(host, want_host) == 0 && err[0] == '\0';
}

static bool parse_fails(const char* in) {
  char host[64], err[96];
  uint16_t port = 99;
  return !loxone_host::parse(in, host, sizeof(host), &port, err, sizeof(err)) && err[0] != '\0';
}

static bool msg_is(int status, const char* transport, const char* want) {
  char b[96];
  loxone_err::test_msg(status, transport, b, sizeof(b));
  return strcmp(b, want) == 0;
}

int main() {
  CHECK(parse_is("192.168.1.10", "192.168.1.10", 80));
  CHECK(parse_is("  192.168.1.10  ", "192.168.1.10", 80));
  CHECK(parse_is("192.168.1.10:8080", "192.168.1.10", 8080));
  CHECK(parse_is("http://miniserver.local:8080/jdev/cfg/api", "miniserver.local", 8080));
  CHECK(parse_is("miniserver.local", "miniserver.local", 80));
  // The discovery suggestion "ip:8080" must not leave the port in the host.
  CHECK(parse_is("192.168.1.34:8080", "192.168.1.34", 8080));
  {
    char host[64], err[96];
    uint16_t port = 0;
    CHECK(loxone_host::parse("192.168.1.10", host, sizeof(host), &port, err, sizeof(err), 8080));
    CHECK(port == 8080 && strcmp(host, "192.168.1.10") == 0);
    // A leftover "ip:8080" in the host field wins over a default of 80.
    CHECK(loxone_host::parse("192.168.1.10:8080", host, sizeof(host), &port, err, sizeof(err), 80));
    CHECK(port == 8080 && strcmp(host, "192.168.1.10") == 0);
  }
  CHECK(parse_fails(""));
  CHECK(parse_fails("   "));
  CHECK(parse_fails("https://192.168.1.10"));
  CHECK(parse_fails("host:"));
  CHECK(parse_fails("host:99999"));
  CHECK(parse_fails("host:abc"));

  // A too-small host buffer is rejected rather than truncated.
  char tiny[4], err[96];
  uint16_t port = 0;
  CHECK(!loxone_host::parse("192.168.1.10", tiny, sizeof(tiny), &port, err, sizeof(err)));

  CHECK(msg_is(401, "", "Benutzer oder Passwort falsch (401)"));
  CHECK(msg_is(403, "ignored", "Benutzer oder Passwort falsch (403)"));
  CHECK(msg_is(500, "", "Server antwortet mit HTTP 500"));
  CHECK(msg_is(0, "ESP_ERR_HTTP_CONNECT", "nicht erreichbar"));
  CHECK(msg_is(0, "", "nicht erreichbar"));
  CHECK(msg_is(0, "body timeout", "Zeit\xC3\xBC" "berschreitung"));
  CHECK(msg_is(0, "ESP_ERR_TIMEOUT", "Zeit\xC3\xBC" "berschreitung"));
  CHECK(msg_is(0, "ESP_ERR_HTTP_EAGAIN", "Zeit\xC3\xBC" "berschreitung"));
  CHECK(msg_is(-1, "HTTPS is not implemented",
               "HTTPS wird nicht unterst\xC3\xBCtzt - http-Adresse oder IPv4 eingeben"));
  char b[96];
  loxone_err::test_msg(0, "read error", b, sizeof(b));
  CHECK(strstr(b, "nicht erreichbar") != nullptr);
  CHECK(strstr(b, "password") == nullptr);
  CHECK(strstr(b, "Passwort") == nullptr);
  CHECK(strstr(b, "secret") == nullptr);

  printf("%d failures\n", fails);
  return fails;
}
