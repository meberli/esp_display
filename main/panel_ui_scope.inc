// Part of panel_ui.cpp (same translation unit, inside its anonymous namespace).
// Oszi / Logik (SenseCAP D1 only, home button): oscilloscope and logic
// analyzer measured by the on-board RP2040 at the Grove ports.
// The RP2040 link (rp_link.*) runs on core 0; this page only posts commands
// and drains rp_link_take() from its own LVGL timer (UI task, core 1).
// Planning / rendering: scope_view.* (host-tested).
//
// Layout (480 x 480, never scrolls): one 44 px top bar (back, Oszi | Logik,
// Run / Single / Stop, settings), one status line with the 3.3 V note, the
// full-width plot (480 x 336) with its readouts drawn over it, one bottom bar
// (timebase, zoom, pan). Everything else is in the settings drawer.

lv_obj_t* sc_root = nullptr;
lv_obj_t* sc_canvas = nullptr;
lv_obj_t* sc_stat = nullptr;   // status line
lv_obj_t* sc_meas = nullptr;   // analog readouts (over the plot, top left)
lv_obj_t* sc_axis = nullptr;   // V/div + position (over the plot, bottom left)
lv_obj_t* sc_trig = nullptr;   // trigger summary (over the plot, bottom right)
lv_obj_t* sc_mode_btn[2] = {};
lv_obj_t* sc_run_btn[3] = {};
lv_obj_t* sc_tb_lbl = nullptr;
lv_obj_t* sc_view_btn[4] = {};  // zoom -, zoom +, pan <, pan >
lv_obj_t* sc_lane[4] = {};
// Settings drawer
lv_obj_t* sc_set = nullptr;
lv_obj_t* sc_set_row_analog[3] = {};  // V/div + position, level, channels
lv_obj_t* sc_ch_btn[3] = {};
lv_obj_t* sc_edge_btn[3] = {};
lv_obj_t* sc_src_btn[4] = {};
lv_obj_t* sc_src_row = nullptr;
lv_obj_t* sc_tm_btn[2] = {};
lv_obj_t* sc_pre_btn[3] = {};
lv_obj_t* sc_vd_lbl = nullptr;
lv_obj_t* sc_pos_lbl = nullptr;
lv_obj_t* sc_lvl_lbl = nullptr;
uint16_t* sc_px = nullptr;  // PSRAM, kPlotW x kPlotH RGB565
bool sc_open = false;

constexpr int kScBarY = 4, kScBarH = 44;
constexpr int kScStatY = 52;
constexpr int kScPlotY = 72;                           // plot: 72 .. 407
constexpr int kScBotY = kScPlotY + scope::kPlotH + 6;  // bottom bar: 414 .. 457 (home bar at 468)

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

const char* const kScLane[4] = {"D0 SDA", "D1 SCL", "D2", "D3"};

// snprintf (with %f) into a label: LVGL's own printf is built without float support.
void sc_textf(lv_obj_t* l, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void sc_textf(lv_obj_t* l, const char* fmt, ...) {
  char b[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(b, sizeof(b), fmt, ap);
  va_end(ap);
  lv_label_set_text(l, b);
}

void sc_set_checked(lv_obj_t* o, bool on) {
  if (o == nullptr) return;
  if (on) lv_obj_add_state(o, LV_STATE_CHECKED);
  else lv_obj_clear_state(o, LV_STATE_CHECKED);
}

void sc_set_enabled(lv_obj_t* o, bool on) {
  if (o == nullptr) return;
  if (on) lv_obj_clear_state(o, LV_STATE_DISABLED);
  else lv_obj_add_state(o, LV_STATE_DISABLED);
}

void sc_render() {
  if (sc_px == nullptr) return;
  scope::View v{scope::kVdivMv[sc.vdiv], sc.offset, sc.level, sc.ch == 2 ? sc.src_a : 0, sc.edge != 'n'};
  if (!sc_have || (sc_last.type == RPS_T_LOGIC) != sc.logic) scope::render_empty(sc_px, sc.logic);
  else if (sc.logic) scope::render_logic(sc_px, sc_last);
  else scope::render_analog(sc_px, sc_last, v);
  lv_obj_invalidate(sc_canvas);
}

// Status line, readouts over the plot and the logic lane labels.
void sc_meas_text() {
  char b[200];
  char t1[24], t2[24];
  const bool frame = sc_have && (sc_last.type == RPS_T_LOGIC) == sc.logic;
  if (!sc_caps) {
    snprintf(b, sizeof(b), "%s",
             sc_noanswer ? "RP2040 antwortet nicht \xE2\x80\x93 Scope-Firmware fehlt?" : "Verbinde mit dem RP2040 \xE2\x80\xA6");
  } else if (!frame) {
    snprintf(b, sizeof(b), "%s", sc_state[0] ? sc_state : "Bereit \xC2\xB7 Run oder Single dr\xC3\xBC" "cken");
  } else {
    const rps_frame_t& f = sc_last;
    scope::fmt_rate(f.rate, t1, sizeof(t1));
    const char* trig = (f.flags & RPS_F_TRIGGERED) ? "getriggert" : (f.flags & RPS_F_AUTO) ? "Auto, kein Trigger" : "frei";
    char z[16] = "";
    if (sc.zoom > 1) snprintf(z, sizeof(z), " \xC2\xB7 Zoom x%d", sc.zoom);
    snprintf(b, sizeof(b), "%s \xC2\xB7 %lu Pkt \xC2\xB7 %s%s%s%s%s", t1, static_cast<unsigned long>(f.n), trig,
             (f.flags & RPS_F_OVERRUN) ? " \xC2\xB7 \xC3\x9C" "berlauf" : "", z, sc_state[0] ? " \xC2\xB7 " : "", sc_state);
  }
  lv_label_set_text(sc_stat, b);

  // Analog readouts (one line per channel, in the trace colour).
  if (!sc.logic && frame && sc_last.type == RPS_T_ANALOG) {
    char m[240];
    size_t o = 0;
    m[0] = 0;
    const rps_frame_t& f = sc_last;
    for (int c = 0; c < f.nch && c < 2 && o < sizeof(m); ++c) {
      const rps_ameas_t& a = f.am[c];
      scope::fmt_freq(a.freq_hz, t2, sizeof(t2));
      int name = f.nch == 2 ? c : (f.chmode == 1 ? 1 : 0);
      o += snprintf(m + o, sizeof(m) - o, "%s#%06lX A%d#  Vpp %.2f \xC2\xB7 min %.2f \xC2\xB7 max %.2f \xC2\xB7 \xC3\x98 %.2f V \xC2\xB7 %s",
                    c ? "\n" : "", static_cast<unsigned long>(scope::kChColor[name]), name,
                    scope::counts_to_volts(a.vmax - a.vmin), scope::counts_to_volts(a.vmin),
                    scope::counts_to_volts(a.vmax), scope::counts_to_volts(a.vmean), t2);
    }
    lv_label_set_text(sc_meas, m);
    show(sc_meas);
  } else {
    hide(sc_meas);
  }

  // Logic: per-lane readout in the lane label.
  for (int c = 0; c < RPS_LOGIC_CH; ++c) {
    if (!sc.logic) { hide(sc_lane[c]); continue; }
    show(sc_lane[c]);
    if (frame && sc_last.type == RPS_T_LOGIC) {
      const rps_lmeas_t& m = sc_last.lm[c];
      scope::fmt_freq(m.freq_hz, t2, sizeof(t2));
      if (m.freq_hz > 0)
        sc_textf(sc_lane[c], "%s \xC2\xB7 %s \xC2\xB7 %.1f %% \xC2\xB7 %lu Fl.", kScLane[c], t2, m.duty_pm / 10.0,
                              static_cast<unsigned long>(m.rising));
      else
        sc_textf(sc_lane[c], "%s \xC2\xB7 %s", kScLane[c], m.level0 ? "high" : "low");
    } else {
      lv_label_set_text(sc_lane[c], kScLane[c]);
    }
  }
}

void sc_labels() {
  char t[32];
  scope::fmt_time(sc.logic ? scope::kLogicTb[sc.tb_l] : scope::kAnalogTb[sc.tb_a], t, sizeof(t));
  sc_textf(sc_tb_lbl, "%s/div", t);
  sc_textf(sc_vd_lbl, "%u mV", static_cast<unsigned>(scope::kVdivMv[sc.vdiv]));
  if (sc.offset) sc_textf(sc_pos_lbl, "%+d div", sc.offset);
  else lv_label_set_text(sc_pos_lbl, "0 div");
  sc_textf(sc_lvl_lbl, "%.2f V", scope::counts_to_volts(sc.level));
  // Over the plot
  if (sc.logic) {
    hide(sc_axis);
  } else {
    if (sc.offset) sc_textf(sc_axis, "%u mV/div \xC2\xB7 Pos %+d div", static_cast<unsigned>(scope::kVdivMv[sc.vdiv]), sc.offset);
    else sc_textf(sc_axis, "%u mV/div", static_cast<unsigned>(scope::kVdivMv[sc.vdiv]));
    show(sc_axis);
  }
  int src = sc.logic ? sc.src_l : (sc.ch == 2 ? sc.src_a : sc.ch);
  if (sc.edge == 'n') {
    sc_textf(sc_trig, "Trigger aus \xC2\xB7 Pre %d %%", sc.pre);
  } else if (sc.logic) {
    sc_textf(sc_trig, "Trig %s D%d \xC2\xB7 %s \xC2\xB7 Pre %d %%", sc.edge == 'r' ? "steigend" : "fallend", src,
                          sc.normal ? "Normal" : "Auto", sc.pre);
  } else {
    sc_textf(sc_trig, "Trig %s A%d %.2f V \xC2\xB7 %s \xC2\xB7 Pre %d %%", sc.edge == 'r' ? "steigend" : "fallend",
                          src, scope::counts_to_volts(sc.level), sc.normal ? "Normal" : "Auto", sc.pre);
  }
  for (int i = 0; i < 2; ++i) sc_set_checked(sc_mode_btn[i], (i == 1) == sc.logic);
  static const int kRunOf[3] = {1, 2, 0};  // buttons Run, Single, Stop
  for (int i = 0; i < 3; ++i) sc_set_checked(sc_run_btn[i], sc.run == kRunOf[i]);
  // Zoom / pan work on the stored capture: only when stopped with a capture.
  const bool can_view = sc.run == 0 && sc_have && sc_cap_n != 0 && (sc_last.type == RPS_T_LOGIC) == sc.logic;
  sc_set_enabled(sc_view_btn[0], can_view && sc.zoom > 1);
  sc_set_enabled(sc_view_btn[1], can_view && sc.zoom < 64);
  sc_set_enabled(sc_view_btn[2], can_view && sc.zoom > 1);
  sc_set_enabled(sc_view_btn[3], can_view && sc.zoom > 1);
  // Drawer
  for (int i = 0; i < 3; ++i) sc_set_checked(sc_ch_btn[i], sc.ch == i);
  static const char kEdges[3] = {'r', 'f', 'n'};
  for (int i = 0; i < 3; ++i) sc_set_checked(sc_edge_btn[i], sc.edge == kEdges[i]);
  for (int i = 0; i < 4; ++i) {
    sc_set_checked(sc_src_btn[i], src == i);
    sc_textf(lv_obj_get_child(sc_src_btn[i], 0), "%c%d", sc.logic ? 'D' : 'A', i);
    if (sc.logic || i < 2) show(sc_src_btn[i]);
    else hide(sc_src_btn[i]);
  }
  // Analog with one channel: the trigger source is that channel (no choice).
  if (sc.logic || sc.ch == 2) show(sc_src_row);
  else hide(sc_src_row);
  for (int i = 0; i < 2; ++i) sc_set_checked(sc_tm_btn[i], sc.normal == (i == 1));
  static const int kPre[3] = {10, 50, 90};
  for (int i = 0; i < 3; ++i) sc_set_checked(sc_pre_btn[i], sc.pre == kPre[i]);
  for (lv_obj_t* r : sc_set_row_analog) {
    if (sc.logic) hide(r);
    else show(r);
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
  snprintf(sc_state, sizeof(sc_state), "%s", sc.normal ? "warte auf Trigger \xE2\x80\xA6" : "");
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

void sc_drawer(bool open) {
  if (sc_set == nullptr) return;
  if (open) {
    show(sc_set);
    lv_obj_move_foreground(sc_set);
  } else {
    hide(sc_set);
  }
}

bool sc_drawer_open() { return sc_set != nullptr && !lv_obj_has_flag(sc_set, LV_OBJ_FLAG_HIDDEN); }

// Button codes: 0 Oszi, 1 Logik, 2 Run, 3 Single, 4 Stop, 5/6 timebase -/+,
// 7/8 V/div -/+, 11/12 level -/+, 16/17 position -/+, 18/19 zoom -/+,
// 20/21 pan, 30 back, 31/32 settings open/close, 40+ channels (A0, A1, both),
// 44+ edge (r, f, off), 48+ trigger source, 52/53 Auto/Normal, 56+ pre-trigger.
void on_sc_btn(lv_event_t* e) {
  if (gesture_consumed) return;
  lv_obj_t* target = lv_event_get_current_target(e);
  if (target != nullptr && lv_obj_has_state(target, LV_STATE_DISABLED)) return;
  int w = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  console_printf("Activate scope %d\n", w);
  if (w == 30) {
    go_home();
    return;
  }
  if (w == 31 || w == 32) {
    sc_drawer(w == 31);
    return;
  }
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
    case 11: sc.level = sc.level > lvl_step ? sc.level - lvl_step : 0; break;
    case 12: sc.level = sc.level + lvl_step < 4095 ? sc.level + lvl_step : 4095; break;
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
    case 40: case 41: case 42: sc.ch = w - 40; break;
    case 44: sc.edge = 'r'; break;
    case 45: sc.edge = 'f'; break;
    case 46: sc.edge = 'n'; break;
    case 48: case 49: case 50: case 51:
      if (sc.logic) sc.src_l = w - 48;
      else if (w < 50) sc.src_a = w - 48;
      break;
    case 52: case 53: sc.normal = w == 53; break;
    case 56: sc.pre = 10; break;
    case 57: sc.pre = 50; break;
    case 58: sc.pre = 90; break;
    default: break;
  }
  if (rearm && sc.run != 0) sc_arm();
  sc_labels();
  sc_render();
  sc_meas_text();
}

lv_obj_t* sc_btn(lv_obj_t* parent, int x, int y, int w, const char* text, int user, const char* key) {
  return make_choice(parent, x, y, w, kScBarH, text, on_sc_btn, user, key);
}

lv_obj_t* sc_icon_btn(lv_obj_t* parent, int x, int y, int w, const char* icon, int user, const char* key) {
  lv_obj_t* b = sc_btn(parent, x, y, w, "", user, key);
  lv_obj_t* t = lv_obj_get_child(b, 0);
  lv_obj_set_style_text_font(t, &font_icons_24, 0);
  lv_label_set_text(t, icon);
  lv_obj_center(t);
  return b;
}

// Readout over the plot: small text on a translucent plate.
lv_obj_t* sc_plate(lv_obj_t* parent, uint32_t color) {
  lv_obj_t* l = make_label(parent, &font_inter_14, lv_color_hex(color));
  lv_obj_set_style_bg_color(l, lv_color_hex(0x0E1216), 0);
  lv_obj_set_style_bg_opa(l, LV_OPA_70, 0);
  lv_obj_set_style_pad_hor(l, 5, 0);
  lv_obj_set_style_pad_ver(l, 2, 0);
  lv_obj_set_style_radius(l, 4, 0);
  return l;
}

// Drawer row: a caption on the left, controls from x = 92 (row-relative).
lv_obj_t* sc_row(lv_obj_t* list, const char* caption) {
  lv_obj_t* r = lv_obj_create(list);
  style_plain(r);
  lv_obj_set_size(r, LV_PCT(100), kScBarH);
  if (caption != nullptr) {
    lv_obj_t* c = make_label(r, &font_inter_14, lv_color_hex(kTextDim));
    lv_label_set_text(c, caption);
    lv_obj_align(c, LV_ALIGN_LEFT_MID, 0, 0);
  }
  return r;
}

lv_obj_t* sc_val(lv_obj_t* parent, int x, int w) {
  lv_obj_t* l = make_label(parent, &font_inter_sb16, lv_color_hex(kText));
  lv_obj_set_width(l, w);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(l, LV_ALIGN_LEFT_MID, x, 0);
  return l;
}

void build_scope_drawer() {
  // Backdrop over the whole page (tap outside the panel closes it).
  sc_set = lv_obj_create(sc_root);
  style_plain(sc_set);
  lv_obj_set_pos(sc_set, 0, 0);
  lv_obj_set_size(sc_set, kScreen, kScreen);
  lv_obj_set_style_bg_color(sc_set, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(sc_set, LV_OPA_50, 0);
  lv_obj_add_flag(sc_set, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(sc_set, on_sc_btn, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<intptr_t>(32)));

  lv_obj_t* p = lv_obj_create(sc_set);
  style_glass(p, 18);
  lv_obj_add_flag(p, LV_OBJ_FLAG_CLICKABLE);  // taps on the panel do not close it
  lv_obj_set_pos(p, 8, kScStatY);
  lv_obj_set_size(p, kScreen - 16, kScreen - kScStatY - 22);
  lv_obj_set_style_pad_hor(p, 14, 0);
  lv_obj_set_style_pad_ver(p, 8, 0);
  lv_obj_set_flex_flow(p, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(p, 3, 0);

  // Header: title + Fertig (ends at x 448 < 456, the right-edge back zone).
  lv_obj_t* hdr = sc_row(p, nullptr);
  lv_obj_t* t = make_label(hdr, &font_inter_20, lv_color_hex(kText));
  lv_label_set_text(t, "Einstellungen");
  lv_obj_align(t, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_t* done = sc_btn(hdr, 0, 0, 96, "Fertig", 32, "scope:set-close");
  lv_obj_align(done, LV_ALIGN_RIGHT_MID, -4, 0);

  const int x0 = 80;  // inner width 436
  lv_obj_t* r = sc_row(p, "Kan\xC3\xA4le");
  sc_set_row_analog[0] = r;
  static const char* kCh[3] = {"A0", "A1", "A0 + A1"};
  static const char* kChKey[3] = {"scope:ch0", "scope:ch1", "scope:ch2"};
  for (int i = 0; i < 3; ++i) sc_ch_btn[i] = sc_btn(r, x0 + i * 82, 0, i == 2 ? 108 : 78, kCh[i], 40 + i, kChKey[i]);

  r = sc_row(p, "V/div");
  sc_set_row_analog[1] = r;
  sc_btn(r, x0, 0, 48, "-", 7, "scope:vd-");
  sc_vd_lbl = sc_val(r, x0 + 48, 76);
  sc_btn(r, x0 + 124, 0, 48, "+", 8, "scope:vd+");
  lv_obj_t* pc = make_label(r, &font_inter_14, lv_color_hex(kTextDim));
  lv_label_set_text(pc, "Pos");
  lv_obj_align(pc, LV_ALIGN_LEFT_MID, x0 + 182, 0);
  sc_btn(r, x0 + 212, 0, 44, "-", 16, "scope:pos-");
  sc_pos_lbl = sc_val(r, x0 + 256, 56);
  sc_btn(r, x0 + 312, 0, 44, "+", 17, "scope:pos+");

  r = sc_row(p, "Trigger");
  static const char* kEdge[3] = {"Steigend", "Fallend", "Aus"};
  static const char* kEdgeKey[3] = {"scope:edge-r", "scope:edge-f", "scope:edge-n"};
  for (int i = 0; i < 3; ++i) sc_edge_btn[i] = sc_btn(r, x0 + i * 116, 0, i == 2 ? 92 : 112, kEdge[i], 44 + i, kEdgeKey[i]);

  sc_src_row = r = sc_row(p, "Quelle");
  static const char* kSrc[4] = {"D0", "D1", "D2", "D3"};
  static const char* kSrcKey[4] = {"scope:src0", "scope:src1", "scope:src2", "scope:src3"};
  for (int i = 0; i < 4; ++i) {
    sc_src_btn[i] = sc_btn(r, x0 + i * 72, 0, 68, kSrc[i], 48 + i, kSrcKey[i]);
  }

  r = sc_row(p, "Level");
  sc_set_row_analog[2] = r;
  sc_btn(r, x0, 0, 48, "-", 11, "scope:lvl-");
  sc_lvl_lbl = sc_val(r, x0 + 48, 76);
  sc_btn(r, x0 + 124, 0, 48, "+", 12, "scope:lvl+");

  r = sc_row(p, "Modus");
  sc_tm_btn[0] = sc_btn(r, x0, 0, 66, "Auto", 52, "scope:auto");
  sc_tm_btn[1] = sc_btn(r, x0 + 70, 0, 80, "Normal", 53, "scope:normal");
  lv_obj_t* prc = make_label(r, &font_inter_14, lv_color_hex(kTextDim));
  lv_label_set_text(prc, "Pre");
  lv_obj_align(prc, LV_ALIGN_LEFT_MID, x0 + 160, 0);
  static const char* kPre[3] = {"10 %", "50 %", "90 %"};
  static const char* kPreKey[3] = {"scope:pre10", "scope:pre50", "scope:pre90"};
  for (int i = 0; i < 3; ++i) sc_pre_btn[i] = sc_btn(r, x0 + 190 + i * 56, 0, 54, kPre[i], 56 + i, kPreKey[i]);

  lv_obj_t* info = make_label(p, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_set_width(info, LV_PCT(100));
  lv_label_set_long_mode(info, LV_LABEL_LONG_WRAP);
  lv_label_set_text(info,
                    "A0/A1 = GPIO26/27 \xC2\xB7 D0/D1 = Grove-I\xC2\xB2" "C SDA/SCL, D2/D3 = GPIO26/27. Max. 3.3 V, "
                    "h\xC3\xB6her nur \xC3\xBC" "ber Spannungsteiler, nie Netzspannung. GND verbinden.");
  hide(sc_set);
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
      else if (m.state == 1) snprintf(sc_state, sizeof(sc_state), "%s", sc.normal ? "warte auf Trigger \xE2\x80\xA6" : "");
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
  sc_drawer(false);
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
  // Fixed 480 x 480 page: style_plain() leaves it non-scrollable on purpose.
  sc_root = lv_obj_create(screen);
  lv_obj_set_pos(sc_root, 0, 0);
  lv_obj_set_size(sc_root, kScreen, kScreen);
  style_plain(sc_root);
  lv_obj_clear_flag(sc_root, LV_OBJ_FLAG_SCROLLABLE);

  // Top bar: back | Oszi Logik | Run Single Stop | settings (all < x 456,
  // the right-edge back zone; 44 px high).
  const int y = kScBarY;
  sc_icon_btn(sc_root, 4, y, 46, ICON_BACK, 30, "scope:back");
  sc_mode_btn[0] = sc_btn(sc_root, 56, y, 74, "Oszi", 0, "scope:osc");
  sc_mode_btn[1] = sc_btn(sc_root, 132, y, 74, "Logik", 1, "scope:logic");
  sc_run_btn[0] = sc_btn(sc_root, 214, y, 56, "Run", 2, "scope:run");
  sc_run_btn[1] = sc_btn(sc_root, 274, y, 66, "Single", 3, "scope:single");
  sc_run_btn[2] = sc_btn(sc_root, 344, y, 56, "Stop", 4, "scope:stop");
  sc_icon_btn(sc_root, 406, y, 48, ICON_TUNE, 31, "scope:set");

  // Status line + compact 3.3 V note.
  sc_stat = make_label(sc_root, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_set_pos(sc_stat, 8, kScStatY);
  lv_obj_set_width(sc_stat, 360);
  label_dot1(sc_stat);
  lv_label_set_text(sc_stat, "");
  lv_obj_t* warn = make_label(sc_root, &font_inter_14, lv_color_hex(kOrange));
  lv_label_set_text(warn, "max 3.3 V");
  lv_obj_align(warn, LV_ALIGN_TOP_RIGHT, -8, kScStatY);
  lv_obj_t* wi = make_label(sc_root, &font_icons_16, lv_color_hex(kOrange));
  lv_label_set_text(wi, ICON_WARNING);
  lv_obj_align_to(wi, warn, LV_ALIGN_OUT_LEFT_MID, -4, 0);

  // Plot, full width.
  sc_canvas = lv_canvas_create(sc_root);
  lv_canvas_set_buffer(sc_canvas, sc_px, scope::kPlotW, scope::kPlotH, LV_IMG_CF_TRUE_COLOR);
  lv_obj_set_pos(sc_canvas, 0, kScPlotY);
  const int lane = scope::kPlotH / 4;
  for (int i = 0; i < 4; ++i) {
    sc_lane[i] = sc_plate(sc_root, scope::kChColor[i]);
    lv_obj_set_pos(sc_lane[i], 6, kScPlotY + i * lane + (scope::kLogicHiPad + lane - scope::kLogicLoPad) / 2 - 10);  // between high and low
    lv_label_set_text(sc_lane[i], kScLane[i]);
    hide(sc_lane[i]);
  }
  sc_meas = sc_plate(sc_root, kText);
  lv_label_set_recolor(sc_meas, true);
  lv_obj_set_pos(sc_meas, 4, kScPlotY + 4);
  lv_label_set_text(sc_meas, "");
  hide(sc_meas);
  sc_axis = sc_plate(sc_root, kTextDim);
  lv_obj_align(sc_axis, LV_ALIGN_TOP_LEFT, 4, kScPlotY + scope::kPlotH - 24);
  sc_trig = sc_plate(sc_root, 0xFF9F43);
  lv_obj_align(sc_trig, LV_ALIGN_TOP_RIGHT, -4, kScPlotY + scope::kPlotH - 24);

  // Bottom bar: timebase | zoom - + | pan < > (ends at x 454, y 458).
  const int yb = kScBotY;
  sc_btn(sc_root, 4, yb, 52, "-", 5, "scope:tb-");
  sc_tb_lbl = make_label(sc_root, &font_inter_sb16, lv_color_hex(kText));
  lv_obj_set_width(sc_tb_lbl, 120);
  lv_obj_set_style_text_align(sc_tb_lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(sc_tb_lbl, 56, yb + 13);
  sc_btn(sc_root, 176, yb, 52, "+", 6, "scope:tb+");
  sc_view_btn[0] = sc_icon_btn(sc_root, 244, yb, 50, ICON_ZOOM_OUT, 18, "scope:zoom-");
  sc_view_btn[1] = sc_icon_btn(sc_root, 298, yb, 50, ICON_ZOOM_IN, 19, "scope:zoom+");
  sc_view_btn[2] = sc_icon_btn(sc_root, 354, yb, 48, ICON_CHEVRON_L, 20, "scope:pan-");
  sc_view_btn[3] = sc_icon_btn(sc_root, 406, yb, 48, ICON_CHEVRON, 21, "scope:pan+");
  for (lv_obj_t* b : sc_view_btn) lv_obj_set_style_opa(b, LV_OPA_40, LV_STATE_DISABLED);

  make_home_bar(sc_root);
  build_scope_drawer();
  hide(sc_root);

  lv_timer_create(sc_tick, 40, nullptr);
  scope::render_empty(sc_px, false);
  sc_labels();
  sc_meas_text();
}

void open_scope() {
  if (sc_root == nullptr || message_open) return;
  hide(home_root);
  hide(cat_root);
  hide(lox_root);
  hide(wx_root);
  hide(cam_root);
  hide_settings();  // also sc_hide() via info_hide_all()
  nav = Nav::Scope;
  show(sc_root);
  sc_open = true;
  sc_noanswer = false;
  rp_link_open();  // installs the UART on the first open
  sc_labels();
  sc_render();
  sc_meas_text();
}

// Back (right-edge swipe, page swipe back): closes the drawer first, then home.
void scope_back() {
  if (sc_drawer_open()) {
    sc_drawer(false);
    return;
  }
  go_home();
}

void scope_describe(char* out, size_t n) {
  RpLinkStats st = rp_link_stats();
  snprintf(out, n, "scope mode=%s run=%d set=%d caps=%d noanswer=%d have=%d rx=%lu bad=%lu sensor=%lu tx=%lu refused=%lu",
           sc.logic ? "logic" : "analog", sc.run, sc_drawer_open() ? 1 : 0, sc_caps ? 1 : 0, sc_noanswer ? 1 : 0,
           sc_have ? 1 : 0, static_cast<unsigned long>(st.rx_frames), static_cast<unsigned long>(st.rx_bad),
           static_cast<unsigned long>(st.rx_sensor), static_cast<unsigned long>(st.tx_cmds),
           static_cast<unsigned long>(st.tx_refused));
}
