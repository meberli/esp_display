// Host test for main/co4_data.cpp and main/co4_oidc.cpp (Co4 Energie: fixture,
// zones, ratios, device-flow parsing and polling). Example values only.
//   g++ -std=c++17 -Wall -I main -I components/ArduinoJson/src tools/test_co4.cpp main/co4_data.cpp main/co4_oidc.cpp -o /tmp/tco4 && TZ=Europe/Zurich /tmp/tco4
#include <string>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "co4_bff.h"
#include "co4_data.h"
#include "co4_oidc.h"
#include "co4_testdata.h"

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
  // BEISPIELDATEN: negative Bezug (correction) is kept in the sum, shares stay in [0, 100].
  v[kCo4Bezug][0] = 2;
  v[kCo4Bezug][1] = -2.4f;
  v[kCo4Eigen][0] = 3;
  v[kCo4Pv][0] = 5;
  CHECK(co4_ratio(v, 0, kCo4MaxBars, &r));
  CHECK(fabsf(r.sum[kCo4Bezug] + 0.4f) < 0.001f);
  CHECK(r.autarky_pct == 100.0f && fabsf(r.self_pct - 60.0f) < 0.01f);
  for (auto& s : v)
    for (float& x : s) x = NAN;
  v[kCo4Bezug][0] = -1;  // only a negative Bezug: data, but no share
  CHECK(co4_ratio(v, 0, kCo4MaxBars, &r) && r.autarky_pct == 0.0f && r.self_pct == 0.0f);
  char t[16];
  co4_fmt_kwh(t, sizeof(t), -0.4f);
  CHECK(strcmp(t, "-0.4 kWh") == 0);
  co4_fmt_kwh(t, sizeof(t), -0.01f);
  CHECK(strcmp(t, "0.0 kWh") == 0);
  co4_fmt_kwh(t, sizeof(t), -250);
  CHECK(strcmp(t, "-250 kWh") == 0);
  co4_fmt_kwh(t, sizeof(t), -1250);
  CHECK(strcmp(t, "-1.25 MWh") == 0);
  co4_fmt_kwh(t, sizeof(t), NAN);
  CHECK(strcmp(t, "0.0 kWh") == 0);
  co4_fmt_kwh(t, sizeof(t), 12.34f);
  CHECK(strcmp(t, "12.3 kWh") == 0);
}

// Object tree with the placeholder tree (co4CoreNavRoots / co4CoreNavGet form).
static void test_tree() {
  static Co4NavLevel L;
  char err[48], path[96];
  CHECK(co4_bff_nav_path(path, sizeof(path), "") && strcmp(path, "co4CoreNavRoots") == 0);
  CHECK(co4_bff_nav_path(path, sizeof(path), "9001") && strcmp(path, "co4CoreNavGet?id=9001&hidden=false") == 0);
  CHECK(!co4_bff_nav_path(path, sizeof(path), "9001&x=1"));
  CHECK(co4_bff_nav_fixture("", &L, err, sizeof(err)) && L.n == 2 && L.total == 2);
  CHECK(strcmp(L.it[0].name, "Archiv (leer)") == 0 && !L.it[0].kids);  // sorted by name
  CHECK(strcmp(L.it[1].soid, "9001") == 0 && L.it[1].kids && L.it[1].zone[0] == '\0');
  CHECK(co4_bff_nav_fixture("9001", &L, err, sizeof(err)) && L.n == 2);
  CHECK(L.it[0].zone[0] == '\0');  // zev_Site is not selectable
  CHECK(co4_bff_nav_fixture("9010", &L, err, sizeof(err)) && L.n == 3);
  CHECK(strcmp(L.it[0].name, "Hauptz\xC3\xA4hler") == 0 && L.it[0].zone[0] == '\0');
  CHECK(strcmp(L.it[1].soid, "9011") == 0 && strcmp(L.it[1].zone, "7011") == 0 && L.it[1].kids);
  CHECK(strcmp(L.it[2].zone, "7012") == 0 && !L.it[2].kids);
  CHECK(co4_bff_nav_fixture("9002", &L, err, sizeof(err)) && L.n == 0 && L.total == 0);
  // several natures: the zev_Zone entry wins; string ids; missing name; no extra fields kept
  const char* multi =
      "{\"data\":{\"object\":{\"id\":\"1\",\"children\":[{\"id\":\"5\",\"hasChildren\":false,"
      "\"properties\":[{\"label\":\"description\",\"value\":\"x\"}],\"permissions\":[1],"
      "\"natures\":[{\"id\":\"8\",\"nature\":{\"code\":\"core_Folder\"}},{\"id\":\"9\",\"nature\":{\"code\":\"zev_Zone\"}}]},"
      "{\"properties\":[{\"label\":\"name\",\"value\":\"ohne id\"}]}]}}}";
  CHECK(co4_bff_parse_nav(multi, strlen(multi), false, &L, err, sizeof(err)) && L.n == 1);
  CHECK(strcmp(L.it[0].zone, "9") == 0 && strcmp(L.it[0].name, "(ohne Namen)") == 0);
  const char* bad = "{\"errors\":[{\"message\":\"x\"}]}";
  CHECK(!co4_bff_parse_nav(bad, strlen(bad), true, &L, err, sizeof(err)) && err[0] != '\0');
  CHECK(!co4_bff_parse_nav("nope", 4, true, &L, err, sizeof(err)));
  const char* nobj = "{\"data\":{\"object\":null}}";
  CHECK(!co4_bff_parse_nav(nobj, strlen(nobj), false, &L, err, sizeof(err)));
  // more children than kept: counted, list cut
  std::string big = "{\"data\":{\"roots\":[";
  for (int k = 0; k < kCo4NavMax + 5; ++k) {
    char e[96];
    snprintf(e, sizeof(e), "%s{\"id\":%d,\"properties\":[{\"label\":\"name\",\"value\":\"N%03d\"}]}", k ? "," : "",
             100 + k, k);
    big += e;
  }
  big += "]}}";
  CHECK(co4_bff_parse_nav(big.data(), big.size(), true, &L, err, sizeof(err)) && L.n == kCo4NavMax &&
        L.total == kCo4NavMax + 5);
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

  char b[256];
  auto pub = [&]() { return !strstr(b, "client_secret") && !strstr(b, "code_challenge") && !strstr(b, "code_verifier"); };
  CHECK(co4_form_device(b, sizeof(b), "co4-panel", "openid offline_access"));
  CHECK(strcmp(b, "client_id=co4-panel&scope=openid+offline_access") == 0 && pub());
  CHECK(co4_form_poll(b, sizeof(b), "co4-panel", "DEVCODE-PLACEHOLDER"));
  CHECK(strcmp(b, "grant_type=urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Adevice_code&device_code=DEVCODE-PLACEHOLDER"
                  "&client_id=co4-panel") == 0 && pub());
  CHECK(co4_form_refresh(b, sizeof(b), "co4-panel", "RT.PLACEHOLDER"));
  CHECK(strcmp(b, "grant_type=refresh_token&refresh_token=RT.PLACEHOLDER&client_id=co4-panel") == 0 && pub());
  CHECK(!co4_form_refresh(b, sizeof(b), "co4-panel", "") && b[0] == '\0');
  CHECK(!co4_form_device(b, 10, "co4-panel", "openid offline_access") && b[0] == '\0');
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
  const char ze[] = "{\"errors\":[{\"message\":\"Zone 9999 not exist\"}]}";
  CHECK(co4_bff_parse_stats(ze, strlen(ze), &c.v[0][0], 96, &c.n, &partial, err, sizeof(err)) == Co4BffRc::Error);
  CHECK(strstr(err, "Zone unbekannt") != nullptr);
  const char ge[] = "{\"errors\":[{\"message\":\"internal\"}]}";
  CHECK(co4_bff_parse_stats(ge, strlen(ge), &c.v[0][0], 96, &c.n, &partial, err, sizeof(err)) == Co4BffRc::Error);
  CHECK(strcmp(err, "Co4 meldet einen Fehler") == 0);
  const char em[] = "{\"data\":{\"energyStats\":{\"start\":1,\"end\":[2],\"sources\":[],\"partialSources\":false}}}";
  CHECK(co4_bff_parse_stats(em, strlen(em), &c.v[0][0], 96, &c.n, &partial, err, sizeof(err)) == Co4BffRc::Ok);
  CHECK(c.n == 1 && isnan(c.v[kCo4Bezug][0]));
  const char er[] = "{\"errors\":[{\"message\":\"Permission denied\"}],\"data\":null}";
  CHECK(co4_bff_parse_stats(er, strlen(er), &c.v[0][0], 96, &c.n, &partial, err, sizeof(err)) == Co4BffRc::Error);
  CHECK(co4_bff_parse_stats("{", 1, &c.v[0][0], 96, &c.n, &partial, err, sizeof(err)) == Co4BffRc::Error);

  Co4ZoneList zl;
  CHECK(co4_bff_parse_zones(kZonesJson, strlen(kZonesJson), "900", &zl, err, sizeof(err)));
  CHECK(zl.n == 2);
  CHECK(strcmp(zl.z[0].id, "1001") == 0 && strcmp(zl.z[0].name, "Wohnung 1 B\xC3\xBCro") == 0);
  CHECK(strcmp(zl.z[1].id, "1002") == 0 && strcmp(zl.z[1].name, "1002") == 0);
  CHECK(strcmp(zl.z[0].site, "900") == 0 && strcmp(zl.z[1].site, "900") == 0);
  CHECK(!co4_bff_parse_zones(er, strlen(er), "900", &zl, err, sizeof(err)));
}

// Placeholder site answer (shape as zevGetSiteEnergyStats), no real data.
static const char kSiteJson[] =
    "{\"data\":{\"energyStats\":{\"start\":1000,\"end\":[2000,3000,4000],"
    "\"gridProduction\":[1.5,null,2],\"feedIn\":[0,3,1],\"selfConsumption\":[4,5,6],"
    "\"pvProduction\":[4,8,7],\"pvConsumption\":[1,1,1],\"consumption\":[9,9,9],"
    "\"batteryProduction\":[0,0,0.5],\"batteryConsumption\":[0.25,0,0],\"batteryFromLocalPv\":[0,0,0],"
    "\"sources\":[{\"category\":\"GRID\",\"production\":[1],\"type\":\"x\"}],\"partialSources\":true}}}";

static void test_site() {
  char path[200];
  CHECK(co4_bff_site_stats_path(path, sizeof(path), "900", 1000, 2000, "1D"));
  CHECK(strstr(path, "zevGetSiteEnergyStats?") == path && strstr(path, "siteNiid=900") && strstr(path, "period=1D"));
  CHECK(!co4_bff_site_stats_path(path, sizeof(path), "9a", 1000, 2000, "1D"));
  CHECK(co4_bff_zone_path(path, sizeof(path), "1001") && strcmp(path, "zevGetZone?niid=1001") == 0);
  CHECK(!co4_bff_zone_path(path, sizeof(path), ""));

  static Co4Curve c;
  char err[48];
  bool partial = false;
  CHECK(co4_bff_parse_site_stats(kSiteJson, strlen(kSiteJson), &c.v[0][0], 96, &c.n, &partial, err, sizeof(err)) ==
        Co4BffRc::Ok);
  CHECK(c.n == 3 && partial);
  CHECK(c.v[kCo4Bezug][0] == 1.5f && isnan(c.v[kCo4Bezug][1]) && c.v[kCo4Bezug][2] == 2.f);
  CHECK(c.v[kCo4Einspeisung][1] == 3.f && c.v[kCo4Pv][1] == 8.f && c.v[kCo4Eigen][2] == 6.f);
  CHECK(c.v[kCo4BatLaden][0] == 0.25f && c.v[kCo4BatEntladen][2] == 0.5f);
  // no battery arrays -> battery NAN (block hidden), Bezug untouched
  const char nb[] = "{\"data\":{\"energyStats\":{\"start\":1,\"end\":[2],\"gridProduction\":[3],\"feedIn\":[1]}}}";
  CHECK(co4_bff_parse_site_stats(nb, strlen(nb), &c.v[0][0], 96, &c.n, &partial, err, sizeof(err)) == Co4BffRc::Ok);
  CHECK(c.n == 1 && c.v[kCo4Bezug][0] == 3.f && isnan(c.v[kCo4BatLaden][0]) && isnan(c.v[kCo4BatEntladen][0]));
  const char em[] = "{\"data\":{\"energyStats\":{\"start\":1,\"end\":[]}}}";
  CHECK(co4_bff_parse_site_stats(em, strlen(em), &c.v[0][0], 96, &c.n, &partial, err, sizeof(err)) == Co4BffRc::Ok);
  CHECK(c.n == 0);
  const char nl[] = "{\"data\":{\"energyStats\":null}}";
  CHECK(co4_bff_parse_site_stats(nl, strlen(nl), &c.v[0][0], 96, &c.n, &partial, err, sizeof(err)) == Co4BffRc::Error);
  const char er[] = "{\"errors\":[{\"message\":\"denied\"}],\"data\":null}";
  CHECK(co4_bff_parse_site_stats(er, strlen(er), &c.v[0][0], 96, &c.n, &partial, err, sizeof(err)) == Co4BffRc::Error);
  CHECK(co4_bff_parse_site_stats("{", 1, &c.v[0][0], 96, &c.n, &partial, err, sizeof(err)) == Co4BffRc::Error);

  char site[20];
  const char zj[] = "{\"data\":{\"zone\":{\"niid\":\"1001\",\"site\":{\"niid\":\"900\"},\"contract\":{\"x\":1}}}}";
  CHECK(co4_bff_parse_zone_site(zj, strlen(zj), site, sizeof(site), err, sizeof(err)) && strcmp(site, "900") == 0);
  const char zn[] = "{\"data\":{\"zone\":{\"niid\":1001,\"site\":{\"niid\":900}}}}";
  CHECK(co4_bff_parse_zone_site(zn, strlen(zn), site, sizeof(site), err, sizeof(err)) && strcmp(site, "900") == 0);
  const char z0[] = "{\"data\":{\"zone\":null}}";
  CHECK(!co4_bff_parse_zone_site(z0, strlen(z0), site, sizeof(site), err, sizeof(err)));

  // fixture: site Ok, battery present, site Bezug larger than zone Bezug
  static Co4Data d;
  co4_fixture_fill(1767225600000LL + 12 * 3600000LL, &d);
  CHECK(d.site.state == Co4SiteState::Ok && d.site.day.n > 0 && d.site.days.n > 0 && d.site.months.n > 0);
  bool bat = false;
  for (int i = 0; i < d.site.days.n; ++i) bat |= !isnan(d.site.days.v[kCo4BatLaden][i]);
  CHECK(bat);
  CHECK(isnan(d.days.v[kCo4BatLaden][0]));
}

int main() {
  test_fixture();
  test_ratio();
  test_tree();
  test_utf8();
  test_discovery();
  test_device();
  test_token();
  test_form();
  test_refresh_at();
  test_bff();
  test_site();
  printf(fails ? "%d FAILED\n" : "all Co4 tests passed\n", fails);
  return fails ? 1 : 0;
}
