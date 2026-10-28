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
Co4NavLevel* co4_z = nullptr;  // current object-tree level (copy)
bool co4_dirty = true;

// Dashboard widgets (per page)
lv_obj_t* c4_status[2] = {};
lv_obj_t* c4_body[2] = {};
lv_obj_t* c4_empty[2] = {};
lv_obj_t* c4_empty_title[2] = {};
lv_obj_t* c4_empty_text[2] = {};
lv_obj_t* c4_empty_btn[2] = {};
lv_obj_t* c4_empty_btn_lbl[2] = {};
lv_obj_t* c4_chart = nullptr;
lv_chart_series_t* c4_ser[4] = {};  // PV, Bezug, Einspeisung, Batterie (only if delivered)
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
constexpr int kCo4SetRows = 3;
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
lv_obj_t* c4z_btn_lbl[2] = {};
lv_obj_t* c4z_crumb = nullptr;
char c4z_shown[64] = "";  // signature of the rendered level

// Whole-site block (Heute: today, Verlauf: month + year). Battery column only
// when the site delivers battery values; never added to Bezug.
struct C4SiteTbl {
  int rows;
  lv_obj_t* hdr_bat;
  lv_obj_t* msg;
  lv_obj_t* name[2];
  lv_obj_t* bz[2];
  lv_obj_t* es[2];
  lv_obj_t* bat[2];
};
C4SiteTbl c4_st[2] = {};

constexpr uint32_t kC4Bat = 0x9C8FD0;  // battery: muted violet, both designs
constexpr int kC4ChartW = 396;
constexpr int kC4ChartH = 100;
constexpr int kC4DayBase = 98;  // Verlauf: day bars baseline inside card 1 (+40)
constexpr int kC4DayH = 64;
constexpr int kC4MonBase = 70;
constexpr int kC4MonH = 44;

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
  } else if (!v.zone_set) {
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

void c4_site_build(lv_obj_t* c, C4SiteTbl* t, int rows, const char* const* names) {
  t->rows = rows;
  const lv_font_t* vf = rows == 1 ? &font_inter_sb16 : &font_inter_14;
  en_lbl(c, &font_inter_14, kText, 18, 4, "ZEV gesamt");
  en_lbl(c, &font_inter_14, kAmber, 120, 4, "Bezug");
  en_lbl(c, &font_inter_14, kAccent, 214, 4, "Einspeisung");
  t->hdr_bat = en_lbl(c, &font_inter_14, kC4Bat, 316, 4, "Batterie");
  for (int r = 0; r < rows; ++r) {
    const int y = rows == 1 ? 28 : 22 + r * 17;
    t->name[r] = en_lbl(c, &font_inter_14, kTextDim, 18, rows == 1 ? y + 2 : y, names[r]);
    t->bz[r] = en_lbl(c, vf, kText, 120, y, "");
    t->es[r] = en_lbl(c, vf, kText, 214, y, "");
    t->bat[r] = en_lbl(c, vf, kC4Bat, 316, y, "");
  }
  t->msg = en_lbl(c, &font_inter_14, kTextDim, 120, rows == 1 ? 30 : 32, "");
  lv_obj_set_width(t->msg, 280);
  lv_label_set_long_mode(t->msg, LV_LABEL_LONG_DOT);
}

// Sum of v[from, to), NAN skipped; *has = at least one value.
float c4_vsum(const float* v, int from, int to, bool* has) {
  float s = 0;
  for (int i = from; i < to; ++i)
    if (!isnan(v[i])) {
      s += v[i];
      *has = true;
    }
  return s;
}

// Gaps stay as Co4 delivers them (null block = no point). A value with an empty
// block on both sides would draw no line at all: only such points get a dot.
void on_c4_chart_draw(lv_event_t* e) {
  lv_obj_draw_part_dsc_t* d = lv_event_get_draw_part_dsc(e);
  if (d->part != LV_PART_ITEMS || d->type != LV_CHART_DRAW_PART_LINE_AND_POINT || d->rect_dsc == nullptr) return;
  const lv_chart_series_t* ser = static_cast<const lv_chart_series_t*>(d->sub_part_ptr);
  const int i = static_cast<int>(d->id);
  if (ser == nullptr || i < 0 || i >= kCo4MaxPoints) return;
  const bool prev = i > 0 && ser->y_points[i - 1] != LV_CHART_POINT_NONE;
  const bool next = i + 1 < kCo4MaxPoints && ser->y_points[i + 1] != LV_CHART_POINT_NONE;
  if (prev || next) d->rect_dsc->bg_opa = LV_OPA_TRANSP;
}

// Zone blocks have no PV production, only the PV energy the ZEV allocated to the
// zone (kCo4Eigen). That is what "PV" shows for a zone.
Co4Series c4_pv_key(const float* pv, int n) {
  for (int i = 0; i < n; ++i)
    if (!isnan(pv[i])) return kCo4Pv;
  return kCo4Eigen;
}

// Eigenverbrauchsquote (selfConsumption / pvProduction) only exists for the
// whole site: false if the site block is not there or has no PV.
bool c4_site_self(const Co4SiteData& sd, const Co4Totals& b, int from, int to, Co4Ratio* r) {
  *r = {};
  if (sd.state != Co4SiteState::Ok || from < 0) return false;
  return co4_ratio(b.v, from, to > b.n ? b.n : to, r) && r->sum[kCo4Pv] > 0.0f;
}

// ranges: per row the block and [from, to) of the energy totals.
void c4_site_show(C4SiteTbl* t, const Co4SiteData& sd, const Co4Totals* const* blk, const int* from, const int* to) {
  const char* msg = nullptr;
  if (sd.state == Co4SiteState::None) msg = "Site noch nicht bekannt";
  else if (sd.state == Co4SiteState::Error) msg = sd.err[0] ? sd.err : "Site nicht verf\xC3\xBCgbar";
  bool any_bat = false;
  for (int r = 0; r < t->rows && msg == nullptr; ++r) {
    const Co4Totals& b = *blk[r];
    const int a = from[r] < 0 ? 0 : from[r], e = to[r] > b.n ? b.n : to[r];
    bool hb = false, he = false, hl = false, hx = false;
    float bz = c4_vsum(b.v[kCo4Bezug], a, e, &hb);
    float es = c4_vsum(b.v[kCo4Einspeisung], a, e, &he);
    float bl = c4_vsum(b.v[kCo4BatLaden], a, e, &hl);
    float bx = c4_vsum(b.v[kCo4BatEntladen], a, e, &hx);
    char x[16];
    co4_fmt_kwh(x, sizeof(x), bz);
    lv_label_set_text(t->bz[r], hb ? x : "\xE2\x80\x93");
    co4_fmt_kwh(x, sizeof(x), es);
    lv_label_set_text(t->es[r], he ? x : "\xE2\x80\x93");
    if (hl || hx) {
      any_bat = true;
      char bt[24];
      if (bl >= 1000 || bx >= 1000)
        snprintf(bt, sizeof(bt), "+%.1f/-%.1f MWh", static_cast<double>(bl / 1000), static_cast<double>(bx / 1000));
      else
        snprintf(bt, sizeof(bt), "+%.0f/-%.0f kWh", static_cast<double>(bl), static_cast<double>(bx));
      lv_label_set_text(t->bat[r], bt);
    }
  }
  if (msg != nullptr) {
    show(t->msg);
    lv_label_set_text(t->msg, msg);
  } else {
    hide(t->msg);
  }
  for (int r = 0; r < t->rows; ++r) {
    if (msg != nullptr) {
      hide(t->bz[r]);
      hide(t->es[r]);
    } else {
      show(t->bz[r]);
      show(t->es[r]);
    }
    if (any_bat) show(t->bat[r]);
    else hide(t->bat[r]);
  }
  if (any_bat) show(t->hdr_bat);
  else hide(t->hdr_bat);
}

void build_co4_screens(lv_obj_t* screen) {
  co4_v = static_cast<Co4View*>(heap_caps_calloc(1, sizeof(Co4View), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  co4_d = static_cast<Co4Data*>(heap_caps_calloc(1, sizeof(Co4Data), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  co4_z = static_cast<Co4NavLevel*>(heap_caps_calloc(1, sizeof(Co4NavLevel), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  // --- Heute
  lv_obj_t* r = co4_root[0] = make_screen_root(screen, "Co4 Energie");
  c4_build_common(r, 0);
  lv_obj_t* c = en_card(c4_body[0], 56, 196);
  en_lbl(c, &font_inter_sb16, kText, 18, 12, "Heute");
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
  lv_obj_set_style_size(c4_chart, 4, LV_PART_INDICATOR);  // dots only for isolated values
  lv_obj_add_event_cb(c4_chart, on_c4_chart_draw, LV_EVENT_DRAW_PART_BEGIN, nullptr);
  lv_obj_clear_flag(c4_chart, LV_OBJ_FLAG_CLICKABLE);
  const uint32_t sc[4] = {kGreen, kAmber, kAccent, kC4Bat};
  for (int i = 0; i < 4; ++i) {
    c4_ser[i] = lv_chart_add_series(c4_chart, lv_color_hex(sc[i]), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_all_value(c4_chart, c4_ser[i], LV_CHART_POINT_NONE);
  }
  for (int h = 0; h <= 24; h += 6) {
    char t[4];
    snprintf(t, sizeof(t), "%d", h);
    lv_obj_t* l = en_lbl(c, &font_inter_14, kTextDim, 0, 164, "");
    lv_label_set_text(l, t);
    lv_obj_set_x(l, h == 24 ? 396 : 18 + h * kC4ChartW / 24);
  }
  c4_peak = en_lbl(c, &font_inter_14, kTextDim, 0, 12, "");
  lv_obj_set_width(c4_peak, 150);
  lv_obj_set_x(c4_peak, 90);
  c = en_card(c4_body[0], 260, 112);
  const char* at[2] = {"Autarkie", "Eigenverbrauch"};
  for (int i = 0; i < 2; ++i) {
    const int x0 = 16 + i * 216;
    en_lbl(c, &font_inter_sb16, kText, x0, 8, at[i]);
    c4_arc[i] = mx_donut(c, x0, 30, 76);
    lv_obj_set_style_arc_width(c4_arc[i], 11, LV_PART_MAIN);
    lv_obj_set_style_arc_width(c4_arc[i], 11, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(c4_arc[i], lv_color_hex(i == 0 ? kGreen : kAccent), LV_PART_INDICATOR);
    c4_pct[i] = make_label(c, &font_inter_sb16, lv_color_hex(kText));
    lv_obj_set_width(c4_pct[i], 66);
    lv_obj_set_style_text_align(c4_pct[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(c4_pct[i], x0 + 5, 58);
    c4_arc_sub[i] = en_lbl(c, &font_inter_14, kTextDim, x0 + 88, 36, "");
    lv_obj_set_width(c4_arc_sub[i], 112);
    lv_label_set_long_mode(c4_arc_sub[i], LV_LABEL_LONG_WRAP);
  }
  c = en_card(c4_body[0], 380, 60);
  static const char* const kHeute[1] = {"Heute"};
  c4_site_build(c, &c4_st[0], 1, kHeute);
  c4_dots(r, 0);
  // --- Verlauf
  r = co4_root[1] = make_screen_root(screen, "Co4 Verlauf");
  c4_build_common(r, 1);
  c = en_card(c4_body[1], 56, 182);
  en_lbl(c, &font_inter_sb16, kText, 18, 12, "Dieser Monat");
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
  c = en_card(c4_body[1], 246, 130);
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
  c = en_card(c4_body[1], 384, 60);
  static const char* const kMY[2] = {"Monat", "Jahr"};
  c4_site_build(c, &c4_st[1], 2, kMY);
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
  float vmax = 0, vmin = 0;  // negative Bezug: the axis goes below 0
  for (int s = 0; s < kCo4SeriesN; ++s)
    for (int i = 0; i < day.n; ++i) {
      if (day.v[s][i] > vmax) vmax = day.v[s][i];
      if (day.v[s][i] < vmin) vmin = day.v[s][i];
    }
  const float lim = 300.0f;  // x100 must fit lv_coord_t
  vmax = fminf(vmax, lim);
  vmin = fmaxf(vmin, -lim);
  const int top = vmax > 0 ? static_cast<int>(vmax * 100.0f * 1.1f) + 1 : 100;
  const int bot = vmin < 0 ? static_cast<int>(vmin * 100.0f * 1.1f) - 1 : 0;
  lv_chart_set_range(c4_chart, LV_CHART_AXIS_PRIMARY_Y, bot, top);
  const Co4Series pvk = c4_pv_key(day.v[kCo4Pv], day.n);
  const Co4Series map[4] = {pvk, kCo4Bezug, kCo4Einspeisung, kCo4BatEntladen};
  int peak_i = -1;
  for (int k = 0; k < 4; ++k) {
    for (int i = 0; i < kCo4MaxPoints; ++i) {
      float x = i < day.n ? day.v[map[k]][i] : NAN;
      c4_ser[k]->y_points[i] = x == x ? static_cast<lv_coord_t>(lroundf(fmaxf(-lim, fminf(lim, x)) * 100.0f))
                                      : LV_CHART_POINT_NONE;
      if (k == 0 && x == x && (peak_i < 0 || x > day.v[pvk][peak_i])) peak_i = i;
    }
  }
  lv_chart_refresh(c4_chart);
  if (peak_i >= 0 && day.v[pvk][peak_i] > 0) {
    char hm[8];
    fmt_hm(hm, sizeof(hm), (day.t0_ms + static_cast<int64_t>(peak_i) * day.step_ms) / 1000);
    char pk[48];  // LVGL's own printf has no %f (LV_SPRINTF_USE_FLOAT off)
    snprintf(pk, sizeof(pk), "PV max %.1f kW \xC2\xB7 %s", static_cast<double>(day.v[pvk][peak_i]), hm);
    lv_label_set_text(c4_peak, pk);
  } else {
    lv_label_set_text(c4_peak, "");
  }
  // Totals of today from the day block (energy).
  const Co4Totals& d = co4_d->days;
  int ti = co4_index(d.t0_ms, d.step_ms, d.n, now_ms);
  Co4Ratio rt = {};
  bool ok = ti >= 0 && co4_ratio(d.v, ti, ti + 1, &rt);
  const Co4Series dpk = c4_pv_key(d.v[kCo4Pv], d.n);
  bool has_es = false;
  if (ok) c4_vsum(d.v[kCo4Einspeisung], ti, ti + 1, &has_es);
  char pv[16], bz[16], es[16];
  co4_fmt_kwh(pv, sizeof(pv), rt.sum[dpk]);
  co4_fmt_kwh(bz, sizeof(bz), rt.sum[kCo4Bezug]);
  if (has_es) co4_fmt_kwh(es, sizeof(es), rt.sum[kCo4Einspeisung]);
  else snprintf(es, sizeof(es), "\xE2\x80\x93");  // a zone feeds nothing in itself
  if (ok)
    lv_label_set_text_fmt(c4_sum, "#%06X PV# %s \xC2\xB7 #%06X Bezug# %s \xC2\xB7 #%06X Einspeisung# %s",
                          static_cast<unsigned>(kGreen), pv, static_cast<unsigned>(kAmber), bz,
                          static_cast<unsigned>(kAccent), es);
  else lv_label_set_text(c4_sum, "Noch keine Tageswerte");
  // Autarkie: zone (PV to the zone / (that + Bezug)). Eigenverbrauch: the
  // site's PV used on site / PV produced (a zone produces nothing itself).
  Co4Ratio srt = {};
  const Co4SiteData& sdd = co4_d->site;
  bool sok = dpk == kCo4Pv ? (ok && rt.sum[kCo4Pv] > 0.0f)
                           : c4_site_self(sdd, sdd.days, co4_index(sdd.days.t0_ms, sdd.days.step_ms, sdd.days.n, now_ms),
                                          co4_index(sdd.days.t0_ms, sdd.days.step_ms, sdd.days.n, now_ms) + 1, &srt);
  if (dpk == kCo4Pv) srt = rt;
  const bool okp[2] = {ok, sok};
  const float pct[2] = {rt.autarky_pct, srt.self_pct};
  char e1[16];
  co4_fmt_kwh(e1, sizeof(e1), srt.sum[kCo4Eigen]);
  for (int i = 0; i < 2; ++i) {
    lv_arc_set_value(c4_arc[i], okp[i] ? static_cast<int16_t>(pct[i] * 10.0f) : 0);
    if (okp[i]) lv_label_set_text_fmt(c4_pct[i], "%d %%", static_cast<int>(pct[i] + 0.5f));
    else lv_label_set_text(c4_pct[i], "\xE2\x80\x93");
  }
  lv_label_set_text(c4_arc_sub[0], ok ? "vom Verbrauch aus PV" : "");
  if (sok) lv_label_set_text_fmt(c4_arc_sub[1], dpk == kCo4Pv ? "der PV selbst genutzt\n%s" : "der Site-PV vor Ort\ngenutzt: %s", e1);
  else lv_label_set_text(c4_arc_sub[1], ok ? "Site-Werte fehlen" : "");
  const Co4SiteData& sd = co4_d->site;
  const Co4Totals* sb[1] = {&sd.days};
  int si = co4_index(sd.days.t0_ms, sd.days.step_ms, sd.days.n, now_ms);
  const int sf[1] = {si < 0 ? 0 : si}, st[1] = {si < 0 ? 0 : si + 1};
  c4_site_show(&c4_st[0], sd, sb, sf, st);
}

void co4_refresh_verlauf(int64_t now_ms) {
  const Co4Totals& d = co4_d->days;
  const Co4Series dk = c4_pv_key(d.v[kCo4Pv], d.n);
  float max = 0;
  for (int i = 0; i < d.n; ++i) {
    if (d.v[dk][i] > max) max = d.v[dk][i];
    if (d.v[kCo4Bezug][i] > max) max = d.v[kCo4Bezug][i];
  }
  for (int i = 0; i < 31; ++i) {
    bool in = i < d.n;
    if (!in) {
      hide(c4_dpv[i]);
      hide(c4_dbz[i]);
      continue;
    }
    c4_bar_pair(c4_dpv[i], c4_dbz[i], kC4DayBase + 40, kC4DayH, d.v[dk][i], d.v[kCo4Bezug][i], max);
  }
  const int last = d.n > 20 ? d.n : 31;  // last day of the month
  lv_label_set_text_fmt(c4_dlbl[3], "%d", last);
  lv_obj_set_x(c4_dlbl[3], 20 + (last - 1) * 13 - 4);
  Co4Ratio r = {};
  char pv[16], bz[16];
  if (co4_ratio(d.v, 0, d.n, &r)) {
    co4_fmt_kwh(pv, sizeof(pv), r.sum[dk]);
    co4_fmt_kwh(bz, sizeof(bz), r.sum[kCo4Bezug]);
    lv_label_set_text_fmt(c4_mtot, "PV %s \xC2\xB7 Bezug %s \xC2\xB7 Autarkie %d %%", pv, bz,
                          static_cast<int>(r.autarky_pct + 0.5f));
  } else {
    lv_label_set_text(c4_mtot, "Noch keine Monatswerte");
  }
  const Co4Totals& m = co4_d->months;
  const Co4Series mk = c4_pv_key(m.v[kCo4Pv], m.n);
  max = 0;
  for (int i = 0; i < m.n; ++i) {
    if (m.v[mk][i] > max) max = m.v[mk][i];
    if (m.v[kCo4Bezug][i] > max) max = m.v[kCo4Bezug][i];
  }
  for (int i = 0; i < 12; ++i) {
    if (i >= m.n) {
      hide(c4_mpv[i]);
      hide(c4_mbz[i]);
      continue;
    }
    c4_bar_pair(c4_mpv[i], c4_mbz[i], kC4MonBase + 30, kC4MonH, m.v[mk][i], m.v[kCo4Bezug][i], max);
  }
  r = {};
  if (co4_ratio(m.v, 0, m.n, &r)) {
    co4_fmt_kwh(pv, sizeof(pv), r.sum[mk]);
    co4_fmt_kwh(bz, sizeof(bz), r.sum[kCo4Bezug]);
    Co4Ratio sr = {};
    bool sok = mk == kCo4Pv ? r.sum[kCo4Pv] > 0.0f
                            : c4_site_self(co4_d->site, co4_d->site.months, 0, co4_d->site.months.n, &sr);
    if (mk == kCo4Pv) sr = r;
    if (sok)
      lv_label_set_text_fmt(c4_ytot, "PV %s \xC2\xB7 Bezug %s \xC2\xB7 Eigenverbrauch%s %d %%", pv, bz,
                            mk == kCo4Pv ? "" : " Site", static_cast<int>(sr.self_pct + 0.5f));
    else
      lv_label_set_text_fmt(c4_ytot, "PV %s \xC2\xB7 Bezug %s \xC2\xB7 Autarkie %d %%", pv, bz,
                            static_cast<int>(r.autarky_pct + 0.5f));
  } else {
    lv_label_set_text(c4_ytot, "Noch keine Jahreswerte");
  }
  const Co4SiteData& sd = co4_d->site;
  const Co4Totals* sb[2] = {&sd.days, &sd.months};
  const int sf[2] = {0, 0}, st[2] = {sd.days.n, sd.months.n};
  c4_site_show(&c4_st[1], sd, sb, sf, st);
  (void)now_ms;
}

void co4_refresh_dash() {
  const int p = co4_page;
  const Co4View& v = *co4_v;
  bool have = co4_get_data(co4_d);
  lv_obj_t* st = c4_status[p];
  // On the Co4 header bar (gradient) the status is white / light amber.
  const uint32_t c_dim = g_theme->header_bar ? 0xFFFFFF : kTextDim;
  const uint32_t c_warn = g_theme->header_bar ? 0xFFD27A : kAmber;
  lv_obj_set_style_text_color(st, lv_color_hex(c_dim), 0);
  if ((v.state == Co4State::NoLogin || v.state == Co4State::Starting || v.state == Co4State::Code)) {
    lv_label_set_text(st, "");
    c4_set_empty(p, "Co4 nicht verbunden",
                 "Melde das Panel einmal bei Co4 an. Danach zeigt es hier die Energiedaten deiner Zone.",
                 "Co4 verbinden");
    return;
  }
  if (v.state == Co4State::Expired) {
    lv_label_set_text(st, "");
    c4_set_empty(p, "Co4-Anmeldung abgelaufen", "Bitte melde das Panel erneut bei Co4 an.", "Erneut anmelden");
    return;
  }
  if (!v.zone_set) {
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
  } else {
    lv_label_set_text_fmt(st, "%s \xC2\xB7 %s", v.zone_name[0] ? v.zone_name : "Zone", hm);
  }
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

// Home Energie tile: Co4 when it is set up (logged in, zone chosen), else
// the whatwatt Go / PV pages when that source is on, else the Co4 page with
// its "not connected" hint and setup button (Einstellungen > Energie-Quellen).
void energy_home_open() {
  co4_get(co4_v);
  const bool co4_ok = co4_v->state == Co4State::LoggedIn && co4_v->zone_set;
  const bool direct = settings_load_energy().on || settings_load_pv_on();
  console_printf("Energie tile: source=%s\n", co4_ok ? "co4" : direct ? "direct" : "none");
  if (co4_ok || !direct) co4_enter(0);
  else energy_enter(0);
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
  if (v.have_data) {
    char hm[8];
    fmt_hm(hm, sizeof(hm), v.data_ms / 1000);
    lv_label_set_text_fmt(co4set_val[2], "%s \xC2\xB7 Stand %s", v.fetch == Co4Fetch::Offline ? "Offline" : "Daten ok", hm);
  } else {
    lv_label_set_text(co4set_val[2], v.fetch == Co4Fetch::Offline ? (v.data_err[0] ? v.data_err : "Offline") : "noch keine Daten");
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
  set_group = 3;  // back goes to Energie-Quellen
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
  } else if (row == 3) {
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

// Object tree: root > ... > node. Rows open their children; only objects with
// the nature zev_Zone get "Auswählen". The tree requests run on the Co4 task.

void on_c4z_row(lv_event_t* e) {
  if (gesture_consumed) return;
  int i = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  if (i < 0 || i >= co4_z->n) return;
  const Co4NavItem& it = co4_z->it[i];
  if (!it.kids) return;
  console_printf("Co4 tree: open level %u\n", co4_z->depth + 1);  // names/IDs are not logged
  c4z_shown[0] = '\0';
  co4_nav_open(it.soid, it.name);
}

void on_c4z_pick(lv_event_t* e) {
  if (gesture_consumed) return;
  int i = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  if (i < 0 || i >= co4_z->n || co4_z->it[i].zone[0] == '\0') return;
  console_printf("Co4 zone chosen\n");  // id/name are not logged
  // Site niid: not taken from the tree (not unambiguous), zevGetZone fills it.
  co4_zone_choose(co4_z->it[i].zone, co4_z->it[i].name, "");
  open_co4set();
}

void on_c4z_btn(lv_event_t* e) {
  if (gesture_consumed) return;
  int id = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  c4z_shown[0] = '\0';
  if (id == 0) {
    if (co4_z->depth > 0) co4_nav_back();
    else open_co4set();
  } else if (id == 1) {
    if (co4_v->zones_state == Co4ZonesState::Error || co4_z->depth == 0) co4_nav_reload();
    else open_co4set();
  }
}

void co4_open_zones() {
  nav = Nav::Co4Zones;
  hide_settings();
  info_hide_views();
  hide(home_root);
  c4z_shown[0] = '\0';
  show(co4zone_root);
  co4_nav_root();
  co4_refresh();
}

void build_co4_zones(lv_obj_t* screen) {
  co4zone_root = make_screen_root(screen, "Zone w\xC3\xA4hlen");
  c4z_crumb = make_label(co4zone_root, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_set_width(c4z_crumb, 440);
  lv_label_set_long_mode(c4z_crumb, LV_LABEL_LONG_DOT);
  lv_obj_set_pos(c4z_crumb, 22, 60);
  c4z_list = make_list(co4zone_root, 88, 272);
  c4z_msg = make_label(co4zone_root, &font_inter_16, lv_color_hex(kTextDim));
  lv_obj_set_width(c4z_msg, 432);
  lv_label_set_long_mode(c4z_msg, LV_LABEL_LONG_WRAP);
  lv_obj_set_pos(c4z_msg, 24, 160);
  lv_obj_set_style_text_align(c4z_msg, LV_TEXT_ALIGN_CENTER, 0);
  c4z_btn[0] = co4_btn(co4zone_root, 206, "Abbrechen", on_c4z_btn, 0, "co4:zno", false, &c4z_btn_lbl[0]);
  lv_obj_set_pos(c4z_btn[0], 24, 372);
  c4z_btn[1] = co4_btn(co4zone_root, 206, "Nochmals versuchen", on_c4z_btn, 1, "co4:zretry", true, &c4z_btn_lbl[1]);
  lv_obj_set_pos(c4z_btn[1], 250, 372);
}

// "Co4 > A > B" (shortened from the left when long; the label dots the end).
void c4z_crumb_text(char* out, size_t n) {
  int first = co4_z->depth > 3 ? co4_z->depth - 3 : 0;
  int w = snprintf(out, n, "%s", first > 0 ? "Co4 > ... " : "Co4");
  for (int d = first; d < co4_z->depth && w > 0 && static_cast<size_t>(w) < n; ++d)
    w += snprintf(out + w, n - w, " > %s", co4_z->path[d].name);
}

void c4z_row(int i) {
  const Co4NavItem& it = co4_z->it[i];
  bool zone = it.zone[0] != '\0';
  lv_obj_t* row = make_row(c4z_list, 64, zone ? ICON_BOLT : ICON_PLACE, zone ? kGreen : kAccent, on_c4z_row, i);
  if (zone) {
    lv_obj_set_style_border_color(row, lv_color_hex(kGreen), 0);
    lv_obj_set_style_border_width(row, 2, 0);
  }
  int text_w = zone ? 220 : 300;
  lv_obj_t* n = make_label(row, &font_inter_sb16, lv_color_hex(kText));
  lv_obj_set_width(n, text_w);
  lv_label_set_long_mode(n, LV_LABEL_LONG_DOT);
  lv_label_set_text(n, it.name);
  lv_obj_align(n, LV_ALIGN_LEFT_MID, 56, -10);
  lv_obj_t* sub = make_label(row, &font_inter_14, lv_color_hex(zone ? kGreen : kTextDim));
  bool cur = zone && co4_v->zone_set && strcmp(co4_v->zone_id, it.zone) == 0;
  lv_label_set_text(sub, zone ? (cur ? "ZEV-Zone \xC2\xB7 gew\xC3\xA4hlt" : "ZEV-Zone")
                              : (it.kids ? "Enth\xC3\xA4lt Objekte" : "Keine Unterobjekte"));
  lv_obj_align(sub, LV_ALIGN_LEFT_MID, 56, 12);
  if (it.kids) {
    lv_obj_t* ch = make_label(row, &font_icons_24, lv_color_hex(kTextDim));
    lv_label_set_text(ch, ICON_CHEVRON);
    lv_obj_align(ch, LV_ALIGN_RIGHT_MID, -8, 0);
  }
  char key[16];
  snprintf(key, sizeof(key), "co4:z%d", i);
  reg(key, row);
  if (zone) {
    lv_obj_t* b = lv_btn_create(row);
    lv_obj_set_size(b, 112, 40);
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(kGreen), 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_add_event_cb(b, on_c4z_pick, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<intptr_t>(i)));
    watch(b);
    lv_obj_t* l = make_label(b, &font_inter_sb16, lv_color_hex(0x0B2129));
    lv_label_set_text(l, "Ausw\xC3\xA4hlen");
    lv_obj_center(l);
    lv_obj_align(b, LV_ALIGN_RIGHT_MID, it.kids ? -36 : -8, 0);
    snprintf(key, sizeof(key), "co4:zp%d", i);
    reg(key, b);
  }
}

void co4_zones_refresh() {
  const Co4View& v = *co4_v;
  co4_get_nav(co4_z);
  char crumb[160];
  c4z_crumb_text(crumb, sizeof(crumb));
  lv_label_set_text(c4z_crumb, crumb);
  bool err = v.zones_state == Co4ZonesState::Error;
  bool ok = v.zones_state == Co4ZonesState::Ok;
  lv_label_set_text(c4z_btn_lbl[0], co4_z->depth > 0 ? "Zur\xC3\xBC" "ck" : "Abbrechen");
  if (err || co4_z->depth == 0) {
    lv_label_set_text(c4z_btn_lbl[1], err ? "Nochmals versuchen" : "Neu laden");
    show(c4z_btn[1]);
  } else {
    lv_label_set_text(c4z_btn_lbl[1], "Abbrechen");
    show(c4z_btn[1]);
  }
  if (ok && co4_z->n > 0) {
    char sig[64];
    snprintf(sig, sizeof(sig), "%u/%s/%u/%u/%d", co4_z->depth,
             co4_z->depth ? co4_z->path[co4_z->depth - 1].soid : "", co4_z->n, co4_z->total, v.zone_set);
    if (strcmp(sig, c4z_shown) != 0) {
      unreg_prefix("co4:z");
      reg("co4:zno", c4z_btn[0]);
      reg("co4:zretry", c4z_btn[1]);
      lv_obj_clean(c4z_list);
      for (int i = 0; i < co4_z->n; ++i) c4z_row(i);
      if (co4_z->total > co4_z->n) {
        lv_obj_t* more = make_label(c4z_list, &font_inter_14, lv_color_hex(kTextDim));
        lv_label_set_text_fmt(more, "Weitere %u Objekte nicht angezeigt", co4_z->total - co4_z->n);
      }
      lv_obj_scroll_to_y(c4z_list, 0, LV_ANIM_OFF);
      snprintf(c4z_shown, sizeof(c4z_shown), "%s", sig);
    }
    show(c4z_list);
    hide(c4z_msg);
    return;
  }
  hide(c4z_list);
  c4z_shown[0] = '\0';
  show(c4z_msg);
  if (err) {
    lv_label_set_text_fmt(c4z_msg, "Objekte konnten nicht geladen werden.\n%s", v.zones_err);
  } else if (ok) {
    lv_label_set_text(c4z_msg, co4_z->depth == 0 ? "Dein Co4-Konto zeigt keine Objekte."
                                                 : "Dieses Objekt enth\xC3\xA4lt keine Unterobjekte.");
  } else {
    lv_label_set_text(c4z_msg, "Lade Objekte \xE2\x80\xA6");
  }
}

void build_co4set_screen(lv_obj_t* screen) {
  co4set_root = make_screen_root(screen, "Co4 Energie");
  lv_obj_t* list = make_list(co4set_root, 56, 404);
  struct Row { const char* name; const char* icon; uint32_t color; const char* key; };
  const Row rows[kCo4SetRows] = {
      {"Co4-Konto", ICON_ROUTER, kAccent, "co4:acct"},
      {"Zone", ICON_PLACE, kGreen, "co4:zone"},
      {"Datenstatus", ICON_BOLT, kGreen, "co4:status"},  // status only: the dashboard opens from the home Energie tile
  };
  for (int i = 0; i < kCo4SetRows; ++i)
    make_value_row(list, 0, rows[i].name, rows[i].icon, rows[i].color, on_co4set_row, i, rows[i].key, &co4set_val[i]);
  co4_logout_row = make_row(list, 56, nullptr, 0, on_co4set_row, 3);
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
