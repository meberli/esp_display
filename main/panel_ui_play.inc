// Part of panel_ui.cpp (same translation unit, inside its anonymous namespace).
// Einstellungen > Playground: CO4EN ESP-NOW test sender (espnow_play.*).
// The protocol runs in the co4en task on core 0; this page only posts commands
// and shows what play_ui_take() delivers (drained from info_tick).

lv_obj_t* play_root = nullptr;
lv_obj_t* play_qr = nullptr;
lv_obj_t* play_state = nullptr;
lv_obj_t* play_qr_txt = nullptr;
lv_obj_t* play_pair_btn = nullptr;
lv_obj_t* play_pair_lbl = nullptr;
lv_obj_t* play_fwd_btn = nullptr;
lv_obj_t* play_fwd_lbl = nullptr;
lv_obj_t* play_enc_btn = nullptr;
lv_obj_t* play_enc_lbl = nullptr;
bool play_last_kvtext = false;
bool play_last_cii = false;
lv_obj_t* play_cii_btn = nullptr;
lv_obj_t* play_cii_lbl = nullptr;
lv_obj_t* play_cii_info = nullptr;
lv_obj_t* play_key_ta = nullptr;
lv_obj_t* play_val_ta = nullptr;
lv_obj_t* play_unit_ta = nullptr;
lv_obj_t* play_cnt = nullptr;
lv_obj_t* play_hex = nullptr;
lv_obj_t* play_log = nullptr;
lv_obj_t* play_log_cap = nullptr;
char play_log_buf[8][96];
int play_log_n = 0;

void play_hide() {
  hide(play_root);
}

void on_play_btn(lv_event_t* e) {
  if (gesture_consumed) return;
  int which = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  console_printf("Activate play %d\n", which);
  if (which == 0) play_cmd_pair(play_last_state == PlayState::Off);
  if (which == 1) play_cmd_reset();
  if (which == 2) play_cmd_fwd(!play_last_fwd);
  if (which == 4) play_cmd_kvtext(!play_last_kvtext);
  if (which == 5) play_cmd_pair_fast();
  if (which == 6) play_cmd_cii(!play_last_cii);
  if (which == 3) {
    play_cmd_send(lv_textarea_get_text(play_key_ta), lv_textarea_get_text(play_val_ta),
                  lv_textarea_get_text(play_unit_ta));
    lv_keyboard_set_textarea(kb, nullptr);
    hide(kb);
  }
}

void play_kb_ready(lv_obj_t* ta) {
  if (ta != play_key_ta && ta != play_val_ta && ta != play_unit_ta) return;
  lv_keyboard_set_textarea(kb, nullptr);
  hide(kb);
}

void on_play_ta_click(lv_event_t* e) {
  if (gesture_consumed) return;
  lv_obj_t* ta = lv_event_get_target(e);
  show_keyboard(ta);
  if (ta == play_val_ta) lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_NUMBER);
}

lv_obj_t* play_ta(lv_obj_t* parent, int x, int y, int w, const char* ph, const char* key, size_t max) {
  lv_obj_t* ta = make_ta(parent, y, ph);
  lv_obj_set_pos(ta, x, y);
  lv_obj_set_width(ta, w);
  lv_obj_remove_event_cb(ta, on_ta_click);
  lv_obj_add_event_cb(ta, on_play_ta_click, LV_EVENT_CLICKED, nullptr);
  lv_textarea_set_max_length(ta, max);
  reg(key, ta);
  return ta;
}

lv_obj_t* play_btn(lv_obj_t* parent, int x, int y, int w, const char* text, int user, const char* key,
                   lv_obj_t** label) {
  lv_obj_t* b = make_choice(parent, x, y, w, 52, text, on_play_btn, user, key);
  if (label != nullptr) *label = lv_obj_get_child(b, 0);
  return b;
}

void build_play_screen(lv_obj_t* screen) {
  play_root = make_screen_root(screen, "Playground \xC2\xB7 CO4EN");
  make_page_scrollable(play_root);
#if LV_USE_QRCODE
  play_qr = lv_qrcode_create(play_root, 132, lv_color_hex(0x000000), lv_color_hex(0xFFFFFF));
  lv_obj_set_pos(play_qr, kMargin + 6, 62);
  lv_obj_set_style_border_color(play_qr, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_width(play_qr, 6, 0);
#endif
  play_state = make_label(play_root, &font_inter_sb16, lv_color_hex(kText));
  lv_obj_set_pos(play_state, 176, 64);
  lv_obj_set_width(play_state, 284);
  lv_label_set_text(play_state, "...");
  play_qr_txt = make_label(play_root, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_set_pos(play_qr_txt, 176, 92);
  lv_obj_set_width(play_qr_txt, 284);
  lv_label_set_long_mode(play_qr_txt, LV_LABEL_LONG_WRAP);
  lv_label_set_text(play_qr_txt, "");
  play_pair_btn = play_btn(play_root, kMargin, 214, 140, "Pairing starten", 0, "play:pair", &play_pair_lbl);
  play_btn(play_root, kMargin + 150, 214, 120, "Reset", 1, "play:reset", nullptr);
  play_fwd_btn = play_btn(play_root, kMargin + 280, 214, 160, "Forward: AUS", 2, "play:fwd", &play_fwd_lbl);
  make_caption(play_root, 278, "Testwert senden (Key bekommt Pr\xC3\xA4" "fix test.)");
  play_key_ta = play_ta(play_root, kMargin, 300, 196, "name", "play:key", 20);
  play_val_ta = play_ta(play_root, kMargin + 204, 300, 128, "Wert", "play:val", 20);
  play_unit_ta = play_ta(play_root, kMargin + 340, 300, 100, "Einheit", "play:unit", 10);
  play_btn(play_root, kMargin, 362, 270, "Senden", 3, "play:send", nullptr);
  // CII default is KV-ID + KEYMAP; KV-Text stays available for test/dev.
  play_enc_btn = play_btn(play_root, kMargin + 280, 362, 160, "KV-ID", 4, "play:enc", &play_enc_lbl);
  // Restart the fast 3 s PAIR_REQ phase; CII mode = Wi-Fi off, real channel sweep.
  play_btn(play_root, kMargin, 424, 170, "Pairing 3 s neu", 5, "play:fast", nullptr);
  play_cii_btn = play_btn(play_root, kMargin + 180, 424, 260, "CII-Modus (ohne WLAN): AUS", 6, "play:cii", &play_cii_lbl);
  play_cii_info = make_label(play_root, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_set_pos(play_cii_info, 24, 482);
  lv_obj_set_width(play_cii_info, kScreen - 48);
  lv_label_set_long_mode(play_cii_info, LV_LABEL_LONG_WRAP);
  lv_label_set_text(play_cii_info, "");
  play_cnt = make_label(play_root, &font_inter_14, lv_color_hex(kText));
  lv_obj_set_pos(play_cnt, 24, 530);
  lv_obj_set_width(play_cnt, kScreen - 48);
  lv_label_set_long_mode(play_cnt, LV_LABEL_LONG_WRAP);
  lv_label_set_text(play_cnt, "");
  make_caption(play_root, 574, "Letzter Frame");
  play_hex = make_label(play_root, &font_inter_14, lv_color_hex(kTeal));
  lv_obj_set_pos(play_hex, 24, 594);
  lv_obj_set_width(play_hex, kScreen - 48);
  lv_label_set_long_mode(play_hex, LV_LABEL_LONG_WRAP);
  lv_label_set_text(play_hex, "-");
  lv_obj_t* lc = play_log_cap = make_caption(play_root, 0, "Log");
  lv_obj_align_to(lc, play_hex, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 16);
  play_log = make_label(play_root, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_set_width(play_log, kScreen - 48);
  lv_label_set_long_mode(play_log, LV_LABEL_LONG_WRAP);
  lv_label_set_text(play_log, "");
  lv_obj_align_to(play_log, lc, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);
  if (set_play_val != nullptr) lv_label_set_text(set_play_val, "CO4EN ESP-NOW Testsender");
}

void play_relayout() {
  lv_obj_t* lc = play_log_cap;
  lv_obj_update_layout(play_hex);
  lv_obj_align_to(lc, play_hex, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 16);
  lv_obj_align_to(play_log, lc, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);
}

const char* play_state_de(PlayState s) {
  switch (s) {
    case PlayState::Pairing: return "Pairing l\xC3\xA4uft ...";
    case PlayState::Confirming: return "Best\xC3\xA4tige Pairing ...";
    case PlayState::Paired: return "Gekoppelt";
    case PlayState::Lost: return "Gateway verloren";
    case PlayState::Searching: return "Gateway verloren \xC2\xB7 suche ...";
    default: return "Nicht gekoppelt";
  }
}

void play_apply(const PlayUiMsg& m) {
  const PlayCounters& c = m.c;
  play_last_state = c.state;
  play_last_fwd = c.fwd;
  play_last_kvtext = c.kv_text;
  play_last_cii = c.cii;
  bool linked = c.state != PlayState::Off && c.state != PlayState::Pairing;
  lv_label_set_text(play_enc_lbl, c.kv_text ? "KV-Text" : "KV-ID");
  if (c.kv_text) lv_obj_add_state(play_enc_btn, LV_STATE_CHECKED);
  else lv_obj_clear_state(play_enc_btn, LV_STATE_CHECKED);
  if (set_play_val != nullptr) lv_label_set_text(set_play_val, play_state_de(c.state));
  if (play_root == nullptr) return;
  if (m.kind == PlayMsgKind::Qr) {
#if LV_USE_QRCODE
    lv_qrcode_update(play_qr, m.text, strlen(m.text));
#endif
    lv_label_set_text(play_qr_txt, m.text);
  } else if (m.kind == PlayMsgKind::Frame) {
    lv_label_set_text(play_hex, m.text);
    play_relayout();
  } else if (m.kind == PlayMsgKind::Log) {
    if (play_log_n == 8) {
      memmove(play_log_buf[0], play_log_buf[1], sizeof(play_log_buf[0]) * 7);
      play_log_n = 7;
    }
    time_t now = time(nullptr);
    struct tm lt;
    localtime_r(&now, &lt);
    snprintf(play_log_buf[play_log_n++], sizeof(play_log_buf[0]), "%02d:%02d:%02d %s", lt.tm_hour, lt.tm_min,
             lt.tm_sec, m.text);
    char all[8 * 98] = "";
    for (int i = play_log_n - 1; i >= 0; --i) {  // newest first
      strlcat(all, play_log_buf[i], sizeof(all));
      if (i > 0) strlcat(all, "\n", sizeof(all));
    }
    lv_label_set_text(play_log, all);
  }
  char st[96];
  if (linked)
    snprintf(st, sizeof(st), "%s\nGW %02X:%02X:%02X:%02X:%02X:%02X \xC2\xB7 Kanal %u", play_state_de(c.state),
             c.gw_mac[0], c.gw_mac[1], c.gw_mac[2], c.gw_mac[3], c.gw_mac[4], c.gw_mac[5], c.channel);
  else
    snprintf(st, sizeof(st), "%s \xC2\xB7 Kanal %u", play_state_de(c.state), c.channel);
  lv_label_set_text(play_state, st);
  lv_obj_set_pos(play_qr_txt, 176, linked ? 112 : 92);
  lv_label_set_text(play_cii_lbl, c.cii ? "CII-Modus (ohne WLAN): EIN" : "CII-Modus (ohne WLAN): AUS");
  if (c.cii) lv_obj_add_state(play_cii_btn, LV_STATE_CHECKED);
  else lv_obj_clear_state(play_cii_btn, LV_STATE_CHECKED);
  char ci[200];
  int n = 0;
  if (c.cii)
    n = snprintf(ci, sizeof(ci), "WLAN getrennt \xC2\xB7 Kanalsuche 1-13 \xC2\xB7 kein whatwatt-Forward, nur manuelle Werte");
  else
    n = snprintf(ci, sizeof(ci), "Mit WLAN: Panel bleibt auf dem WLAN-Kanal");
  if (c.state == PlayState::Searching && n > 0)
    snprintf(ci + n, sizeof(ci) - n, "\nSuche: Kanal %u \xC2\xB7 Fehlversuche %u", c.sweep_ch, c.sweep_fails);
  else if (c.state == PlayState::Lost && n > 0)
    snprintf(ci + n, sizeof(ci) - n, "\nN\xC3\xA4" "chste Suche in %lu s \xC2\xB7 Fehlversuche %u",
             static_cast<unsigned long>(c.sweep_next_s), c.sweep_fails);
  lv_label_set_text(play_cii_info, ci);
  lv_label_set_text(play_pair_lbl, c.state == PlayState::Off       ? "Pairing starten"
                                   : c.state == PlayState::Pairing ? "Pairing stoppen"
                                                                   : "Gekoppelt");
  lv_label_set_text(play_fwd_lbl, c.fwd ? "Forward: EIN" : "Forward: AUS");
  if (c.fwd) lv_obj_add_state(play_fwd_btn, LV_STATE_CHECKED);
  else lv_obj_clear_state(play_fwd_btn, LV_STATE_CHECKED);
  lv_label_set_text_fmt(play_cnt,
                        "Gesendet %lu \xC2\xB7 MAC ok %lu \xC2\xB7 ACK %lu \xC2\xB7 Fehler %lu \xC2\xB7 "
                        "nur geloggt %lu \xC2\xB7 PAIR_REQ %lu \xC2\xB7 seq %lu%s",
                        static_cast<unsigned long>(c.sent), static_cast<unsigned long>(c.mac_ok),
                        static_cast<unsigned long>(c.acked), static_cast<unsigned long>(c.failed),
                        static_cast<unsigned long>(c.dry), static_cast<unsigned long>(c.pair_req),
                        static_cast<unsigned long>(c.last_seq), c.radio ? " \xC2\xB7 Funk an" : "");
}

void play_tick() {
  static PlayUiMsg m;  // UI task only
  for (int i = 0; i < 8 && play_ui_take(&m); ++i) play_apply(m);
  static uint32_t keep = 0;  // keep the status stream alive while the page is shown
  if (nav == Nav::Playground && now_ms() - keep > 10000) {
    keep = now_ms();
    play_cmd_open();
  }
}

void open_play() {
  nav = Nav::Playground;
  hide_settings();
  show(play_root);
  lv_obj_scroll_to_y(play_root, 0, LV_ANIM_OFF);
  play_cmd_open();
}
