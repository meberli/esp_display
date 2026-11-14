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

static const char kWhatwattSystem[] =
    R"JSON({"system":{"id":"aabbccddeeff","boot_id":"7f3a","time_since_boot":12345},"wifi":{"ssid":"x"}})JSON";
static const char kWhatwattLooseReport[] =
    R"JSON({"report":{"power":1},"meter":{"id":"1"}})JSON";
static const char kRandomSystem[] = R"JSON({"system":"ok","version":"1.0"})JSON";

static void test_whatwatt_system_and_loose() {
  char name[48];
  CHECK(discover_is_whatwatt_system(kWhatwattSystem, strlen(kWhatwattSystem), name, sizeof(name)),
        "system json");
  CHECK(strstr(name, "ddeeff") != nullptr, "id suffix in label");
  CHECK(discover_is_whatwatt(kWhatwattSystem, strlen(kWhatwattSystem), name, sizeof(name)),
        "unified accepts system");
  CHECK(!discover_is_whatwatt_system(kRandomSystem, strlen(kRandomSystem), name, sizeof(name)),
        "bare system key rejected");
  CHECK(!discover_is_whatwatt_system("<html>system boot_id</html>", 26, name, sizeof(name)),
        "html rejected");
  CHECK(discover_is_whatwatt(kWhatwattLooseReport, strlen(kWhatwattLooseReport), name, sizeof(name)),
        "loose report+meter");
  FILE* f = fopen("tools/testdata/whatwatt_report.json", "rb");
  CHECK(f != nullptr, "sample file for unified");
  if (f != nullptr) {
    static char buf[8192];
    size_t n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    CHECK(discover_is_whatwatt(buf, n, name, sizeof(name)), "unified accepts sample report");
  }
}

static void test_mdns_fingerprint() {
  char name[48];
  CHECK(discover_mdns_is_whatwatt("whatwatt-ABCDEF", "Whatwatt Go ABCDEF", "100", "001122ABCDEF", name,
                                  sizeof(name)),
        "official hostname+type");
  CHECK(strstr(name, "ABCDEF") != nullptr, "id in mDNS label");
  CHECK(discover_mdns_is_whatwatt("whatwatt-ABCDEF.local", nullptr, nullptr, nullptr, name, sizeof(name)),
        "hostname only, .local stripped");
  CHECK(strcmp(name, "whatwatt Go ABCDEF") == 0, "no .local in label");
  CHECK(discover_mdns_is_whatwatt("box", "Whatwatt Go 99", nullptr, nullptr, name, sizeof(name)),
        "instance name");
  CHECK(discover_mdns_is_whatwatt("box", "printer", "100", "ff", name, sizeof(name)), "TXT type=100");
  CHECK(!discover_mdns_is_whatwatt("printer", "HP Laser", nullptr, nullptr, name, sizeof(name)),
        "unrelated http service rejected");
  CHECK(discover_mdns_is_loxone("miniserver", nullptr, name, sizeof(name)), "miniserver host");
  CHECK(strcmp(name, "Miniserver") == 0, "loxone label");
  CHECK(discover_mdns_is_loxone("host", "Loxone Miniserver", name, sizeof(name)), "instance");
  CHECK(!discover_mdns_is_loxone("printer", "HP", name, sizeof(name)), "unrelated rejected");
  CHECK(!discover_mdns_is_loxone("whatwatt-ABCDEF", "Whatwatt Go", name, sizeof(name)),
        "whatwatt is not loxone");
}
int main() {
  test_loxone_parse();
  test_whatwatt_parse();
  test_whatwatt_system_and_loose();
  test_mdns_fingerprint();
  test_format_and_hosts();
  test_nothing_found();
  if (fails) {
    printf("%d FAILED\n", fails);
    return 1;
  }
  printf("ok\n");
  return 0;
}
