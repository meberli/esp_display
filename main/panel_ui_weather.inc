// Part of panel_ui.cpp (same translation unit, included inside its anonymous
// namespace). Split for readability only; not compiled on its own.

// --- Weather icons: Material Symbols glyphs, by WMO weather code ----------
enum class Sky { None, Sun, Partly, Mostly, Cloud, Showers, Rain, Snow, Thunder, Fog };

Sky sky_for(int code) {
  if (code < 0) return Sky::None;
  if (code == 0) return Sky::Sun;
  if (code <= 2) return Sky::Partly;
  if (code == 3) return Sky::Cloud;
  if (code == 45 || code == 48) return Sky::Fog;
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return Sky::Snow;
  if (code >= 95) return Sky::Thunder;
  if (code >= 80 && code <= 82) return Sky::Showers;
  if (code >= 51 && code <= 67) return Sky::Rain;
  return Sky::Cloud;
}

Sky sky_for_day(int kind) {
  switch (static_cast<DayKind>(kind)) {
    case DayKind::Sunny: return Sky::Sun;
    case DayKind::Partly: return Sky::Partly;
    case DayKind::MostlyCloudy: return Sky::Mostly;
    case DayKind::Overcast: return Sky::Cloud;
    case DayKind::Fog: return Sky::Fog;
    case DayKind::Showers: return Sky::Showers;
    case DayKind::Rain: return Sky::Rain;
    case DayKind::SnowShowers:
    case DayKind::Snow: return Sky::Snow;
    case DayKind::Thunder: return Sky::Thunder;
  }
  return Sky::Cloud;
}

struct Glyph {
  const char* text;
  uint32_t color;
};

Glyph glyph_for(Sky sky) {
  switch (sky) {
    case Sky::None: return {"", kTextDim};
    case Sky::Sun: return {ICON_W_SUN, kAmber};
    case Sky::Partly: return {ICON_W_PARTLY, kAmber};
    case Sky::Mostly: return {ICON_W_CLOUD, 0xC4CCDC};
    case Sky::Cloud: return {ICON_W_CLOUD, 0x9AA4B8};
    case Sky::Showers: return {ICON_W_SHOWERS, kAccent};
    case Sky::Rain: return {ICON_W_RAIN, kAccent};
    case Sky::Snow: return {ICON_W_SNOW, kText};
    case Sky::Thunder: return {ICON_W_THUNDER, kAmber};
    case Sky::Fog: return {ICON_W_FOG, 0xA8B2C6};
  }
  return {ICON_W_CLOUD, kTextDim};
}

void set_glyph(lv_obj_t* label, Glyph g) {
  lv_label_set_text(label, g.text);
  lv_obj_set_style_text_color(label, lv_color_hex(g.color), 0);
}

// label is an lv_label with one of the icon fonts.
void weather_icon(lv_obj_t* label, int code) { set_glyph(label, glyph_for(sky_for(code))); }
void day_icon(lv_obj_t* label, int kind) { set_glyph(label, glyph_for(sky_for_day(kind))); }

// "Buchs SG · Teilweise bewölkt" under the temperature.
void update_weather_line() {
  if (weather_place == nullptr) return;
  char buf[100];
  snprintf(buf, sizeof(buf), "%s \xC2\xB7 %s", cur_place, cur_cond);
  lv_label_set_text(weather_place, buf);
}

const char* weather_name(int code) {
  switch (code) {
    case 0:
      return "Klar";
    case 1:
      return "Heiter";
    case 2:
      return "Bewölkt";
    case 3:
      return "Bedeckt";
    case 45:
    case 48:
      return "Nebel";
    case 51:
    case 53:
    case 55:
    case 56:
    case 57:
      return "Niesel";
    case 61:
    case 63:
    case 65:
    case 66:
    case 67:
      return "Regen";
    case 71:
    case 73:
    case 75:
    case 77:
      return "Schnee";
    case 80:
    case 81:
    case 82:
      return "Regenschauer";
    case 85:
    case 86:
      return "Schneeschauer";
    case 95:
    case 96:
    case 99:
      return "Gewitter";
    default:
      return "Wetter";
  }
}

Section section_of(const LoxoneControl& ctrl) {
  if (ctrl.favorite) {
    return kSecFav;
  }
  if (ctrl.kind == kLoxoneSwitch) {
    return kSecLight;
  }
  if (ctrl.kind == kLoxoneShutter) {
    return kSecShutter;
  }
  return kSecOther;
}

bool has_status(const LoxoneControl& ctrl) {
  return ctrl.kind == kLoxoneSwitch || ctrl.kind == kLoxoneShutter;
}

int compare_index(const void* left, const void* right) {
  int a = *static_cast<const int*>(left);
  int b = *static_cast<const int*>(right);
  int room = strcasecmp(controls[a].room, controls[b].room);
  if (room != 0) {
    return room;
  }
  int shutter_a = controls[a].kind == kLoxoneShutter ? 1 : 0;
  int shutter_b = controls[b].kind == kLoxoneShutter ? 1 : 0;
  if (shutter_a != shutter_b) {
    return shutter_a - shutter_b;
  }
  return strcasecmp(controls[a].name, controls[b].name);
}

void pack_indices(Section section, int* idxs, int n) {
  if (n <= 0 || page_n >= kMaxPages) {
    return;
  }
  qsort(idxs, static_cast<size_t>(n), sizeof(int), compare_index);
  PagePlan* current = &pages[page_n++];
  current->section = section;
  current->nrows = 0;
  current->rows[0] = RowPlan{false, -1, -1};
  current->rows[1] = RowPlan{false, -1, -1};
  int i = 0;
  while (i < n) {
    if (current->nrows == kRowsPerPage) {
      if (page_n >= kMaxPages) {
        return;
      }
      current = &pages[page_n++];
      current->section = section;
      current->nrows = 0;
      current->rows[0] = RowPlan{false, -1, -1};
      current->rows[1] = RowPlan{false, -1, -1};
    }
    RowPlan* row = &current->rows[current->nrows];
    if (controls[idxs[i]].kind == kLoxoneShutter) {
      row->shutter = true;
      row->a = idxs[i];
      row->b = -1;
      current->nrows++;
      i++;
      continue;
    }
    row->shutter = false;
    row->a = idxs[i];
    row->b = -1;
    i++;
    if (i < n && controls[idxs[i]].kind != kLoxoneShutter) {
      row->b = idxs[i];
      i++;
    }
    current->nrows++;
  }
}

void add_empty_page(Section section) {
  if (page_n >= kMaxPages) {
    return;
  }
  PagePlan* current = &pages[page_n++];
  current->section = section;
  current->nrows = 0;
  current->rows[0] = RowPlan{false, -1, -1};
  current->rows[1] = RowPlan{false, -1, -1};
}

bool has_section(Section section) {
  for (int i = 0; i < page_n; ++i) {
    if (pages[i].section == section) {
      return true;
    }
  }
  return false;
}

void pack_other(int* idxs, int n) {
  if (n > 1) {
    qsort(idxs, static_cast<size_t>(n), sizeof(int), compare_index);
  }
  if (page_n >= kMaxPages) {
    return;
  }
  // Boards without a relay (SenseCAP D1): no "Sonstiges" page unless there
  // are other controls, and no relay tile.
  if (!kBoardHasRelay && n == 0) {
    return;
  }
  PagePlan* current = &pages[page_n++];
  current->section = kSecOther;
  current->nrows = 1;
  current->rows[0] = RowPlan{false, kBoardHasRelay ? kLocalRelay : idxs[0], -1};
  current->rows[1] = RowPlan{false, -1, -1};
  int i = kBoardHasRelay ? 0 : 1;
  if (i < n) {
    current->rows[0].b = idxs[i++];
  }
  while (i < n) {
    if (current->nrows == kRowsPerPage) {
      if (page_n >= kMaxPages) {
        return;
      }
      current = &pages[page_n++];
      current->section = kSecOther;
      current->nrows = 0;
      current->rows[0] = RowPlan{false, -1, -1};
      current->rows[1] = RowPlan{false, -1, -1};
    }
    RowPlan* row = &current->rows[current->nrows];
    row->shutter = false;
    row->a = idxs[i++];
    row->b = -1;
    if (i < n) {
      row->b = idxs[i++];
    }
    current->nrows++;
  }
}

void index_sections() {
  for (int s = 0; s < 4; ++s) {
    section_origin[s] = -1;
    section_pages[s] = 0;
  }
  for (int i = 0; i < page_n; ++i) {
    int s = static_cast<int>(pages[i].section);
    if (section_origin[s] < 0) {
      section_origin[s] = i;
    }
    section_pages[s]++;
  }
}

void rebuild_pages() {
  page_n = 0;
  for (int section = kSecFav; section <= kSecShutter; ++section) {
    int idxs[kLoxoneControlCap];
    int n = 0;
    for (size_t i = 0; i < control_count; ++i) {
      if (section_of(controls[i]) == static_cast<Section>(section)) {
        idxs[n++] = static_cast<int>(i);
      }
    }
    pack_indices(static_cast<Section>(section), idxs, n);
    if (!has_section(static_cast<Section>(section))) {
      add_empty_page(static_cast<Section>(section));
    }
  }
  int idxs[kLoxoneControlCap];
  int n = 0;
  for (size_t i = 0; i < control_count; ++i) {
    if (section_of(controls[i]) == kSecOther) {
      idxs[n++] = static_cast<int>(i);
    }
  }
  pack_other(idxs, n);
  index_sections();
}

bool index_on_page(int index) {
  if (page < 0 || page >= page_n) {
    return false;
  }
  const PagePlan& current = pages[page];
  for (int row = 0; row < current.nrows; ++row) {
    if (current.rows[row].a == index || current.rows[row].b == index) {
      return true;
    }
  }
  return false;
}

bool visible_pending() {
  for (size_t i = 0; i < control_count; ++i) {
    if (needs_read[i] && reread_after[i] == 0 && index_on_page(static_cast<int>(i))) {
      return true;
    }
  }
  return false;
}

void publish_status() {
  if (visible_pending()) {
    ui_set_status("Aktualisiere...");
    return;
  }
  for (size_t i = 0; i < control_count; ++i) {
    if (index_on_page(static_cast<int>(i)) && has_status(controls[i]) && !known[i]) {
      ui_set_status("Status nicht verfügbar");
      return;
    }
  }
  ui_set_status(server_name);
}

void arm_visible_reads() {
  for (size_t i = 0; i < control_count; ++i) {
    prefer[i] = false;
    if (!index_on_page(static_cast<int>(i)) || !has_status(controls[i])) {
      continue;
    }
    if (reread_after[i] == 0) {
      needs_read[i] = true;
    }
    prefer[i] = true;
  }
}

// Control tile: glass; "on" glows amber (icon, state text, border).
void style_control(int slot, LoxoneKind kind, bool is_known, bool on, const char* icon) {
  lv_obj_t* btn = tiles[slot];
  style_glass(btn, 22);
  bool lit = is_known && on;
  if (lit) {
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x3A3220), 0);
    lv_obj_set_style_bg_grad_color(btn, lv_color_hex(0x1F1C16), 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(kAmber), 0);
    lv_obj_set_style_border_opa(btn, LV_OPA_40, 0);
  }
  uint32_t ic = lit ? kAmber : kind == kLoxonePulse ? kAccent : kOffIcon;
  lv_label_set_text(tile_icon[slot], icon);
  lv_obj_set_style_text_color(tile_icon[slot], lv_color_hex(ic), 0);
  lv_obj_set_style_text_color(tile_sub[slot], lv_color_hex(lit ? kAmber : kTextDim), 0);
}

void format_switch_sub(char* out, size_t n, int index) {
  const LoxoneControl& ctrl = controls[index];
  const char* mark = "";
  if (known[index]) {
    mark = known_on[index] ? "An" : "Aus";
  } else if (needs_read[index]) {
    mark = "...";
  }
  if (ctrl.room[0] != '\0' && mark[0] != '\0') {
    snprintf(out, n, "%s · %s", ctrl.room, mark);
  } else if (ctrl.room[0] != '\0') {
    snprintf(out, n, "%s", ctrl.room);
  } else if (mark[0] != '\0') {
    snprintf(out, n, "%s", mark);
  } else if (ctrl.kind == kLoxonePulse) {
    snprintf(out, n, "Taster");
  } else {
    snprintf(out, n, "Licht");
  }
}

void format_shutter_pos(char* out, size_t n, int index) {
  if (moving[index]) {
    snprintf(out, n, "Bewegt");
    return;
  }
  if (!known[index]) {
    snprintf(out, n, "%s", needs_read[index] ? "..." : "");
    return;
  }
  int pct = position_pct[index];
  if (pct <= 2) {
    snprintf(out, n, "Offen");
  } else if (pct >= 98) {
    snprintf(out, n, "Geschlossen");
  } else {
    snprintf(out, n, "%d%%", pct);
  }
}

void fill_title(char* out, size_t n, const LoxoneControl& ctrl) {
  // "Room · Name", but only the name when both are the same
  // (e.g. a shutter called "Gästezimmer" in room "Gästezimmer").
  if (ctrl.room[0] != '\0' && strcasecmp(ctrl.room, ctrl.name) != 0) {
    snprintf(out, n, "%s · %s", ctrl.room, ctrl.name);
  } else {
    snprintf(out, n, "%s", ctrl.name);
  }
}

void show_relay_tile(int slot) {
  show(tiles[slot]);
  bool on = panel_relay_is_on();
  lv_label_set_text(tile_name[slot], "Relais");
  lv_label_set_text(tile_sub[slot], on ? "An" : "Aus");
  style_control(slot, kLoxoneSwitch, true, on, on ? ICON_TOGGLE : ICON_TOGGLE_OFF);
}

void show_switch_tile(int slot, int index) {
  if (index == kLocalRelay) {
    show_relay_tile(slot);
    return;
  }
  show(tiles[slot]);
  const LoxoneControl& ctrl = controls[index];
  lv_label_set_text(tile_name[slot], ctrl.name);
  char sub[96];
  format_switch_sub(sub, sizeof(sub), index);
  lv_label_set_text(tile_sub[slot], sub);
  bool on = known[index] && known_on[index];
  bool light = current_section == kSecLight || ctrl.state_kind == kLoxoneStatePosition ||
               ctrl.state_kind == kLoxoneStateScene;
  const char* icon = light ? ICON_BULB : ctrl.kind == kLoxonePulse ? ICON_TOGGLE : on ? ICON_TOGGLE : ICON_TOGGLE_OFF;
  style_control(slot, ctrl.kind, known[index], known_on[index], icon);
}

void show_shutter_row(int row_slot, int index) {
  show(shutter_box[row_slot]);
  char title[110];
  char pos[16];
  fill_title(title, sizeof(title), controls[index]);
  format_shutter_pos(pos, sizeof(pos), index);
  lv_label_set_text(shutter_title[row_slot], title);
  lv_label_set_text(shutter_pos[row_slot], pos);
}

void fill_home_cards() {}  // the home buttons show no counts any more

void refresh_category() {
  if (section_pages[current_section] <= 0 || section_origin[current_section] < 0) {
    return;
  }
  int count = section_pages[current_section];
  if (section_page >= count) {
    section_page = count - 1;
  }
  if (section_page < 0) {
    section_page = 0;
  }
  page = section_origin[current_section] + section_page;
  // Left-edge handle: a previous page exists (swipe right to reach it).
  if (prev_hint != nullptr) {
    if (section_page > 0) {
      show(prev_hint);
    } else {
      hide(prev_hint);
    }
  }
  // Right-edge handle: a next page exists (swipe left to reach it).
  if (next_hint != nullptr) {
    if (section_page + 1 < count) {
      show(next_hint);
    } else {
      hide(next_hint);
    }
  }
  for (int slot = 0; slot < kPerPage; ++slot) {
    hide(tiles[slot]);
  }
  for (int row = 0; row < kRowsPerPage; ++row) {
    hide(shutter_box[row]);
  }
  lv_label_set_text(cat_title, section_name(current_section));
  lv_label_set_text(cat_icon, kSectionIcon[current_section]);
  lv_obj_set_style_text_color(cat_icon, lv_color_hex(g_theme->header_bar ? 0xFFFFFF : kSectionAccent[current_section]), 0);
  if (count > 1) {
    char text[16];
    snprintf(text, sizeof(text), "%d / %d", section_page + 1, count);
    lv_label_set_text(cat_page, text);
    show(cat_page);
  } else {
    hide(cat_page);
  }
  const PagePlan& current = pages[page];
  if (current.nrows == 0) {
    lv_label_set_text(empty_hint, empty_text(current.section));
    show(empty_hint);
  } else {
    hide(empty_hint);
    for (int row_slot = 0; row_slot < current.nrows; ++row_slot) {
      if (current.rows[row_slot].shutter) {
        show_shutter_row(row_slot, current.rows[row_slot].a);
        continue;
      }
      if (current.rows[row_slot].a >= 0 || current.rows[row_slot].a == kLocalRelay) {
        show_switch_tile(row_slot * 2, current.rows[row_slot].a);
      }
      if (current.rows[row_slot].b >= 0 || current.rows[row_slot].b == kLocalRelay) {
        show_switch_tile(row_slot * 2 + 1, current.rows[row_slot].b);
      }
    }
  }
}

void hide_settings() {
  hide(set_root);
  hide(wl_root);
  hide(wp_root);
  hide(loc_root);
  hide(disp_root);
  hide(time_root);
  cam_hide_all();
  info_hide_all();
  if (kb != nullptr) {
    lv_keyboard_set_textarea(kb, nullptr);
    hide(kb);
  }
}

void open_settings();
void settings_back();

void show_home() {
  nav = Nav::Home;
  page = -1;
  hide(cat_root);
  hide(wx_root);
  hide(cam_root);
  hide(message);
  hide_settings();
  info_hide_views();
  show(home_root);
  fill_home_cards();
  home_chips_refresh();
  home_warn_refresh();
  message_open = false;
}

void enter_section(Section section) {
  if (section_pages[section] <= 0) {
    return;
  }
  nav = Nav::Category;
  current_section = section;
  section_page = 0;
  message_open = false;
  hide(message);
  hide(home_root);
  hide(wx_root);
  info_hide_views();
  show(cat_root);
  refresh_category();
  arm_visible_reads();
  publish_status();
}

void go_home() {
  if (message_open) {
    return;
  }
  show_home();
}

void back_level() {
  if (nav == Nav::Category) {
    go_home();
  }
}

void refresh_weather_day();
void open_weather_day(int day);

void swipe_page(int dir) {
  if (nav == Nav::Cameras) {
    cam_swipe(dir);
    return;
  }
  if (nav == Nav::Transit) {
    tr_swipe(dir);
    return;
  }
  if (nav == Nav::Energy) {
    energy_swipe(dir);
    return;
  }
  if (nav == Nav::EnSet) {
    if (dir < 0) settings_back();
    return;
  }
  if (nav == Nav::Rain) {
    if (dir > 0) open_weather_day(0);
    return;
  }
  if (nav == Nav::TransitSet || nav == Nav::CalSet || nav == Nav::Maint || nav == Nav::RemSet ||
      nav == Nav::Playground) {
    if (dir < 0) settings_back();
    return;
  }
  if (nav == Nav::StationSearch) {
    if (dir < 0) open_trset();
    return;
  }
  if (nav == Nav::TextEdit) {
    if (dir < 0) txt_go_back();
    return;
  }
  if (nav == Nav::CamId || nav == Nav::CamSearch || nav == Nav::CamKey) {
    if (dir < 0) open_camset();
    return;
  }
  if (nav == Nav::CamSettings) {
    if (dir < 0) settings_back();
    return;
  }
  if (nav == Nav::WifiList) {
    if (dir < 0) wifi_list_leave();
    return;
  }
  if (nav == Nav::WifiPass || nav == Nav::Location || nav == Nav::Display ||
      nav == Nav::Time) {
    if (dir < 0) settings_back();
    return;
  }
  if (nav == Nav::Weather) {
    if (wx_day == 0 && dir < 0) {  // before "Heute": warnings and the rain nowcast
      rain_enter();
      return;
    }
    int next = wx_day + dir;
    if (next >= 0 && next < wx_count) {
      wx_day = next;
      refresh_weather_day();
    }
    return;
  }
  if (nav != Nav::Category) {
    return;
  }
  int count = section_pages[current_section];
  int next = section_page + dir;
  if (next < 0 || next >= count) {
    return;
  }
  section_page = next;
  refresh_category();
  arm_visible_reads();
  publish_status();
}

// Tap vs swipe. Every touch is tracked from finger-down to finger-up in the
// input read path (ui_touch_sample), not per widget: LVGL hands a press to
// the next widget when the finger slides onto it, so per-widget PRESSED
// restarted the swipe and the release then clicked that widget.
// A touch is a tap only if it never moved more than kTapSlop px from where it
// started and lasted at most kTapMaxMs. Anything else is a gesture and no
// widget handler acts on it (gesture_consumed). Swipes need kSwipe px.
constexpr int kTapSlop = 12;
constexpr uint32_t kTapMaxMs = 1000;

bool touch_down = false;
uint32_t touch_t0 = 0;
int touch_max_move = 0;
lv_coord_t touch_x = 0;
lv_coord_t touch_y = 0;

void run_gesture(void* param) {
  intptr_t g = reinterpret_cast<intptr_t>(param);
  if (message_open) {
    return;
  }
  if (g == 1) {
    go_home();
  } else if (g == 2) {
    back_level();
  } else if (g == 3) {
    swipe_page(1);
  } else if (g == 4) {
    swipe_page(-1);
  }
}

void finish_touch() {
  int dx = touch_x - gesture_x0;
  int dy = touch_y - gesture_y0;
  int adx = abs(dx);
  int ady = abs(dy);
  bool tap = touch_max_move <= kTapSlop && now_ms() - touch_t0 <= kTapMaxMs;
  gesture_consumed = !tap;
  intptr_t g = 0;
  if (adx >= kSwipe || ady >= kSwipe) {
    // Home is the only edge gesture: swipe up from the bottom edge. There is
    // no right-edge "back" any more: a left swipe that starts near the right
    // edge (where a thumb naturally starts) used to go home instead of
    // paging. Horizontal swipes page from any start position.
    if (gesture_y0 >= kScreen - kEdge && dy <= -kSwipe && ady > adx) {
      g = 1;
    } else if ((nav == Nav::Weather || nav == Nav::Cameras || nav == Nav::Transit) && dy <= -kSwipe && ady > adx + 8) {
      g = 1;  // the day view has no vertical use, so any upward swipe goes home
    } else if (adx >= kSwipe && adx > ady + 8) {
      g = dx < 0 ? 3 : 4;
    }
  }
  console_printf("Touch %s from %d,%d to %d,%d dx=%d dy=%d move=%d %ums%s\n", tap ? "tap" : "gesture",
                static_cast<int>(gesture_x0), static_cast<int>(gesture_y0), static_cast<int>(touch_x),
                static_cast<int>(touch_y), dx, dy, touch_max_move,
                static_cast<unsigned>(now_ms() - touch_t0),
                g == 1 ? " home" : g == 3 ? " next" : g == 4 ? " prev" : "");
  if (g != 0) {
    // Not from inside the input read; let LVGL finish this release first.
    lv_async_call(run_gesture, reinterpret_cast<void*>(g));
  }
}

// Press feedback only for taps. LVGL sets LV_STATE_PRESSED the moment a
// finger lands (and again on every widget it slides onto), so swipes lit up
// tiles. Every interactive widget drops that state right away and instead
// flashes it for kFlashMs once the touch has been classified as a tap
// (CLICKED with !gesture_consumed). No group is used, so no focus lingers.
constexpr uint32_t kFlashMs = 120;

void clear_flash(lv_timer_t* timer) {
  lv_obj_t* obj = static_cast<lv_obj_t*>(timer->user_data);
  // The widget may be gone (a list rebuilt by its own click handler).
  if (lv_obj_is_valid(obj)) lv_obj_clear_state(obj, LV_STATE_PRESSED);
  // repeat count 1: LVGL deletes the timer itself after this call (an extra
  // lv_timer_del here freed it twice and could take other timers with it).
}

void on_press_visual(lv_event_t* event) {
  lv_obj_t* obj = lv_event_get_target(event);
  lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
    lv_obj_clear_state(obj, LV_STATE_PRESSED);
  } else if (code == LV_EVENT_CLICKED && !gesture_consumed && !lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) {
    lv_obj_add_state(obj, LV_STATE_PRESSED);
    lv_timer_t* timer = lv_timer_create(clear_flash, kFlashMs, obj);
    lv_timer_set_repeat_count(timer, 1);
  }
}

void watch(lv_obj_t* obj) {
  lv_obj_add_event_cb(obj, on_press_visual, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(obj, on_press_visual, LV_EVENT_PRESSING, nullptr);
  lv_obj_add_event_cb(obj, on_press_visual, LV_EVENT_CLICKED, nullptr);
}

int control_for_slot(int slot) {
  if (page < 0 || page >= page_n) {
    return -1;
  }
  int row_slot = slot / 2;
  int col = slot % 2;
  const PagePlan& current = pages[page];
  if (row_slot < 0 || row_slot >= current.nrows || current.rows[row_slot].shutter) {
    return -1;
  }
  return col == 0 ? current.rows[row_slot].a : current.rows[row_slot].b;
}

void queue_command(int index, const char* command) {
  snprintf(pending.action, sizeof(pending.action), "%s", controls[index].action);
  snprintf(pending.command, sizeof(pending.command), "%s", command);
  pending.index = index;
  command_pending = true;
  if (!visible_pending()) {
    ui_set_status("Sende...");
  }
}

void on_tile(lv_event_t* event) {
  if (gesture_consumed) {
    return;
  }
  int slot = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
  int index = control_for_slot(slot);
  console_printf("Activate tile slot %d control %d\n", slot, index);
  if (index == kLocalRelay) {
    panel_relay_set(!panel_relay_is_on());
    console_printf("Relay 1 %s\n", panel_relay_is_on() ? "on" : "off");
    refresh_category();
    return;
  }
  if (command_pending) {
    return;
  }
  if (index < 0 || static_cast<size_t>(index) >= control_count) {
    return;
  }
  const LoxoneControl& ctrl = controls[index];
  const char* command = "Pulse";
  if (ctrl.kind == kLoxoneSwitch) {
    command = (known[index] && known_on[index]) ? "Off" : "On";
  }
  queue_command(index, command);
  lv_label_set_text(tile_sub[slot], "Sende...");
}

void on_shutter(lv_event_t* event) {
  if (gesture_consumed || command_pending) {
    return;
  }
  intptr_t tag = reinterpret_cast<intptr_t>(lv_event_get_user_data(event));
  int row_slot = static_cast<int>(tag / 2);
  int which = static_cast<int>(tag % 2);
  if (page < 0 || page >= page_n || row_slot < 0 || row_slot >= kRowsPerPage || which < 0 || which > 1) {
    return;
  }
  const PagePlan& current = pages[page];
  if (row_slot >= current.nrows || !current.rows[row_slot].shutter) {
    return;
  }
  console_printf("Activate shutter row %d %s\n", row_slot, kShutterCommand[which]);
  queue_command(current.rows[row_slot].a, kShutterCommand[which]);
  lv_label_set_text(shutter_pos[row_slot], "Sende...");
}

void on_category(lv_event_t* event) {
  if (gesture_consumed) {
    return;
  }
  int which = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
  if (which == 4) {
    console_printf("Activate settings\n");
    open_settings();
    return;
  }
  if (which == 5) {
    console_printf("Activate cameras\n");
    cam_enter();
    return;
  }
  if (which < kSecFav || which > kSecOther) {
    return;
  }
  console_printf("Activate category %d\n", which);
  enter_section(static_cast<Section>(which));
}

void open_weather_day(int day) {
  if (wx_root == nullptr || message_open) {
    return;
  }
  nav = Nav::Weather;
  wx_day = day < 0 ? 0 : day;
  hide(home_root);
  hide(cat_root);
  info_hide_views();
  show(wx_root);
  refresh_weather_day();
}

void on_day_panel(lv_event_t* event) {
  if (gesture_consumed) {
    return;
  }
  int day = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
  console_printf("Activate weather day %d\n", day);
  open_weather_day(day);
}

void on_msg_wifi(lv_event_t*);

void on_retry(lv_event_t*) {
  if (gesture_consumed) {
    return;
  }
  retry_pending = true;
}

void style_shutter_button(lv_obj_t* btn) {
  style_glass(btn, 18);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x232D42), 0);
  lv_obj_set_style_bg_grad_color(btn, lv_color_hex(0x1A2234), 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(g_theme->checked_top), LV_STATE_PRESSED);
  lv_obj_set_style_border_color(btn, lv_color_hex(kAccent), LV_STATE_PRESSED);
}

bool apply_state_value(int index, const char* value) {
  if (value == nullptr || value[0] == '\0') {
    return false;
  }
  char* end = nullptr;
  float parsed = strtof(value, &end);
  if (end == value) {
    return false;
  }
  const LoxoneControl& ctrl = controls[index];
  if (ctrl.kind == kLoxoneShutter) {
    float frac = parsed;
    if (parsed > 1.f) {
      frac = parsed / 100.f;
    }
    if (frac < 0.f) {
      frac = 0.f;
    }
    if (frac > 1.f) {
      frac = 1.f;
    }
    position_pct[index] = static_cast<int>(frac * 100.f + 0.5f);
    known_on[index] = position_pct[index] >= 98;
    known[index] = true;
    return true;
  }
  known_on[index] = parsed != 0.f;
  known[index] = true;
  return true;
}

bool waiting_for_later(int index, uint32_t now) {
  return reread_after[index] != 0 && static_cast<int32_t>(now - reread_after[index]) < 0;
}

int pick_state(bool preferred_only, uint32_t now) {
  for (size_t i = 0; i < control_count; ++i) {
    if (!needs_read[i] || waiting_for_later(static_cast<int>(i), now)) {
      continue;
    }
    if (preferred_only && !prefer[i]) {
      continue;
    }
    return static_cast<int>(i);
  }
  return -1;
}

void refresh_visible() {
  if (message_open) {
    return;
  }
  if (nav == Nav::Category) {
    refresh_category();
  } else {
    fill_home_cards();
  }
}

// ---- Weather day detail ---------------------------------------------------
const char* kWeekday[7] = {"Sonntag", "Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag"};
const char* kWeekdayShort[7] = {"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};
const char* kMonth[12] = {"Januar", "Februar", "M\xC3\xA4rz", "April", "Mai", "Juni",
                          "Juli", "August", "September", "Oktober", "November", "Dezember"};
const char* kMonthShort[12] = {"Jan", "Feb", "M\xC3\xA4r", "Apr", "Mai", "Jun", "Jul", "Aug", "Sep", "Okt", "Nov", "Dez"};

int weekday_of(int y, int m, int d) {  // 0 = Sunday (Sakamoto)
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y -= 1;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

lv_obj_t* wx_label(lv_obj_t* parent, const lv_font_t* font, uint32_t color, int x, int y) {
  lv_obj_t* l = make_label(parent, font, lv_color_hex(color));
  lv_obj_set_pos(l, x, y);
  return l;
}

lv_obj_t* make_bar(lv_obj_t* parent, int x, int y, int w, int h, uint32_t color, lv_opa_t opa) {
  lv_obj_t* b = lv_obj_create(parent);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_size(b, w, h);
  lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(b, opa, 0);
  lv_obj_set_style_border_width(b, 0, 0);
  lv_obj_set_style_pad_all(b, 0, 0);
  lv_obj_clear_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
  return b;
}

// Edge handle: a next/previous page exists.
lv_obj_t* wx_handle(lv_obj_t* parent, int x) { return make_bar(parent, x, 190, 5, 80, kAccent, LV_OPA_50); }

// Home bar at the bottom: swipe up from the bottom edge goes home.
void make_home_bar(lv_obj_t* parent) { make_bar(parent, 200, 468, 80, 4, 0xFFFFFF, LV_OPA_30); }

lv_obj_t* make_divider(lv_obj_t* parent, int x, int y, int w, int h) {
  lv_obj_t* d = make_bar(parent, x, y, w, h, kBorder, LV_OPA_COVER);
  lv_obj_set_style_radius(d, 0, 0);
  return d;
}

constexpr int kColW = 55;
constexpr int kCurveTop = 66;
constexpr int kCurveH = 40;

void build_weather_screen(lv_obj_t* screen) {
  wx_root = lv_obj_create(screen);
  lv_obj_set_pos(wx_root, 0, 0);
  lv_obj_set_size(wx_root, kScreen, kScreen);
  style_plain(wx_root);

  wx_title = wx_label(wx_root, &font_inter_14, kTextDim, 22, 20);
  lv_obj_set_width(wx_title, 380);
  lv_label_set_long_mode(wx_title, LV_LABEL_LONG_DOT);
  wx_page = make_label(wx_root, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_align(wx_page, LV_ALIGN_TOP_RIGHT, -22, 20);

  wx_icon = wx_label(wx_root, &font_icons_80, kAmber, 16, 40);
  wx_temp = wx_label(wx_root, &font_inter_light_40, kText, 112, 50);
  lv_label_set_recolor(wx_temp, true);
  wx_cond = wx_label(wx_root, &font_inter_16, kTextDim, 114, 100);
  lv_obj_set_width(wx_cond, 344);
  lv_label_set_long_mode(wx_cond, LV_LABEL_LONG_DOT);
  wx_empty = wx_label(wx_root, &font_inter_20, kTextDim, 114, 64);

  // Info chips: rain, wind, sunrise, sunset.
  static const char* kChipIcon[4] = {ICON_DROP, ICON_AIR, ICON_SUNRISE, ICON_SUNSET};
  for (int i = 0; i < 4; ++i) {
    lv_obj_t* chip = lv_obj_create(wx_root);
    lv_obj_set_pos(chip, 20 + i * 112, 136);
    lv_obj_set_size(chip, 104, 52);
    style_glass(chip, 14);
    lv_obj_clear_flag(chip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t* ic = wx_label(chip, &font_icons_16, kAccent, 10, 6);
    lv_label_set_text(ic, kChipIcon[i]);
    wx_info[i] = wx_label(chip, &font_inter_sb16, kText, 10, 25);
  }

  lv_obj_t* tl = lv_obj_create(wx_root);
  lv_obj_set_pos(tl, 20, 200);
  lv_obj_set_size(tl, kScreen - 40, 232);
  style_glass(tl, 22);
  lv_obj_clear_flag(tl, LV_OBJ_FLAG_CLICKABLE);

  wx_curve = lv_line_create(tl);
  lv_obj_set_pos(wx_curve, 0, 0);
  lv_obj_set_style_line_width(wx_curve, 3, 0);
  lv_obj_set_style_line_color(wx_curve, lv_color_hex(kAccent), 0);
  lv_obj_set_style_line_rounded(wx_curve, true, 0);

  for (int i = 0; i < kWxSlots; ++i) {
    int cx = i * kColW;
    wx_hour[i] = wx_label(tl, &font_inter_14, kTextDim, cx, 8);
    lv_obj_set_width(wx_hour[i], kColW);
    lv_obj_set_style_text_align(wx_hour[i], LV_TEXT_ALIGN_CENTER, 0);
    char h[4];
    snprintf(h, sizeof(h), "%02d", i * 3);
    lv_label_set_text(wx_hour[i], h);
    wx_sicon[i] = wx_label(tl, &font_icons_24, kAmber, cx, 30);
    lv_obj_set_width(wx_sicon[i], kColW);
    lv_obj_set_style_text_align(wx_sicon[i], LV_TEXT_ALIGN_CENTER, 0);
    wx_dot[i] = make_bar(tl, cx + kColW / 2 - 4, kCurveTop, 8, 8, kAccent, LV_OPA_COVER);
    wx_stemp[i] = wx_label(tl, &font_inter_sb16, kText, cx, 120);
    lv_obj_set_width(wx_stemp[i], kColW);
    lv_obj_set_style_text_align(wx_stemp[i], LV_TEXT_ALIGN_CENTER, 0);
    wx_smm[i] = wx_label(tl, &font_inter_14, kAccent, cx, 148);
    lv_obj_set_width(wx_smm[i], kColW);
    lv_obj_set_style_text_align(wx_smm[i], LV_TEXT_ALIGN_CENTER, 0);
    wx_sprob[i] = wx_label(tl, &font_inter_14, kTextDim, cx, 166);
    lv_obj_set_width(wx_sprob[i], kColW);
    lv_obj_set_style_text_align(wx_sprob[i], LV_TEXT_ALIGN_CENTER, 0);
    wx_swind[i] = wx_label(tl, &font_inter_14, kTextDim, cx, 184);
    lv_obj_set_width(wx_swind[i], kColW);
    lv_obj_set_style_text_align(wx_swind[i], LV_TEXT_ALIGN_CENTER, 0);
  }

  // Row units for the three bottom rows.
  lv_obj_t* legend = make_label(tl, &font_inter_14, lv_color_hex(kTextDim));
  lv_label_set_text(legend, "Regen mm  \xC2\xB7  Wahrscheinlichkeit %  \xC2\xB7  Wind km/h");
  lv_obj_align(legend, LV_ALIGN_BOTTOM_MID, 0, -8);
  lv_obj_set_style_text_opa(legend, LV_OPA_70, 0);

  wx_prev_hint = wx_handle(wx_root, 5);
  wx_next_hint = wx_handle(wx_root, 470);
  lv_obj_t* up = make_label(wx_root, &font_icons_24, lv_color_hex(kTextDim));
  lv_label_set_text(up, ICON_UP);
  lv_obj_align(up, LV_ALIGN_BOTTOM_MID, 0, -14);
  make_home_bar(wx_root);
  hide(wx_root);
}

void refresh_weather_day() {
  if (wx_root == nullptr) {
    return;
  }
  if (wx_count <= 0 || wx_week == nullptr) {
    lv_label_set_text(wx_title, "Wetter");
    lv_label_set_text(wx_page, "");
    lv_label_set_text(wx_empty, "Noch keine Vorhersage");
    show(wx_empty);
    hide(wx_prev_hint);
    hide(wx_next_hint);
    return;
  }
  hide(wx_empty);
  if (wx_day >= wx_count) wx_day = wx_count - 1;
  const WeatherDetailDay& d = wx_week[wx_day];
  int y = atoi(d.date), m = atoi(d.date + 5), dd = atoi(d.date + 8);
  char buf[128];
  const char* rel = wx_day == 0 ? "Heute \xC2\xB7 " : wx_day == 1 ? "Morgen \xC2\xB7 " : "";
  bool date_ok = m >= 1 && m <= 12;
  snprintf(buf, sizeof(buf), "%s%s, %d. %s \xC2\xB7 %s", rel, date_ok ? kWeekday[weekday_of(y, m, dd)] : "", dd,
           date_ok ? kMonth[m - 1] : "", cur_place);
  lv_label_set_text(wx_title, buf);
  snprintf(buf, sizeof(buf), "%d / %d", wx_day + 1, wx_count);
  lv_label_set_text(wx_page, buf);
  lv_obj_align(wx_page, LV_ALIGN_TOP_RIGHT, -22, 20);
  day_icon(wx_icon, d.kind);
  snprintf(buf, sizeof(buf), "%d\xC2\xB0 #8A94A8 / %d\xC2\xB0#", round_c(d.t_max), round_c(d.t_min));
  lv_label_set_text(wx_temp, buf);
  snprintf(buf, sizeof(buf), "%s \xC2\xB7 Regen %d %%", day_kind_name(static_cast<DayKind>(d.kind)), d.prob_max);
  lv_label_set_text(wx_cond, buf);
  snprintf(buf, sizeof(buf), "%.1f mm", static_cast<double>(d.precip));
  lv_label_set_text(wx_info[0], buf);
  snprintf(buf, sizeof(buf), "%d km/h", round_c(d.wind_max));
  lv_label_set_text(wx_info[1], buf);
  lv_label_set_text(wx_info[2], d.sunrise);
  lv_label_set_text(wx_info[3], d.sunset);

  float lo = d.slot[0].temp, hi = d.slot[0].temp;
  for (int i = 0; i < kWxSlots; ++i) {
    if (d.slot[i].temp < lo) lo = d.slot[i].temp;
    if (d.slot[i].temp > hi) hi = d.slot[i].temp;
  }
  float span = hi - lo < 1.f ? 1.f : hi - lo;
  for (int i = 0; i < kWxSlots; ++i) {
    const WeatherSlot& sl = d.slot[i];
    weather_icon(wx_sicon[i], sl.code);
    snprintf(buf, sizeof(buf), "%d\xC2\xB0", round_c(sl.temp));
    lv_label_set_text(wx_stemp[i], buf);
    wx_curve_pts[i].x = i * kColW + kColW / 2;
    wx_curve_pts[i].y = kCurveTop + kCurveH - static_cast<int>((sl.temp - lo) / span * kCurveH);
    lv_obj_set_pos(wx_dot[i], wx_curve_pts[i].x - 4, wx_curve_pts[i].y - 4);
    snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(sl.precip));
    lv_label_set_text(wx_smm[i], buf);
    snprintf(buf, sizeof(buf), "%d%%", sl.prob);
    lv_label_set_text(wx_sprob[i], buf);
    snprintf(buf, sizeof(buf), "%d", round_c(sl.wind));
    lv_label_set_text(wx_swind[i], buf);
  }
  lv_line_set_points(wx_curve, wx_curve_pts, kWxSlots);
  if (wx_day > 0) show(wx_prev_hint); else hide(wx_prev_hint);
  if (wx_day + 1 < wx_count) show(wx_next_hint); else hide(wx_next_hint);
}

