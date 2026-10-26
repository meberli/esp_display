// Host test for main/co4_data.cpp and main/co4_oidc.cpp (Co4 Energie: fixture,
// zones, ratios, device-flow parsing and polling). Example values only.
//   g++ -std=c++17 -Wall -I main -I components/ArduinoJson/src tools/test_co4.cpp main/co4_data.cpp main/co4_oidc.cpp -o /tmp/tco4 && TZ=Europe/Zurich /tmp/tco4
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "co4_bff.h"
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

// Anonymised, shortened copies of real BFF answers (structure as delivered,
// placeholder niids/names, 4 blocks instead of 96).
static const char kStatsJson[] =
    "{\"data\":{\"energyStats\":{\"start\":1793055600000,"
    "\"end\":[1793056500000,1793057400000,1793058300000,1793059200000],"
    "\"sources\":[{\"category\":\"ELECTRICITY\",\"type\":\"GRID\",\"production\":[0.25,0.5,null,1.0]},"
    "{\"category\":\"ELECTRICITY\",\"type\":\"PV\",\"production\":[0,0.125,0.5,null]},"
    "{\"category\":\"ELECTRICITY\",\"type\":\"PV\",\"production\":[0,0.125,0,0]},"
    "{\"category\":\"ELECTRICITY\",\"type\":\"BATTERY\",\"production\":[9,9,9,9]}],"
    "\"partialSources\":true}}}";
static const char kZonesJson[] =
    "{\"data\":{\"zones\":[{\"id\":11,\"niid\":1001,\"zoneType\":\"PRIVATE\",\"subAccountId\":\"sub-x\","
    "\"zoneOccupations\":[{\"invoicingEmail\":\"person@example.invalid\"}],"
    "\"details\":{\"name\":[{\"value\":\"Wohnung 1 B\xC3\xBC" "ro\"}]}},"
    "{\"id\":12,\"niid\":\"1002\",\"zoneType\":\"SHARED\",\"details\":{\"name\":[]}},"
    "{\"id\":13,\"niid\":\"x;y\",\"zoneType\":\"SHARED\"}]}}";

static void test_bff() {
  char p[160];
  CHECK(co4_bff_stats_path(p, sizeof(p), "1001", 1000, 2000, "900S"));
  CHECK(strcmp(p, "zevGetZoneEnergyStats?zoneNiid=1001&start=1000&end=2000&period=900S") == 0);
  CHECK(!co4_bff_stats_path(p, sizeof(p), "zone-beispiel-1", 1000, 2000, "900S"));
  CHECK(!co4_bff_stats_path(p, sizeof(p), "1001&x=1", 1000, 2000, "900S"));
  CHECK(!co4_bff_stats_path(p, 20, "1001", 1000, 2000, "900S"));
  CHECK(co4_bff_zones_path(p, sizeof(p), "42") && strcmp(p, "zevGetConsumptionZones?niid=42") == 0);
  CHECK(!co4_bff_niid_ok("") && !co4_bff_niid_ok("12345678901234567890") && co4_bff_niid_ok("1234567890123456789"));

  // 2026-10-26 15:37 local (UTC+1)
  const int64_t now = 1793025000LL * 1000 + 7 * 60000;  // fixture time + 7 min
  Co4BffWindow w[3];
  co4_bff_windows(now, w);
  CHECK(strcmp(w[0].period, "900S") == 0 && w[0].step_ms == 900000);
  CHECK(w[0].end_ms >= now && w[0].end_ms - now < 900000 && (w[0].end_ms - w[0].start_ms) % 900000 == 0);
  CHECK(strcmp(w[1].period, "1D") == 0 && (w[1].end_ms - w[1].start_ms) == 26LL * 86400000 + 3600000);  // DST end Oct 25
  CHECK(strcmp(w[2].period, "1M") == 0 && w[2].res == Co4Res::Month);
  time_t t = static_cast<time_t>(w[2].start_ms / 1000);
  struct tm lt;
  localtime_r(&t, &lt);
  CHECK(lt.tm_mon == 0 && lt.tm_mday == 1 && lt.tm_hour == 0);
  t = static_cast<time_t>(w[2].end_ms / 1000);
  localtime_r(&t, &lt);
  CHECK(lt.tm_mon == 10 && lt.tm_mday == 1);

  static Co4Curve c;
  char err[64];
  bool partial = false;
  CHECK(co4_bff_parse_stats(kStatsJson, strlen(kStatsJson), &c.v[0][0], 96, &c.n, &partial, err, sizeof(err)) ==
        Co4BffRc::Ok);
  CHECK(c.n == 4 && partial);
  CHECK(c.v[kCo4Bezug][1] == 0.5f && isnan(c.v[kCo4Bezug][2]));
  CHECK(c.v[kCo4Eigen][1] == 0.25f && c.v[kCo4Eigen][2] == 0.5f && c.v[kCo4Eigen][3] == 0.0f);
  CHECK(isnan(c.v[kCo4Einspeisung][0]) && isnan(c.v[kCo4Pv][0]));
  co4_bff_energy_to_power(c.v[kCo4Bezug], c.n, 900000);
  CHECK(c.v[kCo4Bezug][0] == 1.0f && isnan(c.v[kCo4Bezug][2]));
  // capping: only 2 of 4 blocks
  static Co4Totals tt;
  CHECK(co4_bff_parse_stats(kStatsJson, strlen(kStatsJson), &tt.v[0][0], 2, &tt.n, &partial, err, sizeof(err)) ==
        Co4BffRc::Ok);
  CHECK(tt.n == 2 && tt.v[kCo4Bezug][1] == 0.5f);

  const char nr[] = "{\"data\":{\"energyStats\":{\"start\":1,\"end\":[2,3],\"sources\":null,\"partialSources\":false}}}";
  CHECK(co4_bff_parse_stats(nr, strlen(nr), &c.v[0][0], 96, &c.n, &partial, err, sizeof(err)) == Co4BffRc::NotReady);
  const char em[] = "{\"data\":{\"energyStats\":{\"start\":1,\"end\":[2],\"sources\":[],\"partialSources\":false}}}";
  CHECK(co4_bff_parse_stats(em, strlen(em), &c.v[0][0], 96, &c.n, &partial, err, sizeof(err)) == Co4BffRc::Ok);
  CHECK(c.n == 1 && isnan(c.v[kCo4Bezug][0]));
  const char er[] = "{\"errors\":[{\"message\":\"Permission denied\"}],\"data\":null}";
  CHECK(co4_bff_parse_stats(er, strlen(er), &c.v[0][0], 96, &c.n, &partial, err, sizeof(err)) == Co4BffRc::Error);
  CHECK(co4_bff_parse_stats("{", 1, &c.v[0][0], 96, &c.n, &partial, err, sizeof(err)) == Co4BffRc::Error);

  Co4ZoneList zl;
  CHECK(co4_bff_parse_zones(kZonesJson, strlen(kZonesJson), &zl, err, sizeof(err)));
  CHECK(zl.n == 2);
  CHECK(strcmp(zl.z[0].id, "1001") == 0 && strcmp(zl.z[0].name, "Wohnung 1 B\xC3\xBCro") == 0);
  CHECK(strcmp(zl.z[1].id, "1002") == 0 && strcmp(zl.z[1].name, "1002") == 0);
  CHECK(!co4_bff_parse_zones(er, strlen(er), &zl, err, sizeof(err)));
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
  test_bff();
  printf(fails ? "%d FAILED\n" : "all Co4 tests passed\n", fails);
  return fails ? 1 : 0;
}
