// ÖV, Kalender, MeteoSchweiz (warnings + rain nowcast) and Wartung screens.
// Included into panel_ui.cpp inside its anonymous namespace: uses the shared
// widgets (make_label, make_row, style_glass, reg, ...) and the nav state.

// --- shared ---------------------------------------------------------------

constexpr uint32_t kTransitHomeMs = 300000;  // home chip: every 5 min
constexpr uint32_t kTransitViewMs = 60000;   // board on screen: every minute
constexpr uint32_t kTransitRetryMs = 30000;
constexpr uint32_t kCalendarMs = 1800000;
constexpr uint32_t kMeteoMs = 600000;
constexpr uint32_t kInfoRetryMs = 120000;
constexpr int kTrRows = 7;

Station stations[kStationN];
EXT_RAM_BSS_ATTR Board boards[kStationN];
uint32_t tr_next_ms[kStationN] = {0, 0};
bool tr_inflight = false;
int tr_idx = 0;

Calendar* cal = nullptr;  // PSRAM (large)
bool cal_have = false;
uint32_t cal_next_ms = 0;
bool cal_inflight = false;

EXT_RAM_BSS_ATTR MeteoNow meteo;
bool meteo_have = false;
uint32_t meteo_next_ms = 0;
bool meteo_inflight = false;

// Firmware update: always "Firmware prüfen" first; the install action only
// appears when the check found a strictly newer version.
enum class OtaUi { Idle, Checking, Current, Newer, Error, Installing };
OtaUi ota_state = OtaUi::Idle;
bool ota_check_req = false;    // UI -> net: check
bool ota_install_req = false;  // UI -> net: install ota_new_url
bool ota_running = false;      // request handed to the net task
uint32_t ota_t0 = 0;
char ota_msg[96] = "";
char ota_new_ver[32] = "";
char ota_new_url[256] = "";
char ota_notes[96] = "";

lv_obj_t* home_warn = nullptr;   // pill in the weather card
lv_obj_t* home_warn_txt = nullptr;
lv_obj_t* home_warn_icon = nullptr;
lv_obj_t* chip_tr_top = nullptr;
lv_obj_t* chip_tr_main = nullptr;
lv_obj_t* chip_tr_icon = nullptr;
lv_obj_t* chip_cal_top = nullptr;
lv_obj_t* chip_cal_main = nullptr;
lv_obj_t* chip_cal_icon = nullptr;

lv_obj_t* tr_root = nullptr;
lv_obj_t* tr_title = nullptr;
lv_obj_t* tr_page = nullptr;
lv_obj_t* tr_status = nullptr;
lv_obj_t* tr_row[kTrRows] = {};
lv_obj_t* tr_badge[kTrRows] = {};
lv_obj_t* tr_line[kTrRows] = {};
lv_obj_t* tr_to[kTrRows] = {};
lv_obj_t* tr_min[kTrRows] = {};
lv_obj_t* tr_sub[kTrRows] = {};
lv_obj_t* tr_prev = nullptr;
lv_obj_t* tr_next = nullptr;

lv_obj_t* calv_root = nullptr;
lv_obj_t* calv_list = nullptr;
lv_obj_t* calv_status = nullptr;

lv_obj_t* rain_root = nullptr;
lv_obj_t* rain_bar[kRainSteps] = {};
lv_obj_t* rain_range[kRainSteps] = {};
lv_obj_t* rain_sum = nullptr;
lv_obj_t* rain_list = nullptr;
lv_obj_t* rain_status = nullptr;
constexpr int kRainChartH = 110;

lv_obj_t* trset_root = nullptr;
lv_obj_t* trset_val[kStationN] = {};
lv_obj_t* st_root = nullptr;
lv_obj_t* st_title = nullptr;
lv_obj_t* st_ta = nullptr;
lv_obj_t* st_status = nullptr;
lv_obj_t* st_list = nullptr;
int st_target = 0;
Station st_hits[kMaxStationHits];
int st_hit_n = 0;
bool st_busy = false;

lv_obj_t* calset_root = nullptr;
lv_obj_t* calset_val[4] = {};  // iCal, Gemeinde (BFS), API, Zone
lv_obj_t* calset_until_val = nullptr;
uint16_t waste_until_min = kWasteUntilDefault;  // loaded in ui_init
uint16_t waste_from_min = kWasteFromDefault;    // "Am Vortag ab"
lv_obj_t* calset_from_val = nullptr;
constexpr uint16_t kWasteFromPresets[] = {720, 840, 960, 1020, 1080, 1140, 1200, 1260};
constexpr uint16_t kWasteUntilPresets[] = {420, 480, 540, 600, 720, 840, 1080, 1440};
lv_obj_t* txt_root = nullptr;
lv_obj_t* txt_title = nullptr;
lv_obj_t* txt_cur = nullptr;
lv_obj_t* txt_ta = nullptr;
lv_obj_t* txt_status = nullptr;
uint8_t txt_which = 0;
bool txt_busy = false;
Nav txt_back = Nav::Settings;

lv_obj_t* maint_root = nullptr;
lv_obj_t* maint_val[8] = {};
lv_obj_t* maint_web_lbl = nullptr;
lv_obj_t* maint_web_sw = nullptr;
lv_obj_t* web_confirm = nullptr;  // "Export mit Geheimnissen erlauben?" (main screen)
void build_web_confirm();
lv_obj_t* maint_ota_lbl = nullptr;
lv_obj_t* maint_ota_title = nullptr;
lv_obj_t* maint_url_val = nullptr;


int64_t days_from_civil_ui(int y, unsigned m, unsigned d) {  // H. Hinnant
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

void txt_go_back() {
  if (txt_back == Nav::Maint) open_maint();
  else if (txt_back == Nav::EnSet) open_enset();
  else open_calset();
}

void open_enset();
void info_hide_all() {
  hide(enset_root);
  hide(trset_root);
  hide(st_root);
  hide(calset_root);
  hide(txt_root);
  hide(maint_root);
  hide(remset_root);
  play_hide();
  co4_hide_all();
}

void info_hide_views() {
  hide(tr_root);
  hide(calv_root);
  hide(rain_root);
  hide(en_root[0]);
  hide(en_root[1]);
  hide(en_root[2]);
  co4_hide_views();
}

void enter_view(Nav n, lv_obj_t* root) {
  nav = n;
  message_open = false;
  hide(message);
  hide(home_root);
  hide(cat_root);
  hide(wx_root);
  hide(cam_root);
  hide_settings();
  info_hide_views();
  show(root);
}

void fmt_hm(char* out, size_t n, int64_t t) {
  time_t tt = static_cast<time_t>(t);
  struct tm lt;
  localtime_r(&tt, &lt);
  snprintf(out, n, "%02d:%02d", lt.tm_hour, lt.tm_min);
}

// Days from today (local): 0 today, 1 tomorrow, -1 yesterday.
int day_offset(int64_t t);

// A waste collection that is today and past the "Hinweis bis" time: no longer
// shown on the home screen (chip) nor as a reminder card.
bool waste_done_today(const CalEvent& e) {
  if (e.kind != 1 || day_offset(e.start) != 0) return false;
  time_t now = time(nullptr);
  struct tm lt;
  localtime_r(&now, &lt);
  return lt.tm_hour * 60 + lt.tm_min >= waste_until_min;
}

void fmt_waste_until(char* out, size_t n, uint16_t m) {
  if (m >= 1440) snprintf(out, n, "ganzer Tag");
  else snprintf(out, n, "%02u:%02u Uhr", m / 60, m % 60);
}

int day_offset(int64_t t) {
  time_t now = time(nullptr);
  struct tm a, b;
  localtime_r(&now, &a);
  time_t tt = static_cast<time_t>(t);
  localtime_r(&tt, &b);
  int da = static_cast<int>(days_from_civil_ui(a.tm_year + 1900, a.tm_mon + 1, a.tm_mday));
  int db = static_cast<int>(days_from_civil_ui(b.tm_year + 1900, b.tm_mon + 1, b.tm_mday));
  return db - da;
}

void fmt_day(char* out, size_t n, int64_t t) {
  int off = day_offset(t);
  if (off == 0) {
    snprintf(out, n, "Heute");
  } else if (off == 1) {
    snprintf(out, n, "Morgen");
  } else {
    time_t tt = static_cast<time_t>(t);
    struct tm lt;
    localtime_r(&tt, &lt);
    snprintf(out, n, "%s %d.%d.", kWeekdayShort[lt.tm_wday], lt.tm_mday, lt.tm_mon + 1);
  }
}

uint32_t level_color(uint8_t level) {
  switch (level) {
    case 2: return 0xFFE066;  // yellow
    case 3: return 0xFFA94D;  // orange
    case 4: return 0xFF6B6B;  // red
    default: return level >= 5 ? 0xD05CFF : kTextDim;
  }
}

// --- ÖV ---------------------------------------------------------------------

uint32_t line_color(const Departure& d) {
  if (d.bus) return 0xFFC857;
  if (d.line[0] == 'S' && d.line[1] >= '0' && d.line[1] <= '9') return 0x7CC4FF;
  if (strncmp(d.line, "IC", 2) == 0 || strncmp(d.line, "EC", 2) == 0 || strncmp(d.line, "RJ", 2) == 0) return 0xFF7A7A;
  if (strncmp(d.line, "IR", 2) == 0 || strncmp(d.line, "RE", 2) == 0) return 0xFFA94D;
  return 0xB4A8FF;
}

void tr_refresh_view() {
  if (tr_root == nullptr) return;
  const Board& b = boards[tr_idx];
  lv_label_set_text(tr_title, stations[tr_idx].name);
  lv_label_set_text_fmt(tr_page, "%d / %d", tr_idx + 1, kStationN);
  tr_idx == 0 ? hide(tr_prev) : show(tr_prev);
  tr_idx == kStationN - 1 ? hide(tr_next) : show(tr_next);
  const int64_t now = time(nullptr);
  int shown = 0;
  for (int i = 0; i < b.n && shown < kTrRows; ++i) {
    const Departure& d = b.d[i];
    int64_t real = d.dep + d.delay * 60;
    if (real < now - 30) continue;  // already gone
    int r = shown++;
    show(tr_row[r]);
    lv_label_set_text(tr_line[r], d.line);
    lv_obj_set_style_bg_color(tr_badge[r], lv_color_hex(line_color(d)), 0);
    lv_label_set_text(tr_to[r], d.to);
    int mins = static_cast<int>((real - now + 59) / 60);
    if (mins <= 0) lv_label_set_text(tr_min[r], "jetzt");
    else if (mins < 60) lv_label_set_text_fmt(tr_min[r], "%d'", mins);
    else {
      char hm[8];
      fmt_hm(hm, sizeof(hm), real);
      lv_label_set_text(tr_min[r], hm);
    }
    char hm[8];
    fmt_hm(hm, sizeof(hm), d.dep);
    char sub[48];
    int o = snprintf(sub, sizeof(sub), "%s", hm);
    if (d.delay > 0) o += snprintf(sub + o, sizeof(sub) - o, " #FF7A7A +%d#", d.delay);
    if (d.platform[0] != '\0') snprintf(sub + o, sizeof(sub) - o, "  %s %s", d.bus ? "Kante" : "Gl.", d.platform);
    lv_label_set_text(tr_sub[r], sub);
  }
  for (int r = shown; r < kTrRows; ++r) hide(tr_row[r]);
  if (!b.ok && b.fetched == 0) {
    lv_label_set_text(tr_status, tr_inflight ? "Lade Abfahrten ..." : "Keine Daten (opendata.ch nicht erreichbar)");
  } else if (shown == 0) {
    lv_label_set_text(tr_status, "Keine Abfahrten");
  } else {
    char hm[8];
    fmt_hm(hm, sizeof(hm), b.fetched);
    lv_label_set_text_fmt(tr_status, "Stand %s%s \xC2\xB7 transport.opendata.ch", hm, b.ok ? "" : " (veraltet)");
  }
}

void home_chips_refresh();

void tr_enter(int idx) {
  if (message_open || tr_root == nullptr) return;
  tr_idx = idx;
  enter_view(Nav::Transit, tr_root);
  const int64_t now = time(nullptr);
  if (boards[tr_idx].fetched == 0 || now - boards[tr_idx].fetched > 50) tr_next_ms[tr_idx] = now_ms();
  tr_refresh_view();
}

void tr_swipe(int dir) {
  int next = tr_idx + dir;
  if (next < 0 || next >= kStationN) return;
  tr_enter(next);
}

// --- Kalender view -------------------------------------------------------------

const char* waste_icon_color(const char* title, uint32_t* color) {
  if (strstr(title, "apier") != nullptr) *color = 0x7CC4FF;
  else if (strstr(title, "arton") != nullptr) *color = 0xD9A066;
  else if (strstr(title, "Gr\xC3\xBCn") != nullptr || strstr(title, "Garten") != nullptr) *color = 0x7EE0A1;
  else if (strstr(title, "etall") != nullptr) *color = 0xB8C2D6;
  else *color = 0xFFC857;
  return ICON_RECYCLE;
}

void cal_add_row(const CalEvent& e, int i) {
  lv_obj_t* row = lv_obj_create(calv_list);
  lv_obj_set_size(row, kScreen - 2 * kMargin, 52);
  style_glass(row, 16);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);
  uint32_t color = kAccent;
  const char* icon = e.kind == 1 ? waste_icon_color(e.title, &color) : ICON_EVENT;
  if (e.kind == 0) color = kPurple;
  lv_obj_t* ic = make_label(row, &font_icons_24, lv_color_hex(color));
  lv_label_set_text(ic, icon);
  lv_obj_align(ic, LV_ALIGN_LEFT_MID, 14, 0);
  char day[24], when[40];
  fmt_day(day, sizeof(day), e.start);
  if (e.all_day) snprintf(when, sizeof(when), "%s", day);
  else {
    char hm[8];
    fmt_hm(hm, sizeof(hm), e.start);
    snprintf(when, sizeof(when), "%s %s", day, hm);
  }
  lv_obj_t* w = make_label(row, &font_inter_14, lv_color_hex(day_offset(e.start) <= 1 ? kAmber : kTextDim));
  lv_label_set_text(w, when);
  lv_obj_set_width(w, 110);
  lv_label_set_long_mode(w, LV_LABEL_LONG_DOT);
  lv_obj_align(w, LV_ALIGN_LEFT_MID, 50, 0);
  lv_obj_t* t = make_label(row, &font_inter_sb16, lv_color_hex(kText));
  lv_label_set_text(t, e.title);
  lv_obj_set_width(t, 266);
  lv_obj_set_height(t, 20);
  lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
  lv_obj_align(t, LV_ALIGN_LEFT_MID, 164, 0);
  char key[12];
  snprintf(key, sizeof(key), "cal:%d", i);
  reg(key, row);
}

void calv_refresh() {
  if (calv_root == nullptr || cal == nullptr) return;
  unreg_prefix("cal:");
  lv_obj_clean(calv_list);
  // Waste: next three collections first, then family events (12 max).
  int k = 0;
  if (cal_have) {
    lv_obj_t* h1 = make_label(calv_list, &font_inter_14, lv_color_hex(kTextDim));
    lv_label_set_text(h1, "Abfuhr");
    for (int i = 0; i < cal->n_waste && i < 3; ++i) cal_add_row(cal->waste[i], k++);
    if (cal->n_waste == 0) {
      lv_obj_t* l = make_label(calv_list, &font_inter_14, lv_color_hex(kTextDim));
      lv_label_set_text(l, cal->ok_waste ? "Keine Abfuhr in den n\xC3\xA4" "chsten Wochen" : "Abfallkalender nicht erreichbar");
    }
    lv_obj_t* h2 = make_label(calv_list, &font_inter_14, lv_color_hex(kTextDim));
    lv_label_set_text(h2, "Termine");
    for (int i = 0; i < cal->n_events && i < 12; ++i) cal_add_row(cal->events[i], k++);
    if (cal->n_events == 0) {
      lv_obj_t* l = make_label(calv_list, &font_inter_14, lv_color_hex(kTextDim));
      lv_label_set_text(l, !cal->have_events_url ? "Kein Kalender (Einstellungen > Kalender)"
                           : cal->ok_events     ? "Keine Termine in den n\xC3\xA4" "chsten 60 Tagen"
                                                : "Kalender nicht erreichbar");
    }
    char hm[8];
    fmt_hm(hm, sizeof(hm), cal->fetched);
    lv_label_set_text_fmt(calv_status, "Stand %s", hm);
  } else {
    lv_label_set_text(calv_status, cal_inflight ? "Lade Kalender ..." : "Noch keine Daten");
  }
}

void calv_enter() {
  if (message_open || calv_root == nullptr) return;
  enter_view(Nav::Calendar, calv_root);
  calv_refresh();
}

// --- Regen / Warnungen (weather pager page before "Heute") ------------------

void rain_refresh() {
  if (rain_root == nullptr) return;
  float peak = 0.5f;
  for (int i = 0; i < kRainSteps; ++i) {
    if (meteo.rain_max[i] > peak) peak = meteo.rain_max[i];
    if (meteo.rain[i] > peak) peak = meteo.rain[i];
  }
  for (int i = 0; i < kRainSteps; ++i) {
    int h = meteo_have ? static_cast<int>(meteo.rain[i] / peak * kRainChartH + 0.5f) : 0;
    int hr = meteo_have ? static_cast<int>(meteo.rain_max[i] / peak * kRainChartH + 0.5f) : 0;
    if (h < 2) h = 2;
    if (hr < h) hr = h;
    lv_obj_set_height(rain_bar[i], h);
    lv_obj_set_y(rain_bar[i], 36 + kRainChartH - h);
    lv_obj_set_height(rain_range[i], hr);
    lv_obj_set_y(rain_range[i], 36 + kRainChartH - hr);
    lv_obj_set_style_bg_opa(rain_bar[i], meteo.rain[i] > 0.05f ? LV_OPA_COVER : LV_OPA_30, 0);
  }
  if (!meteo_have) {
    lv_label_set_text(rain_sum, meteo_inflight ? "Lade MeteoSchweiz ..." : "Keine Daten");
  } else {
    int first = -1;
    float max1h = 0;
    for (int i = 0; i < 6; ++i) {
      if (meteo.rain[i] > 0.05f && first < 0) first = i;
      if (meteo.rain[i] > max1h) max1h = meteo.rain[i];
    }
    if (first < 0) {
      lv_label_set_text(rain_sum, "Kein Regen in der n\xC3\xA4" "chsten Stunde");
    } else if (first == 0) {
      lv_label_set_text_fmt(rain_sum, "Regen jetzt, bis %.1f mm/h", static_cast<double>(max1h));
    } else {
      char hm[8];
      fmt_hm(hm, sizeof(hm), meteo.rain_start + first * 600);
      lv_label_set_text_fmt(rain_sum, "Regen ab %s, bis %.1f mm/h", hm, static_cast<double>(max1h));
    }
  }
  unreg_prefix("warn:");
  lv_obj_clean(rain_list);
  for (int i = 0; i < meteo.n; ++i) {
    const Warning& w = meteo.w[i];
    lv_obj_t* card = lv_obj_create(rain_list);
    lv_obj_set_size(card, kScreen - 2 * kMargin, LV_SIZE_CONTENT);
    style_glass(card, 16);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(level_color(w.level)), 0);
    // Cards must be hit-testable so a drag on them scrolls the list (LVGL 8
    // only hit-tests clickable objects); the scroll chains to rain_list.
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(card, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_t* ic = make_label(card, &font_icons_24, lv_color_hex(level_color(w.level)));
    lv_label_set_text(ic, ICON_WARNING);
    lv_obj_set_pos(ic, 0, 0);
    lv_obj_t* t = make_label(card, &font_inter_sb16, lv_color_hex(kText));
    char from[24] = "", to[24] = "";
    if (w.from > 0) {
      char d[16], hm[8];
      fmt_day(d, sizeof(d), w.from);
      fmt_hm(hm, sizeof(hm), w.from);
      snprintf(from, sizeof(from), "%s %s", d, hm);
    }
    if (w.to > 0) {
      char d[16], hm[8];
      fmt_day(d, sizeof(d), w.to);
      fmt_hm(hm, sizeof(hm), w.to);
      snprintf(to, sizeof(to), " - %s %s", d, hm);
    }
    lv_label_set_text_fmt(t, "%s \xC2\xB7 Stufe %d%s", warning_name(w.type), w.level, w.outlook ? " (Vorinfo)" : "");
    lv_obj_set_pos(t, 34, 2);
    lv_obj_t* when = make_label(card, &font_inter_14, lv_color_hex(kTextDim));
    lv_label_set_text_fmt(when, "%s%s", from, to);
    lv_obj_set_pos(when, 34, 24);
    lv_obj_t* body = make_label(card, &font_inter_14, lv_color_hex(kText));
    lv_obj_set_width(body, kScreen - 2 * kMargin - 24);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_label_set_text(body, w.text);
    lv_obj_set_pos(body, 0, 48);
    char key[12];
    snprintf(key, sizeof(key), "warn:%d", i);
    reg(key, card);
  }
  if (meteo_have && meteo.n == 0) {
    lv_obj_t* l = make_label(rain_list, &font_inter_14, lv_color_hex(kTextDim));
    lv_label_set_text(l, "Keine Warnungen");
  }
  if (meteo_have) {
    char hm[8];
    fmt_hm(hm, sizeof(hm), meteo.fetched);
    lv_label_set_text_fmt(rain_status, "Quelle: MeteoSchweiz \xC2\xB7 Stand %s", hm);
  } else {
    lv_label_set_text(rain_status, "Quelle: MeteoSchweiz");
  }
}

void rain_enter() {
  if (message_open || rain_root == nullptr) return;
  enter_view(Nav::Rain, rain_root);
  rain_refresh();
}

// --- Home: warning pill + ÖV / Kalender chips ----------------------------------

void home_warn_refresh() {
  if (home_warn == nullptr) return;
  if (!meteo_have || meteo.n == 0) {
    hide(home_warn);
    return;
  }
  const Warning& w = meteo.w[0];
  uint32_t c = level_color(w.level);
  lv_obj_set_style_border_color(home_warn, lv_color_hex(c), 0);
  lv_obj_set_style_text_color(home_warn_icon, lv_color_hex(c), 0);
  if (meteo.n > 1) lv_label_set_text_fmt(home_warn_txt, "%s %d  +%d", warning_name(w.type), w.level, meteo.n - 1);
  else lv_label_set_text_fmt(home_warn_txt, "%s \xC2\xB7 Stufe %d", warning_name(w.type), w.level);
  show(home_warn);
}

void energy_chip_refresh();
void energy_enter(int page);
void home_chips_refresh() {
  if (chip_tr_main == nullptr) return;
  energy_chip_refresh();
  // ÖV: next departure from station 1.
  const Board& b = boards[0];
  const int64_t now = time(nullptr);
  lv_label_set_text_fmt(chip_tr_top, "%s", stations[0].name);
  const Departure* next = nullptr;
  for (int i = 0; i < b.n; ++i) {
    if (b.d[i].dep + b.d[i].delay * 60 >= now) {
      next = &b.d[i];
      break;
    }
  }
  if (next != nullptr) {
    int mins = static_cast<int>((next->dep + next->delay * 60 - now + 59) / 60);
    char when[12];
    if (mins <= 0) snprintf(when, sizeof(when), "jetzt");
    else if (mins < 60) snprintf(when, sizeof(when), "%d'", mins);
    else fmt_hm(when, sizeof(when), next->dep);
    lv_label_set_text_fmt(chip_tr_main, "%s %s \xC2\xB7 %s", next->line, next->to, when);
    lv_label_set_text(chip_tr_icon, next->bus ? ICON_BUS : ICON_TRAIN);
  } else {
    lv_label_set_text(chip_tr_main, b.fetched == 0 ? "Abfahrten ..." : "Keine Abfahrt");
  }
  // Kalender: waste today/tomorrow wins ("Morgen Papier"), else the next event.
  if (cal == nullptr || !cal_have) {
    lv_label_set_text(chip_cal_top, "Kalender");
    lv_label_set_text(chip_cal_main, "...");
    return;
  }
  const CalEvent* waste = nullptr;
  for (int i = 0; i < cal->n_waste; ++i) {
    int off = day_offset(cal->waste[i].start);
    if (off >= 0 && off <= 1 && !waste_done_today(cal->waste[i])) {
      waste = &cal->waste[i];
      break;
    }
  }
  if (waste != nullptr) {
    uint32_t color;
    waste_icon_color(waste->title, &color);
    lv_label_set_text(chip_cal_icon, ICON_RECYCLE);
    lv_obj_set_style_text_color(chip_cal_icon, lv_color_hex(color), 0);
    lv_label_set_text_fmt(chip_cal_top, "Abfuhr");
    lv_label_set_text_fmt(chip_cal_main, "%s %s", day_offset(waste->start) == 0 ? "Heute" : "Morgen", waste->title);
    return;
  }
  lv_label_set_text(chip_cal_icon, ICON_EVENT);
  lv_obj_set_style_text_color(chip_cal_icon, lv_color_hex(kPurple), 0);
  if (cal->n_events > 0) {
    const CalEvent& e = cal->events[0];
    char day[24], hm[8];
    fmt_day(day, sizeof(day), e.start);
    fmt_hm(hm, sizeof(hm), e.start);
    if (e.all_day) lv_label_set_text(chip_cal_top, day);
    else lv_label_set_text_fmt(chip_cal_top, "%s %s", day, hm);
    lv_label_set_text(chip_cal_main, e.title);
  } else if (cal->n_waste > (waste_done_today(cal->waste[0]) ? 1 : 0)) {
    const CalEvent& w = cal->waste[waste_done_today(cal->waste[0]) ? 1 : 0];
    char day[24];
    fmt_day(day, sizeof(day), w.start);
    lv_label_set_text_fmt(chip_cal_top, "Abfuhr %s", day);
    lv_label_set_text(chip_cal_main, w.title);
  } else {
    lv_label_set_text(chip_cal_top, "Kalender");
    lv_label_set_text(chip_cal_main, "Keine Termine");
  }
}

void on_info_tap(lv_event_t* e) {
  if (gesture_consumed) return;
  int which = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  console_printf("Activate info %d\n", which);
  if (which == 0) tr_enter(0);
  else if (which == 1) calv_enter();
  else if (which == 3) energy_enter(0);
  else rain_enter();
}

lv_obj_t* make_chip(lv_obj_t* parent, int x, int which, const char* key, uint32_t color, const char* icon,
                    lv_obj_t** ic, lv_obj_t** top, lv_obj_t** main, int w = 136) {
  lv_obj_t* c = lv_btn_create(parent);
  lv_obj_set_pos(c, x, 380);
  lv_obj_set_size(c, w, 76);
  style_tile(c, color);
  lv_obj_add_event_cb(c, on_info_tap, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<intptr_t>(which)));
  watch(c);
  reg(key, c);
  *ic = make_label(c, strcmp(icon, ICON_BOLT) == 0 ? &font_icon_bolt_24 : &font_icons_24, lv_color_hex(color));
  lv_label_set_text(*ic, icon);
  lv_obj_set_pos(*ic, 12, 12);
  *top = make_label(c, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_set_pos(*top, 42, 15);
  lv_obj_set_width(*top, w - 52);
  lv_label_set_long_mode(*top, LV_LABEL_LONG_DOT);
  *main = make_label(c, &font_inter_sb16, lv_color_hex(kText));
  lv_obj_set_pos(*main, 12, 44);
  lv_obj_set_width(*main, w - 22);
  lv_obj_set_height(*main, 20);
  lv_label_set_long_mode(*main, LV_LABEL_LONG_DOT);
  return c;
}

void build_home_info(lv_obj_t* weather_card) {
  chip_tr_icon = nullptr;
  // Three tiles: ÖV, Kalender/Abfuhr, Energie (136 px each, 12 px gaps).
  make_chip(home_root, 24, 0, "home:oev", kAccent, ICON_TRAIN, &chip_tr_icon, &chip_tr_top, &chip_tr_main);
  make_chip(home_root, 172, 1, "home:cal", kPurple, ICON_EVENT, &chip_cal_icon, &chip_cal_top, &chip_cal_main);
  lv_obj_t* en_icon = nullptr;
  lv_obj_t* en_tile = make_chip(home_root, 320, 3, "home:energy", kAmber, ICON_BOLT, &en_icon, &chip_en_top, &chip_en_main);
  lv_obj_set_y(chip_en_main, 36);
  chip_en_sub = make_label(en_tile, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_set_pos(chip_en_sub, 12, 56);
  lv_obj_set_width(chip_en_sub, 136 - 22);
  lv_label_set_long_mode(chip_en_sub, LV_LABEL_LONG_DOT);

  home_warn = lv_btn_create(weather_card);
  lv_obj_set_size(home_warn, LV_SIZE_CONTENT, 34);
  style_glass(home_warn, 17);
  lv_obj_set_style_bg_color(home_warn, lv_color_hex(0x2A1F14), 0);
  lv_obj_set_style_bg_grad_color(home_warn, lv_color_hex(0x221A12), 0);
  lv_obj_set_style_pad_hor(home_warn, 10, 0);
  lv_obj_set_flex_flow(home_warn, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(home_warn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(home_warn, 6, 0);
  lv_obj_align(home_warn, LV_ALIGN_TOP_RIGHT, -14, 14);
  lv_obj_add_event_cb(home_warn, on_info_tap, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<intptr_t>(2)));
  watch(home_warn);
  reg("home:warn", home_warn);
  home_warn_icon = make_label(home_warn, &font_icons_16, lv_color_hex(kAmber));
  lv_label_set_text(home_warn_icon, ICON_WARNING);
  home_warn_txt = make_label(home_warn, &font_inter_14, lv_color_hex(kText));
  hide(home_warn);
  home_chips_refresh();
}

// --- Settings: ÖV ---------------------------------------------------------------

void trset_refresh() {
  for (int i = 0; i < kStationN; ++i) {
    if (trset_val[i] != nullptr) lv_label_set_text(trset_val[i], stations[i].name);
  }
  if (set_tr_val != nullptr) lv_label_set_text_fmt(set_tr_val, "%s \xC2\xB7 %s", stations[0].name, stations[1].name);
}

void open_trset() {
  nav = Nav::TransitSet;
  hide_settings();
  trset_refresh();
  show(trset_root);
}

void on_trset_row(lv_event_t* e) {
  if (gesture_consumed) return;
  st_target = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  console_printf("Activate station row %d\n", st_target);
  nav = Nav::StationSearch;
  hide_settings();
  lv_label_set_text_fmt(st_title, "Haltestelle %d", st_target + 1);
  st_hit_n = 0;
  unreg_prefix("st:");
  lv_obj_clean(st_list);
  lv_textarea_set_text(st_ta, "");
  lv_label_set_text(st_status, "Name der Haltestelle eingeben, dann OK");
  show(st_root);
  show_keyboard(st_ta);
}

void on_st_hit(lv_event_t* e) {
  if (gesture_consumed || settings_pending || st_busy) return;
  int i = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  if (i < 0 || i >= st_hit_n) return;
  SettingsRequest r = {};
  r.kind = SettingsRequest::SetStation;
  r.station_idx = st_target;
  snprintf(r.station_id, sizeof(r.station_id), "%s", st_hits[i].id);
  snprintf(r.station_name, sizeof(r.station_name), "%s", st_hits[i].name);
  st_busy = true;
  lv_label_set_text(st_status, "Speichere ...");
  queue_settings(r);
}

void fill_st_list() {
  unreg_prefix("st:");
  lv_obj_clean(st_list);
  for (int i = 0; i < st_hit_n; ++i) {
    lv_obj_t* row = make_row(st_list, 56, ICON_TRAIN, kAccent, on_st_hit, i);
    lv_obj_t* n = make_label(row, &font_inter_sb16, lv_color_hex(kText));
    lv_obj_set_width(n, 300);
    lv_label_set_long_mode(n, LV_LABEL_LONG_DOT);
    lv_label_set_text(n, st_hits[i].name);
    lv_obj_align(n, LV_ALIGN_LEFT_MID, 54, 0);
    lv_obj_t* id = make_label(row, &font_inter_14, lv_color_hex(kTextDim));
    lv_label_set_text(id, st_hits[i].id);
    lv_obj_align(id, LV_ALIGN_RIGHT_MID, -14, 0);
    char key[12];
    snprintf(key, sizeof(key), "st:%d", i);
    reg(key, row);
  }
}

bool st_kb_ready(lv_obj_t* ta) {
  if (ta != st_ta) return false;
  const char* q = lv_textarea_get_text(st_ta);
  if (q[0] == '\0' || settings_pending || st_busy) return true;
  SettingsRequest r = {};
  r.kind = SettingsRequest::StationSearch;
  snprintf(r.query, sizeof(r.query), "%s", q);
  st_busy = true;
  lv_label_set_text(st_status, "Suche ...");
  lv_keyboard_set_textarea(kb, nullptr);
  hide(kb);
  queue_settings(r);
  return true;
}

// --- Settings: generic text (calendar URLs, update URL) ---------------------------

const char* kTextTitle[kTextSettingN] = {"Familienkalender (iCal)", "Abfallkalender-API", "Update-URL",
                                         "MeteoSchweiz PLZ",        "Gemeinde (BFS-Nr.)", "Abfuhr-Zone",
                                         "whatwatt-Adresse",        "Einheitstarif (Rp./kWh)", "Hochtarif (Rp./kWh)",
                                         "Niedertarif (Rp./kWh)",   "Einspeiseverg\xC3\xBCtung (Rp./kWh)",
                                         "Wasserpreis (CHF/m\xC2\xB3)", "HT-Zeiten Mo-Fr", "HT/NT ab Datum",
                                         "PV-Gateway (IP)",         "PV-Gateway Port", "PV Modbus Unit-ID"};

// "https://calendar.google.com/…e4f825" style summary; secrets show host only.
void url_summary(char* out, size_t n, uint8_t which, const char* v, bool own) {
  if (v[0] == '\0') {
    snprintf(out, n, "nicht gesetzt");
    return;
  }
  const char* host = strstr(v, "://");
  host = host != nullptr ? host + 3 : v;
  const char* slash = strchr(host, '/');
  int hl = slash != nullptr ? static_cast<int>(slash - host) : static_cast<int>(strlen(host));
  if (which == kUrlIcal) {
    snprintf(out, n, "%.*s/**** (%s)", hl, host, own ? "eigene" : "Standard");
  } else {
    snprintf(out, n, "%s (%s)", host, own ? "eigene" : "Standard");
  }
}

void calset_refresh() {
  char v[256], sum[96];
  bool own = false;
  const uint8_t kRowText[4] = {kUrlIcal, kTextWasteBfs, kUrlWaste, kTextWasteZone};
  for (int i = 0; i < 4; ++i) {
    if (calset_val[i] == nullptr) continue;
    uint8_t w = kRowText[i];
    settings_load_text(static_cast<TextSetting>(w), v, sizeof(v), &own);
    if (w == kTextWasteBfs) {
      bool named = cal != nullptr && cal_have && cal->waste_name[0] != '\0';
      int h = cal != nullptr && cal_have && !cal->ok_waste ? cal->waste_http : 200;
      const char* err = h == 404 ? " - unbekannt" : h == 503 ? " - noch keine Daten" : h != 200 ? " - Fehler" : "";
      snprintf(sum, sizeof(sum), "%s%s%s%s%s", named ? cal->waste_name : "", named ? " \xC2\xB7 " : "", v,
               own ? "" : " (Standard)", err);
    } else if (w == kTextWasteZone) {
      int z = cal != nullptr && cal_have ? cal->waste_zones : 0;
      if (v[0] != '\0')
        snprintf(sum, sizeof(sum), "%s%s", v, z > 0 && !cal->waste_zone_used ? " (nicht gefunden)" : "");
      else
        snprintf(sum, sizeof(sum), z > 0 ? "alle (%d Zonen, bitte w\xC3\xA4hlen)" : "ganze Gemeinde", z);
    } else {
      url_summary(sum, sizeof(sum), w, v, own);
    }
    lv_label_set_text(calset_val[i], sum);
  }
  memset(v, 0, sizeof(v));
  if (calset_until_val != nullptr) {
    char t[24];
    fmt_waste_until(t, sizeof(t), waste_until_min);
    lv_label_set_text(calset_until_val, t);
  }
  if (calset_from_val != nullptr) {
    char t[24];
    fmt_waste_until(t, sizeof(t), waste_from_min);
    lv_label_set_text(calset_from_val, t);
  }
  if (set_cal_val != nullptr) {
    if (cal != nullptr && cal_have)
      lv_label_set_text_fmt(set_cal_val, "%d Termine \xC2\xB7 %d Abfuhrdaten", cal->n_events, cal->n_waste);
    else
      lv_label_set_text(set_cal_val, "Familienkalender und Abfuhr");
  }
}

void open_calset() {
  nav = Nav::CalSet;
  hide_settings();
  calset_refresh();
  show(calset_root);
}

void open_text(uint8_t which, Nav back) {
  nav = Nav::TextEdit;
  txt_which = which;
  txt_back = back;
  hide_settings();
  lv_label_set_text(txt_title, kTextTitle[which]);
  char v[256], sum[96];
  bool own = false;
  settings_load_text(static_cast<TextSetting>(which), v, sizeof(v), &own);
  if (which == kUrlIcal) url_summary(sum, sizeof(sum), which, v, own);
  else if (which == kTextWasteZone && v[0] == '\0') snprintf(sum, sizeof(sum), "keine (ganze Gemeinde)");
  else snprintf(sum, sizeof(sum), "%s%s", v[0] != '\0' ? v : "nicht gesetzt", own ? "" : " (Standard)");
  memset(v, 0, sizeof(v));
  lv_label_set_text(txt_cur, sum);
  lv_textarea_set_text(txt_ta, "");
  lv_textarea_set_password_mode(txt_ta, which == kUrlIcal);
  lv_label_set_text(txt_status, which == kUrlIcal ? "Neue geheime iCal-Adresse eingeben, dann OK"
                                : which == kUrlWaste ? "Basis-Adresse der API (https://...), dann OK"
                                : which == kTextWasteBfs ? "BFS-Nummer der Gemeinde (Buchs SG = 3271), dann OK"
                                : which == kTextWasteZone ? "Zone/Tour genau wie in der API, dann OK"
                                : which == kTextEnHost ? "IP oder Name, ohne http:// (leer = keine), dann OK"
                                : which == kTextEnHtWin ? "z.B. 06:00-10:00,17:00-21:00 (leer = Standard)"
                                : which == kTextEnSwitch ? "Datum JJJJ-MM-TT (leer = 2027-01-01), dann OK"
                                : which == kTextPvGw ? "IPv4 des Modbus-TCP-Gateways (nur lesen), dann OK"
                                : which == kTextPvGwPort ? "Modbus-TCP-Port, Standard 502 (leer = Standard)"
                                : which == kTextPvUnit ? "Unit-ID des Wechselrichters, Standard 1 (leer = Standard)"
                                : which >= kTextEnFlat ? "Preis z.B. 27.27 (leer = Standard), dann OK"
                                                     : "Neue Adresse eingeben, dann OK");
  show(txt_root);
}

void on_calset_row(lv_event_t* e) {
  if (gesture_consumed) return;
  int which = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  console_printf("Activate calset %d\n", which);
  open_text(static_cast<uint8_t>(which), Nav::CalSet);
}

// "Hinweis am Abfuhrtag bis": each tap steps to the next preset.
void on_waste_from(lv_event_t*) {
  if (gesture_consumed) return;
  constexpr int n = sizeof(kWasteFromPresets) / sizeof(kWasteFromPresets[0]);
  int i = 0;
  while (i < n && kWasteFromPresets[i] <= waste_from_min) ++i;
  ui_set_waste_from(kWasteFromPresets[i < n ? i : 0]);
}

void on_waste_until(lv_event_t*) {
  if (gesture_consumed) return;
  size_t n = sizeof(kWasteUntilPresets) / sizeof(kWasteUntilPresets[0]);
  size_t i = 0;
  while (i < n && kWasteUntilPresets[i] <= waste_until_min) ++i;
  ui_set_waste_until(kWasteUntilPresets[i < n ? i : 0]);
}

void queue_text(const char* value) {
  SettingsRequest r = {};
  r.kind = SettingsRequest::SetText;
  r.text_which = txt_which;
  snprintf(r.text, sizeof(r.text), "%s", value);
  txt_busy = true;
  lv_label_set_text(txt_status, "Speichere ...");
  queue_settings(r);
}

void on_txt_default(lv_event_t*) {
  if (gesture_consumed || settings_pending || txt_busy) return;
  queue_text("");
}

bool txt_kb_ready(lv_obj_t* ta) {
  if (ta != txt_ta) return false;
  const char* v = lv_textarea_get_text(txt_ta);
  if (settings_pending || txt_busy) return true;
  if ((txt_which == kUrlOta || txt_which == kUrlWaste) && strncmp(v, "https://", 8) != 0) {
    lv_label_set_text(txt_status, "Adresse muss mit https:// beginnen");
    return true;
  }
  if (txt_which == kTextWasteBfs) {
    bool ok = strlen(v) == 4;
    for (const char* c = v; *c != '\0'; ++c) ok = ok && *c >= '0' && *c <= '9';
    if (!ok) {
      lv_label_set_text(txt_status, "BFS-Nummer: 4 Ziffern, z.B. 3271");
      return true;
    }
  }
  if (txt_which >= kTextEnHost && v[0] != '\0') {
    uint16_t p, w[2][2];
    int32_t ymd;
    bool ok = true;
    const char* msg = "";
    if (txt_which == kTextPvGw) {
      int a, b2, c2, d, n2 = 0;
      ok = sscanf(v, "%d.%d.%d.%d%n", &a, &b2, &c2, &d, &n2) == 4 && v[n2] == '\0' && a >= 0 && a < 256 &&
           b2 >= 0 && b2 < 256 && c2 >= 0 && c2 < 256 && d > 0 && d < 255;
      msg = "IPv4-Adresse, z.B. 192.168.1.244";
    } else if (txt_which == kTextPvUnit) {
      int u = 0, n2 = 0;
      ok = sscanf(v, "%d%n", &u, &n2) == 1 && v[n2] == '\0' && u >= 1 && u <= 247;
      msg = "Unit-ID 1-247";
    } else if (txt_which == kTextPvGwPort) {
      int port = 0, n2 = 0;
      ok = sscanf(v, "%d%n", &port, &n2) == 1 && v[n2] == '\0' && port > 0 && port < 65536;
      msg = "Port 1-65535";
    } else if (txt_which == kTextEnHost) {
      for (const char* c = v; *c; ++c) ok = ok && (isalnum(static_cast<unsigned char>(*c)) || *c == '.' || *c == '-');
      msg = "Nur IP-Adresse oder Name, ohne http://";
    } else if (txt_which == kTextEnHtWin) {
      ok = energy_parse_windows(v, w);
      msg = "Format: 06:00-10:00,17:00-21:00";
    } else if (txt_which == kTextEnSwitch) {
      ok = energy_parse_date(v, &ymd);
      msg = "Format: 2027-01-01";
    } else {
      ok = energy_parse_price(v, &p);
      msg = "Preis als Zahl, z.B. 27.27";
    }
    if (!ok) {
      lv_label_set_text(txt_status, msg);
      return true;
    }
  }
  if (txt_which == kUrlIcal && strncmp(v, "http://", 7) != 0 && strncmp(v, "https://", 8) != 0) {
    lv_label_set_text(txt_status, "Adresse muss mit http:// oder https:// beginnen");
    return true;
  }
  queue_text(v);
  lv_textarea_set_text(txt_ta, "");  // do not keep a private URL in the widget
  lv_keyboard_set_textarea(kb, nullptr);
  hide(kb);
  return true;
}

// --- Settings: Wartung ---------------------------------------------------------

void maint_refresh() {
  if (maint_root == nullptr) return;
  MaintStatus s;
  maint_collect(&s);
  char b[64];
  if (s.wifi) snprintf(b, sizeof(b), "%s \xC2\xB7 %d dBm%s", s.ssid, s.rssi, s.rssi < -80 ? " (schwach)" : "");
  else snprintf(b, sizeof(b), "nicht verbunden");
  lv_label_set_text(maint_val[0], b);
  lv_label_set_text(maint_val[1], s.ip[0] != '\0' ? s.ip : "-");
  unsigned d = s.uptime_s / 86400, h = (s.uptime_s / 3600) % 24, m = (s.uptime_s / 60) % 60;
  if (d > 0) snprintf(b, sizeof(b), "%u d %u h %u min", d, h, m);
  else snprintf(b, sizeof(b), "%u h %u min %u s", h, m, static_cast<unsigned>(s.uptime_s % 60));
  lv_label_set_text(maint_val[2], b);
  snprintf(b, sizeof(b), "%u KB (Block %u KB)", static_cast<unsigned>(s.free_internal / 1024),
           static_cast<unsigned>(s.largest_internal / 1024));
  lv_label_set_text(maint_val[3], b);
  snprintf(b, sizeof(b), "%u KB", static_cast<unsigned>(s.free_psram / 1024));
  lv_label_set_text(maint_val[4], b);
  snprintf(b, sizeof(b), "%s \xC2\xB7 %s", s.version, s.partition);
  lv_label_set_text(maint_val[5], b);
  if (!s.ntp_synced) snprintf(b, sizeof(b), "nicht synchronisiert");
  else snprintf(b, sizeof(b), "synchronisiert (vor %u min)", static_cast<unsigned>(s.ntp_age_s / 60));
  lv_label_set_text(maint_val[6], b);
  if (s.web) lv_obj_add_state(maint_web_sw, LV_STATE_CHECKED);
  else lv_obj_clear_state(maint_web_sw, LV_STATE_CHECKED);
  if (s.web) lv_label_set_text_fmt(maint_web_lbl, "http://%s/  PIN %s", s.ip, maint_web_pin());
  else lv_label_set_text(maint_web_lbl, "Backup, Restore und Upload im Browser");
  int pct = maint_ota_progress();
  if (ota_state == OtaUi::Checking && now_ms() - ota_t0 > 45000) {  // e.g. offline: never sent
    ota_check_req = false;
    ota_state = OtaUi::Error;
    snprintf(ota_msg, sizeof(ota_msg), "Keine Antwort - WLAN verbunden?");
  }
  const char* run = ota_running_version();
  char title[64];
  char sub[128];
  if (maint_restart_pending() || (ota_state == OtaUi::Installing && !ota_running && !ota_install_req && pct < 0)) {
    snprintf(title, sizeof(title), "Installiert");
    snprintf(sub, sizeof(sub), "Neustart ...");
  } else if (ota_state == OtaUi::Installing || pct >= 0) {
    snprintf(title, sizeof(title), "Installiere v%s", ota_new_ver[0] ? ota_new_ver : "?");
    snprintf(sub, sizeof(sub), "Update l\xC3\xA4uft ... %d %%", pct < 0 ? 0 : pct);
  } else if (ota_state == OtaUi::Checking) {
    snprintf(title, sizeof(title), "Pr\xC3\xBC" "fe ...");
    snprintf(sub, sizeof(sub), "Installiert: v%s", run);
  } else if (ota_state == OtaUi::Newer) {
    snprintf(title, sizeof(title), "Update auf v%s installieren", ota_new_ver);
    if (ota_notes[0] != '\0') snprintf(sub, sizeof(sub), "Neu: %s", ota_notes);
    else snprintf(sub, sizeof(sub), "Installiert: v%s - tippen zum Installieren", run);
  } else if (ota_state == OtaUi::Current) {
    snprintf(title, sizeof(title), "Aktuell (v%s)", run);
    snprintf(sub, sizeof(sub), "%s", ota_msg[0] ? ota_msg : "Tippen, um erneut zu pr\xC3\xBC" "fen");
  } else if (ota_state == OtaUi::Error) {
    snprintf(title, sizeof(title), "Firmware pr\xC3\xBC" "fen");
    snprintf(sub, sizeof(sub), "%s", ota_msg);
  } else {
    snprintf(title, sizeof(title), "Firmware pr\xC3\xBC" "fen");
    snprintf(sub, sizeof(sub), "Installiert: v%s", run);
  }
  lv_label_set_text(maint_ota_title, title);
  lv_label_set_text(maint_ota_lbl, sub);
  char v[256];
  bool own = false;
  settings_load_text(kUrlOta, v, sizeof(v), &own);
  const char* tail = strrchr(v, '/');
  lv_label_set_text_fmt(maint_url_val, "%s%s", v[0] != '\0' ? v : "nicht gesetzt", own ? "" : " (Standard)");
  (void)tail;
  if (set_maint_val != nullptr) lv_label_set_text_fmt(set_maint_val, "%s \xC2\xB7 %s", s.version, s.ip[0] ? s.ip : "-");
}

void open_maint() {
  nav = Nav::Maint;
  // A finished check is not kept: back on this screen it starts from
  // "Firmware prüfen" again (the URL may have changed meanwhile).
  if (!ota_running && !ota_check_req && !ota_install_req && ota_state != OtaUi::Installing) {
    ota_state = OtaUi::Idle;
    ota_msg[0] = '\0';
  }
  hide_settings();
  maint_refresh();
  show(maint_root);
}

void on_maint_btn(lv_event_t* e) {
  if (gesture_consumed) return;
  int which = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  console_printf("Activate maint %d\n", which);
  if (which == 0) {
    if (maint_web_running()) maint_web_stop();
    else if (!maint_web_start()) snprintf(ota_msg, sizeof(ota_msg), "Webserver konnte nicht starten");
  } else if (which == 1) {
    open_text(kUrlOta, Nav::Maint);
    return;
  } else if (which == 2) {
    if (ota_running || ota_check_req || ota_install_req || maint_restart_pending()) {
      // busy
    } else if (ota_state == OtaUi::Newer) {
      ota_install_req = true;  // only offered after a check found a newer version
      ota_state = OtaUi::Installing;
    } else {
      ota_check_req = true;
      ota_state = OtaUi::Checking;
      ota_t0 = now_ms();
      ota_msg[0] = '\0';
    }
  }
  maint_refresh();
}

// --- builders -------------------------------------------------------------------

lv_obj_t* make_value_row(lv_obj_t* parent, int y, const char* name, const char* icon, uint32_t color, lv_event_cb_t cb,
                         int user, const char* key, lv_obj_t** val) {
  lv_obj_t* row = make_row(parent, 72, icon, color, cb, user);
  lv_obj_set_pos(row, kMargin, y);
  reg(key, row);
  lv_obj_t* n = make_label(row, &font_inter_sb16, lv_color_hex(kText));
  lv_label_set_text(n, name);
  lv_obj_align(n, LV_ALIGN_TOP_LEFT, icon != nullptr ? 56 : 16, 13);
  *val = make_label(row, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_set_width(*val, 330);
  lv_label_set_long_mode(*val, LV_LABEL_LONG_DOT);
  lv_obj_align(*val, LV_ALIGN_BOTTOM_LEFT, icon != nullptr ? 56 : 16, -13);
  return row;
}

void build_info_screens(lv_obj_t* screen) {
  settings_load_stations(stations);
  cal = static_cast<Calendar*>(heap_caps_calloc(1, sizeof(Calendar), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));

  // ÖV board
  tr_root = make_screen_root(screen, "");
  tr_title = lv_obj_get_child(tr_root, 0);
  header_title_at(tr_title, 56, 20, 320);
  lv_label_set_long_mode(tr_title, LV_LABEL_LONG_DOT);
  lv_obj_t* tic = make_label(tr_root, &font_icons_24, lv_color_hex(kAccent));
  lv_label_set_text(tic, ICON_TRAIN);
  lv_obj_set_pos(tic, 22, 19);
  tr_page = make_label(tr_root, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_align(tr_page, LV_ALIGN_TOP_RIGHT, -24, 25);
  header_note(tr_page);
  tr_status = make_label(tr_root, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_set_pos(tr_status, 24, 50);
  lv_obj_set_width(tr_status, 432);
  lv_label_set_long_mode(tr_status, LV_LABEL_LONG_DOT);
  for (int r = 0; r < kTrRows; ++r) {
    lv_obj_t* row = lv_obj_create(tr_root);
    lv_obj_set_pos(row, kMargin, 76 + r * 54);
    lv_obj_set_size(row, kScreen - 2 * kMargin, 48);
    style_glass(row, 14);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);
    tr_row[r] = row;
    lv_obj_t* badge = lv_obj_create(row);
    lv_obj_set_size(badge, 58, 28);
    lv_obj_align(badge, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_set_style_radius(badge, 8, 0);
    lv_obj_set_style_border_width(badge, 0, 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(badge, 0, 0);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
    tr_badge[r] = badge;
    tr_line[r] = make_label(badge, &font_inter_sb16, lv_color_hex(0x0B0F1A));
    lv_obj_center(tr_line[r]);
    tr_to[r] = make_label(row, &font_inter_sb16, lv_color_hex(kText));
    lv_obj_set_pos(tr_to[r], 80, 4);
    lv_obj_set_width(tr_to[r], 270);
    lv_obj_set_height(tr_to[r], 20);
    lv_label_set_long_mode(tr_to[r], LV_LABEL_LONG_DOT);
    tr_sub[r] = make_label(row, &font_inter_14, lv_color_hex(kTextDim));
    lv_label_set_recolor(tr_sub[r], true);
    lv_obj_set_pos(tr_sub[r], 80, 25);
    tr_min[r] = make_label(row, &font_inter_20, lv_color_hex(kAmber));
    lv_obj_align(tr_min[r], LV_ALIGN_RIGHT_MID, -14, 0);
    hide(row);
  }
  tr_prev = wx_handle(tr_root, 5);
  tr_next = wx_handle(tr_root, 470);

  // Kalender view
  calv_root = make_screen_root(screen, "Kalender");
  header_title_at(lv_obj_get_child(calv_root, 0), 56, 20, 0);
  lv_obj_t* cic = make_label(calv_root, &font_icons_24, lv_color_hex(kPurple));
  lv_label_set_text(cic, ICON_CALENDAR);
  lv_obj_set_pos(cic, 22, 19);
  calv_status = make_label(calv_root, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_align(calv_status, LV_ALIGN_TOP_RIGHT, -24, 25);
  header_note(calv_status);
  calv_list = make_list(calv_root, 58, 400);
  lv_obj_set_style_pad_row(calv_list, 6, 0);

  // Regen + Warnungen
  rain_root = make_screen_root(screen, "N\xC3\xA4" "chste 2 Stunden");
  lv_obj_t* chart = lv_obj_create(rain_root);
  lv_obj_set_pos(chart, kMargin, 52);
  lv_obj_set_size(chart, kScreen - 2 * kMargin, 196);
  style_glass(chart, 20);
  lv_obj_clear_flag(chart, LV_OBJ_FLAG_CLICKABLE);
  rain_sum = make_label(chart, &font_inter_sb16, lv_color_hex(kText));
  lv_obj_set_pos(rain_sum, 16, 10);
  constexpr int kBarW = 26, kBarGap = 7, kX0 = 18;
  for (int i = 0; i < kRainSteps; ++i) {
    int x = kX0 + i * (kBarW + kBarGap);
    rain_range[i] = make_bar(chart, x, 36, kBarW, 2, kAccent, LV_OPA_20);
    rain_bar[i] = make_bar(chart, x, 36, kBarW, 2, kAccent, LV_OPA_COVER);
  }
  make_divider(chart, 14, 36 + kRainChartH + 2, kScreen - 2 * kMargin - 28, 1);
  static const char* kTick[5] = {"jetzt", "+30'", "+60'", "+90'", "+120'"};
  for (int k = 0; k < 5; ++k) {
    lv_obj_t* l = make_label(chart, &font_inter_14, lv_color_hex(kTextDim));
    lv_label_set_text(l, kTick[k]);
    int x = kX0 + k * 3 * (kBarW + kBarGap) - (k == 4 ? 30 : 0);
    lv_obj_set_pos(l, x, 36 + kRainChartH + 8);
  }
  lv_obj_t* unit = make_label(chart, &font_inter_14, lv_color_hex(kTextDim));
  lv_label_set_text(unit, "mm/h");
  lv_obj_align(unit, LV_ALIGN_TOP_RIGHT, -16, 12);
  rain_list = make_list(rain_root, 258, 172);
  lv_obj_add_flag(rain_list, LV_OBJ_FLAG_CLICKABLE);  // drags between cards scroll too
  rain_status = make_label(rain_root, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_set_pos(rain_status, 24, 434);
  lv_obj_t* rn = wx_handle(rain_root, 470);
  (void)rn;

  // Einstellungen > ÖV
  trset_root = make_screen_root(screen, "\xC3\x96V-Haltestellen");
  for (int i = 0; i < kStationN; ++i) {
    char key[12], name[16];
    snprintf(key, sizeof(key), "trset:%d", i);
    snprintf(name, sizeof(name), "Haltestelle %d", i + 1);
    make_value_row(trset_root, 60 + i * 80, name, ICON_TRAIN, kAccent, on_trset_row, i, key, &trset_val[i]);
  }
  lv_obj_t* trnote = make_label(trset_root, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_set_pos(trnote, 24, 228);
  lv_obj_set_width(trnote, 432);
  lv_label_set_long_mode(trnote, LV_LABEL_LONG_WRAP);
  lv_label_set_text(trnote, "Fahrplandaten: transport.opendata.ch (Open Data \xC3\x96V Schweiz).");

  st_root = make_screen_root(screen, "Haltestelle");
  st_title = lv_obj_get_child(st_root, 0);
  st_ta = make_ta(st_root, 56, "Haltestelle suchen");
  reg("st:ta", st_ta);
  st_status = make_label(st_root, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_set_pos(st_status, 24, 116);
  lv_obj_set_width(st_status, 432);
  lv_label_set_long_mode(st_status, LV_LABEL_LONG_DOT);
  st_list = make_list(st_root, 140, 316);

  // Einstellungen > Kalender
  calset_root = make_screen_root(screen, "Kalender");
  make_value_row(calset_root, 60, "Familienkalender", ICON_EVENT, kPurple, on_calset_row, 0, "calset:ical", &calset_val[0]);
  make_value_row(calset_root, 140, "Abfall: Am Vortag ab", ICON_SCHEDULE, kAmber, on_waste_from, 0, "calset:from",
                 &calset_from_val);
  make_value_row(calset_root, 220, "Abfall: Am Sammeltag bis", ICON_SCHEDULE, kAmber, on_waste_until, 0, "calset:until",
                 &calset_until_val);
  make_value_row(calset_root, 300, "Abfall: Gemeinde", ICON_RECYCLE, kGreen, on_calset_row, kTextWasteBfs, "calset:bfs",
                 &calset_val[1]);
  make_value_row(calset_root, 380, "Abfall: Zone", ICON_RECYCLE, kGreen, on_calset_row, kTextWasteZone, "calset:zone",
                 &calset_val[3]);
  make_value_row(calset_root, 460, "Abfallkalender-API", ICON_RECYCLE, kGreen, on_calset_row, kUrlWaste, "calset:waste",
                 &calset_val[2]);
  make_page_scrollable(calset_root);  // the API row ends at 532: was unreachable before 0.9.0

  txt_root = make_screen_root(screen, "");
  txt_title = lv_obj_get_child(txt_root, 0);
  make_caption(txt_root, 58, "Aktuell");
  txt_cur = make_label(txt_root, &font_inter_sb16, lv_color_hex(kText));
  lv_obj_set_pos(txt_cur, 24, 80);
  lv_obj_set_width(txt_cur, 432);
  lv_label_set_long_mode(txt_cur, LV_LABEL_LONG_DOT);
  txt_ta = make_ta(txt_root, 112, "Neue Adresse");
  lv_textarea_set_password_bullet(txt_ta, "*");
  lv_textarea_set_max_length(txt_ta, 255);
  reg("text:ta", txt_ta);
  make_choice(txt_root, kMargin, 178, kScreen - 2 * kMargin, 48, "Standard verwenden", on_txt_default, 0, "text:default");
  txt_status = make_label(txt_root, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_set_pos(txt_status, 24, 240);
  lv_obj_set_width(txt_status, 432);
  lv_label_set_long_mode(txt_status, LV_LABEL_LONG_WRAP);

  // Einstellungen > Wartung
  maint_root = make_screen_root(screen, "Wartung");
  static const char* kName[7] = {"WLAN", "IP", "Laufzeit", "RAM intern", "PSRAM frei", "Firmware", "NTP"};
  lv_obj_t* box = lv_obj_create(maint_root);
  lv_obj_set_pos(box, kMargin, 52);
  lv_obj_set_size(box, kScreen - 2 * kMargin, 192);
  style_glass(box, 18);
  lv_obj_clear_flag(box, LV_OBJ_FLAG_CLICKABLE);
  for (int i = 0; i < 7; ++i) {
    lv_obj_t* k = make_label(box, &font_inter_14, lv_color_hex(kTextDim));
    lv_label_set_text(k, kName[i]);
    lv_obj_set_pos(k, 16, 10 + i * 25);
    maint_val[i] = make_label(box, &font_inter_14, lv_color_hex(kText));
    lv_obj_set_pos(maint_val[i], 124, 10 + i * 25);
    lv_obj_set_width(maint_val[i], 300);
    lv_label_set_long_mode(maint_val[i], LV_LABEL_LONG_DOT);
  }
  lv_obj_t* web = make_value_row(maint_root, 254, "Web-Wartung", ICON_ROUTER, kTeal, on_maint_btn, 0, "maint:web",
                                 &maint_web_lbl);
  maint_web_sw = lv_switch_create(web);
  lv_obj_set_size(maint_web_sw, 60, 32);
  lv_obj_align(maint_web_sw, LV_ALIGN_RIGHT_MID, -14, 0);
  lv_obj_clear_flag(maint_web_sw, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(maint_web_sw, lv_color_hex(kBorder), static_cast<lv_style_selector_t>(LV_PART_MAIN));
  lv_obj_set_style_bg_color(maint_web_sw, lv_color_hex(kAccent),
                            static_cast<lv_style_selector_t>(LV_PART_INDICATOR) | LV_STATE_CHECKED);
  lv_obj_set_width(maint_web_lbl, 290);
  lv_obj_t* upd = make_row(maint_root, 72, ICON_UPDATE, kGreen, on_maint_btn, 2);
  lv_obj_set_pos(upd, kMargin, 334);
  reg("maint:ota", upd);
  maint_ota_title = make_label(upd, &font_inter_sb16, lv_color_hex(kText));
  lv_obj_set_width(maint_ota_title, 350);
  lv_label_set_long_mode(maint_ota_title, LV_LABEL_LONG_DOT);
  lv_label_set_text(maint_ota_title, "Firmware pr\xC3\xBC" "fen");
  lv_obj_align(maint_ota_title, LV_ALIGN_TOP_LEFT, 56, 13);
  maint_ota_lbl = make_label(upd, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_set_width(maint_ota_lbl, 350);
  lv_label_set_long_mode(maint_ota_lbl, LV_LABEL_LONG_DOT);
  lv_obj_align(maint_ota_lbl, LV_ALIGN_BOTTOM_LEFT, 56, -13);
  // Advanced: own update URL (checked the same way, by the image version).
  lv_obj_t* url = make_row(maint_root, 50, ICON_PUBLIC, kAccent, on_maint_btn, 1);
  lv_obj_set_pos(url, kMargin, 414);
  reg("maint:url", url);
  lv_obj_t* uln = make_label(url, &font_inter_14, lv_color_hex(kText));
  lv_label_set_text(uln, "Update-URL");
  lv_obj_align(uln, LV_ALIGN_LEFT_MID, 56, 0);
  maint_url_val = make_label(url, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_set_width(maint_url_val, 250);
  lv_label_set_long_mode(maint_url_val, LV_LABEL_LONG_DOT);
  lv_obj_align(maint_url_val, LV_ALIGN_RIGHT_MID, -16, 0);
  build_web_confirm();
}

void on_web_confirm(lv_event_t* e) {
  bool allow = lv_event_get_user_data(e) != nullptr;
  maint_secrets_answer(allow);
  hide(web_confirm);
}

void build_web_confirm() {
  web_confirm = lv_obj_create(lv_obj_get_screen(maint_root));  // main screen (also in screenshots)
  lv_obj_set_size(web_confirm, kScreen - 2 * kMargin, 250);
  lv_obj_center(web_confirm);
  style_glass(web_confirm, 24);
  lv_obj_set_style_border_color(web_confirm, lv_color_hex(kAmber), 0);
  lv_obj_set_style_border_width(web_confirm, 2, 0);
  lv_obj_set_style_pad_all(web_confirm, 20, 0);
  lv_obj_clear_flag(web_confirm, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* t = make_label(web_confirm, &font_inter_sb16, lv_color_hex(kText));
  lv_label_set_text(t, "Backup mit Geheimnissen?");
  lv_obj_set_pos(t, 0, 0);
  lv_obj_t* d = make_label(web_confirm, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_set_width(d, kScreen - 2 * kMargin - 40);
  lv_label_set_long_mode(d, LV_LABEL_LONG_WRAP);
  lv_label_set_text(d, "Ein Browser m\xC3\xB6" "chte WLAN-Passwort, Loxone-Passwort, API-Key und Kalender-URL "
                       "herunterladen. Nur erlauben, wenn du das selbst angefordert hast.");
  lv_obj_set_pos(d, 0, 30);
  const char* names[2] = {"Ablehnen", "Erlauben"};
  const char* keys[2] = {"web:deny", "web:allow"};
  for (int i = 0; i < 2; ++i) {
    lv_obj_t* b = lv_btn_create(web_confirm);
    lv_obj_set_size(b, 170, 52);
    lv_obj_align(b, i == 0 ? LV_ALIGN_BOTTOM_LEFT : LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_radius(b, 16, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(i == 0 ? kBorder : kAmber), 0);
    lv_obj_add_event_cb(b, on_web_confirm, LV_EVENT_CLICKED, i == 0 ? nullptr : reinterpret_cast<void*>(1));
    lv_obj_t* l = make_label(b, &font_inter_sb16, lv_color_hex(i == 0 ? kText : 0x1A1300));
    lv_label_set_text(l, names[i]);
    lv_obj_center(l);
    reg(keys[i], b);
  }
  hide(web_confirm);
}

// Once a second (clock_tick).
void info_tick() {
  play_tick();
  static uint32_t last = 0;
  disp_save_flush(false);
  if (maint_web_tick() && nav == Nav::Maint) maint_refresh();
  if (web_confirm != nullptr) {
    bool want = maint_secrets_requested();
    bool shown = !lv_obj_has_flag(web_confirm, LV_OBJ_FLAG_HIDDEN);
    if (want && !shown) {
      if (scr_off) screen_on();
      show(web_confirm);
      lv_obj_move_foreground(web_confirm);
    } else if (!want && shown) {
      hide(web_confirm);
    }
  }
  rem_tick(false);
  if (scr_off) return;
  if (nav == Nav::Maint) maint_refresh();
  co4_tick();
  if (nav == Nav::Energy) energy_refresh();
  if (nav == Nav::EnSet && lv_tick_get() % 5000 < 1000) enset_refresh();
  if (nav == Nav::Home) energy_chip_refresh();
  if (now_ms() - last < 20000) return;  // relative times ("5'")
  last = now_ms();
  if (nav == Nav::Home) home_chips_refresh();
  if (nav == Nav::Transit) tr_refresh_view();
}

bool info_describe(char* out, size_t n) {
  switch (nav) {
    case Nav::Transit:
      snprintf(out, n, "transit %d/%d n=%d ok=%d loading=%d", tr_idx + 1, kStationN, boards[tr_idx].n,
               boards[tr_idx].ok ? 1 : 0, tr_inflight ? 1 : 0);
      return true;
    case Nav::Calendar:
      snprintf(out, n, "calendar events=%d waste=%d have=%d loading=%d", cal != nullptr ? cal->n_events : 0,
               cal != nullptr ? cal->n_waste : 0, cal_have ? 1 : 0, cal_inflight ? 1 : 0);
      return true;
    case Nav::Rain:
      snprintf(out, n, "rain warnings=%d ok=%d loading=%d", meteo.n, meteo_have ? 1 : 0, meteo_inflight ? 1 : 0);
      return true;
    case Nav::TransitSet:
      snprintf(out, n, "trset %s|%s", stations[0].id, stations[1].id);
      return true;
    case Nav::StationSearch:
      snprintf(out, n, "stsearch %d busy=%d", st_hit_n, (st_busy || settings_pending) ? 1 : 0);
      return true;
    case Nav::CalSet:
      snprintf(out, n, "calset");
      return true;
    case Nav::Energy:
      snprintf(out, n, "energy page=%d", en_page);
      return true;
    case Nav::EnSet:
      snprintf(out, n, "enset");
      return true;
    case Nav::Co4:
    case Nav::Co4Set:
    case Nav::Co4Login:
    case Nav::Co4Zones: {
      Co4View v;
      co4_get(&v);
      static const char* const kView[4] = {"co4", "co4set", "co4login", "co4zones"};
      snprintf(out, n, "%s page=%d state=%d token=%s zone=%s mode=%d fetch=%d data=%d zones=%d",
               kView[static_cast<int>(nav) - static_cast<int>(Nav::Co4)], co4_page, static_cast<int>(v.state),
               v.token_set ? "set" : "unset", v.zone_set ? "set" : "unset", static_cast<int>(v.mode),
               static_cast<int>(v.fetch), v.have_data ? 1 : 0, static_cast<int>(v.zones_state));
      return true;
    }
    case Nav::Playground:
      snprintf(out, n, "playground state=%d fwd=%d", static_cast<int>(play_last_state), play_last_fwd ? 1 : 0);
      return true;
    case Nav::TextEdit: {
      char v[256];
      bool own = false;
      settings_load_text(static_cast<TextSetting>(txt_which), v, sizeof(v), &own);
      memset(v, 0, sizeof(v));
      snprintf(out, n, "text %d own=%d busy=%d", txt_which, own ? 1 : 0, (txt_busy || settings_pending) ? 1 : 0);
      return true;
    }
    case Nav::Maint:
      snprintf(out, n, "maint web=%d pin=%s confirm=%d ota=%d otaui=%d new=%s", maint_web_running() ? 1 : 0,
               maint_web_running() ? maint_web_pin() : "-", maint_secrets_requested() ? 1 : 0, maint_ota_progress(),
               static_cast<int>(ota_state), ota_new_ver[0] ? ota_new_ver : "-");
      return true;
    default:
      return false;
  }
}

bool info_list_dump() {
  if (nav == Nav::Transit) {
    const Board& b = boards[tr_idx];
    for (int i = 0; i < b.n; ++i) {
      char hm[8];
      fmt_hm(hm, sizeof(hm), b.d[i].dep);
      console_printf("ITEM %d %s|%s|%s|+%d|%s\n", i, b.d[i].line, b.d[i].to, hm, b.d[i].delay, b.d[i].platform);
    }
  } else if (nav == Nav::Calendar && cal != nullptr) {
    for (int i = 0; i < cal->n_waste; ++i) {
      char d[24];
      fmt_day(d, sizeof(d), cal->waste[i].start);
      console_printf("ITEM W%d %s|%s\n", i, d, cal->waste[i].title);
    }
    for (int i = 0; i < cal->n_events; ++i) {
      char d[24], hm[8];
      fmt_day(d, sizeof(d), cal->events[i].start);
      fmt_hm(hm, sizeof(hm), cal->events[i].start);
      console_printf("ITEM E%d %s %s|%d chars\n", i, d, cal->events[i].all_day ? "ganztags" : hm,
                     static_cast<int>(strlen(cal->events[i].title)));  // titles are private: length only
    }
  } else if (nav == Nav::Rain) {
    for (int i = 0; i < meteo.n; ++i)
      console_printf("ITEM %d %s level=%d %lld-%lld\n", i, warning_name(meteo.w[i].type), meteo.w[i].level,
                     static_cast<long long>(meteo.w[i].from), static_cast<long long>(meteo.w[i].to));
    for (int i = 0; i < kRainSteps; ++i) console_printf("ITEM R%d %.2f\n", i, static_cast<double>(meteo.rain[i]));
  } else if (nav == Nav::StationSearch) {
    for (int i = 0; i < st_hit_n; ++i) console_printf("ITEM %d %s|%s\n", i, st_hits[i].id, st_hits[i].name);
  } else {
    return false;
  }
  console_printf("%s", "ITEM_END\n");
  return true;
}
