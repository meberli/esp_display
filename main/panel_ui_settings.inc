// Part of panel_ui.cpp (same translation unit, included inside its anonymous
// namespace). Split for readability only; not compiled on its own.

// --- Settings -------------------------------------------------------------
// Einstellungen: WLAN (scan, pick, password, connect) and Wetter-Ort
// (Open-Meteo geocoding). The network work runs in the net task; the UI only
// queues one SettingsRequest (ui_take_settings) and gets ui_*_done back.

// Co4: the title label itself carries the petrol-to-green header bar, so it
// stays child 0 (wp_title and the Bilanz screen rely on that).
// The bar is kHeaderH high so content placed at y >= 56 (cards, lists) stays
// below it. Screens with an icon left of the title must not move the label
// afterwards (that shifted the whole bar); they call header_title_at().
constexpr int32_t kHeaderH = 52;
void style_header(lv_obj_t* t) {
  if (!g_theme->header_bar) return;
  lv_obj_set_pos(t, 0, 0);
  lv_obj_set_size(t, kScreen, kHeaderH);
  lv_obj_set_style_pad_left(t, 24, 0);
  lv_obj_set_style_pad_top(t, 16, 0);
  lv_obj_set_style_bg_color(t, lv_color_hex(g_theme->grad_a), 0);
  lv_obj_set_style_bg_grad_color(t, lv_color_hex(g_theme->grad_b), 0);
  lv_obj_set_style_bg_grad_dir(t, LV_GRAD_DIR_HOR, 0);
  lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(t, lv_color_hex(0xFFFFFF), 0);
  lv_label_set_long_mode(t, LV_LABEL_LONG_CLIP);
}

// Title text at (x, y) with optional text width w. With a header bar the bar
// keeps spanning the screen and only the text inset changes.
void header_title_at(lv_obj_t* t, int32_t x, int32_t y, int32_t w) {
  if (!g_theme->header_bar) {
    lv_obj_set_pos(t, x, y);
    if (w > 0) lv_obj_set_width(t, w);
    return;
  }
  lv_obj_set_style_pad_left(t, x, 0);
  lv_obj_set_style_pad_right(t, w > 0 ? kScreen - x - w : 0, 0);
}

// Small status/page text that sits on the header row: readable on the bar.
void header_note(lv_obj_t* l) {
  if (!g_theme->header_bar) return;
  lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_opa(l, LV_OPA_80, 0);
}

lv_obj_t* make_screen_root(lv_obj_t* screen, const char* title) {
  lv_obj_t* root = lv_obj_create(screen);
  lv_obj_set_pos(root, 0, 0);
  lv_obj_set_size(root, kScreen, kScreen);
  style_plain(root);
  lv_obj_t* t = make_label(root, &font_inter_20, lv_color_hex(kText));
  lv_obj_set_pos(t, 24, 20);
  lv_label_set_text(t, title);
  style_header(t);
  make_home_bar(root);
  hide(root);
  return root;
}

// A page whose rows run past the screen: the whole page scrolls vertically
// (horizontal swipes still page back), with room for the home bar at the end.
void make_page_scrollable(lv_obj_t* root) {
  lv_obj_add_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(root, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(root, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_pad_bottom(root, 28, 0);
}

lv_obj_t* make_list(lv_obj_t* parent, int y, int h) {
  lv_obj_t* list = lv_obj_create(parent);
  lv_obj_set_pos(list, kMargin, y);
  lv_obj_set_size(list, kScreen - 2 * kMargin, h);
  style_plain(list);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(list, 8, 0);
  lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
  return list;
}

lv_obj_t* make_row(lv_obj_t* parent, int h, const char* icon, uint32_t icon_color, lv_event_cb_t cb, int user) {
  lv_obj_t* row = lv_btn_create(parent);
  lv_obj_set_size(row, kScreen - 2 * kMargin, h);
  style_glass(row, 18);
  lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<intptr_t>(user)));
  watch(row);
  if (icon != nullptr) {
    if (g_theme->icon_mono) icon_color = g_theme->row_icon;
    lv_obj_t* ic = make_label(row, strcmp(icon, ICON_BOLT) == 0 ? &font_icon_bolt_24 : &font_icons_24,
                              lv_color_hex(icon_color));
    lv_label_set_text(ic, icon);
    lv_obj_align(ic, LV_ALIGN_LEFT_MID, 16, 0);
  }
  return row;
}

void show_keyboard(lv_obj_t* ta) {
  lv_keyboard_set_textarea(kb, ta);
  lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
  show(kb);
  lv_obj_move_foreground(kb);
}

void queue_settings(const SettingsRequest& r) {
  settings_req = r;
  settings_pending = true;
}

const char* timeout_text(uint16_t s) {
  switch (s) {
    case 0: return "Aus";
    case 30: return "30 s";
    case 60: return "1 min";
    case 120: return "2 min";
    case 300: return "5 min";
    case 600: return "10 min";
    default: return "?";
  }
}

void refresh_display_settings() {
  char buf[64];
  static const char* kSaverName[3] = {"Schwarz", "Digitaluhr", "Analoguhr"};
  if (disp.timeout_s == 0) {
    snprintf(buf, sizeof(buf), "Bleibt an");
  } else if (disp.saver == kSaverBlack) {
    snprintf(buf, sizeof(buf), "Nach %s: Schwarz, Hinweis %s", timeout_text(disp.timeout_s), disp.hint ? "an" : "aus");
  } else {
    snprintf(buf, sizeof(buf), "Nach %s: %s", timeout_text(disp.timeout_s), kSaverName[disp.saver]);
  }
  for (int i = 0; i < 3; ++i) {
    if (disp_saver_btn[i] == nullptr) continue;
    if (i == disp.saver) {
      lv_obj_add_state(disp_saver_btn[i], LV_STATE_CHECKED);
    } else {
      lv_obj_clear_state(disp_saver_btn[i], LV_STATE_CHECKED);
    }
  }
  if (set_disp_val != nullptr) lv_label_set_text(set_disp_val, buf);
  for (int i = 0; i < kTimeoutChoiceN; ++i) {
    if (disp_to_btn[i] == nullptr) continue;
    if (kTimeoutChoices[i] == disp.timeout_s) {
      lv_obj_add_state(disp_to_btn[i], LV_STATE_CHECKED);
    } else {
      lv_obj_clear_state(disp_to_btn[i], LV_STATE_CHECKED);
    }
  }
  if (disp_hint_sw != nullptr) {
    if (disp.hint) {
      lv_obj_add_state(disp_hint_sw, LV_STATE_CHECKED);
    } else {
      lv_obj_clear_state(disp_hint_sw, LV_STATE_CHECKED);
    }
  }
}

void open_display() {
  nav = Nav::Display;
  hide_settings();
  refresh_display_settings();
  show(disp_root);
}

// Display choices are saved 1.5 s after the last tap (or when leaving the
// screen / restarting), so tapping through options writes NVS once.
uint32_t disp_save_at = 0;  // 0: nothing pending
void disp_save_later() { disp_save_at = now_ms() + 1500; if (disp_save_at == 0) disp_save_at = 1; }
void disp_save_flush(bool force) {
  if (disp_save_at == 0) return;
  if (!force && static_cast<int32_t>(now_ms() - disp_save_at) < 0) return;
  disp_save_at = 0;
  bool ok = settings_save_display(disp);
  console_printf("Display settings saved=%d\n", ok ? 1 : 0);
}

void on_timeout_btn(lv_event_t* e) {
  if (gesture_consumed) return;
  int i = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  disp.timeout_s = kTimeoutChoices[i];
  scr_timeout_s = disp.timeout_s;
  last_activity = now_ms();
  disp_save_later();
  console_printf("Screen timeout %u s\n", static_cast<unsigned>(disp.timeout_s));
  refresh_display_settings();
}

void on_saver_btn(lv_event_t* e) {
  if (gesture_consumed) return;
  int i = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  disp.saver = static_cast<uint8_t>(i);
  disp_save_later();
  console_printf("Screen saver %d\n", i);
  refresh_display_settings();
}

void on_hint_row(lv_event_t*) {
  if (gesture_consumed) return;
  disp.hint = !disp.hint;
  disp_save_later();
  console_printf("Screen hint %d\n", disp.hint ? 1 : 0);
  refresh_display_settings();
}

// --- Screen timeout and screensavers ---
// While off, sleep_layer (a screen of its own) is the active screen, so LVGL
// only renders it: black, the dim "wake me up", or one of the clocks.
void move_hint() {
  int x = 16 + static_cast<int>(esp_random() % (kScreen - 140));
  int y = 16 + static_cast<int>(esp_random() % (kScreen - 50));
  lv_obj_set_pos(sleep_hint, x, y);
  hint_moved_ms = now_ms();
}

bool local_now(struct tm* out, int* ms) {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  time_t t = tv.tv_sec;
  localtime_r(&t, out);
  if (ms != nullptr) *ms = static_cast<int>(tv.tv_usec / 1000);
  return out->tm_year + 1900 >= 2024;  // false until SNTP has set the clock
}

// Swiss railway clock geometry (fractions of the dial radius R).
constexpr float kDialR = 200.f;
lv_point_t polar(float cx, float cy, float deg, float r) {
  float a = deg * static_cast<float>(M_PI) / 180.f;
  lv_point_t p;
  p.x = static_cast<lv_coord_t>(lroundf(cx + r * sinf(a)));
  p.y = static_cast<lv_coord_t>(lroundf(cy - r * cosf(a)));
  return p;
}

// Tapered bar from -back to +len along deg (a hand), drawn as a polygon.
void draw_hand(lv_draw_ctx_t* ctx, float cx, float cy, float deg, float back, float len, float w_back, float w_tip,
               lv_color_t color) {
  float a = deg * static_cast<float>(M_PI) / 180.f;
  float dx = sinf(a), dy = -cosf(a);  // along the hand
  float nx = -dy, ny = dx;            // across
  float bx = cx - dx * back, by = cy - dy * back;
  float tx = cx + dx * len, ty = cy + dy * len;
  lv_point_t pts[4] = {
      {static_cast<lv_coord_t>(lroundf(bx + nx * w_back / 2)), static_cast<lv_coord_t>(lroundf(by + ny * w_back / 2))},
      {static_cast<lv_coord_t>(lroundf(tx + nx * w_tip / 2)), static_cast<lv_coord_t>(lroundf(ty + ny * w_tip / 2))},
      {static_cast<lv_coord_t>(lroundf(tx - nx * w_tip / 2)), static_cast<lv_coord_t>(lroundf(ty - ny * w_tip / 2))},
      {static_cast<lv_coord_t>(lroundf(bx - nx * w_back / 2)), static_cast<lv_coord_t>(lroundf(by - ny * w_back / 2))},
  };
  lv_draw_rect_dsc_t dsc;
  lv_draw_rect_dsc_init(&dsc);
  dsc.bg_color = color;
  dsc.bg_opa = LV_OPA_COVER;
  lv_draw_polygon(ctx, &dsc, pts, 4);
}

void draw_disc(lv_draw_ctx_t* ctx, float x, float y, float r, lv_color_t color) {
  lv_draw_rect_dsc_t dsc;
  lv_draw_rect_dsc_init(&dsc);
  dsc.radius = LV_RADIUS_CIRCLE;
  dsc.bg_color = color;
  dsc.bg_opa = LV_OPA_COVER;
  lv_area_t a = {static_cast<lv_coord_t>(lroundf(x - r)), static_cast<lv_coord_t>(lroundf(y - r)),
                 static_cast<lv_coord_t>(lroundf(x + r)), static_cast<lv_coord_t>(lroundf(y + r))};
  lv_draw_rect(ctx, &dsc, &a);
}

// SBB clock second hand: sweeps the dial in 58.5 s, rests at 12 until the
// minute hand jumps on the full minute.
float second_angle(const struct tm& t, int ms) {
  float s = static_cast<float>(t.tm_sec) + static_cast<float>(ms) / 1000.f;
  return s >= 58.5f ? 0.f : s * (360.f / 58.5f);
}

// Bounding box of the second hand (red bar plus the disc at its tip).
lv_area_t second_area(lv_obj_t* obj, float deg) {
  lv_area_t c;
  lv_obj_get_coords(obj, &c);
  float cx = (c.x1 + c.x2) / 2.f, cy = (c.y1 + c.y2) / 2.f;
  lv_point_t p1 = polar(cx, cy, deg + 180.f, kDialR * 0.31f);
  lv_point_t p2 = polar(cx, cy, deg, kDialR * 0.645f);
  int m = static_cast<int>(kDialR * 0.105f) + 4;
  lv_area_t a;
  a.x1 = LV_MIN(p1.x, p2.x) - m;
  a.y1 = LV_MIN(p1.y, p2.y) - m;
  a.x2 = LV_MAX(p1.x, p2.x) + m;
  a.y2 = LV_MAX(p1.y, p2.y) + m;
  return a;
}

void analog_draw(lv_event_t* e) {
  lv_obj_t* obj = lv_event_get_target(e);
  lv_draw_ctx_t* ctx = lv_event_get_draw_ctx(e);
  lv_area_t c;
  lv_obj_get_coords(obj, &c);
  float cx = (c.x1 + c.x2) / 2.f, cy = (c.y1 + c.y2) / 2.f;
  const float R = kDialR;
  lv_color_t black = lv_color_hex(0x111111);
  lv_color_t red = lv_color_hex(0xEB0000);
  draw_disc(ctx, cx, cy, R + 12.f, lv_color_hex(0x3A3F48));  // case
  draw_disc(ctx, cx, cy, R, lv_color_hex(0xF4F4F2));         // white face
  lv_draw_line_dsc_t ld;
  lv_draw_line_dsc_init(&ld);
  ld.color = black;
  ld.opa = LV_OPA_COVER;
  for (int i = 0; i < 60; ++i) {
    bool hour = i % 5 == 0;
    ld.width = static_cast<lv_coord_t>(hour ? R * 0.075f : R * 0.028f);
    lv_point_t p1 = polar(cx, cy, i * 6.f, hour ? R * 0.70f : R * 0.86f);
    lv_point_t p2 = polar(cx, cy, i * 6.f, R * 0.95f);
    lv_draw_line(ctx, &ld, &p1, &p2);
  }
  struct tm t;
  int ms = 0;
  local_now(&t, &ms);
  float hour_deg = static_cast<float>(t.tm_hour % 12) * 30.f + static_cast<float>(t.tm_min) * 0.5f;
  float min_deg = static_cast<float>(t.tm_min) * 6.f;  // jumps on the full minute
  draw_hand(ctx, cx, cy, hour_deg, R * 0.24f, R * 0.64f, R * 0.105f, R * 0.085f, black);
  draw_hand(ctx, cx, cy, min_deg, R * 0.24f, R * 0.93f, R * 0.085f, R * 0.06f, black);
  float sd = sec_angle < 0.f ? second_angle(t, ms) : sec_angle;
  ld.color = red;
  ld.width = static_cast<lv_coord_t>(R * 0.024f);
  lv_point_t s1 = polar(cx, cy, sd + 180.f, R * 0.31f);
  lv_point_t s2 = polar(cx, cy, sd, R * 0.645f);
  lv_draw_line(ctx, &ld, &s1, &s2);
  lv_point_t tip = polar(cx, cy, sd, R * 0.645f);
  draw_disc(ctx, tip.x, tip.y, R * 0.105f, red);
  draw_disc(ctx, cx, cy, R * 0.035f, red);
}

void saver_update(bool force) {
  struct tm t;
  int ms = 0;
  bool valid = local_now(&t, &ms);
  int minute = t.tm_hour * 60 + t.tm_min;
  if (disp.saver == kSaverDigital) {
    if (!force && minute == saver_last_min) return;
    saver_last_min = minute;
    char buf[48];
    if (valid) {
      snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
      lv_label_set_text(dig_time, buf);
      snprintf(buf, sizeof(buf), "%s, %d. %s", kWeekday[t.tm_wday], t.tm_mday, kMonth[t.tm_mon]);
      lv_label_set_text(dig_date, buf);
    } else {
      lv_label_set_text(dig_time, "--:--");
      lv_label_set_text(dig_date, "Uhrzeit wird synchronisiert");
    }
    // Small drift every minute against burn-in.
    int k = minute % 9;
    lv_obj_align(saver_digital, LV_ALIGN_CENTER, (k % 3 - 1) * 14, (k / 3 - 1) * 14);
    return;
  }
  if (disp.saver != kSaverAnalog) return;
  if (force || minute != saver_last_min) {
    saver_last_min = minute;
    sec_angle = second_angle(t, ms);
    sec_area = second_area(saver_analog, sec_angle);
    lv_obj_invalidate(saver_analog);  // hour/minute hands moved
    return;
  }
  float deg = second_angle(t, ms);
  if (fabsf(deg - sec_angle) < 0.3f) return;  // resting at 12, or no visible step
  lv_obj_invalidate_area(saver_analog, &sec_area);  // erase the old hand
  sec_angle = deg;
  sec_area = second_area(saver_analog, deg);
  lv_obj_invalidate_area(saver_analog, &sec_area);  // draw the new one
}

void saver_tick(lv_timer_t*) { saver_update(false); }

void screen_off() {
  if (scr_off) return;
  scr_off = true;
  bool clock = disp.saver != kSaverBlack;
  // Co4: the clock saver sits on the darkest petrol; "Schwarz" stays black.
  uint32_t bg = clock && g_theme->header_bar ? g_theme->saver_bg : 0x000000;
  lv_obj_set_style_bg_color(sleep_layer, lv_color_hex(bg), 0);
  if (clock) {
    hide(sleep_hint);
  } else if (disp.hint) {
    show(sleep_hint);
    move_hint();
  } else {
    hide(sleep_hint);
  }
  if (disp.saver == kSaverDigital) show(saver_digital); else hide(saver_digital);
  if (disp.saver == kSaverAnalog) show(saver_analog); else hide(saver_analog);
  lv_scr_load(sleep_layer);
  if (clock) {
    saver_last_min = -1;
    sec_angle = -1.f;
    saver_update(true);
    // Digital: once a second is enough; analog: ~12 fps sweep (dirty rects only).
    lv_timer_set_period(saver_timer, disp.saver == kSaverAnalog ? 80 : 1000);
    lv_timer_resume(saver_timer);
  }
  panel_backlight(clock ? kClockBacklight : disp.hint ? kHintBacklight : 0, 600);
  rem_tick(true);
  console_printf("Screen off (saver %d hint %d)\n", disp.saver, disp.hint ? 1 : 0);
}

void go_home();
void hide_sleep_layer(void*) {
  // Waking always shows the home page (not the page open before the screen
  // went off); open messages (e.g. Wi-Fi failed) stay.
  if (nav != Nav::Home && home_root != nullptr) {
    disp_save_flush(true);
    go_home();
    console_printf("Wake: home\n");
  }
  lv_scr_load(main_screen);
  rem_tick(true);
}

void screen_on() {
  scr_off = false;
  last_activity = now_ms();
  lv_timer_pause(saver_timer);
  panel_backlight(100, 150);
  lv_async_call(hide_sleep_layer, nullptr);  // not from inside the input read
  console_puts("Screen on");
}

void power_tick(lv_timer_t*) {
  uint32_t now = now_ms();
  if (scr_off) {
    if (disp.saver == kSaverBlack && disp.hint && now - hint_moved_ms >= kHintMoveMs) move_hint();
    return;
  }
  if (scr_timeout_s != 0 && !wake_swallow && now - last_activity >= scr_timeout_s * 1000u) screen_off();
}

void build_sleep_layer() {
  sleep_layer = lv_obj_create(nullptr);  // a screen of its own
  lv_obj_set_style_bg_color(sleep_layer, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(sleep_layer, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(sleep_layer, 0, 0);
  lv_obj_set_style_radius(sleep_layer, 0, 0);
  lv_obj_set_style_pad_all(sleep_layer, 0, 0);
  lv_obj_clear_flag(sleep_layer, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(sleep_layer, LV_OBJ_FLAG_CLICKABLE);
  sleep_hint = make_label(sleep_layer, &font_inter_14, lv_color_hex(0x9A9A9A));
  lv_label_set_text(sleep_hint, "wake me up");

  saver_digital = lv_obj_create(sleep_layer);
  style_plain(saver_digital);
  lv_obj_set_size(saver_digital, 440, g_theme->logo ? 300 : 240);
  dig_time = make_label(saver_digital, &font_clock_150, lv_color_hex(kText));
  lv_label_set_text(dig_time, "--:--");
  lv_obj_align(dig_time, LV_ALIGN_TOP_MID, 0, 10);
  dig_date = make_label(saver_digital, &font_inter_20, lv_color_hex(kTextDim));
  lv_obj_align(dig_date, LV_ALIGN_BOTTOM_MID, 0, g_theme->logo ? -76 : -16);
  if (g_theme->logo) {  // moves with the drift, like the clock
    lv_obj_t* logo = lv_img_create(saver_digital);
    lv_img_set_src(logo, &img_co4_logo);
    lv_obj_align(logo, LV_ALIGN_BOTTOM_MID, 0, -10);
  }
  lv_obj_set_style_text_align(dig_date, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(dig_date, 440);
  hide(saver_digital);

  saver_analog = lv_obj_create(sleep_layer);
  style_plain(saver_analog);
  lv_obj_set_style_bg_opa(saver_analog, LV_OPA_TRANSP, 0);
  lv_obj_set_size(saver_analog, kClockD, kClockD);
  lv_obj_center(saver_analog);
  lv_obj_add_event_cb(saver_analog, analog_draw, LV_EVENT_DRAW_MAIN, nullptr);
  hide(saver_analog);

  saver_timer = lv_timer_create(saver_tick, 1000, nullptr);
  lv_timer_pause(saver_timer);
  disp = settings_load_display();
  scr_timeout_s = disp.timeout_s;
  last_activity = now_ms();
  lv_timer_create(power_tick, 250, nullptr);
}

// --- Uhrzeit (NTP server and time zone) ---
void refresh_time_settings() {
  char buf[96];
  snprintf(buf, sizeof(buf), "%s \xC2\xB7 %s", tset.server, tset.tz_name);
  if (set_time_val != nullptr) lv_label_set_text(set_time_val, buf);
  for (int i = 0; i < 4; ++i) {
    if (time_srv_btn[i] == nullptr) continue;
    if (strcmp(tset.server, kNtpServers[i]) == 0) lv_obj_add_state(time_srv_btn[i], LV_STATE_CHECKED);
    else lv_obj_clear_state(time_srv_btn[i], LV_STATE_CHECKED);
    if (strcmp(tset.tz_name, kTzChoices[i].name) == 0) lv_obj_add_state(time_tz_btn[i], LV_STATE_CHECKED);
    else lv_obj_clear_state(time_tz_btn[i], LV_STATE_CHECKED);
  }
  if (time_status == nullptr) return;
  struct tm t;
  if (time_saving) {
    snprintf(buf, sizeof(buf), "Speichere ...");
  } else if (local_now(&t, nullptr)) {
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d \xC2\xB7 %s", t.tm_hour, t.tm_min, t.tm_sec,
             clock_synced() ? "synchronisiert" : "noch nicht synchronisiert");
  } else {
    snprintf(buf, sizeof(buf), "Noch keine Zeit vom Server");
  }
  lv_label_set_text(time_status, buf);
}

void save_time() {
  if (settings_pending) return;
  SettingsRequest r = {};
  r.kind = SettingsRequest::SetTime;
  r.time = tset;
  time_saving = true;
  queue_settings(r);
  clock_apply_tz(tset.tz);
  refresh_time_settings();
}

void open_time() {
  nav = Nav::Time;
  hide_settings();
  bool preset = false;
  for (int i = 0; i < 4; ++i) preset = preset || strcmp(tset.server, kNtpServers[i]) == 0;
  lv_textarea_set_text(time_ta, preset ? "" : tset.server);
  refresh_time_settings();
  show(time_root);
}

void on_srv_btn(lv_event_t* e) {
  if (gesture_consumed) return;
  int i = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  snprintf(tset.server, sizeof(tset.server), "%s", kNtpServers[i]);
  lv_textarea_set_text(time_ta, "");
  console_printf("Time server %s\n", tset.server);
  save_time();
}

void on_tz_btn(lv_event_t* e) {
  if (gesture_consumed) return;
  int i = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  snprintf(tset.tz_name, sizeof(tset.tz_name), "%s", kTzChoices[i].name);
  snprintf(tset.tz, sizeof(tset.tz), "%s", kTzChoices[i].posix);
  console_printf("Time zone %s\n", tset.tz_name);
  save_time();
}

void on_time_ready(lv_event_t*) {
  const char* q = lv_textarea_get_text(time_ta);
  if (q[0] == '\0') return;
  snprintf(tset.server, sizeof(tset.server), "%s", q);
  lv_keyboard_set_textarea(kb, nullptr);
  hide(kb);
  console_printf("Time server %s (custom)\n", tset.server);
  save_time();
}

// 1 s: home header clock, time settings status.
void clock_tick(lv_timer_t*) {
  static int last_min = -2;
  struct tm t;
  bool valid = local_now(&t, nullptr);
  int minute = valid ? t.tm_yday * 1440 + t.tm_hour * 60 + t.tm_min : -1;
  if (minute != last_min && home_time != nullptr) {
    last_min = minute;
    char buf[32];
    if (valid) {
      snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
      lv_label_set_text(home_time, buf);
      snprintf(buf, sizeof(buf), "%s, %d. %s", kWeekdayShort[t.tm_wday], t.tm_mday, kMonthShort[t.tm_mon]);
      lv_label_set_text(home_date, buf);
    } else {
      lv_label_set_text(home_time, "--:--");
      lv_label_set_text(home_date, "");
    }
  }
  if (nav == Nav::Time && !scr_off) refresh_time_settings();
  cam_tick();
  info_tick();
}

void open_settings() {
  if (message_open) return;
  nav = Nav::Settings;
  hide(cam_root);
  hide(home_root);
  hide(cat_root);
  hide(wx_root);
  hide_settings();
  info_hide_views();
  trset_refresh();
  calset_refresh();
  maint_refresh();
  lv_label_set_text(set_wifi_val, cur_ssid[0] != '\0' ? cur_ssid : "nicht verbunden");
  lv_label_set_text(set_loc_val, cur_place);
  refresh_display_settings();
  refresh_time_settings();
  camset_value();
  enset_summary();
  co4set_summary();
  if (set_group >= 0 && set_group < kSetGroups && set_grp_root[set_group] != nullptr) show(set_grp_root[set_group]);
  else show(set_root);
}

void settings_back() {
  disp_save_flush(true);
  open_settings();
}

void on_wifi_row(lv_event_t* e);
void on_loc_row(lv_event_t* e);

void fill_wifi_list() {
  unreg_prefix("wifi:");
  lv_obj_clean(wl_list);
  for (int i = 0; i < ui_ap_n; ++i) {
    const WifiAp& ap = ui_aps[i];
    const char* bars = ap.rssi >= -55 ? ICON_WIFI3 : ap.rssi >= -67 ? ICON_WIFI2 : ap.rssi >= -78 ? ICON_WIFI1 : ICON_WIFI0;
    lv_obj_t* row = make_row(wl_list, 56, bars, kCyan, on_wifi_row, i);
    lv_obj_t* name = make_label(row, &font_inter_sb16, lv_color_hex(kText));
    lv_obj_set_width(name, 260);
    label_dot1(name);
    lv_label_set_text(name, ap.ssid);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 54, 0);
    char info[24];
    snprintf(info, sizeof(info), "%d dBm", ap.rssi);
    lv_obj_t* dbm = make_label(row, &font_inter_14, lv_color_hex(kTextDim));
    lv_label_set_text(dbm, info);
    lv_obj_align(dbm, LV_ALIGN_RIGHT_MID, (ap.secure || strcmp(ap.ssid, cur_ssid) == 0) ? -46 : -16, 0);
    if (ap.secure || strcmp(ap.ssid, cur_ssid) == 0) {
      lv_obj_t* ic = make_label(row, &font_icons_24, lv_color_hex(strcmp(ap.ssid, cur_ssid) == 0 ? kGreen : kTextDim));
      lv_label_set_text(ic, strcmp(ap.ssid, cur_ssid) == 0 ? ICON_CHECK : ICON_LOCK);
      lv_obj_align(ic, LV_ALIGN_RIGHT_MID, -14, 0);
    }
    char key[16];
    snprintf(key, sizeof(key), "wifi:%d", i);
    reg(key, row);
  }
}

constexpr int kWlListY = 122;  // below the status banner (58..114)

// Status banner (Dark Glass): icon + up to 2 lines, never clipped.
void wl_set_status(const char* text, WlTone tone) {
  lv_label_set_text(wl_status, text != nullptr ? text : "");
  uint32_t c = tone == WlTone::Warn ? kAmber : tone == WlTone::Busy ? kAccent : kTextDim;
  lv_label_set_text(wl_icon, tone == WlTone::Warn ? ICON_WARNING : ICON_WIFI3);
  lv_obj_set_style_text_color(wl_icon, lv_color_hex(c), 0);
  lv_obj_set_style_border_color(wl_banner, lv_color_hex(tone == WlTone::Warn ? kAmber : kBorder), 0);
  lv_obj_set_style_border_opa(wl_banner, tone == WlTone::Warn ? LV_OPA_60 : LV_OPA_COVER, 0);
}

// Opened by the firmware: two button rows (Abbrechen | Nochmals versuchen,
// then "Ohne Netzwerk starten" full width); otherwise one row.
void wl_layout(bool setup) {
  if (setup) {
    lv_obj_set_size(wl_list, kScreen - 2 * kMargin, 214);
    lv_obj_set_y(wl_left, 344);
    lv_obj_set_y(wl_right, 344);
    show(wl_offline);
  } else {
    lv_obj_set_size(wl_list, kScreen - 2 * kMargin, 270);
    lv_obj_set_y(wl_left, 400);
    lv_obj_set_y(wl_right, 400);
    hide(wl_offline);
  }
}

void open_wifi_list() {
  nav = Nav::WifiList;
  hide_settings();
  show(wl_root);
  ui_ap_n = 0;
  unreg_prefix("wifi:");
  lv_obj_clean(wl_list);
  wl_scanning = true;
  wl_set_status(wifi_setup_note[0] != '\0' ? wifi_setup_note : "Suche Netzwerke ...",
                wifi_setup_note[0] != '\0' ? WlTone::Warn : WlTone::Busy);
  wl_layout(wifi_setup_note[0] != '\0');
  SettingsRequest r = {};
  r.kind = SettingsRequest::Scan;
  r.scan_gen = wl_gen.start();
  queue_settings(r);
  // Opened by the firmware (saved network unreachable): offer the way back.
  lv_label_set_text(lv_obj_get_child(wl_right, 0), wifi_setup_note[0] != '\0' ? str::kWifiRetry : str::kWifiRescan);
}

// Stop a running scan: the net task stops the driver scan (core 0), a late
// result is dropped by the generation check in ui_wifi_scan_done.
void wifi_scan_abort() {
  if (!wl_scanning) return;
  wifi_scan_cancel(wl_gen.cur);
  wl_gen.cancel();
  wl_scanning = false;
  if (settings_pending && settings_req.kind == SettingsRequest::Scan) settings_pending = false;  // not started yet
  wl_set_status(wifi_setup_note[0] != '\0' ? wifi_setup_note : str::kWifiScanCancelled,
                wifi_setup_note[0] != '\0' ? WlTone::Warn : WlTone::Info);
}

// "Abbrechen" / swipe back. Opened by the firmware: back to the error screen
// (saved credentials untouched, retried in the background; "Nochmals versuchen"
// and "WLAN" there). Otherwise back to the settings.
void wifi_list_leave() {
  wifi_scan_abort();
  if (wifi_setup_note[0] != '\0') {
    nav = Nav::Home;
    ui_show_message(str::kWifiFailedTitle, wifi_setup_note, true);
    return;
  }
  settings_back();
}

void on_wl_cancel(lv_event_t*) {
  if (gesture_consumed) return;
  wifi_list_leave();
}

void on_wl_right(lv_event_t*) {
  if (gesture_consumed) return;
  if (wifi_setup_note[0] == '\0') {
    wifi_scan_abort();
    open_wifi_list();
    return;
  }
  // "Nochmals versuchen": reconnect with the saved credentials (main: ui_take_retry).
  wifi_scan_abort();
  nav = Nav::Home;
  ui_show_connecting();
  retry_pending = true;
}

// "Ohne Netzwerk starten": main pauses Wi-Fi for this session and shows home.
void on_wl_offline(lv_event_t*) {
  if (gesture_consumed) return;
  wifi_scan_abort();
  offline_pending = true;
}

void on_wifi_row(lv_event_t* e) {
  if (gesture_consumed) return;
  int i = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  if (i < 0 || i >= ui_ap_n) return;
  console_printf("Activate wifi %d %s\n", i, ui_aps[i].ssid);
  nav = Nav::WifiPass;
  snprintf(wp_ssid, sizeof(wp_ssid), "%s", ui_aps[i].ssid);
  hide_settings();
  char t[64];
  snprintf(t, sizeof(t), "WLAN: %s", wp_ssid);
  lv_label_set_text(wp_title, t);
  lv_textarea_set_text(wp_ta, "");
  lv_label_set_text(wp_status, ui_aps[i].secure ? "Passwort eingeben, dann OK" : "Offenes Netz: OK zum Verbinden");
  show(wp_root);
  show_keyboard(wp_ta);
}

void on_wifi_ready(lv_event_t*) {
  if (wp_connecting || settings_pending) return;
  SettingsRequest r = {};
  r.kind = SettingsRequest::Connect;
  snprintf(r.creds.ssid, sizeof(r.creds.ssid), "%s", wp_ssid);
  snprintf(r.creds.pass, sizeof(r.creds.pass), "%s", lv_textarea_get_text(wp_ta));
  wp_connecting = true;
  lv_label_set_text(wp_status, "Verbinde ...");
  queue_settings(r);
}

void fill_loc_list() {
  unreg_prefix("loc:");
  lv_obj_clean(loc_list);
  for (int i = 0; i < ui_place_n; ++i) {
    lv_obj_t* row = make_row(loc_list, 64, ICON_PLACE, kOrange, on_loc_row, i);
    lv_obj_t* name = make_label(row, &font_inter_20, lv_color_hex(kText));
    lv_label_set_text(name, ui_places[i].name);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 54, 10);
    lv_obj_t* reg_l = make_label(row, &font_inter_14, lv_color_hex(kTextDim));
    lv_obj_set_width(reg_l, 340);
    label_dot1(reg_l);
    lv_label_set_text(reg_l, ui_places[i].region);
    lv_obj_align(reg_l, LV_ALIGN_BOTTOM_LEFT, 54, -10);
    char key[16];
    snprintf(key, sizeof(key), "loc:%d", i);
    reg(key, row);
  }
}

void open_location() {
  nav = Nav::Location;
  hide_settings();
  ui_place_n = 0;
  unreg_prefix("loc:");
  lv_obj_clean(loc_list);
  lv_textarea_set_text(loc_ta, "");
  lv_label_set_text(loc_status, "Ortsname eingeben, dann OK");
  show(loc_root);
  show_keyboard(loc_ta);
}

void on_loc_ready(lv_event_t*) {
  const char* q = lv_textarea_get_text(loc_ta);
  if (q[0] == '\0' || settings_pending || loc_searching) return;
  SettingsRequest r = {};
  r.kind = SettingsRequest::Geocode;
  snprintf(r.query, sizeof(r.query), "%s", q);
  loc_searching = true;
  lv_label_set_text(loc_status, "Suche ...");
  lv_keyboard_set_textarea(kb, nullptr);
  hide(kb);
  queue_settings(r);
}

void on_loc_row(lv_event_t* e) {
  if (gesture_consumed || settings_pending) return;
  int i = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  if (i < 0 || i >= ui_place_n) return;
  console_printf("Activate location %d %s\n", i, ui_places[i].name);
  SettingsRequest r = {};
  r.kind = SettingsRequest::SetLocation;
  snprintf(r.location.name, sizeof(r.location.name), "%s", ui_places[i].name);
  r.location.lat = ui_places[i].lat;
  r.location.lon = ui_places[i].lon;
  lv_label_set_text(loc_status, "Speichere ...");
  queue_settings(r);
}

void on_kb_event(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t* ta = lv_keyboard_get_textarea(kb);
  if (code == LV_EVENT_READY) {
    if (ta == wp_ta) on_wifi_ready(e);
    if (ta == loc_ta) on_loc_ready(e);
    if (ta == time_ta) on_time_ready(e);
    cam_kb_ready(ta);
    st_kb_ready(ta);
    txt_kb_ready(ta);
    play_kb_ready(ta);
  } else if (code == LV_EVENT_CANCEL) {
    lv_keyboard_set_textarea(kb, nullptr);
    hide(kb);
  }
}

void on_ta_click(lv_event_t* e) {
  if (gesture_consumed) return;
  show_keyboard(lv_event_get_target(e));
}

void on_settings_row(lv_event_t* e) {
  if (gesture_consumed) return;
  int which = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  console_printf("Activate settings row %d\n", which);
  if (which == 0) open_wifi_list();
  if (which == 1) open_location();
  if (which == 2) open_display();
  if (which == 3) open_time();
  if (which == 4) open_camset();
  if (which == 5) open_trset();
  if (which == 6) open_calset();
  if (which == 7) open_remset();
  if (which == 8) open_maint();
  if (which == 9) open_enset();
  if (which == 10) open_play();
  if (which == 11) open_co4set();
  if (which == 12) sw_cycle_target();
}

void on_msg_wifi(lv_event_t*) {
  if (gesture_consumed) return;
  message_open = false;
  hide(message);
  hide(msg_status);
  open_settings();
  open_wifi_list();
}

lv_obj_t* make_ta(lv_obj_t* parent, int y, const char* placeholder) {
  lv_obj_t* ta = lv_textarea_create(parent);
  lv_obj_set_pos(ta, kMargin, y);
  lv_obj_set_size(ta, kScreen - 2 * kMargin, 52);
  lv_textarea_set_one_line(ta, true);
  lv_textarea_set_placeholder_text(ta, placeholder);
  lv_obj_set_style_text_font(ta, &font_inter_20, 0);
  style_glass(ta, 14);
  lv_obj_set_style_pad_all(ta, 12, 0);
  lv_obj_set_style_text_color(ta, lv_color_hex(kText), 0);
  lv_obj_set_style_border_color(ta, lv_color_hex(kAccent), LV_STATE_FOCUSED);
  lv_obj_add_event_cb(ta, on_ta_click, LV_EVENT_CLICKED, nullptr);
  return ta;
}

lv_obj_t* make_choice(lv_obj_t* parent, int x, int y, int w, int h, const char* text, lv_event_cb_t cb, int user,
                      const char* key) {
  lv_obj_t* b = lv_btn_create(parent);
  lv_obj_set_size(b, w, h);
  lv_obj_set_pos(b, x, y);
  style_glass(b, 16);
  style_checked(b);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<intptr_t>(user)));
  watch(b);
  reg(key, b);
  lv_obj_t* t = make_label(b, &font_inter_sb16, lv_color_hex(kText));
  lv_label_set_text(t, text);
  lv_obj_center(t);
  return b;
}

lv_obj_t* make_caption(lv_obj_t* parent, int y, const char* text) {
  lv_obj_t* l = make_label(parent, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_set_pos(l, 24, y);
  lv_label_set_text(l, text);
  return l;
}

// --- Design switch (confirm, save, restart) ---
lv_obj_t* design_confirm = nullptr;
uint8_t design_pending = 0;

void on_design_confirm(lv_event_t* e) {
  if (gesture_consumed) return;
  hide(design_confirm);
  if (lv_event_get_user_data(e) == nullptr) return;  // Abbrechen
  settings_save_design(design_pending);
  lv_timer_t* t = lv_timer_create(
      [](lv_timer_t*) {
        disp_save_flush(true);  // like restart_now (panel_ui_api.inc)
        esp_restart();
      },
      300, nullptr);
  lv_timer_set_repeat_count(t, 1);
}

void on_design_btn(lv_event_t* e) {
  int i = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  lv_obj_t* b = lv_event_get_target(e);
  if (&ui_theme_get(i) == g_theme) {  // already active: keep it checked
    lv_obj_add_state(b, LV_STATE_CHECKED);
    return;
  }
  lv_obj_clear_state(b, LV_STATE_CHECKED);
  design_pending = static_cast<uint8_t>(i);
  show(design_confirm);
  lv_obj_move_foreground(design_confirm);
}

void build_design_confirm(lv_obj_t* screen) {
  design_confirm = lv_obj_create(screen);
  lv_obj_set_size(design_confirm, kScreen - 2 * kMargin, 200);
  lv_obj_center(design_confirm);
  style_glass(design_confirm, 24);
  lv_obj_set_style_border_color(design_confirm, lv_color_hex(kAccent), 0);
  lv_obj_set_style_border_width(design_confirm, 2, 0);
  lv_obj_set_style_pad_all(design_confirm, 20, 0);
  lv_obj_clear_flag(design_confirm, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* t = make_label(design_confirm, &font_inter_sb16, lv_color_hex(kText));
  lv_label_set_text(t, "Design wechseln?");
  lv_obj_set_pos(t, 0, 0);
  lv_obj_t* d = make_label(design_confirm, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_set_width(d, kScreen - 2 * kMargin - 40);
  lv_label_set_long_mode(d, LV_LABEL_LONG_WRAP);
  lv_label_set_text(d, "Das Panel startet dazu neu. Deine Einstellungen bleiben erhalten.");
  lv_obj_set_pos(d, 0, 30);
  const char* names[2] = {"Abbrechen", "Neu starten"};
  const char* keys[2] = {"design:no", "design:yes"};
  for (int i = 0; i < 2; ++i) {
    lv_obj_t* b = lv_btn_create(design_confirm);
    lv_obj_set_size(b, 170, 52);
    lv_obj_align(b, i == 0 ? LV_ALIGN_BOTTOM_LEFT : LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_radius(b, 16, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(i == 0 ? kBorder : kAccent), 0);
    lv_obj_add_event_cb(b, on_design_confirm, LV_EVENT_CLICKED, i == 0 ? nullptr : reinterpret_cast<void*>(1));
    lv_obj_t* l = make_label(b, &font_inter_sb16, lv_color_hex(i == 0 ? kText : 0x0B2129));
    lv_label_set_text(l, names[i]);
    lv_obj_center(l);
    reg(keys[i], b);
  }
  hide(design_confirm);
}

// Settings menu: few top-level entries, related screens grouped. The leaf
// screens, their widget keys and their NVS keys are unchanged; only the way
// to them got one level more. "Anzeige" has a single screen and opens it
// directly. Energy *data* is not shown from here: it lives on the home
// Energie tile (Co4 if set up, else whatwatt Go); only its setup is here.
struct SetLeaf { const char* name; const char* icon; uint32_t color; lv_obj_t** val; const char* key; int id; };

void make_set_row(lv_obj_t* list, const char* name, const char* icon, uint32_t color, lv_obj_t** val, const char* key,
                  lv_event_cb_t cb, int id) {
  lv_obj_t* row = make_row(list, 72, icon, color, cb, id);
  reg(key, row);
  lv_obj_t* l = make_label(row, &font_inter_sb16, lv_color_hex(kText));
  lv_label_set_text(l, name);
  lv_obj_align(l, LV_ALIGN_TOP_LEFT, 56, 13);
  lv_obj_t* v = make_label(row, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_set_width(v, 320);
  label_dot1(v);
  lv_obj_align(v, LV_ALIGN_BOTTOM_LEFT, 56, -13);
  if (val != nullptr) *val = v;
  else lv_label_set_text(v, "");
  lv_obj_t* chev = make_label(row, &font_icons_24, lv_color_hex(kTextDim));
  lv_label_set_text(chev, ICON_CHEVRON);
  lv_obj_align(chev, LV_ALIGN_RIGHT_MID, -14, 0);
}

void on_set_group(lv_event_t* e) {
  if (gesture_consumed) return;
  int g = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  console_printf("Activate settings group %d\n", g);
  if (g == kSetGroupDisplay) {
    open_display();
    return;
  }
  set_group = g;
  open_settings();
}

void build_settings_screens(lv_obj_t* screen) {
  set_root = make_screen_root(screen, "Einstellungen");
  lv_obj_t* set_list = make_list(set_root, 56, 404);
  static const SetLeaf g_net[] = {
      {"WLAN", ICON_WIFI3, kAccent, &set_wifi_val, "set:wifi", 0},
      {"Uhrzeit", ICON_SCHEDULE, kGreen, &set_time_val, "set:time", 3},
  };
  static const SetLeaf g_info[] = {
      {"Wetter-Ort", ICON_PLACE, kAmber, &set_loc_val, "set:loc", 1},
      {"\xC3\x96V", ICON_TRAIN, kAccent, &set_tr_val, "set:oev", 5},
      {"Kalender", ICON_EVENT, kPurple, &set_cal_val, "set:cal", 6},
      {"Kameras", ICON_VIDEOCAM, kTeal, &set_cam_val, "set:cams", 4},
      {"Erinnerungen", ICON_NOTIFY, kAmber, &set_rem_val, "set:rem", 7},
      {"Schalter-Taste", ICON_TOGGLE, kGreen, &set_sw_val, "set:swtarget", 12},
  };
  static const SetLeaf g_en[] = {
      {"Co4", ICON_BOLT, kGreen, &set_co4_val, "set:co4", 11},
      {"whatwatt Go & PV", ICON_BOLT, kAmber, &set_en_val, "set:energy", 9},
  };
  static const SetLeaf g_sys[] = {
      {"Wartung & Update", ICON_MEMORY, kOrange, &set_maint_val, "set:maint", 8},
      {"Playground", ICON_ROUTER, kTeal, &set_play_val, "set:play", 10},
  };
  struct Group { const char* name; const char* sub; const char* icon; uint32_t color; const char* key; const SetLeaf* leaf; int n; };
  const Group groups[kSetGroups] = {
      {"Netzwerk & Zeit", "WLAN \xC2\xB7 Zeitserver \xC2\xB7 Zeitzone", ICON_WIFI3, kAccent, "set:g:net", g_net, 2},
      {"Anzeige", "Ausschalten \xC2\xB7 Bildschirmschoner \xC2\xB7 Design", ICON_SCREEN, kPurple, "set:disp", nullptr, 0},
      {"Orte & Infos", "Wetter \xC2\xB7 \xC3\x96V \xC2\xB7 Kalender \xC2\xB7 Kameras \xC2\xB7 Erinnerungen \xC2\xB7 Schalter", ICON_PLACE, kAmber, "set:g:info", g_info, 6},
      {"Energie-Quellen", "Co4 \xC2\xB7 whatwatt Go \xC2\xB7 PV", ICON_BOLT, kGreen, "set:g:energy", g_en, 2},
      {"System", "Info \xC2\xB7 Update \xC2\xB7 Diagnose \xC2\xB7 Playground", ICON_MEMORY, kOrange, "set:g:sys", g_sys, 2},
  };
  for (int g = 0; g < kSetGroups; ++g) {
    lv_obj_t* sub = nullptr;
    make_set_row(set_list, groups[g].name, groups[g].icon, groups[g].color, g == kSetGroupDisplay ? &set_disp_val : &sub,
                 groups[g].key, on_set_group, g);
    if (sub != nullptr) lv_label_set_text(sub, groups[g].sub);
    if (groups[g].leaf == nullptr) continue;
    set_grp_root[g] = make_screen_root(screen, groups[g].name);
    lv_obj_t* list = make_list(set_grp_root[g], 56, 404);
    for (int i = 0; i < groups[g].n; ++i) {
      const SetLeaf& L = groups[g].leaf[i];
      make_set_row(list, L.name, L.icon, L.color, L.val, L.key, on_settings_row, L.id);
    }
    lv_obj_t* gp = lv_obj_create(list);
    lv_obj_set_size(gp, 10, 8);
    style_plain(gp);
  }
  lv_obj_t* pad = lv_obj_create(set_list);  // room above the home bar at the end of the list
  lv_obj_set_size(pad, 10, 8);
  style_plain(pad);

  // Bildschirm: timeout, screensaver and the black-mode hint.
  disp_root = make_screen_root(screen, "Bildschirm");
  make_caption(disp_root, 58, "Ausschalten nach");
  for (int i = 0; i < kTimeoutChoiceN; ++i) {
    char key[8];
    snprintf(key, sizeof(key), "to:%d", i);
    disp_to_btn[i] = make_choice(disp_root, kMargin + (i % 3) * 148, 80 + (i / 3) * 60, 136, 50,
                                 timeout_text(kTimeoutChoices[i]), on_timeout_btn, i, key);
  }
  make_caption(disp_root, 206, "Bildschirmschoner");
  static const char* kSaverLabel[3] = {"Schwarz", "Digital", "Analog"};
  for (int i = 0; i < 3; ++i) {
    char key[10];
    snprintf(key, sizeof(key), "saver:%d", i);
    disp_saver_btn[i] = make_choice(disp_root, kMargin + i * 148, 228, 136, 56, kSaverLabel[i], on_saver_btn, i, key);
  }
  lv_obj_t* hint_row = make_row(disp_root, 84, nullptr, 0, on_hint_row, 0);
  lv_obj_set_pos(hint_row, kMargin, 300);
  reg("disp:hint", hint_row);
  lv_obj_t* hn = make_label(hint_row, &font_inter_sb16, lv_color_hex(kText));
  lv_label_set_text(hn, "Hinweis anzeigen");
  lv_obj_align(hn, LV_ALIGN_TOP_LEFT, 16, 16);
  lv_obj_t* hd = make_label(hint_row, &font_inter_14, lv_color_hex(kTextDim));
  lv_label_set_text(hd, "Bei Schwarz: dezentes \"wake me up\"");
  lv_obj_set_width(hd, 320);
  label_dot1(hd);
  lv_obj_align(hd, LV_ALIGN_BOTTOM_LEFT, 16, -16);
  disp_hint_sw = lv_switch_create(hint_row);
  lv_obj_set_size(disp_hint_sw, 60, 32);
  lv_obj_align(disp_hint_sw, LV_ALIGN_RIGHT_MID, -14, 0);
  lv_obj_clear_flag(disp_hint_sw, LV_OBJ_FLAG_CLICKABLE);  // the whole row toggles
  lv_obj_set_style_bg_color(disp_hint_sw, lv_color_hex(kBorder), static_cast<lv_style_selector_t>(LV_PART_MAIN));
  lv_obj_set_style_bg_color(disp_hint_sw, lv_color_hex(kAccent), static_cast<lv_style_selector_t>(LV_PART_INDICATOR) | LV_STATE_CHECKED);

  // Design: Dark Glass or Co4. Fixed per boot, so a change restarts after asking.
  make_page_scrollable(disp_root);
  make_caption(disp_root, 400, "Design");
  for (int i = 0; i < kDesignCount; ++i) {
    char key[10];
    snprintf(key, sizeof(key), "design:%d", i);
    lv_obj_t* b = make_choice(disp_root, kMargin + i * 222, 422, 210, 56, ui_theme_get(i).name, on_design_btn, i, key);
    if (&ui_theme_get(i) == g_theme) lv_obj_add_state(b, LV_STATE_CHECKED);
  }
  build_design_confirm(lv_obj_get_screen(disp_root));

  // Uhrzeit: NTP server and time zone.
  time_root = make_screen_root(screen, "Uhrzeit");
  make_caption(time_root, 58, "Zeitserver (NTP)");
  for (int i = 0; i < 4; ++i) {
    char key[8];
    snprintf(key, sizeof(key), "ntp:%d", i);
    time_srv_btn[i] = make_choice(time_root, kMargin + (i % 2) * 226, 80 + (i / 2) * 56, 214, 48, kNtpServers[i],
                                  on_srv_btn, i, key);
  }
  time_ta = make_ta(time_root, 194, "Eigener Server");
  lv_obj_set_height(time_ta, 50);
  reg("ntp:custom", time_ta);
  make_caption(time_root, 258, "Zeitzone");
  for (int i = 0; i < 4; ++i) {
    char key[8];
    snprintf(key, sizeof(key), "tz:%d", i);
    time_tz_btn[i] = make_choice(time_root, kMargin + (i % 2) * 226, 280 + (i / 2) * 56, 214, 48, kTzChoices[i].label,
                                 on_tz_btn, i, key);
  }
  time_status = make_label(time_root, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_set_pos(time_status, 24, 404);
  lv_obj_set_width(time_status, 432);
  lv_label_set_long_mode(time_status, LV_LABEL_LONG_DOT);

  wl_root = make_screen_root(screen, "WLAN");
  wl_banner = lv_obj_create(wl_root);
  lv_obj_set_pos(wl_banner, kMargin, 58);
  lv_obj_set_size(wl_banner, kScreen - 2 * kMargin, 56);
  style_glass(wl_banner, 14);
  lv_obj_set_style_pad_all(wl_banner, 0, 0);
  lv_obj_clear_flag(wl_banner, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(wl_banner, LV_OBJ_FLAG_CLICKABLE);
  wl_icon = make_label(wl_banner, &font_icons_24, lv_color_hex(kCyan));
  lv_label_set_text(wl_icon, ICON_WIFI3);  // colour set by wl_set_status (theme)
  lv_obj_align(wl_icon, LV_ALIGN_LEFT_MID, 14, 0);
  wl_status = make_label(wl_banner, &font_inter_16, lv_color_hex(kText));
  lv_obj_set_width(wl_status, kScreen - 2 * kMargin - 64);
  lv_obj_set_height(wl_status, 42);  // 2 lines of 16 px, then "..."
  lv_label_set_long_mode(wl_status, LV_LABEL_LONG_DOT);
  lv_obj_align(wl_status, LV_ALIGN_LEFT_MID, 50, 0);
  lv_label_set_text(wl_status, "");
  wl_list = make_list(wl_root, kWlListY, 270);
  wl_left = make_choice(wl_root, kMargin, 400, 214, 56, str::kWifiCancel, on_wl_cancel, 0, "wl:cancel");
  wl_right = make_choice(wl_root, kMargin + 226, 400, 214, 56, str::kWifiRescan, on_wl_right, 0, "wl:retry");
  wl_offline = make_choice(wl_root, kMargin, 408, kScreen - 2 * kMargin, 52, str::kWifiOffline, on_wl_offline, 0,
                           "wl:offline");
  lv_obj_set_style_bg_color(wl_offline, lv_color_hex(kAccent), 0);
  lv_obj_set_style_bg_opa(wl_offline, LV_OPA_30, 0);
  hide(wl_offline);

  wp_root = make_screen_root(screen, "WLAN");
  wp_title = lv_obj_get_child(wp_root, 0);
  wp_ta = make_ta(wp_root, 56, "Passwort");
  lv_textarea_set_password_mode(wp_ta, true);
  lv_textarea_set_password_bullet(wp_ta, "*");
  wp_status = make_label(wp_root, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_set_pos(wp_status, 24, 118);
  lv_obj_set_width(wp_status, 432);
  lv_label_set_long_mode(wp_status, LV_LABEL_LONG_WRAP);

  loc_root = make_screen_root(screen, "Wetter-Ort");
  loc_ta = make_ta(loc_root, 56, "Ort suchen");
  loc_status = make_label(loc_root, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_set_pos(loc_status, 24, 116);
  loc_list = make_list(loc_root, 140, 316);

  kb = lv_keyboard_create(screen);
  lv_obj_set_size(kb, kScreen, 220);
  lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_set_style_text_font(kb, &lv_font_montserrat_16, 0);
  lv_obj_add_event_cb(kb, on_kb_event, LV_EVENT_ALL, nullptr);
  hide(kb);
}

bool obj_visible(lv_obj_t* o) {
  for (; o != nullptr; o = lv_obj_get_parent(o)) {
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return false;
  }
  return true;
}

