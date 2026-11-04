// Termin-Erinnerungen: included by panel_ui.cpp after panel_ui_info.inc.
// A reminder card shows over every screen (also over the screensaver layer)
// once an event enters its lead window, until the user taps OK. Acknowledged
// reminders are stored in NVS (hash of event UID + start), so they stay gone
// after a reboot. Several pending reminders queue (oldest first).

const uint16_t kCalLeadPresets[] = {15, 30, 60, 120, 180};  // timed events, minutes

ReminderSettings rem_cfg = {};
struct Pending {
  CalEvent ev;
  uint32_t key;
};
constexpr int kRemQueue = 8;
EXT_RAM_BSS_ATTR Pending rem_q[kRemQueue];
int rem_n = 0;
// Harness: synthetic events (REMTEST) that join the calendar data.
EXT_RAM_BSS_ATTR CalEvent rem_test[4];
int rem_test_n = 0;

lv_obj_t* rem_card = nullptr;
lv_obj_t* rem_icon = nullptr;
lv_obj_t* rem_head = nullptr;
lv_obj_t* rem_title = nullptr;
lv_obj_t* rem_when = nullptr;
lv_obj_t* rem_ok_btn = nullptr;
bool rem_ok_down = false;  // screen off: a press that started on OK
bool rem_shown = false;

lv_obj_t* rem_sw[3] = {};
lv_obj_t* rem_lead_val = nullptr;  // "Termine: Vorlauf"
lv_obj_t* rem_from_val = nullptr;  // "Ganztägig: Am Vortag ab"

uint32_t rem_key(const CalEvent& e) {
  uint32_t h = 2166136261u;
  uint8_t b[12];
  memcpy(b, &e.uid, 4);
  memcpy(b + 4, &e.start, 8);
  for (uint8_t c : b) {
    h ^= c;
    h *= 16777619u;
  }
  return h != 0 ? h : 1;
}

// Local wall time on the event's day (or days before) at hh:mm.
int64_t at_local(int64_t day_of, int days_before, int hh, int mm) {
  time_t tt = static_cast<time_t>(day_of);
  struct tm lt;
  localtime_r(&tt, &lt);
  lt.tm_mday -= days_before;
  lt.tm_hour = hh;
  lt.tm_min = mm;
  lt.tm_sec = 0;
  lt.tm_isdst = -1;
  return static_cast<int64_t>(mktime(&lt));
}

// When the reminder for e appears / drops out (unacknowledged).
// Waste: "Am Vortag ab" .. "Am Sammeltag bis" (Einstellungen > Kalender).
// Family calendar: all-day from "Am Vortag ab" until the end of the event day,
// timed events from the lead time until the start (waste_window.cpp).
int64_t rem_trigger(const CalEvent& e) {
  if (e.kind == 1) return waste_window_start(e.start, waste_from_min);
  return cal_window_start(e.start, e.all_day, rem_cfg.cal_from_min, rem_cfg.cal_lead_min);
}

int64_t rem_expire(const CalEvent& e) {
  if (e.kind == 1) return waste_window_end(e.start, waste_until_min);
  return cal_window_end(e.start, e.all_day);
}

bool rem_queued(uint32_t key) {
  for (int i = 0; i < rem_n; ++i) {
    if (rem_q[i].key == key) return true;
  }
  return false;
}

void rem_consider(const CalEvent& e, int64_t now) {
  bool waste = e.kind == 1;
  if (waste ? !rem_cfg.waste : !rem_cfg.cal) return;
  if (now < rem_trigger(e) || now >= rem_expire(e)) return;
  uint32_t key = rem_key(e);
  if (rem_queued(key) || settings_reminder_acked(key)) return;
  if (rem_n == kRemQueue) return;
  // Keep the queue sorted by start (the soonest first).
  int i = rem_n;
  while (i > 0 && rem_q[i - 1].ev.start > e.start) {
    rem_q[i] = rem_q[i - 1];
    --i;
  }
  rem_q[i].ev = e;
  rem_q[i].key = key;
  ++rem_n;
  console_printf("Reminder queued (kind %d, %d chars, %d pending)\n", e.kind, static_cast<int>(strlen(e.title)), rem_n);
}

void rem_restore_backlight() {
  if (!scr_off) return;
  bool clock = disp.saver != kSaverBlack;
  panel_backlight(clock ? kClockBacklight : disp.hint ? kHintBacklight : 0, 300);
}

// The card shows on the home screen and over every screensaver (not over
// settings or other views, where it would cover controls).
bool rem_want() { return rem_n > 0 && (scr_off || nav == Nav::Home); }

// Only touch the label when the text changes: every set invalidates the card
// (a full redraw costs ~40 ms on this panel).
void set_text_if(lv_obj_t* l, const char* t) {
  if (strcmp(lv_label_get_text(l), t) != 0) lv_label_set_text(l, t);
}

void rem_render() {
  if (rem_card == nullptr) return;
  if (!rem_want()) {
    if (rem_shown) {
      hide(rem_card);
      rem_shown = false;
      rem_restore_backlight();
    }
    return;
  }
  const CalEvent& e = rem_q[0].ev;
  bool waste = e.kind == 1;
  set_text_if(rem_icon, waste ? ICON_RECYCLE : ICON_EVENT);
  lv_color_t ic = lv_color_hex(waste ? kGreen : kPurple);
  if (lv_obj_get_style_text_color(rem_icon, 0).full != ic.full) lv_obj_set_style_text_color(rem_icon, ic, 0);
  char head[48];
  if (rem_n > 1) snprintf(head, sizeof(head), "%s \xC2\xB7 +%d weitere", waste ? "Abfuhr" : "Termin", rem_n - 1);
  else snprintf(head, sizeof(head), "%s", waste ? "Abfuhr" : "Termin");
  set_text_if(rem_head, head);
  set_text_if(rem_title, e.title);
  char d[24], hm[8], b[64];
  fmt_day(d, sizeof(d), e.start);
  fmt_hm(hm, sizeof(hm), e.start);
  int64_t now = time(nullptr);
  if (e.all_day) {
    snprintf(b, sizeof(b), "%s", d);
  } else if (e.start > now && e.start - now < 3600) {
    snprintf(b, sizeof(b), "%s %s \xC2\xB7 in %d min", d, hm, static_cast<int>((e.start - now + 59) / 60));
  } else {
    snprintf(b, sizeof(b), "%s %s", d, hm);
  }
  set_text_if(rem_when, b);
  // Over whatever is shown: the main screen or the screensaver layer.
  lv_obj_t* parent = scr_off ? sleep_layer : main_screen;
  if (lv_obj_get_parent(rem_card) != parent) lv_obj_set_parent(rem_card, parent);
  if (lv_obj_get_index(rem_card) != static_cast<int32_t>(lv_obj_get_child_cnt(parent)) - 1) lv_obj_move_foreground(rem_card);
  if (lv_obj_has_flag(rem_card, LV_OBJ_FLAG_HIDDEN)) show(rem_card);
  if (!rem_shown) {
    rem_shown = true;
    last_activity = now_ms();
  }
  // Black screensaver: light the panel so the card is readable.
  if (scr_off && disp.saver == kSaverBlack && panel_backlight_level() < 50) panel_backlight(50, 300);
}

// Called once a second (also while the screen is off).
void rem_tick(bool force) {
  static uint32_t last = 0;
  if (!force && now_ms() - last < 5000) {
    if (rem_card != nullptr) {
      lv_obj_t* parent = scr_off ? sleep_layer : main_screen;
      if (rem_shown != rem_want() || (rem_shown && lv_obj_get_parent(rem_card) != parent)) rem_render();
    }
    return;
  }
  last = now_ms();
  int64_t now = time(nullptr);
  if (!clock_synced()) return;
  // Drop expired entries.
  int w = 0;
  for (int i = 0; i < rem_n; ++i) {
    if (now < rem_expire(rem_q[i].ev)) rem_q[w++] = rem_q[i];
  }
  rem_n = w;
  if (rem_cfg.on) {
    if (cal_have && cal != nullptr) {
      for (int i = 0; i < cal->n_events; ++i) rem_consider(cal->events[i], now);
      for (int i = 0; i < cal->n_waste; ++i) rem_consider(cal->waste[i], now);
    }
    for (int i = 0; i < rem_test_n; ++i) rem_consider(rem_test[i], now);
  } else {
    rem_n = 0;
  }
  rem_render();
}

void on_rem_ok(lv_event_t*) {
  if (gesture_consumed) return;
  if (rem_n == 0) return;
  bool ok = settings_reminder_ack(rem_q[0].key);
  console_printf("Reminder acknowledged saved=%d\n", ok ? 1 : 0);
  for (int i = 1; i < rem_n; ++i) rem_q[i - 1] = rem_q[i];
  --rem_n;
  rem_render();
  rem_ok_down = false;
  if (scr_off) screen_on();
}

// Screen off with a reminder: only a press on OK goes to LVGL (one-tap
// acknowledge); any other touch wakes the panel (ui_power_touch).
bool rem_ok_hit(int x, int y) {
  if (!rem_shown || rem_ok_btn == nullptr) return false;
  lv_area_t a;
  lv_obj_get_coords(rem_ok_btn, &a);
  bool hit = x >= a.x1 - 8 && x <= a.x2 + 8 && y >= a.y1 - 8 && y <= a.y2 + 8;
  rem_ok_down = hit;
  return hit;
}

bool rem_ok_pressing() { return rem_ok_down && rem_shown; }

// --- Settings: Termin-Erinnerungen -------------------------------------------------

void set_checked(lv_obj_t* o, bool on) {
  if (o == nullptr) return;
  if (on) lv_obj_add_state(o, LV_STATE_CHECKED);
  else lv_obj_clear_state(o, LV_STATE_CHECKED);
}

void remset_refresh() {
  set_checked(rem_sw[0], rem_cfg.on);
  set_checked(rem_sw[1], rem_cfg.cal);
  set_checked(rem_sw[2], rem_cfg.waste);
  if (rem_lead_val != nullptr) {
    uint16_t m = rem_cfg.cal_lead_min;
    if (m % 60 == 0) lv_label_set_text_fmt(rem_lead_val, "%u Std. vor Beginn", m / 60);
    else lv_label_set_text_fmt(rem_lead_val, "%u min vor Beginn", m);
  }
  if (rem_from_val != nullptr)
    lv_label_set_text_fmt(rem_from_val, "%02u:%02u Uhr, bis Ende des Tages", rem_cfg.cal_from_min / 60,
                          rem_cfg.cal_from_min % 60);
  if (set_rem_val != nullptr) {
    if (!rem_cfg.on || (!rem_cfg.cal && !rem_cfg.waste)) {
      lv_label_set_text(set_rem_val, "Aus");
    } else {
      char b[80] = "";
      if (rem_cfg.cal) {
        uint16_t m = rem_cfg.cal_lead_min;
        if (m % 60 == 0) snprintf(b, sizeof(b), "Termine %u Std.", m / 60);
        else snprintf(b, sizeof(b), "Termine %u min", m);
      }
      if (rem_cfg.waste)
        snprintf(b + strlen(b), sizeof(b) - strlen(b), "%sAbfuhr ab %02u:%02u", rem_cfg.cal ? " \xC2\xB7 " : "",
                 waste_from_min / 60, waste_from_min % 60);
      lv_label_set_text(set_rem_val, b);
    }
  }
}

void rem_changed() {
  bool ok = settings_save_reminders(rem_cfg);
  console_printf("Reminders on=%d cal=%d lead=%u from=%u waste=%d saved=%d\n", rem_cfg.on, rem_cfg.cal,
                 rem_cfg.cal_lead_min, rem_cfg.cal_from_min, rem_cfg.waste, ok ? 1 : 0);
  // New settings: rebuild the queue (acknowledged ones stay acknowledged).
  rem_n = 0;
  remset_refresh();
  rem_tick(true);
}

void on_rem_switch(lv_event_t* e) {
  if (gesture_consumed) return;
  int which = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  if (which == 0) rem_cfg.on = !rem_cfg.on;
  if (which == 1) rem_cfg.cal = !rem_cfg.cal;
  if (which == 2) rem_cfg.waste = !rem_cfg.waste;
  rem_changed();
}

// "Termine: Vorlauf": each tap steps to the next preset.
void on_rem_lead(lv_event_t*) {
  if (gesture_consumed) return;
  constexpr int n = sizeof(kCalLeadPresets) / sizeof(kCalLeadPresets[0]);
  int i = 0;
  while (i < n && kCalLeadPresets[i] <= rem_cfg.cal_lead_min) ++i;
  ui_set_cal_lead(kCalLeadPresets[i < n ? i : 0]);
}

// "Ganztägig: Am Vortag ab": same steps as the waste row.
void on_rem_from(lv_event_t*) {
  if (gesture_consumed) return;
  constexpr int n = sizeof(kWasteFromPresets) / sizeof(kWasteFromPresets[0]);
  int i = 0;
  while (i < n && kWasteFromPresets[i] <= rem_cfg.cal_from_min) ++i;
  ui_set_cal_from(kWasteFromPresets[i < n ? i : 0]);
}

void open_remset() {
  nav = Nav::RemSet;
  hide_settings();
  remset_refresh();
  show(remset_root);
}

lv_obj_t* rem_switch_row(lv_obj_t* parent, int y, int h, const char* name, const char* icon, uint32_t color, int which,
                         const char* key) {
  lv_obj_t* row = make_row(parent, h, icon, color, on_rem_switch, which);
  lv_obj_set_pos(row, kMargin, y);
  reg(key, row);
  lv_obj_t* n = make_label(row, &font_inter_sb16, lv_color_hex(kText));
  lv_label_set_text(n, name);
  lv_obj_align(n, LV_ALIGN_LEFT_MID, 56, 0);
  lv_obj_t* sw = lv_switch_create(row);
  lv_obj_set_size(sw, 60, 32);
  lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -14, 0);
  lv_obj_clear_flag(sw, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(sw, lv_color_hex(kBorder), static_cast<lv_style_selector_t>(LV_PART_MAIN));
  lv_obj_set_style_bg_color(sw, lv_color_hex(kAccent), static_cast<lv_style_selector_t>(LV_PART_INDICATOR) | LV_STATE_CHECKED);
  rem_sw[which] = sw;
  return row;
}

void build_reminders(lv_obj_t* screen) {
  rem_cfg = settings_load_reminders();
  waste_until_min = settings_load_waste_until();
  waste_from_min = settings_load_waste_from();

  remset_root = make_screen_root(screen, "Erinnerungen");
  rem_switch_row(remset_root, 58, 60, "Termin-Erinnerungen", ICON_NOTIFY, kAmber, 0, "rem:on");
  rem_switch_row(remset_root, 128, 52, "Familienkalender", ICON_EVENT, kPurple, 1, "rem:cal");
  make_value_row(remset_root, 186, "Termine: Vorlauf", ICON_SCHEDULE, kPurple, on_rem_lead, 0, "rem:lead",
                 &rem_lead_val);
  make_value_row(remset_root, 264, "Ganzt\xC3\xA4gig: Am Vortag ab", ICON_SCHEDULE, kPurple, on_rem_from, 0, "rem:from",
                 &rem_from_val);
  rem_switch_row(remset_root, 344, 52, "Abfallkalender", ICON_RECYCLE, kGreen, 2, "rem:waste");
  lv_obj_t* wnote = make_label(remset_root, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_set_pos(wnote, kMargin, 404);
  lv_obj_set_width(wnote, kScreen - 2 * kMargin);
  lv_label_set_long_mode(wnote, LV_LABEL_LONG_WRAP);
  lv_label_set_text(wnote, "Abfuhr: Hinweis am Vortag ab ... bis am Sammeltag ... "
                           "(Einstellungen > Kalender)");
  hide(remset_root);

  // Reminder card (parent follows the active screen, see rem_render).
  rem_card = lv_obj_create(main_screen);
  lv_obj_set_size(rem_card, kScreen - 2 * kMargin, 156);
  lv_obj_set_pos(rem_card, kMargin, 14);  // over the weather card: buttons and chips stay usable
  style_glass(rem_card, 22);
  lv_obj_set_style_bg_opa(rem_card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(rem_card, lv_color_hex(kAmber), 0);
  lv_obj_set_style_border_width(rem_card, 2, 0);
  // No shadow: LVGL recomputes it on every redraw of the card (the clock
  // under it changes every second), ~70 ms per frame on this panel.
  lv_obj_clear_flag(rem_card, LV_OBJ_FLAG_CLICKABLE);  // only OK reacts
  lv_obj_set_style_pad_all(rem_card, 0, 0);
  lv_obj_clear_flag(rem_card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* bell = make_label(rem_card, &font_icons_24, lv_color_hex(kAmber));
  lv_label_set_text(bell, ICON_NOTIFY);
  lv_obj_set_pos(bell, 18, 16);
  rem_head = make_label(rem_card, &font_inter_14, lv_color_hex(kAmber));
  lv_obj_set_pos(rem_head, 52, 20);
  rem_icon = make_label(rem_card, &font_icons_24, lv_color_hex(kPurple));
  lv_obj_set_pos(rem_icon, 18, 58);
  rem_title = make_label(rem_card, &font_inter_20, lv_color_hex(kText));
  lv_obj_set_pos(rem_title, 52, 56);
  lv_obj_set_width(rem_title, 370);
  lv_label_set_long_mode(rem_title, LV_LABEL_LONG_DOT);
  rem_when = make_label(rem_card, &font_inter_sb16, lv_color_hex(kTextDim));
  lv_obj_set_pos(rem_when, 52, 104);
  lv_obj_t* ok = lv_btn_create(rem_card);
  rem_ok_btn = ok;
  lv_obj_set_size(ok, 112, 52);
  lv_obj_align(ok, LV_ALIGN_BOTTOM_RIGHT, -14, -14);
  style_glass(ok, 18);
  lv_obj_set_style_bg_color(ok, lv_color_hex(kAmber), 0);
  lv_obj_set_style_bg_grad_dir(ok, LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_bg_opa(ok, LV_OPA_COVER, 0);
  lv_obj_add_event_cb(ok, on_rem_ok, LV_EVENT_CLICKED, nullptr);
  reg("rem:ok", ok);
  lv_obj_t* okl = make_label(ok, &font_inter_sb16, lv_color_hex(0x0B0F1A));
  lv_label_set_text(okl, "OK");
  lv_obj_center(okl);
  hide(rem_card);

}

bool rem_shown_now() { return rem_shown; }

bool rem_describe(char* out, size_t n) {
  if (nav != Nav::RemSet) return false;
  snprintf(out, n, "remset on=%d cal=%d lead=%u cfrom=%u waste=%d from=%u", rem_cfg.on, rem_cfg.cal,
           rem_cfg.cal_lead_min, rem_cfg.cal_from_min, rem_cfg.waste, waste_from_min);
  return true;
}

}  // namespace

void ui_rem_test(int minutes, int all_day_kind) {
  if (rem_test_n == 4) rem_test_n = 0;
  CalEvent& e = rem_test[rem_test_n];
  memset(&e, 0, sizeof(e));
  int64_t now = time(nullptr);
  e.kind = all_day_kind == 1 ? 1 : 0;
  if (all_day_kind == 2) {  // all-day family event
    e.all_day = true;
    e.start = at_local(now + static_cast<int64_t>(minutes) * 60, 0, 0, 0);
    snprintf(e.title, sizeof(e.title), "Test-Ganztag %d", rem_test_n + 1);
  } else if (e.kind == 1) {
    e.all_day = true;
    e.start = at_local(now + static_cast<int64_t>(minutes) * 60, 0, 0, 0);
    snprintf(e.title, sizeof(e.title), "Test Papier %d", rem_test_n + 1);
  } else {
    e.start = now + static_cast<int64_t>(minutes) * 60;
    snprintf(e.title, sizeof(e.title), "Test-Termin %d", rem_test_n + 1);
  }
  char u[32];
  snprintf(u, sizeof(u), "harness-%d-%lld", rem_test_n, static_cast<long long>(e.start));
  e.uid = cal_hash(u);
  ++rem_test_n;
  rem_tick(true);
}

void ui_set_waste_until(uint16_t minutes) {
  if (minutes > 1440) return;
  waste_until_min = minutes;
  bool ok = settings_save_waste_until(minutes);
  console_printf("Waste hint until %u min saved=%d\n", minutes, ok ? 1 : 0);
  calset_refresh();
  home_chips_refresh();
  rem_tick(true);
}

uint16_t ui_waste_until() { return waste_until_min; }

void ui_set_waste_from(uint16_t minutes) {
  if (minutes > 1439) return;
  waste_from_min = minutes;
  bool ok = settings_save_waste_from(minutes);
  console_printf("Waste hint from %u min (day before) saved=%d\n", minutes, ok ? 1 : 0);
  calset_refresh();
  remset_refresh();
  rem_tick(true);
}

uint16_t ui_waste_from() { return waste_from_min; }

void ui_set_cal_lead(uint16_t minutes) {
  if (minutes < 1 || minutes > 1440) return;
  rem_cfg.cal_lead_min = minutes;
  rem_changed();
}
uint16_t ui_cal_lead() { return rem_cfg.cal_lead_min; }

void ui_set_cal_from(uint16_t minutes) {
  if (minutes > 1439) return;
  rem_cfg.cal_from_min = minutes;
  rem_changed();
}
uint16_t ui_cal_from() { return rem_cfg.cal_from_min; }

// Test: a waste collection today at the top of the loaded calendar.
bool ui_waste_test() {
  if (cal == nullptr || !cal_have) return false;
  int n = cal->n_waste < kMaxWaste ? cal->n_waste : kMaxWaste - 1;
  for (int i = n; i > 0; --i) cal->waste[i] = cal->waste[i - 1];
  CalEvent& e = cal->waste[0];
  memset(&e, 0, sizeof(e));
  e.kind = 1;
  e.all_day = true;
  e.start = at_local(time(nullptr), 0, 0, 0);
  snprintf(e.title, sizeof(e.title), "Test Papier");
  e.uid = cal_hash("harness-waste-today");
  cal->n_waste = n + 1;
  home_chips_refresh();
  rem_tick(true);
  return true;
}

void ui_rem_clear() {
  rem_test_n = 0;
  rem_n = 0;
  settings_reminder_clear_acks();
  rem_tick(true);
}

int ui_rem_pending() { return rem_n; }

void ui_rem_drop_tests() {  // harness: forget synthetic events, keep acknowledgements
  rem_test_n = 0;
  rem_n = 0;
  rem_tick(true);
}

namespace {
