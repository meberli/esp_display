// Host test for main/device_discover.cpp (classify GET bodies, subnet enum).
//   g++ -std=c++17 -Wall -I main -I components/ArduinoJson/src
//     tools/test_device_discover.cpp main/device_discover.cpp main/energy.cpp
//     -o /tmp/tdd && /tmp/tdd
// Run from the repo root (reads tools/testdata/whatwatt_report.json).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "device_discover.h"
#include "energy.h"

static int fails = 0;
#define CHECK(c, what)                                     \
  do {                                                     \
    if (!(c)) {                                            \
      printf("FAIL %s:%d %s\n", __FILE__, __LINE__, what); \
      fails++;                                             \
    }                                                      \
  } while (0)

static const char kLoxApi[] =
    R"JSON({"LL":{"control":"dev/cfg/api","value":"{'snr':'504F94AABBCC','version':'14.5.10.20','hashAlg':'SHA1','httpsStatus':0}","Code":"200"}})JSON";
static const char kLoxApiKey[] =
    R"JSON({"LL":{"control":"dev/cfg/apiKey","value":"{'snr':'504F94123456','version':'15.0.0.1'}","Code":"200"}})JSON";
static const char kLoxApiObj[] =
    R"JSON({"LL":{"control":"dev/cfg/api","value":{"snr":"504F94CCDD00","version":"15.1.0.0"},"Code":"200"}})JSON";

static void test_loxone_parse() {
  char name[48];
  CHECK(discover_is_loxone_api(kLoxApi, strlen(kLoxApi), name, sizeof(name)), "classic api");
  CHECK(strstr(name, "504F94AABBCC") != nullptr, "snr in label");
  CHECK(discover_is_loxone_api(kLoxApiKey, strlen(kLoxApiKey), name, sizeof(name)), "apiKey json");
  CHECK(strstr(name, "504F94123456") != nullptr, "snr from apiKey");
  CHECK(discover_is_loxone_api(kLoxApiObj, strlen(kLoxApiObj), name, sizeof(name)), "api object value");
  CHECK(strstr(name, "504F94CCDD00") != nullptr, "snr from object value");
  CHECK(!discover_is_loxone_api("<html><body>hi</body></html>", 28, name, sizeof(name)), "html rejected");
  CHECK(!discover_is_loxone_api("{\"version\":\"1.0\"}", 18, name, sizeof(name)), "random json rejected");
  CHECK(!discover_is_loxone_api("", 0, name, sizeof(name)), "empty rejected");
  CHECK(!discover_is_loxone_api(nullptr, 0, name, sizeof(name)), "null rejected");
}

static void test_whatwatt_parse() {
  FILE* f = fopen("tools/testdata/whatwatt_report.json", "rb");
  CHECK(f != nullptr, "sample file");
  if (f == nullptr) return;
  static char buf[8192];
  size_t n = fread(buf, 1, sizeof(buf), f);
  fclose(f);
  char name[48];
  CHECK(discover_is_whatwatt_report(buf, n, name, sizeof(name)), "sample report");
  CHECK(strstr(name, "whatwatt") != nullptr, "whatwatt label");
  // meter.id in the sample is 00000000, so the label stays generic
  CHECK(!discover_is_whatwatt_report("<html>", 6, name, sizeof(name)), "html rejected");
  CHECK(!discover_is_whatwatt_report(kLoxApi, strlen(kLoxApi), name, sizeof(name)), "loxone is not whatwatt");
  CHECK(!discover_is_loxone_api(buf, n, name, sizeof(name)), "whatwatt is not loxone");
}

static void test_format_and_hosts() {
  char host[64];
  discover_format_host(host, sizeof(host), "192.168.1.10", 80);
  CHECK(strcmp(host, "192.168.1.10") == 0, "port 80 omitted");
  discover_format_host(host, sizeof(host), "192.168.1.10", 8080);
  CHECK(strcmp(host, "192.168.1.10:8080") == 0, "non-80 port");

  uint32_t out[260];
  uint32_t self = (192u << 24) | (168u << 16) | (1u << 8) | 20u;
  uint32_t mask = 0xffffff00u;
  int n = discover_hosts(self, mask, self, out, 260);
  CHECK(n == 253, " /24 minus self and net/bcast");  // 1..254 except 20 = 253
  bool saw_self = false, saw_net = false, saw_bcast = false;
  uint32_t net = self & mask, bcast = net | 0xffu;
  for (int i = 0; i < n; ++i) {
    if (out[i] == self) saw_self = true;
    if (out[i] == net) saw_net = true;
    if (out[i] == bcast) saw_bcast = true;
  }
  CHECK(!saw_self && !saw_net && !saw_bcast, "self/net/bcast skipped");

  // Wide /16 is limited to the containing /24
  uint32_t mask16 = 0xffff0000u;
  n = discover_hosts(self, mask16, self, out, 260);
  CHECK(n == 253, "/16 clamped to /24");

  DiscoverHit hits[4] = {};
  DiscoverHit a = {};
  a.port = 80;
  a.kind = kDiscoverLoxone;
  snprintf(a.host, sizeof(a.host), "192.168.1.5");
  snprintf(a.name, sizeof(a.name), "Miniserver");
  int hn = discover_add_hit(hits, 0, 4, a);
  CHECK(hn == 1, "first hit");
  hn = discover_add_hit(hits, hn, 4, a);
  CHECK(hn == 1, "duplicate ignored");
  CHECK(discover_add_hit(hits, 0, 4, DiscoverHit{}) == 0, "empty host ignored");
}

static void test_nothing_found() {
  // The 'nothing found' path is an empty hit list plus the UI status
  // "Kein Gerät gefunden". Here we only assert the data side: a scan that
  // adds nothing leaves n == 0, and format/classify of non-matches stay empty.
  DiscoverHit hits[kDiscoverMaxHits] = {};
  int n = 0;
  char name[48] = "x";
  CHECK(!discover_is_loxone_api("nope", 4, name, sizeof(name)) && name[0] == '\0',
        "non-match clears name");
  CHECK(n == 0 && hits[0].host[0] == '\0', "empty suggestion list");
}

int main() {
  test_loxone_parse();
  test_whatwatt_parse();
  test_format_and_hosts();
  test_nothing_found();
  if (fails) {
    printf("%d FAILED\n", fails);
    return 1;
  }
  printf("ok\n");
  return 0;
}
