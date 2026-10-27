#include "co4_task.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "co4_bff.h"
#include "co4_oidc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "net.h"
#include "sdkconfig.h"
#include "settings.h"
#include "sys.h"

#ifndef CONFIG_PANEL_CO4_REALM_URL
#ifndef CONFIG_PANEL_CO4_SCOPE
#define CONFIG_PANEL_CO4_SCOPE "openid offline_access"
#endif
#define CONFIG_PANEL_CO4_REALM_URL "https://auth.co4.cloud/realms/co4_beta"
#endif
#ifndef CONFIG_PANEL_CO4_CLIENT_ID
#define CONFIG_PANEL_CO4_CLIENT_ID "co4-panel"
#endif
#ifndef CONFIG_PANEL_CO4_BFF_URL
#define CONFIG_PANEL_CO4_BFF_URL "https://bff.beta.co4.dev"
#endif

namespace {

const char* const kTag = "co4";
constexpr size_t kBodyCap = 64 * 1024;  // discovery ~6-10 KB, site day @15 min ~11 KB, tree levels
constexpr size_t kFormCap = 3 * 1024;
constexpr size_t kAccessCap = 2560;
constexpr size_t kRefreshCap = 2048;
constexpr uint32_t kDataPeriodMs = 15 * 60 * 1000;
constexpr uint32_t kRetryMs = 60 * 1000;
constexpr uint32_t kMinWaitMs = 50;

enum Cmd : uint32_t {
  kCmdLogin = 1u << 0,
  kCmdCancel = 1u << 1,
  kCmdLogout = 1u << 2,
  kCmdZones = 1u << 3,
  kCmdZone = 1u << 4,
  kCmdRefresh = 1u << 5,
  kCmdMode = 1u << 6,
};

// Everything big lives in PSRAM.
struct Work {
  char body[kBodyCap + 1];
  char form[kFormCap];
  char access[kAccessCap];   // RAM only, never stored or printed
  char refresh[kRefreshCap];  // mirror of NVS co4_rt
  char new_refresh[kRefreshCap];
  Co4Oidc oidc;
  Co4DeviceAuth dev;
  Co4Poll poll;
  Co4NavLevel nav;
  Co4Data data;
};

SemaphoreHandle_t g_mu = nullptr;
QueueHandle_t g_q = nullptr;
TaskHandle_t g_task = nullptr;
Work* g_w = nullptr;           // task-owned (except zones/data copies under g_mu)
Co4View* g_view = nullptr;     // PSRAM, guarded by g_mu
Co4NavLevel* g_nav = nullptr;  // PSRAM, guarded by g_mu
Co4Data* g_data = nullptr;       // PSRAM, guarded by g_mu
// Inputs from the UI, guarded by g_mu.
char g_pick_id[40];
char g_pick_name[48];
char g_pick_site[20];
Co4Mode g_pick_mode = Co4Mode::Fixture;
enum class NavOp : uint8_t { Root, Open, Back, Reload };
NavOp g_nav_op = NavOp::Root;
char g_nav_soid[20];
char g_nav_name[48];

bool g_have_oidc = false;
uint32_t g_access_until = 0;  // now_ms() when the access token must be renewed
uint32_t g_next_poll = 0;
uint32_t g_next_data = 0;
uint32_t g_next_refresh_try = 0;

void emit(Co4Event e) {
  if (g_q != nullptr) xQueueSend(g_q, &e, 0);  // full queue: UI will redraw anyway
}

template <typename F>
void edit(Co4Event e, F f) {
  xSemaphoreTake(g_mu, portMAX_DELAY);
  f(*g_view);
  xSemaphoreGive(g_mu);
  emit(e);
}

void wipe(char* p, size_t n) {
  volatile char* v = p;
  for (size_t i = 0; i < n; ++i) v[i] = 0;
}

Co4Mode mode() {
  xSemaphoreTake(g_mu, portMAX_DELAY);
  Co4Mode m = g_view->mode;
  xSemaphoreGive(g_mu);
  return m;
}

void set_state(Co4State s, const char* msg) {
  edit(Co4Event::State, [&](Co4View& v) {
    v.state = s;
    snprintf(v.msg, sizeof(v.msg), "%s", msg != nullptr ? msg : "");
    v.token_set = g_w->refresh[0] != '\0';
    if (s != Co4State::Code) {
      v.user_code[0] = v.verify_uri[0] = v.verify_complete[0] = '\0';
    }
  });
}

bool discover() {
  if (g_have_oidc) return true;
  char url[200];
  if (!co4_discovery_url(CONFIG_PANEL_CO4_REALM_URL, url, sizeof(url))) return false;
  HttpGetResult r = http_get(url, nullptr, nullptr, reinterpret_cast<uint8_t*>(g_w->body), kBodyCap, 8000);
  if (!r.ok || r.status != 200) {
    ESP_LOGW(kTag, "discovery failed (HTTP %d, %s)", r.status, r.error);
    return false;
  }
  g_w->body[r.length] = '\0';
  g_have_oidc = co4_parse_discovery(g_w->body, r.length, &g_w->oidc);
  if (!g_have_oidc) ESP_LOGW(kTag, "discovery: endpoints missing");
  return g_have_oidc;
}

HttpGetResult post_form(const char* url, const char* bearer = nullptr) {
  HttpGetResult r = http_post(url, "application/x-www-form-urlencoded", g_w->form, strlen(g_w->form), bearer,
                              reinterpret_cast<uint8_t*>(g_w->body), kBodyCap, 10000);
  wipe(g_w->form, sizeof(g_w->form));  // the form may hold the device code or the refresh token
  g_w->body[r.length < kBodyCap ? r.length : kBodyCap] = '\0';
  return r;
}

void form_start() { g_w->form[0] = '\0'; }
bool form(const char* k, const char* v) { return co4_form_add(g_w->form, sizeof(g_w->form), k, v); }

void drop_tokens() {
  wipe(g_w->access, sizeof(g_w->access));
  wipe(g_w->refresh, sizeof(g_w->refresh));
  wipe(g_w->new_refresh, sizeof(g_w->new_refresh));
  g_access_until = 0;
  settings_save_co4_token("");
}

// Accepts a token answer: access token to RAM, refresh token to NVS (every time).
bool take_tokens(size_t len, Co4TokErr* err) {
  Co4TokenOut t = {g_w->access, sizeof(g_w->access), g_w->new_refresh, sizeof(g_w->new_refresh), 0, Co4TokErr::Other};
  bool ok = co4_parse_token(g_w->body, len, &t);
  wipe(g_w->body, len);
  *err = t.err;
  if (!ok || t.err != Co4TokErr::None || g_w->access[0] == '\0') return false;
  if (g_w->new_refresh[0] != '\0' && strcmp(g_w->new_refresh, g_w->refresh) != 0) {
    snprintf(g_w->refresh, sizeof(g_w->refresh), "%s", g_w->new_refresh);
  }
  wipe(g_w->new_refresh, sizeof(g_w->new_refresh));
  if (g_w->refresh[0] == '\0' || !settings_save_co4_token(g_w->refresh)) {
    ESP_LOGW(kTag, "token: no offline token or NVS write failed");
  }
  int64_t at = co4_refresh_at(now_ms(), t.expires_in);
  g_access_until = static_cast<uint32_t>(at);
  return true;
}

void do_login() {
  set_state(Co4State::Starting, "");
  if (!discover()) {
    set_state(g_w->refresh[0] ? Co4State::LoggedIn : Co4State::NoLogin, "Co4 nicht erreichbar");
    return;
  }
  if (!co4_form_device(g_w->form, sizeof(g_w->form), CONFIG_PANEL_CO4_CLIENT_ID, CONFIG_PANEL_CO4_SCOPE)) return;
  HttpGetResult r = post_form(g_w->oidc.device_ep);
  if (!r.ok || r.status != 200 || !co4_parse_device_auth(g_w->body, r.length, &g_w->dev)) {
    ESP_LOGW(kTag, "device authorization failed (HTTP %d)", r.status);
    set_state(g_w->refresh[0] ? Co4State::LoggedIn : Co4State::NoLogin, "Anmeldung nicht möglich");
    return;
  }
  uint32_t now = now_ms();
  co4_poll_init(&g_w->poll, g_w->dev, now);
  g_next_poll = now + static_cast<uint32_t>(g_w->poll.interval_s) * 1000u;
  edit(Co4Event::State, [&](Co4View& v) {
    v.state = Co4State::Code;
    v.msg[0] = '\0';
    snprintf(v.user_code, sizeof(v.user_code), "%s", g_w->dev.user_code);
    snprintf(v.verify_uri, sizeof(v.verify_uri), "%s", g_w->dev.verify_uri);
    snprintf(v.verify_complete, sizeof(v.verify_complete), "%s", g_w->dev.verify_complete);
    v.code_until_ms = static_cast<uint32_t>(g_w->poll.deadline_ms);
  });
}

void do_poll() {
  if (!co4_form_poll(g_w->form, sizeof(g_w->form), CONFIG_PANEL_CO4_CLIENT_ID, g_w->dev.device_code)) return;
  HttpGetResult r = post_form(g_w->oidc.token_ep);
  Co4TokErr e = Co4TokErr::Other;
  bool got = r.status == 200 && take_tokens(r.length, &e);
  if (!got && r.status != 200 && r.length > 0) {
    Co4TokenOut t = {g_w->access, sizeof(g_w->access), g_w->new_refresh, sizeof(g_w->new_refresh), 0,
                     Co4TokErr::Other};
    co4_parse_token(g_w->body, r.length, &t);
    e = t.err;
  }
  uint32_t now = now_ms();
  Co4PollStep st = co4_poll_step(&g_w->poll, got ? Co4TokErr::None : e, now);
  switch (st) {
    case Co4PollStep::Wait:
      g_next_poll = now + static_cast<uint32_t>(g_w->poll.interval_s) * 1000u;
      return;
    case Co4PollStep::Done:
      wipe(g_w->dev.device_code, sizeof(g_w->dev.device_code));
      ESP_LOGI(kTag, "login ok");
      settings_save_co4_expired(false);
      set_state(Co4State::LoggedIn, "");
      g_next_data = now;
      return;
    case Co4PollStep::Expired:
      set_state(Co4State::NoLogin, "Code abgelaufen – bitte nochmals versuchen");
      break;
    case Co4PollStep::Denied:
      set_state(Co4State::NoLogin, "Anmeldung abgelehnt");
      break;
    case Co4PollStep::Fail:
      set_state(Co4State::NoLogin, "Anmeldung fehlgeschlagen");
      break;
  }
  wipe(g_w->dev.device_code, sizeof(g_w->dev.device_code));
}

// true = access token valid now.
bool ensure_access() {
  if (g_w->refresh[0] == '\0') return false;
  uint32_t now = now_ms();
  if (g_w->access[0] != '\0' && static_cast<int32_t>(g_access_until - now) > 0) return true;
  if (static_cast<int32_t>(g_next_refresh_try - now) > 0) return false;
  if (!discover()) {
    g_next_refresh_try = now + kRetryMs;
    return false;
  }
  if (!co4_form_refresh(g_w->form, sizeof(g_w->form), CONFIG_PANEL_CO4_CLIENT_ID, g_w->refresh)) {
    g_next_refresh_try = now + kRetryMs;
    return false;
  }
  HttpGetResult r = post_form(g_w->oidc.token_ep);
  Co4TokErr e = Co4TokErr::Other;
  if (r.status == 200 && take_tokens(r.length, &e)) return true;
  if (r.status != 0 && r.length > 0 && r.status != 200) {
    Co4TokenOut t = {g_w->access, sizeof(g_w->access), g_w->new_refresh, sizeof(g_w->new_refresh), 0,
                     Co4TokErr::Other};
    co4_parse_token(g_w->body, r.length, &t);
    e = t.err;
  }
  wipe(g_w->access, sizeof(g_w->access));
  if (e == Co4TokErr::InvalidGrant) {
    ESP_LOGW(kTag, "refresh: invalid_grant, login expired");
    drop_tokens();
    settings_save_co4_expired(true);  // survives reboot: "abgelaufen", not "nicht verbunden"
    set_state(Co4State::Expired, "Co4-Anmeldung abgelaufen");
    return false;
  }
  ESP_LOGW(kTag, "refresh failed (HTTP %d), retry later", r.status);
  g_next_refresh_try = now + kRetryMs;
  return false;
}

void do_logout() {
  if (g_w->refresh[0] != '\0' && discover()) {
    if (g_w->oidc.end_session_ep[0] != '\0') {
      form_start();
      form("client_id", CONFIG_PANEL_CO4_CLIENT_ID);
      form("refresh_token", g_w->refresh);
      HttpGetResult r = post_form(g_w->oidc.end_session_ep);
      ESP_LOGI(kTag, "logout: HTTP %d", r.status);
    }
    if (g_w->oidc.revocation_ep[0] != '\0') {
      form_start();
      form("client_id", CONFIG_PANEL_CO4_CLIENT_ID);
      form("token", g_w->refresh);
      form("token_type_hint", "refresh_token");
      HttpGetResult r = post_form(g_w->oidc.revocation_ep);
      ESP_LOGI(kTag, "revoke: HTTP %d", r.status);
    }
  }
  drop_tokens();  // locally always, even if Co4 is not reachable
  settings_save_co4_expired(false);
  xSemaphoreTake(g_mu, portMAX_DELAY);
  g_view->have_data = false;
  g_view->fetch = Co4Fetch::Idle;
  xSemaphoreGive(g_mu);
  set_state(Co4State::NoLogin, "");
  emit(Co4Event::Data);
}

bool bff_get(const char* path, const char* bearer, size_t* len, char* err, size_t err_n);

void publish_nav() {
  xSemaphoreTake(g_mu, portMAX_DELAY);
  *g_nav = g_w->nav;
  xSemaphoreGive(g_mu);
}

// Object tree: one level per request (co4CoreNavRoots / co4CoreNavGet). Names and
// IDs are never logged.
void do_nav() {
  xSemaphoreTake(g_mu, portMAX_DELAY);
  NavOp op = g_nav_op;
  char soid[20], name[48];
  snprintf(soid, sizeof(soid), "%s", g_nav_soid);
  snprintf(name, sizeof(name), "%s", g_nav_name);
  xSemaphoreGive(g_mu);
  Co4NavLevel& L = g_w->nav;
  if (op == NavOp::Root) {
    L.depth = 0;
  } else if (op == NavOp::Open) {
    if (L.depth < kCo4NavDepth && co4_bff_niid_ok(soid)) {
      Co4NavCrumb& c = L.path[L.depth++];
      snprintf(c.soid, sizeof(c.soid), "%s", soid);
      snprintf(c.name, sizeof(c.name), "%s", name);
    }
  } else if (op == NavOp::Back) {
    if (L.depth > 0) --L.depth;
  }
  L.n = 0;
  L.total = 0;
  publish_nav();
  edit(Co4Event::Zones, [](Co4View& v) {
    v.zones_state = Co4ZonesState::Loading;
    v.zones_err[0] = '\0';
  });
  const char* cur = L.depth > 0 ? L.path[L.depth - 1].soid : "";
  char err[48] = "";
  bool ok = false;
  if (mode() != Co4Mode::Live) {
    ok = co4_bff_nav_fixture(cur, &L, err, sizeof(err));
  } else if (!wifi_connected()) {
    snprintf(err, sizeof(err), "%s", "keine Verbindung");
  } else if (!ensure_access()) {
    snprintf(err, sizeof(err), "%s", "Co4 nicht erreichbar");
  } else {
    char path[96];
    size_t len = 0;
    if (!co4_bff_nav_path(path, sizeof(path), cur)) {
      snprintf(err, sizeof(err), "%s", "Objekt ung\xC3\xBCltig");
    } else if (bff_get(path, g_w->access, &len, err, sizeof(err))) {
      ok = co4_bff_parse_nav(g_w->body, len, cur[0] == '\0', &L, err, sizeof(err));
      if (!ok) ESP_LOGW(kTag, "nav: %s", err);
    }
  }
  ESP_LOGI(kTag, "nav depth %u: %s, %u/%u", L.depth, ok ? "ok" : "error", L.n, L.total);
  publish_nav();
  edit(Co4Event::Zones, [&](Co4View& v) {
    v.zones_state = ok ? Co4ZonesState::Ok : Co4ZonesState::Error;
    snprintf(v.zones_err, sizeof(v.zones_err), "%s", err);
  });
}

void do_zone_pick() {
  char id[40], name[48], site[20];
  xSemaphoreTake(g_mu, portMAX_DELAY);
  snprintf(id, sizeof(id), "%s", g_pick_id);
  snprintf(name, sizeof(name), "%s", g_pick_name);
  snprintf(site, sizeof(site), "%s", g_pick_site);
  xSemaphoreGive(g_mu);
  bool saved = settings_save_co4_zone(id, name, site);
  edit(Co4Event::State, [&](Co4View& v) {
    v.zone_set = saved && id[0] != '\0';
    snprintf(v.zone_id, sizeof(v.zone_id), "%s", v.zone_set ? id : "");
    snprintf(v.zone_name, sizeof(v.zone_name), "%s", v.zone_set ? name : "");
    snprintf(v.site_niid, sizeof(v.site_niid), "%s", v.zone_set ? site : "");
    v.have_data = false;
    v.fetch = Co4Fetch::Idle;
  });
  g_next_data = now_ms();
}

// Live source: zevGetZoneEnergyStats on the Co4 BFF, three windows (today 15 min,
// month per day, year per month). Runs on the Co4 task; body buffer in PSRAM.
bool bff_get(const char* path, const char* bearer, size_t* len, char* err, size_t err_n) {
  char url[256];
  int w = snprintf(url, sizeof(url), "%s/operations/%s", CONFIG_PANEL_CO4_BFF_URL, path);
  if (w <= 0 || static_cast<size_t>(w) >= sizeof(url)) {
    snprintf(err, err_n, "Anfrage zu lang");
    return false;
  }
  snprintf(g_w->form, sizeof(g_w->form), "Bearer %s", bearer);
  HttpGetResult r = http_get(url, nullptr, nullptr, reinterpret_cast<uint8_t*>(g_w->body), kBodyCap, 20000,
                             "Authorization", g_w->form);
  wipe(g_w->form, sizeof(g_w->form));
  if (!r.ok || r.status != 200) {
    ESP_LOGW(kTag, "bff: HTTP %d (%s)", r.status, r.ok ? "status" : "transport/size");
    snprintf(err, err_n, "%s", r.status == 401 || r.status == 403 ? "Co4 verweigert Zugriff" : "Co4 nicht erreichbar");
    return false;
  }
  g_w->body[r.length] = '\0';
  *len = r.length;
  return true;
}

template <int N>
bool bff_block(const char* zone, const char* bearer, const Co4BffWindow& win, Co4Block<N>* b, char* err,
               size_t err_n) {
  char path[160];
  if (!co4_bff_stats_path(path, sizeof(path), zone, win.start_ms, win.end_ms, win.period)) {
    snprintf(err, err_n, "Zone ohne Co4-ID");
    return false;
  }
  size_t len = 0;
  if (!bff_get(path, bearer, &len, err, err_n)) return false;
  bool partial = false;
  Co4BffRc rc = co4_bff_parse_stats(g_w->body, len, &b->v[0][0], N, &b->n, &partial, err, err_n);
  b->res = win.res;
  b->t0_ms = win.start_ms;
  b->step_ms = win.step_ms;
  snprintf(b->unit, sizeof(b->unit), "kWh");
  return rc == Co4BffRc::Ok;
}

// Site niid of the chosen zone: from the zone choice (NVS co4_site) or, if that
// is empty, once from zevGetZone (zone.site.niid) and then stored.
bool site_niid(const char* zone, const char* bearer, char* site, size_t n) {
  xSemaphoreTake(g_mu, portMAX_DELAY);
  snprintf(site, n, "%s", g_view->site_niid);
  xSemaphoreGive(g_mu);
  if (co4_bff_niid_ok(site)) return true;
  char path[64], err[48];
  size_t len = 0;
  if (!co4_bff_zone_path(path, sizeof(path), zone) || !bff_get(path, bearer, &len, err, sizeof(err))) return false;
  if (!co4_bff_parse_zone_site(g_w->body, len, site, n, err, sizeof(err))) {
    ESP_LOGW(kTag, "zone site: %s", err);
    return false;
  }
  settings_save_co4_site(site);
  xSemaphoreTake(g_mu, portMAX_DELAY);
  snprintf(g_view->site_niid, sizeof(g_view->site_niid), "%s", site);
  xSemaphoreGive(g_mu);
  return true;
}

template <int N>
bool site_block(const char* site, const char* bearer, const Co4BffWindow& win, Co4Block<N>* b, bool* partial,
                char* err, size_t err_n) {
  char path[160];
  size_t len = 0;
  b->n = 0;
  if (!co4_bff_site_stats_path(path, sizeof(path), site, win.start_ms, win.end_ms, win.period)) return false;
  if (!bff_get(path, bearer, &len, err, err_n)) return false;
  bool p = false;
  Co4BffRc rc = co4_bff_parse_site_stats(g_w->body, len, &b->v[0][0], N, &b->n, &p, err, err_n);
  *partial = *partial || p;
  b->res = win.res;
  b->t0_ms = win.start_ms;
  b->step_ms = win.step_ms;
  snprintf(b->unit, sizeof(b->unit), "kWh");
  return rc == Co4BffRc::Ok;
}

// Whole site after the zone. Failures only mark the site block (zone stays).
// TODO(Co4): zev resolves siteEnergyStats as a subscription ("can take a while");
// the BFF exposes it as a query. Whether 20 s per window (bff_get) suffice is open.
void site_fetch(const char* zone, const char* bearer, const Co4BffWindow win[3], Co4SiteData* s) {
  s->state = Co4SiteState::None;
  s->partial = false;
  s->err[0] = '\0';
  s->day.n = s->days.n = s->months.n = 0;
  char site[20];
  if (!site_niid(zone, bearer, site, sizeof(site))) return;
  bool ok = site_block(site, bearer, win[0], &s->day, &s->partial, s->err, sizeof(s->err)) &&
            site_block(site, bearer, win[1], &s->days, &s->partial, s->err, sizeof(s->err)) &&
            site_block(site, bearer, win[2], &s->months, &s->partial, s->err, sizeof(s->err));
  if (ok) {
    for (int k = 0; k < kCo4SeriesN; ++k) co4_bff_energy_to_power(s->day.v[k], s->day.n, s->day.step_ms);
    snprintf(s->day.unit, sizeof(s->day.unit), "kW");
  }
  s->state = ok ? Co4SiteState::Ok : Co4SiteState::Error;
}

bool bff_fetch(const char* zone, const char* bearer, int64_t now_ms, Co4Data* out, char* err, size_t err_n) {
  if (bearer == nullptr) return false;
  Co4BffWindow win[3];
  co4_bff_windows(now_ms, win);
  if (!bff_block(zone, bearer, win[0], &out->day, err, err_n)) return false;
  for (int s = 0; s < kCo4SeriesN; ++s) co4_bff_energy_to_power(out->day.v[s], out->day.n, out->day.step_ms);
  snprintf(out->day.unit, sizeof(out->day.unit), "kW");
  if (!bff_block(zone, bearer, win[1], &out->days, err, err_n)) return false;
  if (!bff_block(zone, bearer, win[2], &out->months, err, err_n)) return false;
  site_fetch(zone, bearer, win, &out->site);
  out->example = false;
  out->fetched_ms = now_ms;
  return true;
}
const Co4Source kBffLive = {"Co4", false, bff_fetch};

void do_data() {
  Co4View v;
  co4_get(&v);
  bool demo = v.mode == Co4Mode::Demo;
  // Nothing to fetch yet (no zone / not logged in): still move g_next_data, or
  // task() would compute wait=0 and spin on core 0 (task_wdt IDLE0). Login and
  // zone pick set g_next_data = now themselves.
  if ((!v.zone_set && !demo) || (!demo && v.state != Co4State::LoggedIn)) {
    g_next_data = now_ms() + kRetryMs;
    return;
  }
  edit(Co4Event::Data, [](Co4View& w) { w.fetch = Co4Fetch::Loading; });
  const Co4Source& src = v.mode == Co4Mode::Live ? kBffLive : co4_fixture_source();
  const char* bearer = nullptr;
  char err[48] = "";
  bool ok = false;
  if (!src.example) {
    if (ensure_access()) bearer = g_w->access;
  }
  if (src.example || bearer != nullptr) {
    int64_t wall = static_cast<int64_t>(time(nullptr)) * 1000;
    ok = src.fetch(demo ? "" : v.zone_id, bearer, wall, &g_w->data, err, sizeof(err));
  } else if (err[0] == '\0') {
    snprintf(err, sizeof(err), "%s", "Co4 nicht erreichbar");
  }
  if (ok) {
    xSemaphoreTake(g_mu, portMAX_DELAY);
    *g_data = g_w->data;
    xSemaphoreGive(g_mu);
  }
  edit(Co4Event::Data, [&](Co4View& w) {
    w.fetch = ok ? Co4Fetch::Ok : Co4Fetch::Offline;
    if (ok) {
      w.have_data = true;
      w.data_ms = g_w->data.fetched_ms;
    }
    snprintf(w.data_err, sizeof(w.data_err), "%s", err);
  });
  g_next_data = now_ms() + (ok ? kDataPeriodMs : kRetryMs);
}

void task(void*) {
  for (;;) {
    uint32_t now = now_ms();
    uint32_t wait = kRetryMs;
    Co4View v;
    co4_get(&v);
    if (v.state == Co4State::Code) {
      int32_t d = static_cast<int32_t>(g_next_poll - now);
      wait = d > 0 ? static_cast<uint32_t>(d) : 0;
    } else {
      int32_t d = static_cast<int32_t>(g_next_data - now);
      if (d < static_cast<int32_t>(wait)) wait = d > 0 ? static_cast<uint32_t>(d) : 0;
    }
    if (wait < kMinWaitMs) wait = kMinWaitMs;  // always block: never starve IDLE0
    uint32_t cmd = 0;
    xTaskNotifyWait(0, UINT32_MAX, &cmd, pdMS_TO_TICKS(wait));
    if (cmd & kCmdMode) {
      xSemaphoreTake(g_mu, portMAX_DELAY);
      g_view->mode = g_pick_mode;
      g_view->have_data = false;
      g_view->fetch = Co4Fetch::Idle;
      xSemaphoreGive(g_mu);
      emit(Co4Event::Data);
      g_next_data = now_ms();
    }
    if (cmd & kCmdLogout) do_logout();
    if (cmd & kCmdCancel) {
      co4_get(&v);
      if (v.state == Co4State::Code || v.state == Co4State::Starting) {
        wipe(g_w->dev.device_code, sizeof(g_w->dev.device_code));
        set_state(g_w->refresh[0] ? Co4State::LoggedIn : Co4State::NoLogin, "");
      }
    }
    if (cmd & kCmdLogin) do_login();
    if (cmd & kCmdZones) do_nav();
    if (cmd & kCmdZone) do_zone_pick();
    if (cmd & kCmdRefresh) g_next_data = now_ms();
    co4_get(&v);
    now = now_ms();
    if (v.state == Co4State::Code && static_cast<int32_t>(g_next_poll - now) <= 0) {
      if (wifi_connected()) do_poll();
      else g_next_poll = now + 5000;
    }
    co4_get(&v);
    if (v.state != Co4State::Code && static_cast<int32_t>(g_next_data - now_ms()) <= 0) {
      if (wifi_connected() || v.mode != Co4Mode::Live) do_data();
      else {
        edit(Co4Event::Data, [](Co4View& w) { w.fetch = Co4Fetch::Offline; });
        g_next_data = now_ms() + kRetryMs;
      }
    }
  }
}

void cmd(uint32_t bits) {
  if (g_task != nullptr) xTaskNotify(g_task, bits, eSetBits);
}

template <typename T>
T* ps_new() {
  void* p = heap_caps_calloc(1, sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return static_cast<T*>(p);
}

}  // namespace

void co4_start() {
  if (g_task != nullptr) return;
  g_mu = xSemaphoreCreateMutex();
  g_q = xQueueCreate(8, sizeof(Co4Event));
  g_w = ps_new<Work>();
  g_view = ps_new<Co4View>();
  g_nav = ps_new<Co4NavLevel>();
  g_data = ps_new<Co4Data>();
  if (g_mu == nullptr || g_q == nullptr || g_w == nullptr || g_view == nullptr || g_nav == nullptr ||
      g_data == nullptr) {
    ESP_LOGE(kTag, "no memory, Co4 disabled");
    return;
  }
#if CONFIG_PANEL_CO4_FIXTURE
  g_view->mode = Co4Mode::Fixture;
#else
  g_view->mode = Co4Mode::Live;
#endif
  bool tok = settings_load_co4_token(g_w->refresh, sizeof(g_w->refresh));
  g_view->state = tok ? Co4State::LoggedIn : settings_load_co4_expired() ? Co4State::Expired : Co4State::NoLogin;
  g_view->token_set = tok;
  g_view->zone_set = settings_load_co4_zone(g_view->zone_id, sizeof(g_view->zone_id), g_view->zone_name,
                                            sizeof(g_view->zone_name));
  if (g_view->zone_set) settings_load_co4_site(g_view->site_niid, sizeof(g_view->site_niid));
  g_next_data = now_ms() + 5000;  // let Wi-Fi and SNTP come up
  xTaskCreatePinnedToCoreWithCaps(task, "co4", 10 * 1024, nullptr, 3, &g_task, 0, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

bool co4_ui_take(Co4Event* ev) { return g_q != nullptr && xQueueReceive(g_q, ev, 0) == pdTRUE; }

void co4_get(Co4View* out) {
  if (g_view == nullptr || g_mu == nullptr) {
    memset(out, 0, sizeof(*out));
    return;
  }
  xSemaphoreTake(g_mu, portMAX_DELAY);
  *out = *g_view;
  xSemaphoreGive(g_mu);
}

void co4_get_nav(Co4NavLevel* out) {
  out->n = 0;
  out->depth = 0;
  if (g_nav == nullptr) return;
  xSemaphoreTake(g_mu, portMAX_DELAY);
  *out = *g_nav;
  xSemaphoreGive(g_mu);
}

bool co4_get_data(Co4Data* out) {
  if (g_data == nullptr) return false;
  xSemaphoreTake(g_mu, portMAX_DELAY);
  bool have = g_view->have_data;
  if (have) *out = *g_data;
  xSemaphoreGive(g_mu);
  return have;
}

void co4_login() { cmd(kCmdLogin); }
void co4_login_cancel() { cmd(kCmdCancel); }
void co4_logout() { cmd(kCmdLogout); }
namespace {
void nav_req(NavOp op, const char* soid, const char* name) {
  if (g_mu == nullptr) return;
  xSemaphoreTake(g_mu, portMAX_DELAY);
  g_nav_op = op;
  snprintf(g_nav_soid, sizeof(g_nav_soid), "%s", soid != nullptr && co4_bff_niid_ok(soid) ? soid : "");
  co4_copy_utf8(g_nav_name, sizeof(g_nav_name), name != nullptr ? name : "");
  xSemaphoreGive(g_mu);
  cmd(kCmdZones);
}
}  // namespace

void co4_nav_root() { nav_req(NavOp::Root, nullptr, nullptr); }
void co4_nav_open(const char* soid, const char* name) { nav_req(NavOp::Open, soid, name); }
void co4_nav_back() { nav_req(NavOp::Back, nullptr, nullptr); }
void co4_nav_reload() { nav_req(NavOp::Reload, nullptr, nullptr); }
void co4_refresh_now() { cmd(kCmdRefresh); }

void co4_zone_choose(const char* id, const char* name, const char* site) {
  if (g_mu == nullptr) return;
  xSemaphoreTake(g_mu, portMAX_DELAY);
  snprintf(g_pick_site, sizeof(g_pick_site), "%s", co4_bff_niid_ok(site) ? site : "");
  co4_copy_utf8(g_pick_id, sizeof(g_pick_id), id != nullptr ? id : "");
  co4_copy_utf8(g_pick_name, sizeof(g_pick_name), name != nullptr ? name : "");
  xSemaphoreGive(g_mu);
  cmd(kCmdZone);
}

void co4_set_mode(Co4Mode m) {
  if (g_mu == nullptr) return;
  xSemaphoreTake(g_mu, portMAX_DELAY);
  g_pick_mode = m;
  xSemaphoreGive(g_mu);
  cmd(kCmdMode);
}

void co4_diag(char* out, size_t n) {
  static const char* const kState[] = {"nologin", "starting", "code", "loggedin", "expired"};
  static const char* const kMode[] = {"live", "fixture", "demo"};
  static const char* const kFetch[] = {"idle", "loading", "ok", "offline"};
  Co4View v;
  co4_get(&v);
  snprintf(out, n, "CO4 state=%s token=%s zone=%s site=%s src=%s fetch=%s data=%s heap_int=%u psram=%u",
           kState[static_cast<int>(v.state) % 5], v.token_set ? "set" : "unset", v.zone_set ? "set" : "unset",
           v.site_niid[0] ? "set" : "unset",
           kMode[static_cast<int>(v.mode) % 3], kFetch[static_cast<int>(v.fetch) % 4], v.have_data ? "yes" : "no",
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
}
