// Part of panel_ui.cpp (same translation unit, inside its anonymous namespace).
// Einstellungen > Playground > Oszi / Logik (SenseCAP D1 only): oscilloscope
// and logic analyzer measured by the on-board RP2040 at the Grove ports.
// The RP2040 link (rp_link.*) runs on core 0; this page only posts commands
// and drains rp_link_take() from its own LVGL timer (UI task, core 1).
// Planning / rendering: scope_view.* (host-tested).

lv_obj_t* sc_root = nullptr;
lv_obj_t* sc_canvas = nullptr;
lv_obj_t* sc_meas = nullptr;
lv_obj_t* sc_mode_btn[2] = {};
lv_obj_t* sc_run_btn[3] = {};
lv_obj_t* sc_tb_lbl = nullptr;
lv_obj_t* sc_vd_lbl = nullptr;
lv_obj_t* sc_edge_lbl = nullptr;
lv_obj_t* sc_src_lbl = nullptr;
lv_obj_t* sc_lvl_lbl = nullptr;
lv_obj_t* sc_tm_lbl = nullptr;
lv_obj_t* sc_ch_lbl = nullptr;
lv_obj_t* sc_pre_lbl = nullptr;
lv_obj_t* sc_zoom_lbl = nullptr;
lv_obj_t* sc_lane[4] = {};
uint16_t* sc_px = nullptr;  // PSRAM, kPlotW x kPlotH RGB565
bool sc_open = false;

struct ScopeUi {
  bool logic = false;
  int tb_a = 4;          // 200 µs/div
  int tb_l = 6;          // 10 µs/div
  int vdiv = 2;          // 500 mV/div
  int offset = 0;        // bottom line in divisions
  char edge = 'r';
  int src_a = 0, src_l = 0;
  uint16_t level = 2048; // ~1.65 V
  bool normal = false;
  int ch = 0;            // 0 A0, 1 A1, 2 both
  int pre = 10;
  int run = 0;           // 0 stop, 1 run, 2 single
  int zoom = 1;
  int64_t center = -1;   // view centre (sample index), -1 = trigger
} sc;

EXT_RAM_BSS_ATTR rps_frame_t sc_last;   // last frame shown
EXT_RAM_BSS_ATTR RpUiMsg sc_msg;        // UI task only
bool sc_have = false;
uint32_t sc_cap_n = 0, sc_cap_pre = 0;  // last full capture (zoom base)
bool sc_caps = false, sc_noanswer = false;
uint32_t sc_logic_max = 0;
char sc_state[64] = "";

const char* const kScLane[4] = {"D0 SDA \xC2\xB7 GPIO20", "D1 SCL \xC2\xB7 GPIO21", "D2 \xC2\xB7 GPIO26",
                                "D3 \xC2\xB7 GPIO27"};

void sc_render() {
  if (sc_px == nullptr) return;
  scope::View v{scope::kVdivMv[sc.vdiv], sc.offset, sc.level, sc.ch == 2 ? sc.src_a : 0, sc.edge != 'n'};
  if (!sc_have || (sc_last.type == RPS_T_LOGIC) != sc.logic) scope::render_empty(sc_px, sc.logic);
  else if (sc.logic) scope::render_logic(sc_px, sc_last);
  else scope::render_analog(sc_px, sc_last, v);
  lv_obj_invalidate(sc_canvas);
}

void sc_meas_text() {
  char b[420];
  size_t o = 0;
  char t1[24], t2[24];
  if (!sc_caps) {
    o += snprintf(b + o, sizeof(b) - o, "%s",
                  sc_noanswer ? "RP2040 antwortet nicht: Scope-Firmware auf dem RP2040 fehlt (Werkszustand?)."
                              : "Verbinde mit dem RP2040 ...");
  } else if (!sc_have || (sc_last.type == RPS_T_LOGIC) != sc.logic) {
    o += snprintf(b + o, sizeof(b) - o, "%s", sc_state[0] ? sc_state : "Bereit \xC2\xB7 Run oder Single dr\xC3\xBC" "cken");
  } else {
    const rps_frame_t& f = sc_last;
    scope::fmt_rate(f.rate, t1, sizeof(t1));
    const char* trig = (f.flags & RPS_F_TRIGGERED) ? "getriggert" : (f.flags & RPS_F_AUTO) ? "Auto (kein Trigger)" : "frei";
    o += snprintf(b + o, sizeof(b) - o, "%s \xC2\xB7 %lu Punkte \xC2\xB7 %s%s%s%s", t1,
                  static_cast<unsigned long>(f.n), trig, (f.flags & RPS_F_OVERRUN) ? " \xC2\xB7 \xC3\x9C" "berlauf" : "",
                  (f.flags & RPS_F_VIEW) ? " \xC2\xB7 Zoom" : "", sc_state[0] ? " \xC2\xB7 " : "");
    if (sc_state[0]) o += snprintf(b + o, sizeof(b) - o, "%s", sc_state);
    if (f.type == RPS_T_ANALOG) {
      for (int c = 0; c < f.nch && o < sizeof(b); ++c) {
        const rps_ameas_t& m = f.am[c];
        scope::fmt_freq(m.freq_hz, t2, sizeof(t2));
        int name = f.nch == 2 ? c : (f.chmode == 1 ? 1 : 0);
        o += snprintf(b + o, sizeof(b) - o, "\nA%d: Vpp %.2f V \xC2\xB7 min %.2f \xC2\xB7 max %.2f \xC2\xB7 Mittel %.2f V \xC2\xB7 f %s",
                      name, scope::counts_to_volts(m.vmax - m.vmin), scope::counts_to_volts(m.vmin),
                      scope::counts_to_volts(m.vmax), scope::counts_to_volts(m.vmean), t2);
      }
    } else {
      for (int c = 0; c < RPS_LOGIC_CH && o < sizeof(b); ++c) {
        const rps_lmeas_t& m = f.lm[c];
        scope::fmt_freq(m.freq_hz, t2, sizeof(t2));
        if (m.freq_hz > 0)
          o += snprintf(b + o, sizeof(b) - o, "\nD%d: f %s \xC2\xB7 Duty %.1f %% \xC2\xB7 %lu Flanken", c, t2,
                        m.duty_pm / 10.0, static_cast<unsigned long>(m.rising));
        else
          o += snprintf(b + o, sizeof(b) - o, "\nD%d: %s \xC2\xB7 %lu Flanken", c, m.level0 ? "high" : "low",
                        static_cast<unsigned long>(m.rising));
      }
    }
  }
  lv_label_set_text(sc_meas, b);
}

void sc_labels() {
  char t[32];
  scope::fmt_time(sc.logic ? scope::kLogicTb[sc.tb_l] : scope::kAnalogTb[sc.tb_a], t, sizeof(t));
  lv_label_set_text_fmt(sc_tb_lbl, "%s/div", t);
  lv_label_set_text_fmt(sc_vd_lbl, sc.logic ? "\xE2\x80\x93" : "%u mV/div", static_cast<unsigned>(scope::kVdivMv[sc.vdiv]));
  lv_label_set_text(sc_edge_lbl, sc.edge == 'r' ? "Steigend" : sc.edge == 'f' ? "Fallend" : "Kein Trig.");
  if (sc.logic) lv_label_set_text_fmt(sc_src_lbl, "Quelle D%d", sc.src_l);
  else lv_label_set_text_fmt(sc_src_lbl, "Quelle A%d", sc.ch == 2 ? sc.src_a : sc.ch);
  if (sc.logic) lv_label_set_text(sc_lvl_lbl, "\xE2\x80\x93");
  else lv_label_set_text_fmt(sc_lvl_lbl, "%.2f V", scope::counts_to_volts(sc.level));
  lv_label_set_text(sc_tm_lbl, sc.normal ? "Normal" : "Auto");
  lv_label_set_text(sc_ch_lbl, sc.logic ? "4 Kan\xC3\xA4le" : sc.ch == 0 ? "Kanal A0" : sc.ch == 1 ? "Kanal A1" : "A0 + A1");
  lv_label_set_text_fmt(sc_pre_lbl, "Pre %d %%", sc.pre);
  lv_label_set_text_fmt(sc_zoom_lbl, "Zoom x%d", sc.zoom);
  for (int i = 0; i < 2; ++i) {
    if ((i == 1) == sc.logic) lv_obj_add_state(sc_mode_btn[i], LV_STATE_CHECKED);
    else lv_obj_clear_state(sc_mode_btn[i], LV_STATE_CHECKED);
  }
  static const int kRunOf[3] = {1, 2, 0};  // buttons Run, Single, Stop
  for (int i = 0; i < 3; ++i) {
    if (sc.run == kRunOf[i]) lv_obj_add_state(sc_run_btn[i], LV_STATE_CHECKED);
    else lv_obj_clear_state(sc_run_btn[i], LV_STATE_CHECKED);
  }
  for (lv_obj_t* l : sc_lane) {
    if (sc.logic) show(l);
    else hide(l);
  }
}

void sc_arm() {
  if (sc.run == 0) return;
  scope::Arm a{};
  a.logic = sc.logic;
  a.ch = sc.ch;
  a.src = sc.logic ? sc.src_l : (sc.ch == 2 ? sc.src_a : 0);
  a.edge = sc.edge;
  a.normal = sc.normal;
  a.level = sc.level;
  a.pre_pct = sc.pre;
  a.cols = scope::kPlotW;
  scope::Plan p = sc.logic ? scope::plan_logic(scope::kLogicTb[sc.tb_l], sc_logic_max, !sc.normal)
                           : scope::plan_analog(scope::kAnalogTb[sc.tb_a], sc.ch == 2 ? 2 : 1, !sc.normal);
  RpArm r{};
  if (scope::arm_text(a, p, r.text, sizeof(r.text)) == 0) return;
  r.run = sc.run == 1;
  sc.zoom = 1;
  sc.center = -1;
  snprintf(sc_state, sizeof(sc_state), "%s", sc.normal ? "warte auf Trigger ..." : "");
  rp_link_arm(r);
}

void sc_view() {
  if (sc.run != 0 || !sc_have || sc_cap_n == 0) return;
  uint32_t vn = sc_cap_n / static_cast<uint32_t>(sc.zoom);
  if (vn < 16) vn = sc_cap_n < 16 ? sc_cap_n : 16;
  int64_t c = sc.center < 0 ? sc_cap_pre : sc.center;
  int64_t off = c - static_cast<int64_t>(vn / 2);
  if (off + vn > sc_cap_n) off = static_cast<int64_t>(sc_cap_n) - vn;
  if (off < 0) off = 0;
  sc.center = off + vn / 2;
  char t[48];
  if (scope::view_text(static_cast<int32_t>(off), vn, scope::kPlotW, t, sizeof(t)) != 0) rp_link_view(t);
}

void on_sc_btn(lv_event_t* e) {
  if (gesture_consumed) return;
  int w = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  bool rearm = true;
  const uint16_t lvl_step = scope::volts_to_counts(scope::kVdivMv[sc.vdiv] / 4000.0f);
  switch (w) {
    case 0: sc.logic = false; break;
    case 1: sc.logic = true; break;
    case 2: sc.run = 1; break;
    case 3: sc.run = 2; break;
    case 4: sc.run = 0; rp_link_stop(); rearm = false; sc_state[0] = 0; break;
    case 5: if (sc.logic) { if (sc.tb_l > 0) --sc.tb_l; } else if (sc.tb_a > 0) --sc.tb_a; break;
    case 6: if (sc.logic) { if (sc.tb_l < scope::kLogicTbN - 1) ++sc.tb_l; } else if (sc.tb_a < scope::kAnalogTbN - 1) ++sc.tb_a; break;
    case 7: if (sc.vdiv > 0) --sc.vdiv; rearm = false; break;
    case 8: if (sc.vdiv < scope::kVdivN - 1) ++sc.vdiv; rearm = false; break;
    case 9: sc.edge = sc.edge == 'r' ? 'f' : sc.edge == 'f' ? 'n' : 'r'; break;
    case 10:
      if (sc.logic) sc.src_l = (sc.src_l + 1) % RPS_LOGIC_CH;
      else sc.src_a = sc.src_a ? 0 : 1;
      break;
    case 11: sc.level = sc.level > lvl_step ? sc.level - lvl_step : 0; break;
    case 12: sc.level = sc.level + lvl_step < 4095 ? sc.level + lvl_step : 4095; break;
    case 13: sc.normal = !sc.normal; break;
    case 14: sc.ch = (sc.ch + 1) % 3; break;
    case 15: sc.pre = sc.pre == 10 ? 50 : sc.pre == 50 ? 90 : 10; break;
    case 16: if (sc.offset > -8) --sc.offset; rearm = false; break;
    case 17: if (sc.offset < 8) ++sc.offset; rearm = false; break;
    case 18: if (sc.zoom > 1) sc.zoom /= 2; rearm = false; sc_view(); break;
    case 19: if (sc.zoom < 64) sc.zoom *= 2; rearm = false; sc_view(); break;
    case 20: case 21:
      if (sc.center >= 0 && sc.zoom > 1) {
        int64_t step = static_cast<int64_t>(sc_cap_n / static_cast<uint32_t>(sc.zoom) / 2);
        sc.center += w == 20 ? -step : step;
      }
      rearm = false;
      sc_view();
      break;
    default: break;
  }
  console_printf("Activate scope %d\n", w);
  if (rearm && sc.run != 0) sc_arm();
  sc_labels();
  sc_render();
  sc_meas_text();
}

lv_obj_t* sc_btn(int x, int y, int w, const char* text, int user, const char* key) {
  return make_choice(sc_root, x, y, w, 52, text, on_sc_btn, user, key);
}

lv_obj_t* sc_val(int x, int y, int w) {
  lv_obj_t* l = make_label(sc_root, &font_inter_sb16, lv_color_hex(kText));
  lv_obj_set_pos(l, x, y + 16);
  lv_obj_set_width(l, w);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  return l;
}

void sc_apply(const RpUiMsg& m) {
  switch (m.kind) {
    case RpMsgKind::Caps:
      sc_caps = true;
      sc_noanswer = false;
      sc_logic_max = m.caps.logic_max_rate;
      console_printf("RPLINK caps fw=%s sys=%lu logic=%lu\n", m.caps.fw, static_cast<unsigned long>(m.caps.sys_hz),
                     static_cast<unsigned long>(m.caps.logic_max_rate));
      break;
    case RpMsgKind::NoAnswer:
      if (!sc_caps) sc_noanswer = true;
      break;
    case RpMsgKind::Status:
      if (m.state == 0 && (m.flags & RPS_F_TIMEOUT)) snprintf(sc_state, sizeof(sc_state), "kein Trigger");
      else if (m.state == 1) snprintf(sc_state, sizeof(sc_state), "%s", sc.normal ? "warte auf Trigger ..." : "");
      break;
    case RpMsgKind::Err:
      snprintf(sc_state, sizeof(sc_state), "RP2040: %s", m.text);
      break;
    case RpMsgKind::Frame:
      memcpy(&sc_last, &m.f, sizeof(sc_last));
      sc_have = true;
      if (!(m.f.flags & RPS_F_VIEW)) {
        sc_cap_n = m.f.n;
        sc_cap_pre = m.f.pre;
        sc_state[0] = 0;
        if (sc.run == 2) sc.run = 0;  // single shot done
      }
      sc_labels();
      sc_render();
      break;
  }
  sc_meas_text();
}

void sc_tick(lv_timer_t*) {
  if (!sc_open) return;
  for (int i = 0; i < 3 && rp_link_take(&sc_msg); ++i) sc_apply(sc_msg);
}

void sc_hide() {
  if (sc_root == nullptr) return;
  hide(sc_root);
  if (sc_open) {
    sc_open = false;
    sc.run = 0;
    rp_link_close();
  }
}

void build_scope_screen(lv_obj_t* screen) {
  if (!rp_link_supported()) return;
  sc_px = static_cast<uint16_t*>(
      heap_caps_malloc(scope::kPlotW * scope::kPlotH * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (sc_px == nullptr) return;
  sc_root = make_screen_root(screen, "Playground \xC2\xB7 Oszi / Logik");
  make_page_scrollable(sc_root);
  lv_obj_t* warn = make_label(sc_root, &font_inter_14, lv_color_hex(kOrange));
  lv_obj_set_pos(warn, 24, 52);
  lv_label_set_text(warn, "Eingang max. 3.3 V \xE2\x80\x93 h\xC3\xB6here Spannung nur \xC3\xBC" "ber Spannungsteiler!");
  sc_canvas = lv_canvas_create(sc_root);
  lv_canvas_set_buffer(sc_canvas, sc_px, scope::kPlotW, scope::kPlotH, LV_IMG_CF_TRUE_COLOR);
  lv_obj_set_pos(sc_canvas, 15, 76);
  for (int i = 0; i < 4; ++i) {
    sc_lane[i] = make_label(sc_root, &font_inter_14, lv_color_hex(scope::kChColor[i]));
    lv_obj_set_pos(sc_lane[i], 22, 76 + i * (scope::kPlotH / 4) + 20);
    lv_label_set_text(sc_lane[i], kScLane[i]);
    hide(sc_lane[i]);
  }
  sc_meas = make_label(sc_root, &font_inter_14, lv_color_hex(kText));
  lv_obj_set_pos(sc_meas, 24, 338);
  lv_obj_set_width(sc_meas, kScreen - 48);
  lv_label_set_long_mode(sc_meas, LV_LABEL_LONG_WRAP);
  lv_label_set_text(sc_meas, "");
  const int y0 = 440, dy = 60, x = kMargin;
  sc_mode_btn[0] = sc_btn(x, y0, 96, "Oszi", 0, "scope:osc");
  sc_mode_btn[1] = sc_btn(x + 100, y0, 96, "Logik", 1, "scope:logic");
  sc_run_btn[0] = sc_btn(x + 212, y0, 72, "Run", 2, "scope:run");
  sc_run_btn[1] = sc_btn(x + 288, y0, 72, "Single", 3, "scope:single");
  sc_run_btn[2] = sc_btn(x + 364, y0, 76, "Stop", 4, "scope:stop");
  int y = y0 + dy;
  sc_btn(x, y, 52, "-", 5, "scope:tb-");
  sc_tb_lbl = sc_val(x + 52, y, 110);
  sc_btn(x + 162, y, 52, "+", 6, "scope:tb+");
  sc_btn(x + 226, y, 52, "-", 7, "scope:vd-");
  sc_vd_lbl = sc_val(x + 278, y, 110);
  sc_btn(x + 388, y, 52, "+", 8, "scope:vd+");
  y += dy;
  sc_edge_lbl = lv_obj_get_child(sc_btn(x, y, 104, "Steigend", 9, "scope:edge"), 0);
  sc_src_lbl = lv_obj_get_child(sc_btn(x + 108, y, 112, "Quelle A0", 10, "scope:src"), 0);
  sc_btn(x + 232, y, 52, "-", 11, "scope:lvl-");
  sc_lvl_lbl = sc_val(x + 284, y, 104);
  sc_btn(x + 388, y, 52, "+", 12, "scope:lvl+");
  y += dy;
  sc_tm_lbl = lv_obj_get_child(sc_btn(x, y, 96, "Auto", 13, "scope:tmode"), 0);
  sc_ch_lbl = lv_obj_get_child(sc_btn(x + 100, y, 120, "Kanal A0", 14, "scope:ch"), 0);
  sc_pre_lbl = lv_obj_get_child(sc_btn(x + 224, y, 104, "Pre 10 %", 15, "scope:pre"), 0);
  sc_btn(x + 332, y, 52, "v", 16, "scope:pos-");
  sc_btn(x + 388, y, 52, "^", 17, "scope:pos+");
  y += dy;
  sc_btn(x, y, 104, "Zoom -", 18, "scope:zoom-");
  sc_btn(x + 112, y, 104, "Zoom +", 19, "scope:zoom+");
  sc_btn(x + 224, y, 104, "<", 20, "scope:pan-");
  sc_btn(x + 336, y, 104, ">", 21, "scope:pan+");
  y += dy;
  sc_zoom_lbl = make_caption(sc_root, y, "");
  lv_obj_t* info = make_label(sc_root, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_set_pos(info, 24, y + 24);
  lv_obj_set_width(info, kScreen - 48);
  lv_label_set_long_mode(info, LV_LABEL_LONG_WRAP);
  lv_label_set_text(info,
                    "Gemessen vom RP2040 im SenseCAP. Oszi: Grove-ADC-Port A0 = GPIO26, A1 = GPIO27, "
                    "0\xE2\x80\x93" "3.3 V, 12 bit, 500 kS/s (beide Kan\xC3\xA4le: je 250 kS/s), sauber bis ca. 50 kHz. "
                    "Logik: D0/D1 = Grove-I\xC2\xB2" "C SDA/SCL (GPIO20/21), D2/D3 = GPIO26/27, bis 100 MS/s, "
                    "bis 126'976 Samples. Zoom und Verschieben im Stop-Zustand.\n"
                    "ACHTUNG: An keinem Pin mehr als 3.3 V oder weniger als 0 V anlegen \xE2\x80\x93 das zerst\xC3\xB6rt "
                    "den RP2040. H\xC3\xB6here Spannungen nur \xC3\xBC" "ber einen Spannungsteiler (z. B. 10k/3.3k f\xC3\xBC"
                    "r 0\xE2\x80\x93" "12 V), nie an Netzspannung (230 V). Masse des Messobjekts mit Grove-GND verbinden.");
  lv_timer_create(sc_tick, 40, nullptr);
  scope::render_empty(sc_px, false);
  sc_labels();
}

void open_scope() {
  if (sc_root == nullptr) return;
  hide_settings();
  nav = Nav::Scope;
  show(sc_root);
  lv_obj_scroll_to_y(sc_root, 0, LV_ANIM_OFF);
  sc_open = true;
  sc_noanswer = false;
  rp_link_open();
  sc_labels();
  sc_render();
  sc_meas_text();
}

void scope_back() {
  sc_hide();
  open_play();
}

void scope_describe(char* out, size_t n) {
  RpLinkStats st = rp_link_stats();
  snprintf(out, n, "scope mode=%s run=%d caps=%d noanswer=%d have=%d rx=%lu bad=%lu sensor=%lu tx=%lu refused=%lu",
           sc.logic ? "logic" : "analog", sc.run, sc_caps ? 1 : 0, sc_noanswer ? 1 : 0, sc_have ? 1 : 0,
           static_cast<unsigned long>(st.rx_frames), static_cast<unsigned long>(st.rx_bad),
           static_cast<unsigned long>(st.rx_sensor), static_cast<unsigned long>(st.tx_cmds),
           static_cast<unsigned long>(st.tx_refused));
}
