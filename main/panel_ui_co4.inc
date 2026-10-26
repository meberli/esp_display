// Co4 Energie: dashboard (Heute / Verlauf, swipe), Einstellungen > Co4 Energie,
// login (device code + QR) and zone choice. Data and login state come from
// co4_task (core 0): co4_ui_take() tells the UI that something changed, the UI
// then copies the snapshot (co4_get / co4_get_data). Tokens never reach the UI.

lv_obj_t* co4_root[2] = {nullptr, nullptr};  // 0 Heute, 1 Verlauf
lv_obj_t* co4set_root = nullptr;
lv_obj_t* co4login_root = nullptr;
lv_obj_t* co4zone_root = nullptr;
Co4View* co4_v = nullptr;   // PSRAM copies, UI task only
Co4Data* co4_d = nullptr;
Co4ZoneList* co4_z = nullptr;
bool co4_dirty = true;

// Dashboard widgets (per page)
lv_obj_t* c4_status[2] = {};
lv_obj_t* c4_badge[2] = {};
lv_obj_t* c4_body[2] = {};
lv_obj_t* c4_empty[2] = {};
lv_obj_t* c4_empty_title[2] = {};
lv_obj_t* c4_empty_text[2] = {};
lv_obj_t* c4_empty_btn[2] = {};
lv_obj_t* c4_empty_btn_lbl[2] = {};
lv_obj_t* c4_chart = nullptr;
lv_chart_series_t* c4_ser[3] = {};  // PV, Bezug, Einspeisung
lv_obj_t* c4_sum = nullptr;
lv_obj_t* c4_peak = nullptr;
lv_obj_t* c4_arc[2] = {};
lv_obj_t* c4_pct[2] = {};
lv_obj_t* c4_arc_sub[2] = {};
lv_obj_t* c4_mtot = nullptr;
lv_obj_t* c4_dpv[31] = {};
lv_obj_t* c4_dbz[31] = {};
lv_obj_t* c4_dlbl[4] = {};
lv_obj_t* c4_ytot = nullptr;
lv_obj_t* c4_mpv[12] = {};
lv_obj_t* c4_mbz[12] = {};
lv_obj_t* c4_mlbl[12] = {};
// Settings
constexpr int kCo4SetRows = 4;
lv_obj_t* co4set_val[kCo4SetRows] = {};
lv_obj_t* co4_logout_row = nullptr;
lv_obj_t* co4_confirm = nullptr;
// Login
lv_obj_t* c4l_qr = nullptr;
lv_obj_t* c4l_title = nullptr;
lv_obj_t* c4l_text = nullptr;
lv_obj_t* c4l_code = nullptr;
lv_obj_t* c4l_code_cap = nullptr;
lv_obj_t* c4l_left = nullptr;
lv_obj_t* c4l_btn[2] = {};
char c4l_qr_txt[200] = "";
// Zones
lv_obj_t* c4z_list = nullptr;
lv_obj_t* c4z_msg = nullptr;
lv_obj_t* c4z_btn[2] = {};
int c4z_shown = -1;

constexpr int kC4ChartW = 396;
constexpr int kC4ChartH = 120;
constexpr int kC4DayBase = 112;  // Verlauf: day bars baseline inside card 1
constexpr int kC4DayH = 72;
constexpr int kC4MonBase = 104;
constexpr int kC4MonH = 64;

void open_co4set();
void co4_open_login();
void co4_open_zones();
void co4_refresh();

lv_obj_t* co4_btn(lv_obj_t* parent, int w, const char* text, lv_event_cb_t cb, int id, const char* key, bool primary,
                  lv_obj_t** lbl_out = nullptr) {
  lv_obj_t* b = lv_btn_create(parent);
  lv_obj_set_size(b, w, 52);
  lv_obj_set_style_radius(b, 16, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(primary ? kAccent : kBorder), 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<intptr_t>(id)));
  watch(b);
  lv_obj_t* l = make_label(b, &font_inter_sb16, lv_color_hex(primary ? 0x0B2129 : kText));
  lv_label_set_text(l, text);
  lv_obj_center(l);
  if (key != nullptr) reg(key, b);
  if (lbl_out != nullptr) *lbl_out = l;
  return b;
}

void co4_fmt_kwh(char* out, size_t n, float v) {
  if (!(v >= 0)) v = 0;
  if (v >= 1000) snprintf(out, n, "%.2f MWh", v / 1000.0f);
  else if (v >= 100) snprintf(out, n, "%.0f kWh", v);
  else snprintf(out, n, "%.1f kWh", v);
}

// Index of the block interval that contains now (or -1).
int co4_index(const int64_t t0, const int64_t step, int n, int64_t now_ms) {
  if (step <= 0 || now_ms < t0) return -1;
  int64_t i = (now_ms - t0) / step;
  return i < n ? static_cast<int>(i) : -1;
}

// --- Dashboard: empty / state panel ------------------------------------------------

void on_c4_empty_btn(lv_event_t*) {
  if (gesture_consumed) return;
  Co4View& v = *co4_v;
  if (v.state == Co4State::NoLogin || v.state == Co4State::Expired) {
    co4_open_login();
    co4_login();
  } else if (!v.zone_set && v.mode != Co4Mode::Demo) {
    co4_open_zones();
  } else {
    co4_refresh_now();
  }
}

void c4_dots(lv_obj_t* root, int active) {
  for (int i = 0; i < 2; ++i)
    make_bar(root, 228 + i * 16, 452, 8, 8, i == active ? kText : kTextDim, i == active ? LV_OPA_COVER : LV_OPA_50);
}

void c4_build_common(lv_obj_t* r, int page) {
  // Co4 design: the title is a 60 px header bar (style_header), the icon sits on
  // it and the content moves below it.
  const bool bar = g_theme->header_bar;
  lv_obj_t* title = lv_obj_get_child(r, 0);
  if (bar) lv_obj_set_style_pad_left(title, 56, 0);
  else lv_obj_set_pos(title, 56, 20);
  lv_obj_t* ic = make_label(r, &font_icon_bolt_24, lv_color_hex(bar ? 0xFFFFFF : kGreen));
  lv_label_set_text(ic, ICON_BOLT);
  lv_obj_set_pos(ic, 22, 19);
  en_build_status(r, &c4_status[page]);
  c4_body[page] = lv_obj_create(r);
  lv_obj_set_pos(c4_body[page], 0, bar ? 6 : 0);
  lv_obj_set_size(c4_body[page], kScreen, 448);
  style_plain(c4_body[page]);
  lv_obj_clear_flag(c4_body[page], LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t* e = c4_empty[page] = en_card(r, bar ? 68 : 56, bar ? 376 : 388);
  lv_obj_t* big = make_label(e, &font_icon_bolt_24, lv_color_hex(kTextDim));
  lv_label_set_text(big, ICON_BOLT);
  lv_obj_align(big, LV_ALIGN_TOP_MID, 0, 70);
  c4_empty_title[page] = make_label(e, &font_inter_20, lv_color_hex(kText));
  lv_obj_set_width(c4_empty_title[page], 392);
  lv_obj_set_style_text_align(c4_empty_title[page], LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(c4_empty_title[page], LV_ALIGN_TOP_MID, 0, 112);
  c4_empty_text[page] = make_label(e, &font_inter_16, lv_color_hex(kTextDim));
  lv_obj_set_width(c4_empty_text[page], 360);
  lv_label_set_long_mode(c4_empty_text[page], LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(c4_empty_text[page], LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(c4_empty_text[page], LV_ALIGN_TOP_MID, 0, 148);
  c4_empty_btn[page] = co4_btn(e, 220, "Co4 verbinden", on_c4_empty_btn, page, page == 0 ? "co4:act" : "co4:act2", true,
                               &c4_empty_btn_lbl[page]);
  lv_obj_align(c4_empty_btn[page], LV_ALIGN_BOTTOM_MID, 0, -40);
}

lv_obj_t* c4_make_badge(lv_obj_t* card) {
  lv_obj_t* p = lv_obj_create(card);
  lv_obj_set_size(p, LV_SIZE_CONTENT, 22);
  lv_obj_set_style_radius(p, 11, 0);
  lv_obj_set_style_bg_color(p, lv_color_hex(kAmber), 0);
  lv_obj_set_style_bg_opa(p, LV_OPA_20, 0);
  lv_obj_set_style_border_width(p, 1, 0);
  lv_obj_set_style_border_color(p, lv_color_hex(kAmber), 0);
  lv_obj_set_style_pad_hor(p, 8, 0);
  lv_obj_set_style_pad_ver(p, 1, 0);
  lv_obj_clear_flag(p, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* t = make_label(p, &font_inter_14, lv_color_hex(kAmber));
  lv_label_set_text(t, "Beispieldaten");
  lv_obj_center(t);
  lv_obj_align(p, LV_ALIGN_TOP_RIGHT, -14, 10);
  return p;
}

void build_co4_screens(lv_obj_t* screen) {
  co4_v = static_cast<Co4View*>(heap_caps_calloc(1, sizeof(Co4View), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  co4_d = static_cast<Co4Data*>(heap_caps_calloc(1, sizeof(Co4Data), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  co4_z = static_cast<Co4ZoneList*>(heap_caps_calloc(1, sizeof(Co4ZoneList), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  // --- Heute
  lv_obj_t* r = co4_root[0] = make_screen_root(screen, "Co4 Energie");
  c4_build_common(r, 0);
  lv_obj_t* c = en_card(c4_body[0], 56, 220);
  en_lbl(c, &font_inter_sb16, kText, 18, 12, "Heute");
  c4_badge[0] = c4_make_badge(c);
  c4_sum = en_lbl(c, &font_inter_14, kTextDim, 18, 36, "");
  lv_obj_set_width(c4_sum, 396);
  lv_label_set_long_mode(c4_sum, LV_LABEL_LONG_DOT);
  lv_label_set_recolor(c4_sum, true);  // series names in the curve colours (legend)
  c4_chart = lv_chart_create(c);
  lv_obj_set_pos(c4_chart, 18, 60);
  lv_obj_set_size(c4_chart, kC4ChartW, kC4ChartH);
  lv_chart_set_type(c4_chart, LV_CHART_TYPE_LINE);
  lv_chart_set_point_count(c4_chart, kCo4MaxPoints);
  lv_chart_set_div_line_count(c4_chart, 3, 0);
  lv_chart_set_update_mode(c4_chart, LV_CHART_UPDATE_MODE_SHIFT);
  lv_obj_set_style_bg_opa(c4_chart, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(c4_chart, 0, 0);
  lv_obj_set_style_pad_all(c4_chart, 0, 0);
  lv_obj_set_style_line_color(c4_chart, lv_color_hex(kBorder), LV_PART_MAIN);
  lv_obj_set_style_line_width(c4_chart, 2, LV_PART_ITEMS);
  lv_obj_set_style_size(c4_chart, 0, LV_PART_INDICATOR);
  lv_obj_clear_flag(c4_chart, LV_OBJ_FLAG_CLICKABLE);
  const uint32_t sc[3] = {kGreen, kAmber, kAccent};
  for (int i = 0; i < 3; ++i) {
    c4_ser[i] = lv_chart_add_series(c4_chart, lv_color_hex(sc[i]), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_all_value(c4_chart, c4_ser[i], LV_CHART_POINT_NONE);
  }
  for (int h = 0; h <= 24; h += 6) {
    char t[4];
    snprintf(t, sizeof(t), "%d", h);
    lv_obj_t* l = en_lbl(c, &font_inter_14, kTextDim, 0, 184, "");
    lv_label_set_text(l, t);
    lv_obj_set_x(l, h == 24 ? 396 : 18 + h * kC4ChartW / 24);
  }
  c4_peak = en_lbl(c, &font_inter_14, kTextDim, 0, 12, "");
  lv_obj_set_width(c4_peak, 150);
  lv_obj_set_x(c4_peak, 90);
  c = en_card(c4_body[0], 286, 158);
  const char* at[2] = {"Autarkie", "Eigenverbrauch"};
  for (int i = 0; i < 2; ++i) {
    const int x0 = 16 + i * 216;
    en_lbl(c, &font_inter_sb16, kText, x0, 12, at[i]);
    c4_arc[i] = mx_donut(c, x0, 44, 96);
    lv_obj_set_style_arc_width(c4_arc[i], 14, LV_PART_MAIN);
    lv_obj_set_style_arc_width(c4_arc[i], 14, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(c4_arc[i], lv_color_hex(i == 0 ? kGreen : kAccent), LV_PART_INDICATOR);
    c4_pct[i] = make_label(c, &font_inter_sb16, lv_color_hex(kText));
    lv_obj_set_width(c4_pct[i], 80);
    lv_obj_set_style_text_align(c4_pct[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(c4_pct[i], x0 + 8, 82);
    c4_arc_sub[i] = en_lbl(c, &font_inter_14, kTextDim, x0 + 106, 62, "");
    lv_obj_set_width(c4_arc_sub[i], 94);
    lv_label_set_long_mode(c4_arc_sub[i], LV_LABEL_LONG_WRAP);
  }
  c4_dots(r, 0);
  // --- Verlauf
  r = co4_root[1] = make_screen_root(screen, "Co4 Verlauf");
  c4_build_common(r, 1);
  c = en_card(c4_body[1], 56, 200);
  en_lbl(c, &font_inter_sb16, kText, 18, 12, "Dieser Monat");
  c4_badge[1] = c4_make_badge(c);
  c4_mtot = en_lbl(c, &font_inter_14, kTextDim, 18, 36, "");
  lv_obj_set_width(c4_mtot, 396);
  lv_label_set_long_mode(c4_mtot, LV_LABEL_LONG_DOT);
  for (int d = 0; d < 31; ++d) {
    const int x = 22 + d * 13;
    c4_dpv[d] = en_rect(c, x, kC4DayBase + 40 - 1, 5, 1, kGreen);
    c4_dbz[d] = en_rect(c, x + 5, kC4DayBase + 40 - 1, 5, 1, kAmber);
  }
  make_bar(c, 18, kC4DayBase + 40, 396, 1, kBorder, LV_OPA_COVER);
  const int dl[4] = {1, 10, 20, 31};
  for (int i = 0; i < 4; ++i) {
    c4_dlbl[i] = en_lbl(c, &font_inter_14, kTextDim, 20 + (dl[i] - 1) * 13, kC4DayBase + 46, "");
    lv_label_set_text_fmt(c4_dlbl[i], "%d", dl[i]);
  }
  c = en_card(c4_body[1], 266, 178);
  en_lbl(c, &font_inter_sb16, kText, 18, 12, "Dieses Jahr");
  c4_ytot = en_lbl(c, &font_inter_14, kTextDim, 18, 36, "");
  lv_obj_set_width(c4_ytot, 396);
  lv_label_set_long_mode(c4_ytot, LV_LABEL_LONG_DOT);
  static const char* const kMon[12] = {"J", "F", "M", "A", "M", "J", "J", "A", "S", "O", "N", "D"};
  for (int m = 0; m < 12; ++m) {
    const int x = 24 + m * 33;
    c4_mpv[m] = en_rect(c, x, kC4MonBase + 30 - 1, 12, 1, kGreen);
    c4_mbz[m] = en_rect(c, x + 13, kC4MonBase + 30 - 1, 12, 1, kAmber);
    c4_mlbl[m] = en_lbl(c, &font_inter_14, kTextDim, x + 7, kC4MonBase + 36, kMon[m]);
  }
  make_bar(c, 18, kC4MonBase + 30, 396, 1, kBorder, LV_OPA_COVER);
  en_lbl(c, &font_inter_14, kGreen, 290, 12, "PV");
  en_lbl(c, &font_inter_14, kAmber, 330, 12, "Bezug");
  c4_dots(r, 1);
}

// Shows the state panel instead of the data (title "" = data view).
void c4_set_empty(int page, const char* title, const char* text, const char* btn) {
  if (title == nullptr) {
    hide(c4_empty[page]);
    show(c4_body[page]);
    return;
  }
  hide(c4_body[page]);
  show(c4_empty[page]);
  lv_label_set_text(c4_empty_title[page], title);
  lv_label_set_text(c4_empty_text[page], text);
  if (btn == nullptr) hide(c4_empty_btn[page]);
  else {
    show(c4_empty_btn[page]);
    lv_label_set_text(c4_empty_btn_lbl[page], btn);
  }
}

void c4_bar_pair(lv_obj_t* a, lv_obj_t* b, int base, int hmax, float va, float vb, float max) {
  int ha = max > 0 && va > 0 ? static_cast<int>(va / max * hmax + 0.5f) : 0;
  int hb = max > 0 && vb > 0 ? static_cast<int>(vb / max * hmax + 0.5f) : 0;
  en_set_bar(a, base, ha, true);
  en_set_bar(b, base, hb, true);
}

void co4_refresh_heute(int64_t now_ms) {
  const Co4Curve& day = co4_d->day;
  float vmax = 0;
  for (int s = 0; s < kCo4SeriesN; ++s)
    for (int i = 0; i < day.n; ++i)
      if (day.v[s][i] > vmax) vmax = day.v[s][i];
  const int top = vmax > 0 ? static_cast<int>(vmax * 100.0f * 1.1f) + 1 : 100;
  lv_chart_set_range(c4_chart, LV_CHART_AXIS_PRIMARY_Y, 0, top);
  const Co4Series map[3] = {kCo4Pv, kCo4Bezug, kCo4Einspeisung};
  int peak_i = -1;
  for (int k = 0; k < 3; ++k) {
    for (int i = 0; i < kCo4MaxPoints; ++i) {
      float x = i < day.n ? day.v[map[k]][i] : NAN;
      c4_ser[k]->y_points[i] = x == x ? static_cast<lv_coord_t>(x * 100.0f + 0.5f) : LV_CHART_POINT_NONE;
      if (k == 0 && x == x && (peak_i < 0 || x > day.v[kCo4Pv][peak_i])) peak_i = i;
    }
  }
  lv_chart_refresh(c4_chart);
  if (peak_i >= 0 && day.v[kCo4Pv][peak_i] > 0) {
    char hm[8];
    fmt_hm(hm, sizeof(hm), (day.t0_ms + static_cast<int64_t>(peak_i) * day.step_ms) / 1000);
    char pk[48];  // LVGL's own printf has no %f (LV_SPRINTF_USE_FLOAT off)
    snprintf(pk, sizeof(pk), "PV max %.1f kW \xC2\xB7 %s", static_cast<double>(day.v[kCo4Pv][peak_i]), hm);
    lv_label_set_text(c4_peak, pk);
  } else {
    lv_label_set_text(c4_peak, "");
  }
  // Totals of today from the day block (energy).
  const Co4Totals& d = co4_d->days;
  int ti = co4_index(d.t0_ms, d.step_ms, d.n, now_ms);
  Co4Ratio rt = {};
  bool ok = ti >= 0 && co4_ratio(d.v, ti, ti + 1, &rt);
  char pv[16], bz[16], es[16];
  co4_fmt_kwh(pv, sizeof(pv), rt.sum[kCo4Pv]);
  co4_fmt_kwh(bz, sizeof(bz), rt.sum[kCo4Bezug]);
  co4_fmt_kwh(es, sizeof(es), rt.sum[kCo4Einspeisung]);
  if (ok)
    lv_label_set_text_fmt(c4_sum, "#%06X PV# %s \xC2\xB7 #%06X Bezug# %s \xC2\xB7 #%06X Einspeisung# %s",
                          static_cast<unsigned>(kGreen), pv, static_cast<unsigned>(kAmber), bz,
                          static_cast<unsigned>(kAccent), es);
  else lv_label_set_text(c4_sum, "Noch keine Tageswerte");
  const float pct[2] = {rt.autarky_pct, rt.self_pct};
  char e1[16];
  co4_fmt_kwh(e1, sizeof(e1), rt.sum[kCo4Eigen]);
  for (int i = 0; i < 2; ++i) {
    lv_arc_set_value(c4_arc[i], ok ? static_cast<int16_t>(pct[i] * 10.0f) : 0);
    if (ok) lv_label_set_text_fmt(c4_pct[i], "%d %%", static_cast<int>(pct[i] + 0.5f));
    else lv_label_set_text(c4_pct[i], "\xE2\x80\x93");
  }
  lv_label_set_text(c4_arc_sub[0], ok ? "vom Verbrauch aus PV" : "");
  if (ok) lv_label_set_text_fmt(c4_arc_sub[1], "der PV selbst genutzt\n%s", e1);
  else lv_label_set_text(c4_arc_sub[1], "");
}

void co4_refresh_verlauf(int64_t now_ms) {
  const Co4Totals& d = co4_d->days;
  float max = 0;
  for (int i = 0; i < d.n; ++i) {
    if (d.v[kCo4Pv][i] > max) max = d.v[kCo4Pv][i];
    if (d.v[kCo4Bezug][i] > max) max = d.v[kCo4Bezug][i];
  }
  for (int i = 0; i < 31; ++i) {
    bool in = i < d.n;
    if (!in) {
      hide(c4_dpv[i]);
      hide(c4_dbz[i]);
      continue;
    }
    c4_bar_pair(c4_dpv[i], c4_dbz[i], kC4DayBase + 40, kC4DayH, d.v[kCo4Pv][i], d.v[kCo4Bezug][i], max);
  }
  const int last = d.n > 20 ? d.n : 31;  // last day of the month
  lv_label_set_text_fmt(c4_dlbl[3], "%d", last);
  lv_obj_set_x(c4_dlbl[3], 20 + (last - 1) * 13 - 4);
  Co4Ratio r = {};
  char pv[16], bz[16];
  if (co4_ratio(d.v, 0, d.n, &r)) {
    co4_fmt_kwh(pv, sizeof(pv), r.sum[kCo4Pv]);
    co4_fmt_kwh(bz, sizeof(bz), r.sum[kCo4Bezug]);
    lv_label_set_text_fmt(c4_mtot, "PV %s \xC2\xB7 Bezug %s \xC2\xB7 Autarkie %d %%", pv, bz,
                          static_cast<int>(r.autarky_pct + 0.5f));
  } else {
    lv_label_set_text(c4_mtot, "Noch keine Monatswerte");
  }
  const Co4Totals& m = co4_d->months;
  max = 0;
  for (int i = 0; i < m.n; ++i) {
    if (m.v[kCo4Pv][i] > max) max = m.v[kCo4Pv][i];
    if (m.v[kCo4Bezug][i] > max) max = m.v[kCo4Bezug][i];
  }
  for (int i = 0; i < 12; ++i) {
    if (i >= m.n) {
      hide(c4_mpv[i]);
      hide(c4_mbz[i]);
      continue;
    }
    c4_bar_pair(c4_mpv[i], c4_mbz[i], kC4MonBase + 30, kC4MonH, m.v[kCo4Pv][i], m.v[kCo4Bezug][i], max);
  }
  r = {};
  if (co4_ratio(m.v, 0, m.n, &r)) {
    co4_fmt_kwh(pv, sizeof(pv), r.sum[kCo4Pv]);
    co4_fmt_kwh(bz, sizeof(bz), r.sum[kCo4Bezug]);
    lv_label_set_text_fmt(c4_ytot, "PV %s \xC2\xB7 Bezug %s \xC2\xB7 Eigenverbrauch %d %%", pv, bz,
                          static_cast<int>(r.self_pct + 0.5f));
  } else {
    lv_label_set_text(c4_ytot, "Noch keine Jahreswerte");
  }
  (void)now_ms;
}

void co4_refresh_dash() {
  const int p = co4_page;
  const Co4View& v = *co4_v;
  const bool demo = v.mode == Co4Mode::Demo;
  bool have = co4_get_data(co4_d);
  lv_obj_t* st = c4_status[p];
  // On the Co4 header bar (gradient) the status is white / light amber.
  const uint32_t c_dim = g_theme->header_bar ? 0xFFFFFF : kTextDim;
  const uint32_t c_warn = g_theme->header_bar ? 0xFFD27A : kAmber;
  lv_obj_set_style_text_color(st, lv_color_hex(c_dim), 0);
  if (!demo && (v.state == Co4State::NoLogin || v.state == Co4State::Starting || v.state == Co4State::Code)) {
    lv_label_set_text(st, "");
    c4_set_empty(p, "Co4 nicht verbunden",
                 "Melde das Panel einmal bei Co4 an. Danach zeigt es hier die Energiedaten deiner Zone.",
                 "Co4 verbinden");
    return;
  }
  if (!demo && v.state == Co4State::Expired) {
    lv_label_set_text(st, "");
    c4_set_empty(p, "Co4-Anmeldung abgelaufen", "Bitte melde das Panel erneut bei Co4 an.", "Erneut anmelden");
    return;
  }
  if (!demo && !v.zone_set) {
    lv_label_set_text(st, "");
    c4_set_empty(p, "Keine Zone gew\xC3\xA4hlt", "W\xC3\xA4hle einmal die Zone, deren Energiedaten das Panel zeigt.",
                 "Zone w\xC3\xA4hlen");
    return;
  }
  if (!have) {
    if (v.fetch == Co4Fetch::Offline) {
      lv_label_set_text(st, "Offline");
      lv_obj_set_style_text_color(st, lv_color_hex(c_warn), 0);
      c4_set_empty(p, "Co4 nicht erreichbar", v.data_err[0] ? v.data_err : "Keine Daten", "Nochmals versuchen");
    } else {
      lv_label_set_text(st, "");
      c4_set_empty(p, "Lade Co4-Daten \xE2\x80\xA6", "", nullptr);
    }
    return;
  }
  c4_set_empty(p, nullptr, nullptr, nullptr);
  char hm[8];
  fmt_hm(hm, sizeof(hm), co4_d->fetched_ms / 1000);
  if (v.fetch == Co4Fetch::Offline) {
    lv_label_set_text_fmt(st, "Offline \xE2\x80\x93 Stand %s", hm);
    lv_obj_set_style_text_color(st, lv_color_hex(c_warn), 0);
  } else if (demo) {
    lv_label_set_text_fmt(st, "Demo \xC2\xB7 Stand %s", hm);
  } else {
    lv_label_set_text_fmt(st, "%s \xC2\xB7 %s", v.zone_name[0] ? v.zone_name : "Zone", hm);
  }
  if (co4_d->example) show(c4_badge[p]);
  else hide(c4_badge[p]);
  const int64_t now = static_cast<int64_t>(time(nullptr)) * 1000;
  if (p == 0) co4_refresh_heute(now);
  else co4_refresh_verlauf(now);
}

void co4_enter(int page) {
  if (message_open || co4_root[0] == nullptr) return;
  co4_page = page < 0 ? 0 : page > 1 ? 1 : page;
  enter_view(Nav::Co4, co4_root[co4_page]);
  co4_refresh();
}

void co4_swipe(int dir) {
  int next = co4_page + dir;
  if (next < 0) {
    energy_enter(2);  // right swipe on Heute: back to the Energie pages
    return;
  }
  if (next > 1) return;
  co4_enter(next);
}

// --- Einstellungen > Co4 Energie ---------------------------------------------------

void on_co4_confirm(lv_event_t* e) {
  if (gesture_consumed) return;
  hide(co4_confirm);
  if (lv_event_get_user_data(e) == nullptr) return;  // Abbrechen
  console_printf("Co4 logout\n");
  co4_logout();
}

void build_co4_confirm(lv_obj_t* screen) {
  co4_confirm = lv_obj_create(screen);
  lv_obj_set_size(co4_confirm, kScreen - 2 * kMargin, 200);
  lv_obj_center(co4_confirm);
  style_glass(co4_confirm, 24);
  lv_obj_set_style_border_color(co4_confirm, lv_color_hex(kAccent), 0);
  lv_obj_set_style_border_width(co4_confirm, 2, 0);
  lv_obj_set_style_pad_all(co4_confirm, 20, 0);
  lv_obj_clear_flag(co4_confirm, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* t = make_label(co4_confirm, &font_inter_sb16, lv_color_hex(kText));
  lv_label_set_text(t, "Co4 abmelden?");
  lv_obj_set_pos(t, 0, 0);
  lv_obj_t* d = make_label(co4_confirm, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_set_width(d, kScreen - 2 * kMargin - 40);
  lv_label_set_long_mode(d, LV_LABEL_LONG_WRAP);
  lv_label_set_text(d, "Das Panel l\xC3\xB6scht seine Co4-Anmeldung. Die gew\xC3\xA4hlte Zone bleibt gespeichert.");
  lv_obj_set_pos(d, 0, 30);
  lv_obj_t* b = co4_btn(co4_confirm, 170, "Abbrechen", on_co4_confirm, 0, "co4:no", false);
  lv_obj_align(b, LV_ALIGN_BOTTOM_LEFT, 0, 0);  // id 0 = user data nullptr = Abbrechen
  b = co4_btn(co4_confirm, 170, "Abmelden", on_co4_confirm, 1, "co4:yes", true);
  lv_obj_align(b, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
  hide(co4_confirm);
}

const char* co4_mode_text(Co4Mode m) {
  switch (m) {
    case Co4Mode::Live: return "Co4 (Messreihen noch nicht angebunden)";
    case Co4Mode::Fixture: return "Beispieldaten (nach Anmeldung)";
    default: return "Beispieldaten ohne Anmeldung (Demo)";
  }
}

void co4set_refresh() {
  if (co4set_root == nullptr) return;
  co4_get(co4_v);
  const Co4View& v = *co4_v;
  switch (v.state) {
    case Co4State::NoLogin:
      lv_label_set_text(co4set_val[0], v.msg[0] ? v.msg : "nicht verbunden \xC2\xB7 tippen zum Anmelden");
      break;
    case Co4State::Starting: lv_label_set_text(co4set_val[0], "verbinde \xE2\x80\xA6"); break;
    case Co4State::Code: lv_label_set_text_fmt(co4set_val[0], "Anmeldung l\xC3\xA4uft \xC2\xB7 Code %s", v.user_code); break;
    case Co4State::LoggedIn: lv_label_set_text(co4set_val[0], "verbunden"); break;
    case Co4State::Expired: lv_label_set_text(co4set_val[0], "Anmeldung abgelaufen \xC2\xB7 tippen zum Anmelden"); break;
  }
  if (v.zone_set) lv_label_set_text(co4set_val[1], v.zone_name[0] ? v.zone_name : "gew\xC3\xA4hlt");
  else lv_label_set_text(co4set_val[1], "keine gew\xC3\xA4hlt");
  lv_label_set_text(co4set_val[2], co4_mode_text(v.mode));
  if (v.have_data) {
    char hm[8];
    fmt_hm(hm, sizeof(hm), v.data_ms / 1000);
    lv_label_set_text_fmt(co4set_val[3], "%s \xC2\xB7 Stand %s", v.fetch == Co4Fetch::Offline ? "Offline" : "Daten ok", hm);
  } else {
    lv_label_set_text(co4set_val[3], v.fetch == Co4Fetch::Offline ? (v.data_err[0] ? v.data_err : "Offline") : "noch keine Daten");
  }
  if (v.token_set || v.state == Co4State::LoggedIn) lv_obj_clear_state(co4_logout_row, LV_STATE_DISABLED);
  else lv_obj_add_state(co4_logout_row, LV_STATE_DISABLED);
}

void co4set_summary() {
  if (set_co4_val == nullptr) return;
  co4_get(co4_v);
  const Co4View& v = *co4_v;
  if (v.state == Co4State::Expired) lv_label_set_text(set_co4_val, "Anmeldung abgelaufen");
  else if (v.state != Co4State::LoggedIn) lv_label_set_text(set_co4_val, "nicht verbunden");
  else if (!v.zone_set) lv_label_set_text(set_co4_val, "verbunden \xC2\xB7 keine Zone gew\xC3\xA4hlt");
  else lv_label_set_text_fmt(set_co4_val, "verbunden \xC2\xB7 %s", v.zone_name);
}

void open_co4set() {
  nav = Nav::Co4Set;
  hide_settings();
  co4set_refresh();
  show(co4set_root);
}

void on_co4set_row(lv_event_t* e) {
  if (gesture_consumed) return;
  int row = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  console_printf("Activate co4set %d\n", row);
  co4_get(co4_v);
  if (row == 0) {
    if (co4_v->state == Co4State::LoggedIn) return;  // abmelden via its own row
    co4_open_login();
    if (co4_v->state != Co4State::Code && co4_v->state != Co4State::Starting) co4_login();
  } else if (row == 1) {
    co4_open_zones();
  } else if (row == 2) {
    co4_set_mode(static_cast<Co4Mode>((static_cast<int>(co4_v->mode) + 1) % 3));
  } else if (row == 3) {
    co4_enter(0);
  } else if (row == 4) {
    show(co4_confirm);
    lv_obj_move_foreground(co4_confirm);
  }
}

// --- Login (device code) ----------------------------------------------------------

void on_c4l_btn(lv_event_t* e) {
  if (gesture_consumed) return;
  int id = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  if (id == 0) {
    co4_login_cancel();
    open_co4set();
  } else {
    co4_login();
  }
}

void co4_open_login() {
  nav = Nav::Co4Login;
  hide_settings();
  info_hide_views();
  hide(home_root);
  c4l_qr_txt[0] = '\0';
  show(co4login_root);
  co4_refresh();
}

void build_co4_login(lv_obj_t* screen) {
  co4login_root = make_screen_root(screen, "Co4 anmelden");
  lv_obj_t* c = en_card(co4login_root, 56, 300);
#if LV_USE_QRCODE
  c4l_qr = lv_qrcode_create(c, 176, lv_color_hex(0x000000), lv_color_hex(0xFFFFFF));
  lv_obj_set_pos(c4l_qr, 14, 14);
  lv_obj_set_style_border_color(c4l_qr, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_width(c4l_qr, 6, 0);
#endif
  c4l_title = en_lbl(c, &font_inter_sb16, kText, 214, 16, "");
  lv_obj_set_width(c4l_title, 200);
  lv_label_set_long_mode(c4l_title, LV_LABEL_LONG_WRAP);
  c4l_text = en_lbl(c, &font_inter_14, kTextDim, 214, 44, "");
  lv_obj_set_width(c4l_text, 200);
  lv_label_set_long_mode(c4l_text, LV_LABEL_LONG_WRAP);
  c4l_code_cap = en_lbl(c, &font_inter_14, kTextDim, 18, 214, "Code");
  c4l_code = en_lbl(c, &font_inter_20, kText, 18, 236, "");
  c4l_left = en_lbl(c, &font_inter_14, kTextDim, 214, 240, "");
  lv_obj_set_width(c4l_left, 200);
  c4l_btn[0] = co4_btn(co4login_root, 206, "Abbrechen", on_c4l_btn, 0, "co4:lno", false);
  lv_obj_set_pos(c4l_btn[0], 24, 372);
  c4l_btn[1] = co4_btn(co4login_root, 206, "Nochmals versuchen", on_c4l_btn, 1, "co4:lretry", true);
  lv_obj_set_pos(c4l_btn[1], 250, 372);
}

void co4_login_refresh() {
  const Co4View& v = *co4_v;
  hide(c4l_btn[1]);
  lv_label_set_text(c4l_code, "");
  lv_label_set_text(c4l_left, "");
  bool qr = false;
  switch (v.state) {
    case Co4State::Starting:
      lv_label_set_text(c4l_title, "Verbinde mit Co4 \xE2\x80\xA6");
      lv_label_set_text(c4l_text, "Das Panel fragt einen Anmeldecode an.");
      break;
    case Co4State::Code: {
      qr = true;
      lv_label_set_text(c4l_title, "Mit dem Handy anmelden");
      lv_label_set_text_fmt(c4l_text, "QR-Code scannen oder %s \xC3\xB6" "ffnen und den Code eingeben.",
                            v.verify_uri[0] ? v.verify_uri : "die Co4-Anmeldeseite");
      lv_label_set_text(c4l_code, v.user_code);
      int32_t left = static_cast<int32_t>(v.code_until_ms - now_ms()) / 1000;
      if (left < 0) left = 0;
      lv_label_set_text_fmt(c4l_left, "g\xC3\xBCltig noch %d:%02d", static_cast<int>(left / 60), static_cast<int>(left % 60));
      const char* qt = v.verify_complete[0] ? v.verify_complete : v.verify_uri;
#if LV_USE_QRCODE
      if (strcmp(qt, c4l_qr_txt) != 0) {
        snprintf(c4l_qr_txt, sizeof(c4l_qr_txt), "%s", qt);
        lv_qrcode_update(c4l_qr, c4l_qr_txt, strlen(c4l_qr_txt));
      }
#endif
      break;
    }
    case Co4State::LoggedIn:
      lv_label_set_text(c4l_title, "Angemeldet");
      lv_label_set_text(c4l_text, "Das Panel ist mit Co4 verbunden.");
      break;
    case Co4State::Expired:
    case Co4State::NoLogin:
      lv_label_set_text(c4l_title, v.state == Co4State::Expired ? "Co4-Anmeldung abgelaufen" : "Nicht angemeldet");
      lv_label_set_text(c4l_text, v.msg[0] ? v.msg : "Tippe auf \xC2\xAB" "Nochmals versuchen\xC2\xBB.");
      show(c4l_btn[1]);
      break;
  }
  if (c4l_qr != nullptr) {
    if (qr) show(c4l_qr);
    else hide(c4l_qr);
  }
  // Without a QR code the text uses the whole card.
  const int tx = qr ? 214 : 18, tw = qr ? 200 : 396;
  lv_obj_set_pos(c4l_title, tx, 16);
  lv_obj_set_width(c4l_title, tw);
  lv_obj_set_width(c4l_text, tw);
  lv_obj_align_to(c4l_text, c4l_title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);
  if (qr) show(c4l_code_cap);
  else hide(c4l_code_cap);
}

// --- Zone choice ------------------------------------------------------------------

void on_c4z_row(lv_event_t* e) {
  if (gesture_consumed) return;
  int i = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  if (i < 0 || i >= co4_z->n) return;
  console_printf("Co4 zone %d chosen\n", i);  // id/name are not logged
  co4_zone_choose(co4_z->z[i].id, co4_z->z[i].name);
  open_co4set();
}

void on_c4z_btn(lv_event_t* e) {
  if (gesture_consumed) return;
  int id = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  if (id == 0) open_co4set();
  else co4_zones_load();
}

void co4_open_zones() {
  nav = Nav::Co4Zones;
  hide_settings();
  info_hide_views();
  hide(home_root);
  c4z_shown = -1;
  show(co4zone_root);
  co4_zones_load();
  co4_refresh();
}

void build_co4_zones(lv_obj_t* screen) {
  co4zone_root = make_screen_root(screen, "Zone w\xC3\xA4hlen");
  c4z_list = make_list(co4zone_root, 56, 300);
  c4z_msg = make_label(co4zone_root, &font_inter_16, lv_color_hex(kTextDim));
  lv_obj_set_width(c4z_msg, 432);
  lv_label_set_long_mode(c4z_msg, LV_LABEL_LONG_WRAP);
  lv_obj_set_pos(c4z_msg, 24, 120);
  lv_obj_set_style_text_align(c4z_msg, LV_TEXT_ALIGN_CENTER, 0);
  c4z_btn[0] = co4_btn(co4zone_root, 206, "Abbrechen", on_c4z_btn, 0, "co4:zno", false);
  lv_obj_set_pos(c4z_btn[0], 24, 372);
  c4z_btn[1] = co4_btn(co4zone_root, 206, "Nochmals versuchen", on_c4z_btn, 1, "co4:zretry", true);
  lv_obj_set_pos(c4z_btn[1], 250, 372);
}

void co4_zones_refresh() {
  const Co4View& v = *co4_v;
  hide(c4z_btn[1]);
  if (v.zones_state == Co4ZonesState::Ok) {
    co4_get_zones(co4_z);
    if (c4z_shown != co4_z->n) {
      unreg_prefix("co4:z");
      lv_obj_clean(c4z_list);
      for (int i = 0; i < co4_z->n; ++i) {
        lv_obj_t* row = make_row(c4z_list, 56, ICON_BOLT, kGreen, on_c4z_row, i);
        lv_obj_t* n = make_label(row, &font_inter_sb16, lv_color_hex(kText));
        lv_obj_set_width(n, 330);
        lv_label_set_long_mode(n, LV_LABEL_LONG_DOT);
        lv_label_set_text(n, co4_z->z[i].name[0] ? co4_z->z[i].name : co4_z->z[i].id);
        lv_obj_align(n, LV_ALIGN_LEFT_MID, 56, 0);
        if (v.zone_set && strcmp(v.zone_id, co4_z->z[i].id) == 0) {
          lv_obj_t* ck = make_label(row, &font_icons_24, lv_color_hex(kGreen));
          lv_label_set_text(ck, ICON_CHECK);
          lv_obj_align(ck, LV_ALIGN_RIGHT_MID, -14, 0);
        }
        char key[16];
        snprintf(key, sizeof(key), "co4:z%d", i);
        reg(key, row);
      }
      c4z_shown = co4_z->n;
    }
    show(c4z_list);
    if (co4_z->n == 0) {
      lv_label_set_text(c4z_msg, "Dein Co4-Konto hat keine Zonen.");
      show(c4z_msg);
      show(c4z_btn[1]);
    } else {
      hide(c4z_msg);
    }
    return;
  }
  hide(c4z_list);
  c4z_shown = -1;
  show(c4z_msg);
  if (v.zones_state == Co4ZonesState::Error) {
    lv_label_set_text_fmt(c4z_msg, "Zonen konnten nicht geladen werden.\n%s", v.zones_err);
    show(c4z_btn[1]);
  } else {
    lv_label_set_text(c4z_msg, "Lade Zonen \xE2\x80\xA6");
  }
}

void build_co4set_screen(lv_obj_t* screen) {
  co4set_root = make_screen_root(screen, "Co4 Energie");
  lv_obj_t* list = make_list(co4set_root, 56, 404);
  struct Row { const char* name; const char* icon; uint32_t color; const char* key; };
  const Row rows[kCo4SetRows] = {
      {"Co4-Konto", ICON_ROUTER, kAccent, "co4:acct"},
      {"Zone", ICON_PLACE, kGreen, "co4:zone"},
      {"Datenquelle", ICON_MEMORY, kAmber, "co4:src"},
      {"Co4 Energie \xC3\xB6" "ffnen", ICON_BOLT, kGreen, "co4:open"},
  };
  for (int i = 0; i < kCo4SetRows; ++i)
    make_value_row(list, 0, rows[i].name, rows[i].icon, rows[i].color, on_co4set_row, i, rows[i].key, &co4set_val[i]);
  co4_logout_row = make_row(list, 56, nullptr, 0, on_co4set_row, 4);
  reg("co4:logout", co4_logout_row);
  lv_obj_t* l = make_label(co4_logout_row, &font_inter_sb16, lv_color_hex(kRed));
  lv_label_set_text(l, "Co4 abmelden");
  lv_obj_align(l, LV_ALIGN_LEFT_MID, 16, 0);
  lv_obj_set_style_opa(co4_logout_row, LV_OPA_50, LV_STATE_DISABLED);
  lv_obj_t* pad = lv_obj_create(list);
  lv_obj_set_size(pad, 10, 8);
  style_plain(pad);
  build_co4_login(screen);
  build_co4_zones(screen);
  build_co4_confirm(screen);
}

void co4_hide_all() {
  hide(co4set_root);
  hide(co4login_root);
  hide(co4zone_root);
  hide(co4_confirm);
}

void co4_hide_views() {
  hide(co4_root[0]);
  hide(co4_root[1]);
}

// Called from the info tick: drain the queue, redraw the visible Co4 view.
void co4_refresh() {
  if (co4_v == nullptr) return;
  co4_get(co4_v);
  if (nav == Nav::Co4) co4_refresh_dash();
  else if (nav == Nav::Co4Set) co4set_refresh();
  else if (nav == Nav::Co4Login) co4_login_refresh();
  else if (nav == Nav::Co4Zones) co4_zones_refresh();
}

void co4_tick() {
  Co4Event ev;
  bool any = false;
  bool state = false;
  while (co4_ui_take(&ev)) {
    any = true;
    if (ev == Co4Event::State) state = true;
  }
  if (co4_v == nullptr) return;
  // Login done: continue with the zone choice when none is set yet.
  if (state && nav == Nav::Co4Login) {
    co4_get(co4_v);
    if (co4_v->state == Co4State::LoggedIn) {
      if (!co4_v->zone_set) co4_open_zones();
      else open_co4set();
      return;
    }
  }
  if (any || co4_dirty || nav == Nav::Co4Login) {
    co4_dirty = false;
    co4_refresh();
  }
}
