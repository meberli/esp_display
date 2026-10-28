// Energie (whatwatt Go): Strom and Wasser pages (swipe), home chip and
// Einstellungen > Energie. Data from energy_task (core 0) via energy_get().

EnergyView* en_v = nullptr;  // PSRAM copy, UI task only
PvView* pv_v = nullptr;      // PSRAM copy, UI task only

// Today's PV production (inverter "Eac today", via the day history; 0 before the first reading of the day).
float pv_today_kwh(const PvView& p, int64_t now) {
  time_t t = static_cast<time_t>(now);
  struct tm lt;
  localtime_r(&t, &lt);
  const int32_t ymd = (lt.tm_year + 1900) * 10000 + (lt.tm_mon + 1) * 100 + lt.tm_mday;
  return p.hist.ymd == ymd && p.hist.today >= 0 ? p.hist.today : 0.0f;
}

// Strom
lv_obj_t* es_status = nullptr;
lv_obj_t* es_now_lbl = nullptr;
lv_obj_t* es_pill = nullptr;
lv_obj_t* es_pill_txt = nullptr;
lv_obj_t* es_big = nullptr;
lv_obj_t* es_unit = nullptr;
lv_obj_t* es_amps = nullptr;
lv_obj_t* es_val[3] = {};
lv_obj_t* es_name[3] = {};
lv_obj_t* es_price = nullptr;
lv_obj_t* es_hin[24] = {};
lv_obj_t* es_hout[24] = {};
lv_obj_t* es_hself[24] = {};  // PV used directly (hourly, under the grid import)
lv_obj_t* es_hleg = nullptr;
lv_obj_t* es_wk_tot = nullptr;
lv_obj_t* es_din[7] = {};
lv_obj_t* es_dout[7] = {};
lv_obj_t* es_dlbl[7] = {};
lv_obj_t* es_legend_out = nullptr;
// Wasser
lv_obj_t* ew_status = nullptr;
lv_obj_t* ew_big = nullptr;
lv_obj_t* ew_meter = nullptr;
lv_obj_t* ew_val[3] = {};
lv_obj_t* ew_name[3] = {};
lv_obj_t* ew_price = nullptr;
lv_obj_t* ew_hint = nullptr;
lv_obj_t* ew_wk_tot = nullptr;
lv_obj_t* ew_d[7] = {};
lv_obj_t* ew_dval[7] = {};
lv_obj_t* ew_dlbl[7] = {};
// Settings
constexpr int kEnRows = 18;
lv_obj_t* enset_val[kEnRows] = {};

constexpr int kEsBase = 150;    // Strom hourly bars: baseline inside card 1
constexpr int kEsUp = 38;       // import bar height max (stays below the es_amps line)
constexpr int kEsDown = 16;     // export bar height max
constexpr int kDayBase = 118;   // 7-day bars: baseline inside card 2
constexpr int kDayH = 74;


lv_obj_t* en_card(lv_obj_t* parent, int y, int h) {
  lv_obj_t* c = lv_obj_create(parent);
  lv_obj_set_pos(c, 24, y);
  lv_obj_set_size(c, 432, h);
  style_glass(c, 20);
  lv_obj_clear_flag(c, LV_OBJ_FLAG_CLICKABLE);
  return c;
}

lv_obj_t* en_lbl(lv_obj_t* parent, const lv_font_t* f, uint32_t color, int x, int y, const char* text) {
  lv_obj_t* l = make_label(parent, f, lv_color_hex(color));
  lv_obj_set_pos(l, x, y);
  if (text != nullptr) lv_label_set_text(l, text);
  return l;
}

// Day label under a 7-day bar group starting at x: one fixed 56 px slot
// centred on the bars, so the label lines up whatever the font width.
lv_obj_t* en_daylbl(lv_obj_t* c, int x, int y) {
  lv_obj_t* l = en_lbl(c, &font_inter_14, kTextDim, x - 10, y, "-");
  lv_obj_set_width(l, 56);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
  return l;
}

// Right column of the Strom / Wasser cards (x 244..414): name left, value
// right-aligned against the card edge. en_kv_fit() keeps the pair apart
// whatever the design font (Montserrat is wider than Inter) or value length:
// value in its own font if both fit, else the value drops to the name font,
// and only if that still does not fit the name is shortened with "...".
constexpr int kKvR = 414, kKvGap = 6;
constexpr int kKvXStrom = 236;   // separator at 228
constexpr int kKvXWasser = 212;  // separator at 204: longer names ("Kosten gestern")
lv_obj_t* en_kv_name(lv_obj_t* c, int x, int y, const char* text) {
  lv_obj_t* l = en_lbl(c, &font_inter_14, kTextDim, x, y, text);
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  return l;
}
void en_kv_val(lv_obj_t* v, int x, int y) {
  lv_obj_set_width(v, kKvR - x);
  lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
  lv_label_set_long_mode(v, LV_LABEL_LONG_CLIP);
  lv_obj_set_pos(v, x, y);
  lv_obj_set_user_data(v, const_cast<lv_font_t*>(lv_obj_get_style_text_font(v, LV_PART_MAIN)));
}
int en_text_w(lv_obj_t* l, const char* t) {
  lv_point_t sz;
  lv_txt_get_size(&sz, t, lv_obj_get_style_text_font(l, LV_PART_MAIN), lv_obj_get_style_text_letter_space(l, LV_PART_MAIN), 0,
                  LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  return sz.x;
}
void en_kv_fit(lv_obj_t* name, lv_obj_t* val) {
  const int need = en_text_w(name, lv_label_get_text(name));
  const int x = lv_obj_get_x(name);
  const lv_font_t* big = static_cast<const lv_font_t*>(lv_obj_get_user_data(val));
  if (big != nullptr) lv_obj_set_style_text_font(val, big, 0);
  int avail = kKvR - x - en_text_w(val, lv_label_get_text(val)) - kKvGap;
  if (need > avail) {
    lv_obj_set_style_text_font(val, lv_obj_get_style_text_font(name, LV_PART_MAIN), 0);
    avail = kKvR - x - en_text_w(val, lv_label_get_text(val)) - kKvGap;
  }
  lv_obj_set_width(name, need <= avail ? LV_SIZE_CONTENT : (avail > 24 ? avail : 24));
  lv_obj_move_foreground(val);
}

lv_obj_t* en_rect(lv_obj_t* parent, int x, int y, int w, int h, uint32_t color) {
  lv_obj_t* b = make_bar(parent, x, y, w, h, color, LV_OPA_COVER);
  lv_obj_set_style_radius(b, 2, 0);
  return b;
}

// Bar from a baseline: up (h > 0 grows upwards) or down.
void en_set_bar(lv_obj_t* b, int base, int h, bool up) {
  if (h < 1) {
    hide(b);
    return;
  }
  show(b);
  lv_obj_set_height(b, h);
  lv_obj_set_y(b, up ? base - h : base + 1);
}

void en_dots(lv_obj_t* root, int active) {
  for (int i = 0; i < 3; ++i) {
    lv_obj_t* d = make_bar(root, 220 + i * 16, 452, 8, 8, i == active ? kText : kTextDim, i == active ? LV_OPA_COVER : LV_OPA_50);
    (void)d;
  }
}

const char* en_wday(int32_t ymd) {
  static const char* k[7] = {"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};
  if (ymd <= 0) return "-";
  struct tm t = {};
  t.tm_year = ymd / 10000 - 1900;
  t.tm_mon = ymd / 100 % 100 - 1;
  t.tm_mday = ymd % 100;
  t.tm_hour = 12;
  t.tm_isdst = -1;
  mktime(&t);
  return k[t.tm_wday % 7];
}

void en_build_status(lv_obj_t* root, lv_obj_t** st) {
  *st = make_label(root, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_set_width(*st, 220);
  lv_obj_set_style_text_align(*st, LV_TEXT_ALIGN_RIGHT, 0);
  lv_label_set_long_mode(*st, LV_LABEL_LONG_DOT);
  lv_obj_set_pos(*st, 236, 25);
  header_note(*st);
}

// --- Bilanz (PV): Herkunft (PV vs Netz) and Verwendung (Eigenverbrauch vs Einspeisung) as donuts.
lv_obj_t* mx_status = nullptr;
lv_obj_t* mx_sum = nullptr;
lv_obj_t* mx_arc[2] = {};      // 0 Herkunft (indicator = PV), 1 Verwendung (indicator = Eigenverbrauch)
lv_obj_t* mx_pct[2] = {};
lv_obj_t* mx_leg[2][2] = {};
lv_obj_t* mx_wk = nullptr;
lv_obj_t* mx_dself[7] = {};
lv_obj_t* mx_dgrid[7] = {};
lv_obj_t* mx_dlbl[7] = {};
constexpr int kMxBase = 110;
constexpr int kMxH = 64;

lv_obj_t* mx_donut(lv_obj_t* parent, int x, int y, int d) {
  lv_obj_t* a = lv_arc_create(parent);
  lv_obj_set_size(a, d, d);
  lv_obj_set_pos(a, x, y);
  lv_arc_set_rotation(a, 270);
  lv_arc_set_bg_angles(a, 0, 360);
  lv_arc_set_range(a, 0, 1000);
  lv_arc_set_value(a, 0);
  lv_obj_remove_style(a, nullptr, LV_PART_KNOB);
  lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_pad_all(a, 0, 0);
  const lv_style_selector_t parts[2] = {LV_PART_MAIN, LV_PART_INDICATOR};
  for (lv_style_selector_t part : parts) {
    lv_obj_set_style_arc_width(a, 20, part);
    lv_obj_set_style_arc_rounded(a, false, part);
  }
  lv_obj_set_style_arc_color(a, lv_color_hex(kBorder), LV_PART_MAIN);
  return a;
}

void build_mix_screen(lv_obj_t* screen) {
  lv_obj_t* r = en_root[1] = make_screen_root(screen, "Bilanz");
  header_title_at(lv_obj_get_child(r, 0), 56, 20, 0);
  lv_obj_t* ic = make_label(r, &font_icon_bolt_24, lv_color_hex(kGreen));
  lv_label_set_text(ic, ICON_BOLT);
  lv_obj_set_pos(ic, 22, 19);
  en_build_status(r, &mx_status);
  lv_obj_t* c = en_card(r, 56, 226);
  en_lbl(c, &font_inter_sb16, kText, 18, 12, "Heute");
  mx_sum = en_lbl(c, &font_inter_14, kTextDim, 120, 14, "");
  lv_obj_set_width(mx_sum, 294);
  lv_obj_set_style_text_align(mx_sum, LV_TEXT_ALIGN_RIGHT, 0);
  lv_label_set_long_mode(mx_sum, LV_LABEL_LONG_DOT);
  const char* titles[2] = {"Herkunft", "Verwendung"};
  for (int i = 0; i < 2; ++i) {
    const int x0 = 18 + i * 216;
    lv_obj_t* t = en_lbl(c, &font_inter_14, kTextDim, x0, 40, titles[i]);
    lv_obj_set_width(t, 180);
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
    mx_arc[i] = mx_donut(c, x0 + 42, 62, 96);
    lv_obj_set_style_arc_color(mx_arc[i], lv_color_hex(kGreen), LV_PART_INDICATOR);
    mx_pct[i] = make_label(c, &font_inter_sb16, lv_color_hex(kText));
    lv_obj_set_width(mx_pct[i], 80);
    lv_obj_set_style_text_align(mx_pct[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(mx_pct[i], x0 + 50, 101);
    for (int k = 0; k < 2; ++k) {
      const uint32_t col = k == 0 ? kGreen : i == 0 ? kAmber : kAccent;
      mx_leg[i][k] = en_lbl(c, &font_inter_14, col, x0, 168 + k * 20, "");
      lv_obj_set_width(mx_leg[i][k], 180);
      lv_obj_set_style_text_align(mx_leg[i][k], LV_TEXT_ALIGN_CENTER, 0);
    }
  }
  make_bar(c, 215, 44, 1, 160, kBorder, LV_OPA_COVER);
  c = en_card(r, 292, 152);
  en_lbl(c, &font_inter_sb16, kText, 18, 12, "Verbrauch 7 Tage");
  mx_wk = en_lbl(c, &font_inter_14, kTextDim, 174, 14, "");
  lv_obj_set_width(mx_wk, 240);
  lv_obj_set_style_text_align(mx_wk, LV_TEXT_ALIGN_RIGHT, 0);
  for (int d = 0; d < 7; ++d) {
    const int x = 30 + d * 56;
    mx_dself[d] = en_rect(c, x + 4, kMxBase - 1, 28, 1, kGreen);
    mx_dgrid[d] = en_rect(c, x + 4, kMxBase - 1, 28, 1, kAmber);
    mx_dlbl[d] = en_daylbl(c, x, kMxBase + 4);
  }
  make_bar(c, 18, kMxBase, 396, 1, kBorder, LV_OPA_COVER);
  en_dots(r, 1);
}

void build_energy_screens(lv_obj_t* screen) {
  en_v = static_cast<EnergyView*>(heap_caps_calloc(1, sizeof(EnergyView), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  pv_v = static_cast<PvView*>(heap_caps_calloc(1, sizeof(PvView), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  // --- Strom
  lv_obj_t* r = en_root[0] = make_screen_root(screen, "Strom");
  header_title_at(lv_obj_get_child(r, 0), 56, 20, 0);
  lv_obj_t* ic = make_label(r, &font_icon_bolt_24, lv_color_hex(kAmber));
  lv_label_set_text(ic, ICON_BOLT);
  lv_obj_set_pos(ic, 22, 19);
  en_build_status(r, &es_status);
  lv_obj_t* c = en_card(r, 56, 206);
  es_now_lbl = en_lbl(c, &font_inter_14, kTextDim, 18, 14, "Bezug jetzt");
  es_pill = lv_obj_create(c);
  lv_obj_set_size(es_pill, LV_SIZE_CONTENT, 22);
  lv_obj_set_pos(es_pill, 128, 11);
  lv_obj_set_style_radius(es_pill, 11, 0);
  lv_obj_set_style_bg_opa(es_pill, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(es_pill, 1, 0);
  lv_obj_set_style_border_color(es_pill, lv_color_hex(kAccent), 0);
  lv_obj_set_style_pad_hor(es_pill, 8, 0);
  lv_obj_set_style_pad_ver(es_pill, 1, 0);
  lv_obj_clear_flag(es_pill, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(es_pill, LV_OBJ_FLAG_SCROLLABLE);
  es_pill_txt = make_label(es_pill, &font_inter_14, lv_color_hex(kAccent));
  lv_obj_center(es_pill_txt);
  es_big = en_lbl(c, &font_inter_light_40, kText, 16, 36, "-");
  es_unit = en_lbl(c, &font_inter_20, kTextDim, 0, 0, "W");
  es_amps = en_lbl(c, &font_inter_14, kTextDim, 18, 94, "");
  lv_obj_set_width(es_amps, 204);  // left column only (separator at x 228)
  lv_label_set_long_mode(es_amps, LV_LABEL_LONG_DOT);
  make_bar(c, 228, 14, 1, 106, kBorder, LV_OPA_COVER);
  const char* names[3] = {"Bezug heute", "Einspeisung", "Kosten netto"};
  const uint32_t colors[3] = {kAmber, kAccent, kText};
  for (int i = 0; i < 3; ++i) {
    es_name[i] = en_kv_name(c, kKvXStrom, 14 + i * 30, names[i]);
    es_val[i] = make_label(c, &font_inter_sb16, lv_color_hex(colors[i]));
    en_kv_val(es_val[i], kKvXStrom, 12 + i * 30);
  }
  es_price = en_lbl(c, &font_inter_14, kTextDim, kKvXStrom, 102, "");
  lv_obj_set_width(es_price, kKvR - kKvXStrom);
  lv_label_set_long_mode(es_price, LV_LABEL_LONG_DOT);
  for (int h = 0; h < 24; ++h) {
    es_hself[h] = en_rect(c, 22 + h * 16, kEsBase - 1, 11, 1, kGreen);
    es_hin[h] = en_rect(c, 22 + h * 16, kEsBase - 1, 11, 1, kAmber);
    es_hout[h] = en_rect(c, 22 + h * 16, kEsBase + 1, 11, 1, kAccent);
  }
  make_bar(c, 18, kEsBase, 396, 1, kBorder, LV_OPA_COVER);
  // Hour axis: 0 and 24 flush with the chart edges, 6/12/18 centred on their
  // tick; the legend fills the gap between 12 and 18 so it never meets them.
  const char* hn[5] = {"0", "6", "12", "18", "24"};
  for (int i = 0; i < 5; ++i) {
    lv_obj_t* l = en_lbl(c, &font_inter_14, kTextDim, 0, kEsBase + kEsDown + 6, hn[i]);
    lv_obj_set_width(l, 32);
    lv_obj_set_style_text_align(l, i == 0 ? LV_TEXT_ALIGN_LEFT : i == 4 ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_x(l, i == 0 ? 18 : i == 4 ? 414 - 32 : 22 + i * 96 - 16);
  }
  es_hleg = en_lbl(c, &font_inter_14, kGreen, 0, kEsBase + kEsDown + 6, "PV direkt");
  lv_obj_set_width(es_hleg, 64);
  lv_obj_set_style_text_align(es_hleg, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(es_hleg, LV_LABEL_LONG_CLIP);
  lv_obj_set_x(es_hleg, 22 + 2 * 96 + 16);  // 230..294: between "12" and "18"
  c = en_card(r, 272, 172);
  en_lbl(c, &font_inter_sb16, kText, 18, 12, "Letzte 7 Tage");
  es_wk_tot = en_lbl(c, &font_inter_14, kTextDim, 0, 14, "");
  lv_obj_set_width(es_wk_tot, 260);
  lv_obj_set_style_text_align(es_wk_tot, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_x(es_wk_tot, 154);
  for (int d = 0; d < 7; ++d) {
    int x = 30 + d * 56;
    es_din[d] = en_rect(c, x, kDayBase - 1, 16, 1, kAmber);
    es_dout[d] = en_rect(c, x + 19, kDayBase - 1, 16, 1, kAccent);
    es_dlbl[d] = en_daylbl(c, x, kDayBase + 4);
  }
  make_bar(c, 18, kDayBase, 396, 1, kBorder, LV_OPA_COVER);
  en_lbl(c, &font_inter_14, kAmber, 18, 146, "Bezug");
  es_legend_out = en_lbl(c, &font_inter_14, kAccent, 80, 146, "Einspeisung");
  en_dots(r, 0);

  // --- Wasser
  build_mix_screen(screen);
  r = en_root[2] = make_screen_root(screen, "Wasser");
  header_title_at(lv_obj_get_child(r, 0), 56, 20, 0);
  ic = make_label(r, &font_icons_24, lv_color_hex(kAccent));
  lv_label_set_text(ic, ICON_DROP);
  lv_obj_set_pos(ic, 22, 19);
  en_build_status(r, &ew_status);
  c = en_card(r, 56, 206);
  en_lbl(c, &font_inter_14, kTextDim, 18, 14, "Wasser gestern");
  ew_big = en_lbl(c, &font_inter_light_40, kText, 16, 36, "-");
  en_lbl(c, &font_inter_20, kTextDim, 0, 0, "Liter");
  ew_meter = en_lbl(c, &font_inter_14, kTextDim, 18, 94, "");
  lv_obj_set_width(ew_meter, kKvXWasser - 8 - 8 - 18);
  lv_label_set_long_mode(ew_meter, LV_LABEL_LONG_WRAP);  // two lines fit above the separator at 132
  make_bar(c, kKvXWasser - 8, 14, 1, 106, kBorder, LV_OPA_COVER);
  const char* wn[3] = {"Kosten gestern", "\xC3\x98 7 Tage", "Vorgestern"};
  for (int i = 0; i < 3; ++i) {
    ew_name[i] = en_kv_name(c, kKvXWasser, 14 + i * 30, wn[i]);
    ew_val[i] = make_label(c, &font_inter_sb16, lv_color_hex(i == 2 ? kText : kAccent));
    en_kv_val(ew_val[i], kKvXWasser, 12 + i * 30);
  }
  ew_price = en_lbl(c, &font_inter_14, kTextDim, kKvXWasser, 102, "");
  lv_obj_set_width(ew_price, kKvR - kKvXWasser);
  lv_label_set_long_mode(ew_price, LV_LABEL_LONG_DOT);
  // No hourly bars: the water meter reports its total once a day.
  make_bar(c, 18, 132, 396, 1, kBorder, LV_OPA_COVER);
  ew_hint = en_lbl(c, &font_inter_14, kTextDim, 18, 144,
                   "Der Wasserz\xC3\xA4hler meldet einmal t\xC3\xA4glich.\nDer Verbrauch z\xC3\xA4hlt jeweils f\xC3\xBCr den Vortag.");
  lv_obj_set_width(ew_hint, 396);
  c = en_card(r, 272, 172);
  en_lbl(c, &font_inter_sb16, kText, 18, 12, "Letzte 7 Tage");
  ew_wk_tot = en_lbl(c, &font_inter_14, kTextDim, 154, 14, "");
  lv_obj_set_width(ew_wk_tot, 260);
  lv_obj_set_style_text_align(ew_wk_tot, LV_TEXT_ALIGN_RIGHT, 0);
  for (int d = 0; d < 7; ++d) {
    int x = 30 + d * 56;
    ew_d[d] = en_rect(c, x + 6, kDayBase + 18 - 1, 24, 1, kAccent);
    ew_dval[d] = en_lbl(c, &font_inter_14, kTextDim, x - 4, 0, "");
    lv_obj_set_width(ew_dval[d], 44);
    lv_obj_set_style_text_align(ew_dval[d], LV_TEXT_ALIGN_CENTER, 0);
    ew_dlbl[d] = en_daylbl(c, x, kDayBase + 22);
  }
  make_bar(c, 18, kDayBase + 18, 396, 1, kBorder, LV_OPA_COVER);
  en_dots(r, 2);
}

void en_fmt_kwh(char* out, size_t n, float v, bool approx) {
  snprintf(out, n, "%s%.1f kWh", approx ? "~" : "", v < 0 ? 0.0f : v);
}

void en_status_text(lv_obj_t* st) {
  const EnergyView& v = *en_v;
  const int64_t now = time(nullptr);
  if (!v.enabled) {
    lv_label_set_text(st, "nicht eingerichtet");
    lv_obj_set_style_text_color(st, lv_color_hex(kTextDim), 0);
    header_note(st);
  } else if (v.stale) {
    if (v.have) lv_label_set_text_fmt(st, "veraltet \xC2\xB7 %s", v.error[0] ? v.error : "keine Daten");
    else lv_label_set_text_fmt(st, "%s", v.error[0] ? v.error : "verbinde ...");
    lv_obj_set_style_text_color(st, lv_color_hex(kAmber), 0);
  } else {
    char hm[8];
    fmt_hm(hm, sizeof(hm), v.fetched);
    lv_label_set_text_fmt(st, "%s \xC2\xB7 vor %d s", hm, static_cast<int>(now - v.fetched));
    lv_obj_set_style_text_color(st, lv_color_hex(kTextDim), 0);
    header_note(st);
  }
}

void energy_refresh_strom() {
  const EnergyView& v = *en_v;
  const EnergyHist& h = v.h;
  const int64_t now = time(nullptr);
  char b[48];
  en_status_text(es_status);
  const bool live = v.have && !v.stale;
  const float net_w = (v.r.p_in_kw - v.r.p_out_kw) * 1000.0f;
  const bool exporting = live && net_w < -0.5f;
  lv_label_set_text(es_now_lbl, exporting ? "Einspeisung jetzt" : "Bezug jetzt");
  if (live) lv_label_set_text_fmt(es_big, "%d", static_cast<int>(fabsf(net_w) + 0.5f));
  else lv_label_set_text(es_big, "-");
  lv_obj_set_style_text_color(es_big, lv_color_hex(exporting ? kGreen : kText), 0);
  lv_obj_align_to(es_unit, es_big, LV_ALIGN_OUT_RIGHT_BOTTOM, 6, -7);
  TariffSlot slot = tariff_slot(v.tariff, now);
  lv_label_set_text(es_pill_txt, slot == kSlotHt ? "HT \xC2\xB7 T1" : slot == kSlotNt ? "NT \xC2\xB7 T2" : "Einheitstarif");
  lv_obj_set_x(es_pill, exporting ? 150 : 106);
  pv_get(pv_v);
  if (pv_v->enabled) {
    const float pv_today = pv_today_kwh(*pv_v, now);
    // PV replaces the phase currents: production, today and the house load (grid net + PV AC).
    const PvView& p = *pv_v;
    lv_obj_set_style_text_color(es_amps, lv_color_hex(p.have && !p.stale ? kGreen : kTextDim), 0);
    if (p.have && !p.stale && live)
      snprintf(b, sizeof(b), "PV %.0f W \xC2\xB7 %.1f kWh \xC2\xB7 Haus %.0f W", p.r.ac_w, pv_today,
               energy_house_w(net_w, p.r.ac_w));
    else if (p.have && !p.stale)
      snprintf(b, sizeof(b), "PV %.0f W \xC2\xB7 heute %.1f kWh", p.r.ac_w, pv_today);
    else if (p.asleep && live)  // inverter off (night): it produces nothing, house = grid
      snprintf(b, sizeof(b), "PV schl\xC3\xA4" "ft \xC2\xB7 %.1f kWh \xC2\xB7 Haus %.0f W", pv_today, energy_house_w(net_w, 0));
    else if (p.asleep)
      snprintf(b, sizeof(b), "PV schl\xC3\xA4" "ft \xC2\xB7 heute %.1f kWh", pv_today);
    else if (p.have)
      snprintf(b, sizeof(b), "PV veraltet \xC2\xB7 %.1f kWh", pv_today);
    else
      snprintf(b, sizeof(b), "PV: %s", p.down ? "Gateway offline" : p.connected ? "warte auf Daten" : "verbinde");
    lv_label_set_text(es_amps, b);
  } else if (live && v.r.i_a[0] >= 0) {
    lv_obj_set_style_text_color(es_amps, lv_color_hex(kTextDim), 0);
    snprintf(b, sizeof(b), "L1 %.1f A \xC2\xB7 L2 %.1f A \xC2\xB7 L3 %.1f A", v.r.i_a[0], v.r.i_a[1], v.r.i_a[2]);
    lv_label_set_text(es_amps, b);
  } else {
    lv_label_set_text(es_amps, "");
  }
  const bool approx = (h.flags & kDayApprox) != 0;
  en_fmt_kwh(b, sizeof(b), energy_today_in(h), approx);
  lv_label_set_text(es_val[0], h.have ? b : "-");
  en_fmt_kwh(b, sizeof(b), energy_today_out(h), approx);
  lv_label_set_text(es_val[1], h.have && v.show_exp ? b : "-");
  float net = h.cost - (v.show_exp ? h.credit : 0.0f);
  snprintf(b, sizeof(b), "%sCHF %.2f", net < 0 ? "-" : "", fabsf(net));
  lv_label_set_text(es_val[2], h.have ? b : "-");
  for (int i = 0; i < 3; ++i) en_kv_fit(es_name[i], es_val[i]);
  char p1[12], p2[12];
  energy_format_price(p1, sizeof(p1), tariff_price(v.tariff, now));
  energy_format_price(p2, sizeof(p2), v.tariff.feed);
  if (v.show_exp) lv_label_set_text_fmt(es_price, "%s Rp. / Einsp. %s", p1, p2);
  else lv_label_set_text_fmt(es_price, "%s Rp./kWh", p1);
  // Hourly bars (today): up = consumption = PV used directly (green) + grid import (amber),
  // down = export. PV per hour = integrated inverter AC power (scaled to Eac); hours
  // without a known PV value show the import only. Scaled to the largest hour (min 0.5 kWh).
  float hself[24] = {};
  bool hknown[24] = {};
  const bool hpv = pv_v->enabled && h.have && pv_v->hours.ymd == h.ymd;
  if (hpv) {
    float ph[24];
    bool cov[24];
    const float eac = pv_v->hist.ymd == h.ymd && pv_v->hist.fresh ? pv_v->hist.today : -1.0f;
    pv_hours_scaled(pv_v->hours, eac, ph, cov);
    pv_hours_known(pv_v->hours, cov, hknown);
    for (int i = 0; i < 24; ++i) {
      if (!cov[i] && h.h_out[i] > 0.005f) hknown[i] = false;  // exporting without PV samples: unknown
      if (hknown[i]) hself[i] = energy_mix(h.h_in[i], h.h_out[i], ph[i]).self_kwh;
    }
  }
  float mx = 0.5f, mo = 0.5f;
  for (int i = 0; i < 24; ++i) {
    if (h.h_in[i] + hself[i] > mx) mx = h.h_in[i] + hself[i];
    if (h.h_out[i] > mo) mo = h.h_out[i];
  }
  for (int i = 0; i < 24; ++i) {
    const int hs = h.have ? static_cast<int>(hself[i] / mx * kEsUp + 0.5f) : 0;
    en_set_bar(es_hself[i], kEsBase, hs, true);
    en_set_bar(es_hin[i], kEsBase - hs, h.have ? static_cast<int>(h.h_in[i] / mx * kEsUp + 0.5f) : 0, true);
    en_set_bar(es_hout[i], kEsBase, h.have && v.show_exp ? static_cast<int>(h.h_out[i] / mo * kEsDown + 0.5f) : 0, false);
  }
  if (hpv) show(es_hleg);
  else hide(es_hleg);
  // 7 days: [0..5] = days[5..0], [6] = today.
  float din[7], dout[7], tin = 0, tout = 0, dmax = 1;
  int32_t ymd[7];
  uint8_t fl[7];
  for (int d = 0; d < 7; ++d) {
    if (d == 6) {
      din[d] = energy_today_in(h);
      dout[d] = energy_today_out(h);
      ymd[d] = h.have ? h.ymd : 0;
      fl[d] = h.flags;
    } else {
      const EnergyDay& e = h.days[5 - d];
      din[d] = e.ymd ? e.in_kwh : 0;
      dout[d] = e.ymd ? e.out_kwh : 0;
      ymd[d] = e.ymd;
      fl[d] = e.flags;
    }
    tin += din[d];
    tout += dout[d];
    if (din[d] > dmax) dmax = din[d];
    if (v.show_exp && dout[d] > dmax) dmax = dout[d];
  }
  for (int d = 0; d < 7; ++d) {
    en_set_bar(es_din[d], kDayBase, static_cast<int>(din[d] / dmax * kDayH + 0.5f), true);
    en_set_bar(es_dout[d], kDayBase, v.show_exp ? static_cast<int>(dout[d] / dmax * kDayH + 0.5f) : 0, true);
    lv_label_set_text_fmt(es_dlbl[d], "%s%s", d == 6 ? "Heute" : en_wday(ymd[d]), (fl[d] & kDayApprox) && ymd[d] ? "*" : "");
    lv_obj_set_style_text_color(es_dlbl[d], lv_color_hex(d == 6 ? kText : kTextDim), 0);
  }
  // LVGL's printf has no floats: format with snprintf.
  if (v.show_exp) snprintf(b, sizeof(b), "%.0f kWh \xC2\xB7 %.0f kWh Einsp.", tin, tout);
  else snprintf(b, sizeof(b), "%.0f kWh", tin);
  lv_label_set_text(es_wk_tot, b);
  if (v.show_exp) show(es_legend_out);
  else hide(es_legend_out);
}

void energy_refresh_wasser() {
  const EnergyView& v = *en_v;
  const EnergyHist& h = v.h;
  char b[48];
  en_status_text(ew_status);
  uint8_t yfl = 0;
  const float yest = v.water_on ? energy_yesterday_water_l(h, &yfl) : -1;
  const bool have = yest >= 0;
  if (have) lv_label_set_text_fmt(ew_big, "%s%d", (yfl & kDayWaterApprox) ? "~" : "", static_cast<int>(yest + 0.5f));
  else lv_label_set_text(ew_big, "-");
  lv_obj_t* unit = lv_obj_get_child(lv_obj_get_parent(ew_big), 2);
  lv_obj_align_to(unit, ew_big, LV_ALIGN_OUT_RIGHT_BOTTOM, 6, -7);
  if (!v.water_on) {
    lv_label_set_text(ew_meter, "Wasser ist ausgeschaltet");
  } else if (v.have && v.r.has_water) {
    snprintf(b, sizeof(b), "Z\xC3\xA4hlerstand %.3f m\xC2\xB3", v.r.water_m3);
    lv_label_set_text(ew_meter, b);
  } else {
    lv_label_set_text(ew_meter, "kein Wasserz\xC3\xA4hler gemeldet");
  }
  // 7 finished days, yesterday rightmost (days[0]).
  float d[7], tot = 0, dmax = 50;
  int32_t ymd[7];
  uint8_t fl[7];
  int n7 = 0;
  float sum = 0;
  for (int i = 0; i < 7; ++i) {
    const EnergyDay& e = h.days[6 - i];
    d[i] = (v.water_on && e.ymd) ? e.water_l : 0;
    ymd[i] = e.ymd;
    fl[i] = e.flags;
    if (e.ymd && !(e.flags & kDayWaterApprox)) {
      ++n7;
      sum += e.water_l;
    }
    tot += d[i];
    if (d[i] > dmax) dmax = d[i];
  }
  if (have) snprintf(b, sizeof(b), "CHF %.2f", yest / 1000.0f * v.tariff.water / 100.0f);
  else snprintf(b, sizeof(b), "-");
  lv_label_set_text(ew_val[0], b);
  if (n7 > 0) snprintf(b, sizeof(b), "%.0f L", sum / n7);
  else snprintf(b, sizeof(b), "-");
  lv_label_set_text(ew_val[1], b);
  const EnergyDay& y2 = h.days[1];
  if (v.water_on && y2.ymd) snprintf(b, sizeof(b), "%s%.0f L", (y2.flags & kDayWaterApprox) ? "~" : "", y2.water_l);
  else snprintf(b, sizeof(b), "-");
  lv_label_set_text(ew_val[2], b);
  for (int i = 0; i < 3; ++i) en_kv_fit(ew_name[i], ew_val[i]);
  char p[12];
  energy_format_price(p, sizeof(p), v.tariff.water);
  lv_label_set_text_fmt(ew_price, "%s CHF/m\xC2\xB3 mit Abwasser", p);
  for (int i = 0; i < 7; ++i) {
    int bh = static_cast<int>(d[i] / dmax * (kDayH - 22) + 0.5f);
    en_set_bar(ew_d[i], kDayBase + 18, bh, true);
    if (ymd[i] && d[i] > 0) snprintf(b, sizeof(b), "%.0f", d[i]);
    else b[0] = '\0';
    lv_label_set_text(ew_dval[i], b);
    lv_obj_set_y(ew_dval[i], kDayBase + 18 - bh - 19);
    const bool yday = i == 6 && ymd[i] && ymd[i] == energy_prev_ymd(h.ymd);
    lv_label_set_text_fmt(ew_dlbl[i], "%s%s", yday ? "Gestern" : en_wday(ymd[i]),
                          (fl[i] & kDayWaterApprox) && ymd[i] ? "*" : "");
    lv_obj_set_style_text_color(ew_dlbl[i], lv_color_hex(yday ? kText : kTextDim), 0);
  }
  snprintf(b, sizeof(b), "%.2f m\xC2\xB3 \xC2\xB7 CHF %.2f", tot / 1000.0f, tot / 1000.0f * v.tariff.water / 100.0f);
  lv_label_set_text(ew_wk_tot, b);
}

// PV production of a day: today from the inverter counter, earlier days from the PV history; -1 = unknown.
float mix_pv_day(const PvView& p, int32_t ymd, bool today, int64_t now) {
  if (!p.enabled) return -1;
  if (today) return pv_today_kwh(p, now);
  return pv_hist_day(p.hist, ymd);
}

void energy_refresh_mix() {
  const EnergyView& v = *en_v;
  const EnergyHist& h = v.h;
  const int64_t now = time(nullptr);
  char b[64];
  en_status_text(mx_status);
  pv_get(pv_v);
  const PvView& p = *pv_v;
  const EnergyMix m = energy_mix(energy_today_in(h), energy_today_out(h), mix_pv_day(p, h.ymd, true, now));
  const bool ok = h.have && m.valid;
  const bool approx = (h.flags & kDayApprox) != 0;
  if (!h.have) snprintf(b, sizeof(b), "keine Z\xC3\xA4hlerdaten");
  else if (!p.enabled) snprintf(b, sizeof(b), "PV nicht eingerichtet");
  else if (m.inconsistent) snprintf(b, sizeof(b), "PV-Daten unvollst\xC3\xA4ndig");
  // Approx: the meter baseline was taken mid-day (first reading today) while the inverter counts
  // the whole day, so the PV share is overstated: say so instead of a plain total.
  else if (approx) snprintf(b, sizeof(b), "~%.1f kWh \xC2\xB7 Z\xC3\xA4hler erst ab heute", m.use_kwh);
  else snprintf(b, sizeof(b), "Verbrauch %.1f kWh", m.use_kwh);
  lv_label_set_text(mx_sum, b);
  lv_obj_set_style_text_color(mx_sum, lv_color_hex(ok && !m.inconsistent && !approx ? kText : kAmber), 0);
  // Herkunft: PV (indicator) vs Netz (rest); Verwendung: Eigenverbrauch (indicator) vs Einspeisung.
  const bool has[2] = {ok && m.use_kwh > 0.005f, ok && m.self_kwh + m.export_kwh > 0.005f};
  const float share[2] = {m.pv_share, m.self_share};
  const uint32_t rest[2] = {kAmber, kAccent};
  for (int i = 0; i < 2; ++i) {
    lv_arc_set_value(mx_arc[i], has[i] ? static_cast<int16_t>(share[i] * 1000 + 0.5f) : 0);
    lv_obj_set_style_arc_color(mx_arc[i], lv_color_hex(has[i] ? rest[i] : kBorder), LV_PART_MAIN);
    if (has[i]) snprintf(b, sizeof(b), "%d %%", static_cast<int>(share[i] * 100 + 0.5f));
    else snprintf(b, sizeof(b), "-");
    lv_label_set_text(mx_pct[i], b);
  }
  const int pv_pct = static_cast<int>(m.pv_share * 100 + 0.5f), self_pct = static_cast<int>(m.self_share * 100 + 0.5f);
  if (has[0]) {
    snprintf(b, sizeof(b), "PV %.1f kWh \xC2\xB7 %d %%", m.self_kwh, pv_pct);
    lv_label_set_text(mx_leg[0][0], b);
    snprintf(b, sizeof(b), "Netz %.1f kWh \xC2\xB7 %d %%", m.import_kwh, 100 - pv_pct);
    lv_label_set_text(mx_leg[0][1], b);
  } else {
    lv_label_set_text(mx_leg[0][0], "PV -");
    lv_label_set_text(mx_leg[0][1], "Netz -");
  }
  if (has[1]) {
    snprintf(b, sizeof(b), "Eigen %.1f kWh \xC2\xB7 %d %%", m.self_kwh, self_pct);
    lv_label_set_text(mx_leg[1][0], b);
    snprintf(b, sizeof(b), "Einsp. %.1f kWh \xC2\xB7 %d %%", m.export_kwh, 100 - self_pct);
    lv_label_set_text(mx_leg[1][1], b);
  } else {
    lv_label_set_text(mx_leg[1][0], ok ? "noch keine PV-Erzeugung" : "Eigen -");
    lv_label_set_text(mx_leg[1][1], ok ? "" : "Einsp. -");
  }
  // 7 days stacked: PV direkt (green, bottom) + Netz (amber); grey = PV unknown that day.
  EnergyMix dm[7];
  int32_t ymd[7];
  float dmax = 1, su = 0, ss = 0;
  for (int d = 0; d < 7; ++d) {
    if (d == 6) {
      dm[d] = h.have ? m : EnergyMix{};
      ymd[d] = h.have ? h.ymd : 0;
    } else {
      const EnergyDay& e = h.days[5 - d];
      ymd[d] = e.ymd;
      dm[d] = e.ymd ? energy_mix(e.in_kwh, e.out_kwh, mix_pv_day(p, e.ymd, false, now)) : EnergyMix{};
      if (e.ymd && !dm[d].valid) dm[d].use_kwh = dm[d].import_kwh;  // grid only, PV unknown
    }
    const float u = dm[d].use_kwh;
    if (u > dmax) dmax = u;
    if (dm[d].valid) {
      su += dm[d].use_kwh;
      ss += dm[d].self_kwh;
    }
  }
  for (int d = 0; d < 7; ++d) {
    const int hs = dm[d].valid ? static_cast<int>(dm[d].self_kwh / dmax * kMxH + 0.5f) : 0;
    const int hg = static_cast<int>(dm[d].import_kwh / dmax * kMxH + 0.5f);
    en_set_bar(mx_dself[d], kMxBase, hs, true);
    en_set_bar(mx_dgrid[d], kMxBase - hs, hg, true);
    lv_obj_set_style_bg_color(mx_dgrid[d], lv_color_hex(dm[d].valid ? kAmber : kTextDim), 0);
    const uint8_t dfl = d == 6 ? h.flags : h.days[5 - d].flags;
    lv_label_set_text_fmt(mx_dlbl[d], "%s%s", d == 6 ? "Heute" : en_wday(ymd[d]), (dfl & kDayApprox) && ymd[d] ? "*" : "");
    lv_obj_set_style_text_color(mx_dlbl[d], lv_color_hex(d == 6 ? kText : kTextDim), 0);
  }
  if (su > 0.005f) snprintf(b, sizeof(b), "%.0f kWh \xC2\xB7 PV-Anteil %d %%", su, static_cast<int>(ss / su * 100 + 0.5f));
  else snprintf(b, sizeof(b), "-");
  lv_label_set_text(mx_wk, b);
}

void energy_chip_refresh() {
  if (chip_en_main == nullptr || en_v == nullptr) return;
  energy_get(en_v);
  const EnergyView& v = *en_v;
  pv_get(pv_v);
  if (pv_v->enabled && pv_v->have && !pv_v->stale) lv_label_set_text_fmt(chip_en_top, "PV %d W", static_cast<int>(pv_v->r.ac_w + 0.5f));
  else lv_label_set_text(chip_en_top, "Energie");
  if (!v.enabled) {
    lv_label_set_text(chip_en_main, "einrichten");
    return;
  }
  char w[16] = "- W";
  if (v.have && !v.stale) {
    float net = (v.r.p_in_kw - v.r.p_out_kw) * 1000.0f;
    snprintf(w, sizeof(w), "%d W", static_cast<int>(net < 0 ? net - 0.5f : net + 0.5f));
  }
  lv_label_set_text(chip_en_main, w);
  // Second line: yesterday's water (the meter reports once a day); longest form that fits.
  if (chip_en_sub != nullptr) {
    const float yl = v.water_on ? energy_yesterday_water_l(v.h) : -1;
    if (yl < 0) {
      lv_label_set_text(chip_en_sub, "");
    } else {
      const int L = static_cast<int>(yl + 0.5f);
      char t[3][32];
      snprintf(t[0], sizeof(t[0]), "Wasser gestern: %d L", L);
      snprintf(t[1], sizeof(t[1]), "Wasser gest. %d L", L);
      snprintf(t[2], sizeof(t[2]), "gestern %d L", L);
      int pick = 2;
      for (int k = 0; k < 2; ++k) {
        lv_point_t sz;
        lv_txt_get_size(&sz, t[k], &font_inter_14, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (sz.x <= lv_obj_get_width(chip_en_sub)) { pick = k; break; }
      }
      lv_label_set_text(chip_en_sub, t[pick]);
    }
  }
}

void energy_refresh() {
  if (en_v == nullptr) return;
  energy_get(en_v);
  if (en_page == 0) energy_refresh_strom();
  else if (en_page == 1) energy_refresh_mix();
  else energy_refresh_wasser();
}

void energy_enter(int page) {
  if (message_open || en_root[0] == nullptr) return;
  en_page = page < 0 ? 0 : page > 2 ? 2 : page;
  enter_view(Nav::Energy, en_root[en_page]);
  energy_refresh();
}

void energy_swipe(int dir) {
  int next = en_page + dir;
  if (next > 2) {
    co4_enter(0);  // after Wasser: Co4 Energie
    return;
  }
  if (next < 0) return;
  energy_enter(next);
}

// --- Einstellungen > Energie -------------------------------------------------------

const char* const kEnModel[3] = {"Automatisch", "Einheitstarif", "HT/NT"};
const uint16_t kEnPoll[4] = {5, 10, 30, 60};
// Row -> text setting (or -1: stepped/toggled in place)
const int8_t kEnRowText[kEnRows] = {-1, kTextEnHost, -1, -1, kTextEnFlat, kTextEnHt, kTextEnNt, kTextEnHtWin,
                                    kTextEnSwitch, kTextEnFeed, -1, -1, -1, kTextWaPrice, -1, kTextPvGw, kTextPvGwPort, kTextPvUnit};

void enset_refresh() {
  if (enset_root == nullptr) return;
  EnergySettings s = settings_load_energy();
  energy_get(en_v);
  char t[64], b[96];
  lv_label_set_text(enset_val[0], s.on ? "Ein" : "Aus");
  if (s.host[0] == '\0') snprintf(b, sizeof(b), "nicht gesetzt (IP-Adresse des whatwatt Go)");
  else if (en_v->have && !en_v->stale) snprintf(b, sizeof(b), "%s \xC2\xB7 verbunden", s.host);
  else snprintf(b, sizeof(b), "%s \xC2\xB7 %s", s.host, en_v->error[0] ? en_v->error : "keine Daten");
  lv_label_set_text(enset_val[1], b);
  lv_label_set_text_fmt(enset_val[2], "alle %u s", s.poll_s);
  char sw[16];
  settings_load_text(kTextEnSwitch, sw, sizeof(sw), nullptr);
  if (s.model == kTariffAuto) lv_label_set_text_fmt(enset_val[3], "Automatisch (HT/NT ab %s)", sw);
  else lv_label_set_text(enset_val[3], kEnModel[s.model]);
  for (int i = 4; i < kEnRows; ++i) {
    if (kEnRowText[i] < 0) continue;
    bool own = false;
    settings_load_text(static_cast<TextSetting>(kEnRowText[i]), t, sizeof(t), &own);
    const char* unit = i == 13 ? " CHF/m\xC2\xB3" : (i >= 4 && i <= 6) || i == 9 ? " Rp./kWh" : "";
    if (i >= 15) unit = i == 15 ? " (Modbus TCP, nur lesen)" : "";
    snprintf(b, sizeof(b), "%s%s%s", t, unit, own ? "" : " (Standard)");
    lv_label_set_text(enset_val[i], b);
  }
  lv_label_set_text(enset_val[10], s.show_exp ? "Ein" : "Aus");
  lv_label_set_text(enset_val[11], s.water_on ? "Ein" : "Aus");
  lv_label_set_text_fmt(enset_val[12], "M-Bus %u", s.water_bus);
  pv_get(pv_v);
  if (!settings_load_pv_on()) snprintf(b, sizeof(b), "Aus");
  else if (pv_v->have && !pv_v->stale) snprintf(b, sizeof(b), "Ein \xC2\xB7 %.0f W \xC2\xB7 %u Abfragen ok", pv_v->r.ac_w, (unsigned)pv_v->ok);
  else if (pv_v->asleep) snprintf(b, sizeof(b), "Ein \xC2\xB7 Wechselrichter schl\xC3\xA4" "ft");
  else snprintf(b, sizeof(b), "Ein \xC2\xB7 %s", pv_v->error[0] ? pv_v->error : pv_v->connected ? "Gateway verbunden" : "verbinde");
  lv_label_set_text(enset_val[14], b);
}

void enset_summary() {
  if (set_en_val == nullptr) return;
  EnergySettings s = settings_load_energy();
  if (!s.on) lv_label_set_text(set_en_val, "Aus");
  else if (s.host[0] == '\0') lv_label_set_text(set_en_val, "whatwatt Go nicht eingerichtet");
  else lv_label_set_text_fmt(set_en_val, "whatwatt Go %s", s.host);
}

void open_enset() {
  set_group = 3;  // back goes to Energie-Quellen
  nav = Nav::EnSet;
  hide_settings();
  enset_refresh();
  show(enset_root);
}

void on_enset_row(lv_event_t* e) {
  if (gesture_consumed) return;
  int row = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  console_printf("Activate enset %d\n", row);
  if (kEnRowText[row] >= 0) {
    open_text(static_cast<uint8_t>(kEnRowText[row]), Nav::EnSet);
    return;
  }
  EnergySettings s = settings_load_energy();
  if (row == 0) s.on = !s.on;
  if (row == 2) {
    int i = 0;
    while (i < 4 && kEnPoll[i] != s.poll_s) ++i;
    s.poll_s = kEnPoll[(i + 1) % 4];
  }
  if (row == 3) s.model = static_cast<uint8_t>((s.model + 1) % 3);
  if (row == 10) s.show_exp = !s.show_exp;
  if (row == 11) s.water_on = !s.water_on;
  if (row == 12) s.water_bus = static_cast<uint8_t>(s.water_bus % 4 + 1);
  if (row == 14) {
    bool on = !settings_load_pv_on();
    console_printf("PV settings on=%d saved=%d\n", on, settings_save_pv_on(on) ? 1 : 0);
    pv_kick();
    enset_refresh();
    return;
  }
  bool ok = settings_save_energy_flags(s);
  console_printf("Energie settings on=%d poll=%u model=%u exp=%d water=%d bus=%u saved=%d\n", s.on, s.poll_s, s.model,
                 s.show_exp, s.water_on, s.water_bus, ok ? 1 : 0);
  energy_kick();
  enset_refresh();
}

void build_enset_screen(lv_obj_t* screen) {
  enset_root = make_screen_root(screen, "Energie");
  lv_obj_t* list = make_list(enset_root, 56, 404);
  struct Row { const char* name; const char* icon; uint32_t color; const char* key; };
  const Row rows[kEnRows] = {
      {"Energie anzeigen", ICON_BOLT, kAmber, "en:on"},
      {"whatwatt-Adresse", ICON_ROUTER, kAccent, "en:host"},
      {"Abfrage-Intervall", ICON_SCHEDULE, kGreen, "en:poll"},
      {"Tarifmodell", ICON_SCHEDULE, kAmber, "en:model"},
      {"Einheitstarif", ICON_BOLT, kAmber, "en:flat"},
      {"Hochtarif (HT)", ICON_BOLT, kAmber, "en:ht"},
      {"Niedertarif (NT)", ICON_BOLT, kAccent, "en:nt"},
      {"HT-Zeiten Mo-Fr", ICON_SCHEDULE, kAmber, "en:htwin"},
      {"HT/NT ab (Automatisch)", ICON_CALENDAR, kPurple, "en:switch"},
      {"Einspeiseverg\xC3\xBCtung", ICON_BOLT, kGreen, "en:feed"},
      {"Einspeisung anzeigen", ICON_BOLT, kGreen, "en:exp"},
      {"Wasser anzeigen", ICON_DROP, kAccent, "wa:on"},
      {"Wasserz\xC3\xA4hler", ICON_DROP, kAccent, "wa:bus"},
      {"Wasserpreis inkl. Abwasser", ICON_DROP, kAccent, "wa:price"},
      {"PV (Growatt) lesen", ICON_BOLT, kGreen, "pv:on"},
      {"PV: Modbus-Gateway (IP)", ICON_ROUTER, kGreen, "pv:ip"},
      {"PV: Gateway-Port", ICON_ROUTER, kGreen, "pv:port"},
      {"PV: Unit-ID", ICON_ROUTER, kGreen, "pv:unit"},
  };
  for (int i = 0; i < kEnRows; ++i)
    make_value_row(list, 0, rows[i].name, rows[i].icon, rows[i].color, on_enset_row, i, rows[i].key, &enset_val[i]);
  lv_obj_t* pad = lv_obj_create(list);
  lv_obj_set_size(pad, 10, 8);
  style_plain(pad);
}
