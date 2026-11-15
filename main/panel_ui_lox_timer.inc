// Analog Daytimer detail: current level, 0-10 stepper, duration, start/stop
// override, and a same-category Pushbutton if the structure linked one.

lv_obj_t* timer_title = nullptr;
lv_obj_t* timer_room = nullptr;
lv_obj_t* timer_level = nullptr;
lv_obj_t* timer_remain = nullptr;
lv_obj_t* timer_minus = nullptr;
lv_obj_t* timer_plus = nullptr;
lv_obj_t* timer_sel = nullptr;
lv_obj_t* timer_dur_btn[kLoxDurCount] = {};
lv_obj_t* timer_start = nullptr;
lv_obj_t* timer_stop = nullptr;
lv_obj_t* timer_pulse = nullptr;
lv_obj_t* timer_pulse_lbl = nullptr;

int timer_index = -1;
int timer_sel_level = 0;
bool timer_sel_dirty = false;
LoxDurationId timer_dur = kLoxDur1h;

int override_s[kLoxoneControlCap] = {};

void timer_refresh();
void close_timer();
void open_timer(int index);

void style_timer_btn(lv_obj_t* btn, lv_coord_t radius) {
  style_glass(btn, radius);
  style_checked(btn);
  lv_obj_add_flag(btn, LV_OBJ_FLAG_CHECKABLE);
  lv_obj_clear_state(btn, LV_STATE_CHECKED);
}

lv_obj_t* make_timer_btn(lv_obj_t* parent, int x, int y, int w, int h, lv_event_cb_t cb, intptr_t tag) {
  lv_obj_t* btn = lv_obj_create(parent);
  lv_obj_set_pos(btn, x, y);
  lv_obj_set_size(btn, w, h);
  style_timer_btn(btn, 16);
  lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, reinterpret_cast<void*>(tag));
  lv_obj_add_event_cb(btn, on_press_visual, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(btn, on_press_visual, LV_EVENT_PRESSING, nullptr);
  lv_obj_add_event_cb(btn, on_press_visual, LV_EVENT_CLICKED, nullptr);
  return btn;
}

void on_timer_back(lv_event_t* event) {
  (void)event;
  if (gesture_consumed) {
    return;
  }
  close_timer();
}

void on_timer_step(lv_event_t* event) {
  if (gesture_consumed) {
    return;
  }
  int dir = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
  int next = timer_sel_level + dir;
  if (next < kLoxTimerLevelMin) {
    next = kLoxTimerLevelMin;
  }
  if (next > kLoxTimerLevelMax) {
    next = kLoxTimerLevelMax;
  }
  if (next == timer_sel_level) {
    return;
  }
  timer_sel_level = next;
  timer_sel_dirty = true;
  timer_refresh();
}

void on_timer_dur(lv_event_t* event) {
  if (gesture_consumed) {
    return;
  }
  int id = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
  if (id < 0 || id >= kLoxDurCount) {
    return;
  }
  timer_dur = static_cast<LoxDurationId>(id);
  timer_refresh();
}

void on_timer_start(lv_event_t* event) {
  (void)event;
  if (gesture_consumed || command_pending) {
    return;
  }
  if (timer_index < 0 || static_cast<size_t>(timer_index) >= control_count) {
    return;
  }
  uint32_t secs = lox_duration_seconds(timer_dur);
  queue_command(timer_index, "startOverride", timer_sel_level, secs);
  timer_refresh();
}

void on_timer_stop(lv_event_t* event) {
  (void)event;
  if (gesture_consumed || command_pending) {
    return;
  }
  if (timer_index < 0 || static_cast<size_t>(timer_index) >= control_count) {
    return;
  }
  queue_command(timer_index, "stopOverride");
  timer_refresh();
}

int timer_pulse_index() {
  if (timer_index < 0 || static_cast<size_t>(timer_index) >= control_count) {
    return -1;
  }
  const char* action = controls[timer_index].pulse_action;
  if (action[0] == '\0') {
    return -1;
  }
  for (size_t i = 0; i < control_count; ++i) {
    if (controls[i].kind == kLoxonePulse && strcmp(controls[i].action, action) == 0) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void on_timer_pulse(lv_event_t* event) {
  (void)event;
  if (gesture_consumed || command_pending) {
    return;
  }
  int p = timer_pulse_index();
  if (p < 0) {
    return;
  }
  queue_command(p, "Pulse");
  timer_refresh();
}

void timer_refresh() {
  if (timer_root == nullptr || timer_index < 0 || static_cast<size_t>(timer_index) >= control_count) {
    return;
  }
  const LoxoneControl& ctrl = controls[timer_index];
  lv_label_set_text(timer_title, ctrl.name);
  lv_label_set_text(timer_room, ctrl.room[0] != '\0' ? ctrl.room : "");

  char cur[16];
  if (known[timer_index]) {
    lox_format_level(position_pct[timer_index], cur, sizeof(cur));
  } else {
    snprintf(cur, sizeof(cur), "%s", needs_read[timer_index] ? "..." : "--");
  }
  lv_label_set_text(timer_level, cur);
  lv_obj_set_style_text_color(timer_level,
                              lv_color_hex(known[timer_index] && position_pct[timer_index] > 0 ? kAccent : kText), 0);

  char rem[48];
  rem[0] = '\0';
  if (command_pending && pending.index == timer_index && strcmp(pending.command, "startOverride") == 0) {
    snprintf(rem, sizeof(rem), "Sende...");
  } else if (known[timer_index] && override_s[timer_index] > 0) {
    lox_format_remaining(override_s[timer_index], rem, sizeof(rem));
    size_t n = strlen(rem);
    if (n > 0 && n + 13 < sizeof(rem)) {
      snprintf(rem + n, sizeof(rem) - n, " Handbetrieb");
    }
  }
  lv_label_set_text(timer_remain, rem);

  char sel[16];
  lox_format_level(timer_sel_level, sel, sizeof(sel));
  lv_label_set_text(timer_sel, sel);

  for (int i = 0; i < kLoxDurCount; ++i) {
    if (timer_dur_btn[i] == nullptr) {
      continue;
    }
    if (i == static_cast<int>(timer_dur)) {
      lv_obj_add_state(timer_dur_btn[i], LV_STATE_CHECKED);
    } else {
      lv_obj_clear_state(timer_dur_btn[i], LV_STATE_CHECKED);
    }
  }

  bool busy = command_pending;
  if (busy) {
    lv_obj_add_state(timer_start, LV_STATE_DISABLED);
    lv_obj_add_state(timer_stop, LV_STATE_DISABLED);
    if (timer_pulse != nullptr) {
      lv_obj_add_state(timer_pulse, LV_STATE_DISABLED);
    }
  } else {
    lv_obj_clear_state(timer_start, LV_STATE_DISABLED);
    if (known[timer_index] && override_s[timer_index] > 0) {
      lv_obj_clear_state(timer_stop, LV_STATE_DISABLED);
    } else {
      lv_obj_add_state(timer_stop, LV_STATE_DISABLED);
    }
    if (timer_pulse != nullptr) {
      lv_obj_clear_state(timer_pulse, LV_STATE_DISABLED);
    }
  }

  const bool has_pulse = ctrl.pulse_action[0] != '\0' && ctrl.pulse_name[0] != '\0';
  if (has_pulse) {
    show(timer_pulse);
    lv_label_set_text(timer_pulse_lbl, ctrl.pulse_name);
  } else {
    hide(timer_pulse);
  }
}

void close_timer() {
  hide(timer_root);
  if (current_section >= kSecFav && section_pages[current_section] > 0) {
    nav = Nav::Category;
    show(cat_root);
    refresh_category();
    arm_visible_reads();
    publish_status();
    return;
  }
  open_lox_menu();
}

void open_timer(int index) {
  if (index < 0 || static_cast<size_t>(index) >= control_count) {
    return;
  }
  if (controls[index].kind != kLoxoneTimer) {
    return;
  }
  timer_index = index;
  if (!timer_sel_dirty || !known[index]) {
    timer_sel_level = known[index] ? position_pct[index] : 0;
    if (timer_sel_level < kLoxTimerLevelMin) {
      timer_sel_level = kLoxTimerLevelMin;
    }
    if (timer_sel_level > kLoxTimerLevelMax) {
      timer_sel_level = kLoxTimerLevelMax;
    }
  }
  timer_sel_dirty = false;
  nav = Nav::Timer;
  message_open = false;
  hide(message);
  hide(home_root);
  hide(cat_root);
  hide(wx_root);
  hide(lox_root);
  hide_settings();
  info_hide_views();
  show(timer_root);
  if (has_status(controls[index]) && reread_after[index] == 0) {
    needs_read[index] = true;
    prefer[index] = true;
  }
  timer_refresh();
  publish_status();
}

void build_timer_screen(lv_obj_t* screen) {
  timer_root = lv_obj_create(screen);
  lv_obj_set_pos(timer_root, 0, 0);
  lv_obj_set_size(timer_root, kScreen, kScreen);
  style_plain(timer_root);

  lv_obj_t* back = lv_obj_create(timer_root);
  lv_obj_set_pos(back, 8, 8);
  lv_obj_set_size(back, 88, 48);
  style_plain(back);
  lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(back, on_timer_back, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* back_l = make_label(back, &font_inter_16, lv_color_hex(kTextDim));
  lv_label_set_text(back_l, "ZurÃ¼ck");
  lv_obj_center(back_l);

  timer_title = make_label(timer_root, &font_inter_20, lv_color_hex(kText));
  lv_obj_set_pos(timer_title, 104, 12);
  lv_obj_set_width(timer_title, 356);
  lv_label_set_long_mode(timer_title, LV_LABEL_LONG_DOT);

  timer_room = make_label(timer_root, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_set_pos(timer_room, 104, 38);
  lv_obj_set_width(timer_room, 356);
  lv_label_set_long_mode(timer_room, LV_LABEL_LONG_DOT);

  timer_level = make_label(timer_root, &font_inter_light_40, lv_color_hex(kText));
  lv_obj_set_width(timer_level, kScreen);
  lv_obj_set_style_text_align(timer_level, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(timer_level, 0, 64);

  timer_remain = make_label(timer_root, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_set_width(timer_remain, kScreen - 40);
  lv_obj_set_style_text_align(timer_remain, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(timer_remain, 20, 112);

  timer_minus = make_timer_btn(timer_root, 70, 148, 72, 64, on_timer_step, -1);
  lv_obj_t* minus_l = make_label(timer_minus, &font_inter_20, lv_color_hex(kText));
  lv_label_set_text(minus_l, "-");
  lv_obj_center(minus_l);

  timer_sel = make_label(timer_root, &font_inter_light_40, lv_color_hex(kText));
  lv_obj_set_pos(timer_sel, 160, 148);
  lv_obj_set_size(timer_sel, 160, 64);
  lv_obj_set_style_text_align(timer_sel, LV_TEXT_ALIGN_CENTER, 0);

  timer_plus = make_timer_btn(timer_root, 338, 148, 72, 64, on_timer_step, 1);
  lv_obj_t* plus_l = make_label(timer_plus, &font_inter_20, lv_color_hex(kText));
  lv_label_set_text(plus_l, "+");
  lv_obj_center(plus_l);

  const int chip_y0 = 228;
  const int chip_w = 140;
  const int chip_h = 44;
  const int chip_g = 10;
  const int chip_x0 = 20;
  for (int i = 0; i < kLoxDurCount; ++i) {
    int row = i < 3 ? 0 : 1;
    int col = i < 3 ? i : i - 3;
    int w = (i == kLoxDurUntilCancel) ? 210 : chip_w;
    int x = chip_x0 + col * (chip_w + chip_g);
    int y = chip_y0 + row * (chip_h + 8);
    timer_dur_btn[i] = make_timer_btn(timer_root, x, y, w, chip_h, on_timer_dur, i);
    lv_obj_t* lab = make_label(timer_dur_btn[i], &font_inter_14, lv_color_hex(kText));
    lv_label_set_text(lab, lox_duration_label(static_cast<LoxDurationId>(i)));
    lv_obj_center(lab);
  }

  timer_start = make_timer_btn(timer_root, 20, 340, 214, 56, on_timer_start, 0);
  lv_obj_t* start_l = make_label(timer_start, &font_inter_20, lv_color_hex(kText));
  lv_label_set_text(start_l, "Starten");
  lv_obj_center(start_l);

  timer_stop = make_timer_btn(timer_root, 246, 340, 214, 56, on_timer_stop, 0);
  lv_obj_t* stop_l = make_label(timer_stop, &font_inter_14, lv_color_hex(kText));
  lv_label_set_text(stop_l, "Handbetrieb beenden");
  lv_obj_set_width(stop_l, 198);
  lv_obj_set_style_text_align(stop_l, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(stop_l);

  timer_pulse = make_timer_btn(timer_root, 20, 408, 440, 56, on_timer_pulse, 0);
  timer_pulse_lbl = make_label(timer_pulse, &font_inter_20, lv_color_hex(kText));
  lv_obj_set_width(timer_pulse_lbl, 420);
  lv_obj_set_style_text_align(timer_pulse_lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(timer_pulse_lbl);

  hide(timer_root);
}
