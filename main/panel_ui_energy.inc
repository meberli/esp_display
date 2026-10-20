// Energie (whatwatt Go): Strom and Wasser pages (swipe), home chip and
// Einstellungen > Energie. Data from energy_task (core 0) via energy_get().

EnergyView* en_v = nullptr;  // PSRAM copy, UI task only
PvView* pv_v = nullptr;      // PSRAM copy, UI task only

// Strom
lv_obj_t* es_status = nullptr;
lv_obj_t* es_now_lbl = nullptr;
lv_obj_t* es_pill = nullptr;
lv_obj_t* es_pill_txt = nullptr;
lv_obj_t* es_big = nullptr;
lv_obj_t* es_unit = nullptr;
lv_obj_t* es_amps = nullptr;
lv_obj_t* es_val[3] = {};
lv_obj_t* es_price = nullptr;
lv_obj_t* es_hin[24] = {};
lv_obj_t* es_hout[24] = {};
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
lv_obj_t* ew_price = nullptr;
lv_obj_t* ew_hint = nullptr;
lv_obj_t* ew_wk_tot = nullptr;
lv_obj_t* ew_d[7] = {};
lv_obj_t* ew_dval[7] = {};
lv_obj_t* ew_dlbl[7] = {};
// Settings
constexpr int kEnRows = 17;
lv_obj_t* enset_val[kEnRows] = {};

constexpr int kEsBase = 150;    // Strom hourly bars: baseline inside card 1
constexpr int kEsUp = 44;       // import bar height max
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
  for (int i = 0; i < 2; ++i) {
    lv_obj_t* d = make_bar(root, 228 + i * 16, 452, 8, 8, i == active ? kText : kTextDim, i == active ? LV_OPA_COVER : LV_OPA_50);
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
}

void build_energy_screens(lv_obj_t* screen) {
  en_v = static_cast<EnergyView*>(heap_caps_calloc(1, sizeof(EnergyView), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  pv_v = static_cast<PvView*>(heap_caps_calloc(1, sizeof(PvView), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  // --- Strom
  lv_obj_t* r = en_root[0] = make_screen_root(screen, "Strom");
  lv_obj_set_pos(lv_obj_get_child(r, 0), 56, 20);
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
  make_bar(c, 228, 14, 1, 106, kBorder, LV_OPA_COVER);
  const char* names[3] = {"Bezug heute", "Einspeisung", "Kosten netto"};
  const uint32_t colors[3] = {kAmber, kGreen, kText};
  for (int i = 0; i < 3; ++i) {
    en_lbl(c, &font_inter_14, kTextDim, 244, 14 + i * 30, names[i]);
    es_val[i] = make_label(c, &font_inter_sb16, lv_color_hex(colors[i]));
    lv_obj_set_width(es_val[i], 96);
    lv_obj_set_style_text_align(es_val[i], LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(es_val[i], 318, 12 + i * 30);
  }
  es_price = en_lbl(c, &font_inter_14, kTextDim, 244, 102, "");
  lv_obj_set_width(es_price, 184);
  lv_label_set_long_mode(es_price, LV_LABEL_LONG_DOT);
  for (int h = 0; h < 24; ++h) {
    es_hin[h] = en_rect(c, 22 + h * 16, kEsBase - 1, 11, 1, kAmber);
    es_hout[h] = en_rect(c, 22 + h * 16, kEsBase + 1, 11, 1, kGreen);
  }
  make_bar(c, 18, kEsBase, 396, 1, kBorder, LV_OPA_COVER);
  for (int h = 0; h <= 24; h += 6) en_lbl(c, &font_inter_14, kTextDim, h == 24 ? 386 : 18 + h * 16, kEsBase + kEsDown + 6, h == 24 ? "24" : h == 0 ? "0" : h == 6 ? "6" : h == 12 ? "12" : "18");
  c = en_card(r, 272, 172);
  en_lbl(c, &font_inter_sb16, kText, 18, 12, "Letzte 7 Tage");
  es_wk_tot = en_lbl(c, &font_inter_14, kTextDim, 0, 14, "");
  lv_obj_set_width(es_wk_tot, 260);
  lv_obj_set_style_text_align(es_wk_tot, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_x(es_wk_tot, 154);
  for (int d = 0; d < 7; ++d) {
    int x = 30 + d * 56;
    es_din[d] = en_rect(c, x, kDayBase - 1, 16, 1, kAmber);
    es_dout[d] = en_rect(c, x + 19, kDayBase - 1, 16, 1, kGreen);
    es_dlbl[d] = en_lbl(c, &font_inter_14, kTextDim, x + 4, kDayBase + 4, "-");
  }
  make_bar(c, 18, kDayBase, 396, 1, kBorder, LV_OPA_COVER);
  en_lbl(c, &font_inter_14, kAmber, 18, 146, "Bezug");
  es_legend_out = en_lbl(c, &font_inter_14, kGreen, 80, 146, "Einspeisung");
  en_dots(r, 0);

  // --- Wasser
  r = en_root[1] = make_screen_root(screen, "Wasser");
  lv_obj_set_pos(lv_obj_get_child(r, 0), 56, 20);
  ic = make_label(r, &font_icons_24, lv_color_hex(kAccent));
  lv_label_set_text(ic, ICON_DROP);
  lv_obj_set_pos(ic, 22, 19);
  en_build_status(r, &ew_status);
  c = en_card(r, 56, 206);
  en_lbl(c, &font_inter_14, kTextDim, 18, 14, "Wasser gestern");
  ew_big = en_lbl(c, &font_inter_light_40, kText, 16, 36, "-");
  en_lbl(c, &font_inter_20, kTextDim, 0, 0, "Liter");
  ew_meter = en_lbl(c, &font_inter_14, kTextDim, 18, 94, "");
  make_bar(c, 228, 14, 1, 106, kBorder, LV_OPA_COVER);
  const char* wn[3] = {"Kosten gestern", "\xC3\x98 7 Tage", "Vorgestern"};
  for (int i = 0; i < 3; ++i) {
    en_lbl(c, &font_inter_14, kTextDim, 244, 14 + i * 30, wn[i]);
    ew_val[i] = make_label(c, &font_inter_sb16, lv_color_hex(i == 2 ? kText : kAccent));
    lv_obj_set_width(ew_val[i], 96);
    lv_obj_set_style_text_align(ew_val[i], LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(ew_val[i], 318, 12 + i * 30);
  }
  ew_price = en_lbl(c, &font_inter_14, kTextDim, 244, 102, "");
  lv_obj_set_width(ew_price, 184);
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
    ew_dlbl[d] = en_lbl(c, &font_inter_14, kTextDim, x + 8, kDayBase + 22, "-");
  }
  make_bar(c, 18, kDayBase + 18, 396, 1, kBorder, LV_OPA_COVER);
  en_dots(r, 1);
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
  } else if (v.stale) {
    if (v.have) lv_label_set_text_fmt(st, "veraltet \xC2\xB7 %s", v.error[0] ? v.error : "keine Daten");
    else lv_label_set_text_fmt(st, "%s", v.error[0] ? v.error : "verbinde ...");
    lv_obj_set_style_text_color(st, lv_color_hex(kAmber), 0);
  } else {
    char hm[8];
    fmt_hm(hm, sizeof(hm), v.fetched);
    lv_label_set_text_fmt(st, "%s \xC2\xB7 vor %d s", hm, static_cast<int>(now - v.fetched));
    lv_obj_set_style_text_color(st, lv_color_hex(kTextDim), 0);
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
    // PV replaces the phase currents: production, today and the house load (grid net + PV AC).
    const PvView& p = *pv_v;
    lv_obj_set_style_text_color(es_amps, lv_color_hex(p.have && !p.stale ? kGreen : kTextDim), 0);
    if (p.have && !p.stale && live)
      snprintf(b, sizeof(b), "PV %.0f W \xC2\xB7 %.1f kWh \xC2\xB7 Haus %.0f W", p.r.ac_w, p.r.today_kwh,
               net_w + p.r.ac_w < 0 ? 0.0f : net_w + p.r.ac_w);
    else if (p.have && !p.stale)
      snprintf(b, sizeof(b), "PV %.0f W \xC2\xB7 heute %.1f kWh", p.r.ac_w, p.r.today_kwh);
    else if (p.have)
      snprintf(b, sizeof(b), "PV veraltet \xC2\xB7 %.1f kWh", p.r.today_kwh);
    else
      snprintf(b, sizeof(b), "PV: %s", p.connected ? "warte auf Daten" : p.listening ? "warte auf Stick" : "aus");
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
  char p1[12], p2[12];
  energy_format_price(p1, sizeof(p1), tariff_price(v.tariff, now));
  energy_format_price(p2, sizeof(p2), v.tariff.feed);
  if (v.show_exp) lv_label_set_text_fmt(es_price, "%s Rp. / Einsp. %s", p1, p2);
  else lv_label_set_text_fmt(es_price, "%s Rp./kWh", p1);
  // Hourly bars (today), scaled to the largest hour (min 0.5 kWh).
  float mx = 0.5f, mo = 0.5f;
  for (int i = 0; i < 24; ++i) {
    if (h.h_in[i] > mx) mx = h.h_in[i];
    if (h.h_out[i] > mo) mo = h.h_out[i];
  }
  for (int i = 0; i < 24; ++i) {
    en_set_bar(es_hin[i], kEsBase, h.have ? static_cast<int>(h.h_in[i] / mx * kEsUp + 0.5f) : 0, true);
    en_set_bar(es_hout[i], kEsBase, h.have && v.show_exp ? static_cast<int>(h.h_out[i] / mo * kEsDown + 0.5f) : 0, false);
  }
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
    lv_obj_set_x(es_dlbl[d], d == 6 ? 30 + d * 56 - 6 : 30 + d * 56 + 8);
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
    lv_obj_set_x(ew_dlbl[i], yday ? 30 + i * 56 - 10 : 30 + i * 56 + 8);
    lv_obj_set_style_text_color(ew_dlbl[i], lv_color_hex(yday ? kText : kTextDim), 0);
  }
  snprintf(b, sizeof(b), "%.2f m\xC2\xB3 \xC2\xB7 CHF %.2f", tot / 1000.0f, tot / 1000.0f * v.tariff.water / 100.0f);
  lv_label_set_text(ew_wk_tot, b);
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
  else energy_refresh_wasser();
}

void energy_enter(int page) {
  if (message_open || en_root[0] == nullptr) return;
  en_page = page < 0 ? 0 : page > 1 ? 1 : page;
  enter_view(Nav::Energy, en_root[en_page]);
  energy_refresh();
}

void energy_swipe(int dir) {
  int next = en_page + dir;
  if (next < 0 || next > 1) return;
  energy_enter(next);
}

// --- Einstellungen > Energie -------------------------------------------------------

const char* const kEnModel[3] = {"Automatisch", "Einheitstarif", "HT/NT"};
const uint16_t kEnPoll[4] = {5, 10, 30, 60};
// Row -> text setting (or -1: stepped/toggled in place)
const int8_t kEnRowText[kEnRows] = {-1, kTextEnHost, -1, -1, kTextEnFlat, kTextEnHt, kTextEnNt, kTextEnHtWin,
                                    kTextEnSwitch, kTextEnFeed, -1, -1, -1, kTextWaPrice, -1, kTextPvIp, kTextPvPort};

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
    if (i >= 15) unit = i == 15 ? " (nur dieser Stick)" : " (TCP)";
    snprintf(b, sizeof(b), "%s%s%s", t, unit, own ? "" : " (Standard)");
    lv_label_set_text(enset_val[i], b);
  }
  lv_label_set_text(enset_val[10], s.show_exp ? "Ein" : "Aus");
  lv_label_set_text(enset_val[11], s.water_on ? "Ein" : "Aus");
  lv_label_set_text_fmt(enset_val[12], "M-Bus %u", s.water_bus);
  pv_get(pv_v);
  if (!settings_load_pv_on()) snprintf(b, sizeof(b), "Aus");
  else if (pv_v->have && !pv_v->stale) snprintf(b, sizeof(b), "Ein \xC2\xB7 %.0f W \xC2\xB7 %u Datens\xC3\xA4tze", pv_v->r.ac_w, (unsigned)pv_v->records);
  else snprintf(b, sizeof(b), "Ein \xC2\xB7 %s", pv_v->connected ? "Stick verbunden" : pv_v->listening ? "wartet auf den Stick" : pv_v->error[0] ? pv_v->error : "startet");
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
      {"PV (Growatt) empfangen", ICON_BOLT, kGreen, "pv:on"},
      {"PV: Growatt-Stick (IP)", ICON_ROUTER, kGreen, "pv:ip"},
      {"PV: Port", ICON_ROUTER, kGreen, "pv:port"},
  };
  for (int i = 0; i < kEnRows; ++i)
    make_value_row(list, 0, rows[i].name, rows[i].icon, rows[i].color, on_enset_row, i, rows[i].key, &enset_val[i]);
  lv_obj_t* pad = lv_obj_create(list);
  lv_obj_set_size(pad, 10, 8);
  style_plain(pad);
}
