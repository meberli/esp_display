// Part of panel_ui.cpp (same translation unit, included inside its anonymous
// namespace). Split for readability only; not compiled on its own.

// ---------------------------------------------------------------------------
// Kameras: Windy webcams, one per page, 480x480 cover-scaled. The network
// task downloads and scales into the frame that is not on screen; the UI then
// switches the image source. Refresh every kCamRefreshMs while visible.

constexpr uint32_t kCamRefreshMs = 120000;
constexpr uint32_t kCamRetryMs = 30000;
Webcam cams[kMaxCams];
int cam_n = 0;
int cam_idx = 0;
uint16_t* cam_frame[2] = {nullptr, nullptr};
lv_img_dsc_t cam_dsc[2];
int cam_shown = -1;          // frame on screen, -1 none
uint32_t cam_shown_id = 0;   // webcam in that frame
CamInfo cam_info = {};       // details of cam_shown_id
bool cam_inflight = false;
uint32_t cam_next_ms = 0;
char cam_err[96] = "";
lv_obj_t* cam_img = nullptr;
lv_obj_t* cam_card = nullptr;
lv_obj_t* cam_title = nullptr;
lv_obj_t* cam_place = nullptr;
lv_obj_t* cam_age = nullptr;
lv_obj_t* cam_link = nullptr;
lv_obj_t* cam_pill = nullptr;
lv_obj_t* cam_page = nullptr;
lv_obj_t* cam_status = nullptr;
// settings
lv_obj_t* camset_list = nullptr;
lv_obj_t* camset_status = nullptr;
lv_obj_t* camid_ta = nullptr;
lv_obj_t* camid_status = nullptr;
lv_obj_t* camsearch_ta = nullptr;
lv_obj_t* camsearch_status = nullptr;
lv_obj_t* camsearch_list = nullptr;
lv_obj_t* camkey_cur = nullptr;
lv_obj_t* camkey_ta = nullptr;
lv_obj_t* camkey_status = nullptr;
CamInfo* cam_found = nullptr;  // PSRAM, 20 entries
int cam_found_n = 0;
bool cam_busy = false;       // a camera settings request is queued or running
bool key_testing = false;    // CamLookup after a key change
char cam_added[64] = "";

const char* key_error(int status) {
  if (status == 401 || status == 403) return "API-Key ung\xC3\xBCltig (Einstellungen > Kameras)";
  if (status == 404) return "Webcam nicht gefunden";
  if (status == 429) return "Zu viele Anfragen (Windy-Limit)";
  return nullptr;
}

void cam_hide_all() {
  hide(camset_root);
  hide(camid_root);
  hide(camsearch_root);
  hide(camkey_root);
}

void cam_age_text(char* out, size_t n, int64_t updated) {
  time_t now = time(nullptr);
  if (updated <= 0 || !clock_synced()) {
    out[0] = '\0';
    return;
  }
  int64_t d = static_cast<int64_t>(now) - updated;
  if (d < 0) d = 0;
  if (d < 60) snprintf(out, n, "gerade eben");
  else if (d < 3600) snprintf(out, n, "vor %d Min", static_cast<int>(d / 60));
  else if (d < 86400) snprintf(out, n, "vor %d Std", static_cast<int>(d / 3600));
  else snprintf(out, n, "vor %d Tagen", static_cast<int>(d / 86400));
}

void cam_refresh_view() {
  if (cam_root == nullptr) return;
  char buf[96];
  if (cam_n == 0) {
    hide(cam_img);
    hide(cam_card);
    hide(cam_pill);
    lv_label_set_text(cam_status, "Keine Kameras.\nEinstellungen > Kameras");
    show(cam_status);
    return;
  }
  if (cam_idx >= cam_n) cam_idx = cam_n - 1;
  const Webcam& w = cams[cam_idx];
  char fallback[24];
  snprintf(fallback, sizeof(fallback), "Webcam %lu", static_cast<unsigned long>(w.id));
  const char* wtitle = w.title[0] != '\0' ? w.title : fallback;
  lv_label_set_text_fmt(cam_link, "windy.com/webcams/%lu", static_cast<unsigned long>(w.id));
  lv_obj_align(cam_link, LV_ALIGN_TOP_RIGHT, -16, 46);
  bool have = cam_shown >= 0 && cam_shown_id == w.id;
  snprintf(buf, sizeof(buf), "%d / %d", cam_idx + 1, cam_n);
  lv_label_set_text(cam_page, buf);
  show(cam_pill);
  if (have) {
    lv_img_set_src(cam_img, &cam_dsc[cam_shown]);
    show(cam_img);
    lv_label_set_text(cam_title, cam_info.title[0] != '\0' ? cam_info.title : wtitle);
    snprintf(buf, sizeof(buf), "%s%s%s", cam_info.city, (cam_info.city[0] && cam_info.region[0]) ? ", " : "",
             cam_info.region);
    lv_label_set_text(cam_place, buf);
    cam_age_text(buf, sizeof(buf), cam_info.updated);
    lv_label_set_text(cam_age, buf);
    show(cam_card);
    if (cam_err[0] != '\0') {  // refresh failed: keep the last picture, note the error
      lv_label_set_text(cam_status, cam_err);
      show(cam_status);
    } else {
      hide(cam_status);
    }
  } else {
    hide(cam_img);
    lv_label_set_text(cam_title, wtitle);
    lv_label_set_text(cam_place, "");
    lv_label_set_text(cam_age, "");
    show(cam_card);
    lv_label_set_text(cam_status, cam_err[0] != '\0' ? cam_err : "Bild wird geladen ...");
    show(cam_status);
  }
}

void cam_enter() {
  if (message_open || cam_root == nullptr) return;
  nav = Nav::Cameras;
  hide(home_root);
  hide(cat_root);
  hide(wx_root);
  hide_settings();
  show(cam_root);
  cam_err[0] = '\0';
  if (cam_idx >= cam_n) cam_idx = 0;
  cam_next_ms = now_ms();  // always fetch fresh on entry
  cam_refresh_view();
}

void cam_swipe(int dir) {
  int next = cam_idx + dir;
  if (next < 0 || next >= cam_n) return;
  cam_idx = next;
  cam_err[0] = '\0';
  cam_next_ms = now_ms();
  cam_refresh_view();
}

// Once a second from clock_tick: the "vor X Min" text.
void cam_tick() {
  static uint32_t last = 0;
  if (nav != Nav::Cameras || scr_off || now_ms() - last < 15000) return;
  last = now_ms();
  if (cam_shown >= 0 && cam_n > 0 && cam_shown_id == cams[cam_idx].id) {
    char buf[32];
    cam_age_text(buf, sizeof(buf), cam_info.updated);
    lv_label_set_text(cam_age, buf);
  }
}

void camset_value() {
  if (set_cam_val == nullptr) return;
  char key[72];
  bool own = false;
  bool have = settings_load_windy_key(key, sizeof(key), &own);
  char buf[96];
  snprintf(buf, sizeof(buf), "%d Kamera%s \xC2\xB7 Key: %s", cam_n, cam_n == 1 ? "" : "s",
           !have ? "fehlt" : own ? "eigener" : "Standard");
  lv_label_set_text(set_cam_val, buf);
}

void queue_cams_save() {
  SettingsRequest r = {};
  r.kind = SettingsRequest::SetCams;
  r.cam_n = cam_n;
  memcpy(r.cams, cams, sizeof(Webcam) * cam_n);
  cam_busy = true;
  queue_settings(r);
}

void on_cam_del(lv_event_t* e);
void fill_camset_list() {
  unreg_prefix("cam:");
  lv_obj_clean(camset_list);
  for (int i = 0; i < cam_n; ++i) {
    lv_obj_t* row = lv_obj_create(camset_list);
    lv_obj_set_size(row, kScreen - 2 * kMargin, 60);
    style_glass(row, 16);
    // Clickable (no action) so a drag on it scrolls the list: make_list's
    // container is not clickable, and LVGL scrolls from the pressed object up.
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    watch(row);
    lv_obj_t* ic = make_label(row, &font_icons_24, lv_color_hex(kTeal));
    lv_label_set_text(ic, ICON_VIDEOCAM);
    lv_obj_align(ic, LV_ALIGN_LEFT_MID, 14, 0);
    lv_obj_t* t = make_label(row, &font_inter_sb16, lv_color_hex(kText));
    lv_obj_set_width(t, 300);
    lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
    lv_obj_set_height(t, 20);  // one line, ellipsis
    if (cams[i].title[0] != '\0') lv_label_set_text(t, cams[i].title);
    else lv_label_set_text_fmt(t, "Webcam %lu", static_cast<unsigned long>(cams[i].id));
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 50, 9);
    lv_obj_t* id = make_label(row, &font_inter_14, lv_color_hex(kTextDim));
    lv_label_set_text_fmt(id, "ID %lu", static_cast<unsigned long>(cams[i].id));
    lv_obj_align(id, LV_ALIGN_BOTTOM_LEFT, 50, -8);
    lv_obj_t* del = lv_btn_create(row);
    lv_obj_set_size(del, 48, 44);
    lv_obj_align(del, LV_ALIGN_RIGHT_MID, -8, 0);
    style_glass(del, 12);
    lv_obj_add_event_cb(del, on_cam_del, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<intptr_t>(i)));
    watch(del);
    lv_obj_t* di = make_label(del, &font_icons_24, lv_color_hex(kRed));
    lv_label_set_text(di, ICON_DELETE);
    lv_obj_center(di);
    char key[16];
    snprintf(key, sizeof(key), "cam:del:%d", i);
    reg(key, del);
  }
}

void open_camset() {
  nav = Nav::CamSettings;
  hide(home_root);
  hide(cam_root);
  hide_settings();
  fill_camset_list();
  if (cam_added[0] != '\0') {
    lv_label_set_text_fmt(camset_status, "Hinzugef\xC3\xBCgt: %s", cam_added);
    cam_added[0] = '\0';
  } else {
    lv_label_set_text_fmt(camset_status, "%d von %d Kameras", cam_n, kMaxCams);
  }
  show(camset_root);
}

void on_cam_del(lv_event_t* e) {
  if (gesture_consumed || settings_pending) return;
  int i = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  if (i < 0 || i >= cam_n) return;
  console_printf("Remove webcam %d %lu\n", i, static_cast<unsigned long>(cams[i].id));
  char title[64];
  snprintf(title, sizeof(title), "%s", cams[i].title);
  memmove(&cams[i], &cams[i + 1], sizeof(Webcam) * (cam_n - i - 1));
  cam_n--;
  if (cam_idx >= cam_n) cam_idx = cam_n > 0 ? cam_n - 1 : 0;
  queue_cams_save();
  // Rebuild after this event returns (the button is deleted with the list).
  lv_timer_t* t = lv_timer_create([](lv_timer_t*) { open_camset(); }, 2 * kFlashMs, nullptr);
  lv_timer_set_repeat_count(t, 1);
  lv_label_set_text_fmt(camset_status, "Entfernt: %s", title);
}

bool cam_add(uint32_t id, const char* title) {
  for (int i = 0; i < cam_n; ++i) {
    if (cams[i].id == id) return false;
  }
  if (cam_n >= kMaxCams) return false;
  cams[cam_n].id = id;
  snprintf(cams[cam_n].title, sizeof(cams[cam_n].title), "%s", title);
  cam_n++;
  queue_cams_save();
  return true;
}

void show_kb_mode(lv_obj_t* ta, lv_keyboard_mode_t mode) {
  show_keyboard(ta);
  lv_keyboard_set_mode(kb, mode);
}

void on_camset_btn(lv_event_t* e) {
  if (gesture_consumed) return;
  int which = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  console_printf("Activate camset %d\n", which);
  hide_settings();
  if (which == 0) {
    nav = Nav::CamId;
    lv_textarea_set_text(camid_ta, "");
    lv_label_set_text(camid_status,
                      cam_n >= kMaxCams ? "Liste voll (max. 12)" : "Windy-ID eingeben (z.B. aus windy.com/webcams/<ID>)");
    show(camid_root);
    show_kb_mode(camid_ta, LV_KEYBOARD_MODE_NUMBER);
  } else if (which == 1) {
    nav = Nav::CamSearch;
    cam_found_n = 0;
    unreg_prefix("found:");
    lv_obj_clean(camsearch_list);
    lv_textarea_set_text(camsearch_ta, "");
    lv_label_set_text(camsearch_status, "Ortsname eingeben: Webcams im Umkreis von 25 km");
    show(camsearch_root);
    show_keyboard(camsearch_ta);
  } else {
    nav = Nav::CamKey;
    char key[72], mask[32];
    bool own = false;
    bool have = settings_load_windy_key(key, sizeof(key), &own);
    settings_mask_key(key, mask, sizeof(mask));
    memset(key, 0, sizeof(key));
    if (!have) lv_label_set_text(camkey_cur, "Kein Key hinterlegt");
    else lv_label_set_text_fmt(camkey_cur, "%s: %s", own ? "Eigener Key" : "Standard-Key (Firmware)", mask);
    lv_textarea_set_text(camkey_ta, "");
    lv_label_set_text(camkey_status, "Gratis-Key: api.windy.com/keys (Webcams API)");
    show(camkey_root);
  }
}

void on_found_row(lv_event_t* e) {
  if (gesture_consumed || settings_pending) return;
  int i = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  if (i < 0 || i >= cam_found_n) return;
  console_printf("Add found webcam %d %lu\n", i, static_cast<unsigned long>(cam_found[i].id));
  if (cam_add(cam_found[i].id, cam_found[i].title)) {
    lv_label_set_text_fmt(camsearch_status, "Hinzugef\xC3\xBCgt: %s", cam_found[i].title);
    lv_obj_t* row = lv_event_get_target(e);
    lv_obj_t* chk = make_label(row, &font_icons_24, lv_color_hex(kGreen));
    lv_label_set_text(chk, ICON_CHECK);
    lv_obj_align(chk, LV_ALIGN_RIGHT_MID, -14, 0);
  } else {
    lv_label_set_text(camsearch_status, cam_n >= kMaxCams ? "Liste voll (max. 12)" : "Schon in der Liste");
  }
}

void fill_found_list() {
  unreg_prefix("found:");
  lv_obj_clean(camsearch_list);
  for (int i = 0; i < cam_found_n; ++i) {
    lv_obj_t* row = make_row(camsearch_list, 60, ICON_VIDEOCAM, kTeal, on_found_row, i);
    lv_obj_t* t = make_label(row, &font_inter_sb16, lv_color_hex(kText));
    lv_obj_set_width(t, 330);
    lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
    lv_obj_set_height(t, 20);  // one line, ellipsis
    lv_label_set_text(t, cam_found[i].title);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 54, 9);
    lv_obj_t* d = make_label(row, &font_inter_14, lv_color_hex(kTextDim));
    lv_label_set_text_fmt(d, "%s \xC2\xB7 %d.%d km \xC2\xB7 ID %lu", cam_found[i].city,
                          static_cast<int>(cam_found[i].km), static_cast<int>(cam_found[i].km * 10) % 10,
                          static_cast<unsigned long>(cam_found[i].id));
    lv_obj_align(d, LV_ALIGN_BOTTOM_LEFT, 54, -8);
    for (int k = 0; k < cam_n; ++k) {
      if (cams[k].id == cam_found[i].id) {
        lv_obj_t* chk = make_label(row, &font_icons_24, lv_color_hex(kGreen));
        lv_label_set_text(chk, ICON_CHECK);
        lv_obj_align(chk, LV_ALIGN_RIGHT_MID, -14, 0);
      }
    }
    char key[16];
    snprintf(key, sizeof(key), "found:%d", i);
    reg(key, row);
  }
}

void on_key_default(lv_event_t*) {
  if (gesture_consumed || settings_pending) return;
  SettingsRequest r = {};
  r.kind = SettingsRequest::SetWindyKey;
  cam_busy = true;
  lv_label_set_text(camkey_status, "Speichere ...");
  queue_settings(r);
}

bool cam_kb_ready(lv_obj_t* ta) {
  if (ta == nullptr || settings_pending) return false;
  const char* text = lv_textarea_get_text(ta);
  if (ta == camid_ta) {
    unsigned long id = strtoul(text, nullptr, 10);
    if (id == 0) {
      lv_label_set_text(camid_status, "Bitte eine Zahl eingeben");
      return true;
    }
    for (int i = 0; i < cam_n; ++i) {
      if (cams[i].id == id) {
        lv_label_set_text(camid_status, "Schon in der Liste");
        return true;
      }
    }
    if (cam_n >= kMaxCams) {
      lv_label_set_text(camid_status, "Liste voll (max. 12)");
      return true;
    }
    SettingsRequest r = {};
    r.kind = SettingsRequest::CamLookup;
    r.cam_id = static_cast<uint32_t>(id);
    key_testing = false;
    cam_busy = true;
    lv_label_set_text(camid_status, "Pr\xC3\xBC" "fe Webcam ...");
    queue_settings(r);
  } else if (ta == camsearch_ta) {
    if (text[0] == '\0') return true;
    SettingsRequest r = {};
    r.kind = SettingsRequest::CamNearby;
    snprintf(r.query, sizeof(r.query), "%s", text);
    cam_busy = true;
    lv_label_set_text(camsearch_status, "Suche ...");
    queue_settings(r);
  } else if (ta == camkey_ta) {
    size_t len = strlen(text);
    if (len < 16 || len >= 72) {
      lv_label_set_text(camkey_status, "Key wirkt unvollst\xC3\xA4ndig (16-71 Zeichen)");
      return true;
    }
    SettingsRequest r = {};
    r.kind = SettingsRequest::SetWindyKey;
    snprintf(r.key, sizeof(r.key), "%s", text);
    lv_textarea_set_text(camkey_ta, "");  // do not keep the key in the widget
    cam_busy = true;
    lv_label_set_text(camkey_status, "Speichere ...");
    queue_settings(r);
  } else {
    return false;
  }
  lv_keyboard_set_textarea(kb, nullptr);
  hide(kb);
  return true;
}

bool cam_describe(char* out, size_t n) {
  switch (nav) {
    case Nav::Cameras:
      snprintf(out, n, "cameras %d/%d id=%lu img=%d loading=%d err=%s", cam_n > 0 ? cam_idx + 1 : 0, cam_n,
               cam_n > 0 ? static_cast<unsigned long>(cams[cam_idx].id) : 0UL,
               (cam_shown >= 0 && cam_n > 0 && cam_shown_id == cams[cam_idx].id) ? 1 : 0, cam_inflight ? 1 : 0,
               cam_err[0] != '\0' ? cam_err : "-");
      return true;
    case Nav::CamSettings:
      snprintf(out, n, "camset %d busy=%d", cam_n, (cam_busy || settings_pending) ? 1 : 0);
      return true;
    case Nav::CamId:
      snprintf(out, n, "camid busy=%d", (cam_busy || settings_pending) ? 1 : 0);
      return true;
    case Nav::CamSearch:
      snprintf(out, n, "camsearch %d busy=%d", cam_found_n, (cam_busy || settings_pending) ? 1 : 0);
      return true;
    case Nav::CamKey: {
      char key[72];
      bool own = false;
      settings_load_windy_key(key, sizeof(key), &own);
      memset(key, 0, sizeof(key));
      snprintf(out, n, "camkey own=%d busy=%d", own ? 1 : 0, (cam_busy || settings_pending || key_testing) ? 1 : 0);
      return true;
    }
    default:
      return false;
  }
}

bool cam_list_dump() {
  if (nav == Nav::CamSettings || nav == Nav::Cameras) {
    for (int i = 0; i < cam_n; ++i) console_printf("ITEM %d %lu %s\n", i, static_cast<unsigned long>(cams[i].id), cams[i].title);
  } else if (nav == Nav::CamSearch) {
    for (int i = 0; i < cam_found_n; ++i)
      console_printf("ITEM %d %lu %.1fkm %s|%s\n", i, static_cast<unsigned long>(cam_found[i].id), cam_found[i].km,
                     cam_found[i].title, cam_found[i].city);
  } else {
    return false;
  }
  console_printf("%s", "ITEM_END\n");
  return true;
}

lv_obj_t* make_pill_label(lv_obj_t* parent, lv_obj_t** label) {
  lv_obj_t* pill = lv_obj_create(parent);
  lv_obj_set_size(pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  style_glass(pill, 14);
  lv_obj_set_style_bg_color(pill, lv_color_hex(0x0B0F1A), 0);
  lv_obj_set_style_bg_opa(pill, 170, 0);
  lv_obj_set_style_pad_hor(pill, 12, 0);
  lv_obj_set_style_pad_ver(pill, 6, 0);
  lv_obj_clear_flag(pill, LV_OBJ_FLAG_CLICKABLE);
  *label = make_label(pill, &font_inter_14, lv_color_hex(kText));
  return pill;
}

void build_camera_screens(lv_obj_t* screen) {
  cam_n = settings_load_cams(cams, kMaxCams);
  cam_found = static_cast<CamInfo*>(heap_caps_calloc(20, sizeof(CamInfo), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  for (int i = 0; i < 2; ++i) {
    cam_frame[i] = static_cast<uint16_t*>(
        heap_caps_calloc(kCamFrameW * kCamFrameH, sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    memset(&cam_dsc[i], 0, sizeof(cam_dsc[i]));
    cam_dsc[i].header.always_zero = 0;
    cam_dsc[i].header.cf = LV_IMG_CF_TRUE_COLOR;
    cam_dsc[i].header.w = kCamFrameW;
    cam_dsc[i].header.h = kCamFrameH;
    cam_dsc[i].data_size = kCamFrameW * kCamFrameH * sizeof(uint16_t);
    cam_dsc[i].data = reinterpret_cast<const uint8_t*>(cam_frame[i]);
  }
  if (cam_frame[0] == nullptr || cam_frame[1] == nullptr) console_puts("Webcam frames: out of PSRAM");

  // Fullscreen viewer
  cam_root = lv_obj_create(screen);
  lv_obj_set_pos(cam_root, 0, 0);
  lv_obj_set_size(cam_root, kScreen, kScreen);
  style_plain(cam_root);
  lv_obj_set_style_bg_color(cam_root, lv_color_hex(0x05070C), 0);
  lv_obj_set_style_bg_opa(cam_root, LV_OPA_COVER, 0);
  cam_img = lv_img_create(cam_root);
  // The frame centres the picture; shift it up so a 16:9 preview sits between
  // the top pills and the overlay card instead of under the card.
  lv_obj_set_pos(cam_img, 0, -40);
  lv_obj_set_size(cam_img, kScreen, kScreen);
  lv_obj_clear_flag(cam_img, LV_OBJ_FLAG_CLICKABLE);
  hide(cam_img);

  cam_status = make_label(cam_root, &font_inter_16, lv_color_hex(kTextDim));
  lv_obj_set_width(cam_status, 400);
  lv_obj_set_style_text_align(cam_status, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(cam_status, LV_LABEL_LONG_WRAP);
  lv_obj_align(cam_status, LV_ALIGN_CENTER, 0, -40);

  cam_pill = make_pill_label(cam_root, &cam_page);
  lv_obj_align(cam_pill, LV_ALIGN_TOP_RIGHT, -16, 16);
  lv_obj_t* tag_lbl = nullptr;
  lv_obj_t* tag = make_pill_label(cam_root, &tag_lbl);
  lv_obj_set_flex_flow(tag, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(tag, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(tag, 6, 0);
  lv_obj_t* tag_icon = make_label(tag, &font_icons_24, lv_color_hex(kTeal));
  lv_label_set_text(tag_icon, ICON_VIDEOCAM);
  lv_obj_move_to_index(tag_icon, 0);
  lv_label_set_text(tag_lbl, "Kameras");
  lv_obj_align(tag, LV_ALIGN_TOP_LEFT, 16, 16);

  // Overlay card: title, place, capture age, Windy attribution.
  cam_card = lv_obj_create(cam_root);
  lv_obj_set_pos(cam_card, 16, 352);
  lv_obj_set_size(cam_card, kScreen - 32, 104);
  style_glass(cam_card, 20);
  lv_obj_set_style_bg_color(cam_card, lv_color_hex(0x0B0F1A), 0);
  lv_obj_set_style_bg_opa(cam_card, 175, 0);
  lv_obj_clear_flag(cam_card, LV_OBJ_FLAG_CLICKABLE);
  cam_title = make_label(cam_card, &font_inter_20, lv_color_hex(kText));
  lv_obj_set_width(cam_title, kScreen - 32 - 36);
  lv_label_set_long_mode(cam_title, LV_LABEL_LONG_DOT);
  lv_obj_set_height(cam_title, 26);
  lv_obj_set_pos(cam_title, 18, 12);
  cam_place = make_label(cam_card, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_set_width(cam_place, 200);
  lv_label_set_long_mode(cam_place, LV_LABEL_LONG_DOT);
  lv_obj_set_pos(cam_place, 18, 44);
  cam_age = make_label(cam_card, &font_inter_sb16, lv_color_hex(kAmber));
  lv_obj_set_pos(cam_age, 18, 68);
  // Windy terms: courtesy "Webcams provided by windy.com" in the corner, and
  // every image linked to its webcam page (shown as text on this panel).
  cam_link = make_label(cam_card, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_align(cam_link, LV_ALIGN_TOP_RIGHT, -16, 46);
  lv_obj_t* attr = make_label(cam_card, &font_inter_14, lv_color_hex(kTextDim));
  lv_label_set_text(attr, "Webcams provided by windy.com");
  lv_obj_align(attr, LV_ALIGN_BOTTOM_RIGHT, -16, -12);
  make_home_bar(cam_root);
  hide(cam_root);

  // Einstellungen > Kameras
  camset_root = make_screen_root(screen, "Kameras");
  static const char* kBtn[3] = {"+ ID", "Ort suchen", "API-Key"};
  static const char* kKey[3] = {"camset:id", "camset:search", "camset:key"};
  for (int i = 0; i < 3; ++i) make_choice(camset_root, kMargin + i * 148, 56, 136, 48, kBtn[i], on_camset_btn, i, kKey[i]);
  camset_status = make_label(camset_root, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_set_pos(camset_status, 24, 114);
  lv_obj_set_width(camset_status, 432);
  lv_label_set_long_mode(camset_status, LV_LABEL_LONG_DOT);
  camset_list = make_list(camset_root, 138, 318);

  camid_root = make_screen_root(screen, "Webcam per ID");
  camid_ta = make_ta(camid_root, 56, "Windy Webcam-ID");
  lv_textarea_set_accepted_chars(camid_ta, "0123456789");
  lv_textarea_set_max_length(camid_ta, 10);
  reg("camid:ta", camid_ta);
  camid_status = make_label(camid_root, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_set_pos(camid_status, 24, 118);
  lv_obj_set_width(camid_status, 432);
  lv_label_set_long_mode(camid_status, LV_LABEL_LONG_WRAP);

  camsearch_root = make_screen_root(screen, "Webcam suchen");
  camsearch_ta = make_ta(camsearch_root, 56, "Ort suchen");
  reg("camsearch:ta", camsearch_ta);
  camsearch_status = make_label(camsearch_root, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_set_pos(camsearch_status, 24, 116);
  lv_obj_set_width(camsearch_status, 432);
  lv_label_set_long_mode(camsearch_status, LV_LABEL_LONG_DOT);
  camsearch_list = make_list(camsearch_root, 140, 316);

  camkey_root = make_screen_root(screen, "Windy API-Key");
  make_caption(camkey_root, 58, "Aktuell");
  camkey_cur = make_label(camkey_root, &font_inter_sb16, lv_color_hex(kText));
  lv_obj_set_pos(camkey_cur, 24, 80);
  lv_obj_set_width(camkey_cur, 432);
  lv_label_set_long_mode(camkey_cur, LV_LABEL_LONG_DOT);
  camkey_ta = make_ta(camkey_root, 112, "Eigenen Key eingeben");
  lv_textarea_set_password_mode(camkey_ta, true);
  lv_textarea_set_password_bullet(camkey_ta, "*");
  lv_textarea_set_max_length(camkey_ta, 71);
  reg("camkey:ta", camkey_ta);
  lv_obj_t* def = make_choice(camkey_root, kMargin, 178, kScreen - 2 * kMargin, 48, "Eigenen Key l\xC3\xB6schen (Standard verwenden)",
                              on_key_default, 0, "camkey:default");
  (void)def;
  camkey_status = make_label(camkey_root, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_set_pos(camkey_status, 24, 240);
  lv_obj_set_width(camkey_status, 432);
  lv_label_set_long_mode(camkey_status, LV_LABEL_LONG_WRAP);
}
