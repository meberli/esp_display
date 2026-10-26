// Host test for main/co4_data.cpp and main/co4_oidc.cpp (Co4 Energie: fixture,
// zones, ratios, device-flow parsing and polling). Example values only.
//   g++ -std=c++17 -Wall -I main -I components/ArduinoJson/src tools/test_co4.cpp main/co4_data.cpp main/co4_oidc.cpp -o /tmp/tco4 && TZ=Europe/Zurich /tmp/tco4
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "co4_data.h"
#include "co4_oidc.h"

static int fails = 0;
#define CHECK(c)                                          \
  do {                                                    \
    if (!(c)) {                                           \
      printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);  \
      fails++;                                            \
    }                                                     \
  } while (0)

static Co4Data data;

static void test_fixture() {
  // 26.10.2026 15:30 local (CET, UTC+1) = 14:30 UTC
  const int64_t now = 1793025000LL * 1000;
  co4_fixture_fill(now, &data);
  CHECK(data.example);
  CHECK(data.day.n == 96 && data.day.step_ms == 900000);
  time_t t0 = static_cast<time_t>(data.day.t0_ms / 1000);
  struct tm lt;
  localtime_r(&t0, &lt);
  CHECK(lt.tm_hour == 0 && lt.tm_min == 0 && lt.tm_mday == 26);
  CHECK(!isnan(data.day.v[kCo4Pv][60]));    // 15:00 has a value
  CHECK(isnan(data.day.v[kCo4Pv][63]));     // 15:45 is in the future
  CHECK(data.day.v[kCo4Pv][8] == 0.0f);     // 02:00 no PV
  for (int i = 0; i < 62; ++i) {            // balance per point: PV = Eigen + Einspeisung
    float pv = data.day.v[kCo4Pv][i], e = data.day.v[kCo4Eigen][i], x = data.day.v[kCo4Einspeisung][i];
    CHECK(fabsf(pv - e - x) < 1e-4f);
    CHECK(data.day.v[kCo4Bezug][i] >= 0.0f && x >= 0.0f);
  }
  CHECK(data.days.n == 31 && strcmp(data.days.unit, "kWh") == 0);
  CHECK(!isnan(data.days.v[kCo4Pv][25]) && isnan(data.days.v[kCo4Pv][26]));
  CHECK(data.months.n == 12 && !isnan(data.months.v[kCo4Pv][9]) && isnan(data.months.v[kCo4Pv][10]));
  Co4Data again;
  co4_fixture_fill(now, &again);
  CHECK(again.days.v[kCo4Pv][3] == data.days.v[kCo4Pv][3]);
}

static void test_ratio() {
  static float v[kCo4SeriesN][kCo4MaxBars];
  for (auto& s : v)
    for (float& x : s) x = NAN;
  v[kCo4Bezug][0] = 6;
  v[kCo4Eigen][0] = 4;
  v[kCo4Pv][0] = 10;
  v[kCo4Einspeisung][0] = 6;
  Co4Ratio r;
  CHECK(co4_ratio(v, 0, kCo4MaxBars, &r));
  CHECK(fabsf(r.autarky_pct - 40.0f) < 0.01f);
  CHECK(fabsf(r.self_pct - 40.0f) < 0.01f);
  for (auto& s : v)
    for (float& x : s) x = NAN;
  CHECK(!co4_ratio(v, 0, kCo4MaxBars, &r) && r.autarky_pct == 0.0f);
}

static void test_zones() {
  Co4ZoneList z;
  char err[48];
  co4_zones_fixture(&z);
  CHECK(z.n == 3 && strcmp(z.z[0].id, "zone-beispiel-1") == 0);
  CHECK(strstr(z.z[2].name, "B\xC3\xBCro") != nullptr);
  const char* bad = "{\"errors\":[{\"message\":\"x\"}]}";
  CHECK(!co4_zones_parse(bad, strlen(bad), &z, err, sizeof(err)) && err[0] != '\0');
  CHECK(!co4_zones_parse("nope", 4, &z, err, sizeof(err)));
  const char* empty = "{\"data\":[]}";
  CHECK(co4_zones_parse(empty, strlen(empty), &z, err, sizeof(err)) && z.n == 0);
  const char* noname = "{\"data\":[{\"id\":\"z-1\"},{\"name\":\"ohne id\"}]}";
  CHECK(co4_zones_parse(noname, strlen(noname), &z, err, sizeof(err)) && z.n == 1 && strcmp(z.z[0].name, "z-1") == 0);
}

static void test_utf8() {
  char b[6];
  co4_copy_utf8(b, sizeof(b), "abcd\xC3\xBC");  // ü would be cut in half
  CHECK(strcmp(b, "abcd") == 0);
  co4_copy_utf8(b, sizeof(b), "ab\xC3\xBC");
  CHECK(strcmp(b, "ab\xC3\xBC") == 0);
}

static void test_discovery() {
  char url[128];
  CHECK(co4_discovery_url("https://auth.example.invalid/realms/demo/", url, sizeof(url)));
  CHECK(strcmp(url, "https://auth.example.invalid/realms/demo/.well-known/openid-configuration") == 0);
  CHECK(!co4_discovery_url("http://auth.example.invalid/realms/demo", url, sizeof(url)));
  const char* j =
      "{\"issuer\":\"https://auth.example.invalid/realms/demo\","
      "\"device_authorization_endpoint\":\"https://auth.example.invalid/realms/demo/protocol/openid-connect/auth/device\","
      "\"token_endpoint\":\"https://auth.example.invalid/realms/demo/protocol/openid-connect/token\","
      "\"end_session_endpoint\":\"https://auth.example.invalid/realms/demo/protocol/openid-connect/logout\","
      "\"revocation_endpoint\":\"http://insecure.example.invalid/revoke\","
      "\"grant_types_supported\":[\"authorization_code\",\"urn:ietf:params:oauth:grant-type:device_code\"]}";
  Co4Oidc o;
  CHECK(co4_parse_discovery(j, strlen(j), &o));
  CHECK(strstr(o.device_ep, "/auth/device") != nullptr && strstr(o.token_ep, "/token") != nullptr);
  CHECK(strstr(o.end_session_ep, "/logout") != nullptr);
  CHECK(o.revocation_ep[0] == '\0');  // http dropped
  const char* nodev = "{\"token_endpoint\":\"https://a.invalid/t\"}";
  CHECK(!co4_parse_discovery(nodev, strlen(nodev), &o));
}

static void test_device() {
  const char* j =
      "{\"device_code\":\"DEVICE-CODE-PLACEHOLDER\",\"user_code\":\"ABCD-EFGH\","
      "\"verification_uri\":\"https://auth.example.invalid/realms/demo/device\","
      "\"verification_uri_complete\":\"https://auth.example.invalid/realms/demo/device?user_code=ABCD-EFGH\","
      "\"expires_in\":600,\"interval\":2}";
  Co4DeviceAuth a;
  CHECK(co4_parse_device_auth(j, strlen(j), &a));
  CHECK(strcmp(a.user_code, "ABCD-EFGH") == 0 && a.interval_s == 5 && a.expires_s == 600);
  CHECK(strstr(a.verify_complete, "user_code=") != nullptr);
  const char* j2 = "{\"device_code\":\"x\",\"user_code\":\"U\",\"verification_uri\":\"https://a.invalid/d\"}";
  CHECK(co4_parse_device_auth(j2, strlen(j2), &a) && strcmp(a.verify_complete, "https://a.invalid/d") == 0);

  Co4Poll p;
  co4_poll_init(&p, a, 0);
  CHECK(p.interval_s == 5 && p.deadline_ms == 600000);
  CHECK(co4_poll_step(&p, Co4TokErr::Pending, 1000) == Co4PollStep::Wait);
  CHECK(co4_poll_step(&p, Co4TokErr::SlowDown, 2000) == Co4PollStep::Wait && p.interval_s == 10);
  CHECK(co4_poll_step(&p, Co4TokErr::Pending, 595000) == Co4PollStep::Expired);
  CHECK(co4_poll_step(&p, Co4TokErr::Expired, 3000) == Co4PollStep::Expired);
  CHECK(co4_poll_step(&p, Co4TokErr::Denied, 3000) == Co4PollStep::Denied);
  CHECK(co4_poll_step(&p, Co4TokErr::None, 3000) == Co4PollStep::Done);
  CHECK(co4_poll_step(&p, Co4TokErr::Other, 3000) == Co4PollStep::Fail);
}

static void test_token() {
  char acc[64], ref[64];
  Co4TokenOut t = {acc, sizeof(acc), ref, sizeof(ref), 0, Co4TokErr::Other};
  const char* ok = "{\"access_token\":\"ACCESS-PLACEHOLDER\",\"expires_in\":300,\"refresh_token\":\"REFRESH-PLACEHOLDER\",\"token_type\":\"Bearer\"}";
  CHECK(co4_parse_token(ok, strlen(ok), &t) && t.err == Co4TokErr::None && t.expires_in == 300);
  CHECK(strcmp(acc, "ACCESS-PLACEHOLDER") == 0 && strcmp(ref, "REFRESH-PLACEHOLDER") == 0);
  const char* pend = "{\"error\":\"authorization_pending\",\"error_description\":\"x\"}";
  CHECK(co4_parse_token(pend, strlen(pend), &t) && t.err == Co4TokErr::Pending && acc[0] == '\0');
  const char* sd = "{\"error\":\"slow_down\"}";
  CHECK(co4_parse_token(sd, strlen(sd), &t) && t.err == Co4TokErr::SlowDown);
  const char* ig = "{\"error\":\"invalid_grant\",\"error_description\":\"Offline user session not found\"}";
  CHECK(co4_parse_token(ig, strlen(ig), &t) && t.err == Co4TokErr::InvalidGrant);
  CHECK(co4_tok_err("expired_token") == Co4TokErr::Expired && co4_tok_err("access_denied") == Co4TokErr::Denied);
  CHECK(co4_tok_err("server_error") == Co4TokErr::Other);
  char big[200];
  snprintf(big, sizeof(big), "{\"access_token\":\"%080d\",\"refresh_token\":\"r\"}", 1);
  CHECK(!co4_parse_token(big, strlen(big), &t) && acc[0] == '\0' && ref[0] == '\0');
  CHECK(!co4_parse_token("{", 1, &t) && t.err == Co4TokErr::Other);
}

static void test_form() {
  char f[96] = "";
  CHECK(co4_form_add(f, sizeof(f), "grant_type", "urn:ietf:params:oauth:grant-type:device_code"));
  CHECK(co4_form_add(f, sizeof(f), "scope", "openid offline_access"));
  CHECK(strcmp(f, "grant_type=urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Adevice_code&scope=openid+offline_access") == 0);
  char s[12] = "a=b";
  CHECK(!co4_form_add(s, sizeof(s), "client_id", "co4-panel") && strcmp(s, "a=b") == 0);
}

static void test_refresh_at() {
  CHECK(co4_refresh_at(0, 300) == 210000);
  CHECK(co4_refresh_at(0, 60) == 30000);
  CHECK(co4_refresh_at(0, 20) == 10000);
}

int main() {
  test_fixture();
  test_ratio();
  test_zones();
  test_utf8();
  test_discovery();
  test_device();
  test_token();
  test_form();
  test_refresh_at();
  printf(fails ? "%d FAILED\n" : "all Co4 tests passed\n", fails);
  return fails ? 1 : 0;
}
