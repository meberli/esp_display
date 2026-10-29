// Part of panel_ui.cpp: the public ui_* API for settings, cameras and the
// info pages (UI task), included after the anonymous namespace. Split for
// readability only; not compiled on its own.

// --- Settings API (UI task) --------------------------------------------------

bool ui_take_settings(SettingsRequest* out) {
  if (!settings_pending) return false;
  *out = settings_req;
  settings_pending = false;
  return true;
}

void ui_wifi_scan_done(const WifiAp* aps, int n, uint32_t gen) {
  if (!wl_gen.accept(gen)) {
    console_printf("UI: Wi-Fi scan %lu result dropped (cancelled or stale)\n", static_cast<unsigned long>(gen));
    return;
  }
  ui_ap_n = n < 0 ? 0 : (n > kMaxAps ? kMaxAps : n);
  memcpy(ui_aps, aps, sizeof(WifiAp) * ui_ap_n);
  wl_scanning = false;
  if (nav != Nav::WifiList) return;
  char t[96];
  if (wifi_setup_note[0] != '\0') {
    snprintf(t, sizeof(t), "%s", wifi_setup_note);  // why the firmware opened this screen
  } else {
    snprintf(t, sizeof(t), ui_ap_n > 0 ? "%d Netzwerke" : "Keine Netzwerke gefunden", ui_ap_n);
  }
  lv_label_set_text(wl_status, t);
  fill_wifi_list();
}

// status: 0 ok, 1 password/auth rejected, 2 network not found, 3 timeout.
void ui_wifi_connect_done(bool ok, const char* ssid, const char* current, int status, const char* old_ssid,
                          bool reverted) {
  wp_connecting = false;
  snprintf(cur_ssid, sizeof(cur_ssid), "%s", current != nullptr ? current : "");
  if (ok) wifi_setup_note[0] = '\0';
  if (nav != Nav::WifiPass) return;
  if (ok) {
    open_settings();
    return;
  }
  const char* why = status == 1 ? "Passwort falsch?" : status == 2 ? "Netz nicht gefunden." : "Zeitüberschreitung.";
  char t[200];
  if (old_ssid != nullptr && old_ssid[0] != '\0') {
    snprintf(t, sizeof(t), "Verbindung zu %s fehlgeschlagen - %s Zurück zu %s%s", ssid, why, old_ssid,
             reverted ? "." : " (verbinde weiter ...).");
  } else {
    snprintf(t, sizeof(t), "Verbindung zu %s fehlgeschlagen - %s", ssid, why);
  }
  lv_label_set_text(wp_status, t);
  console_printf("UI: %s\n", t);
}

bool ui_wifi_busy() { return wp_connecting || (settings_pending && settings_req.kind == SettingsRequest::Connect); }

// Boot without Wi-Fi or saved network unreachable: open the WLAN selection.
// why: 0 none saved, 1 rejected (password?), 2 not found, 3 timeout.
void ui_wifi_setup_open(const char* saved, int why) {
  if (saved == nullptr || saved[0] == '\0') {
    snprintf(wifi_setup_note, sizeof(wifi_setup_note), "Kein WLAN eingerichtet - Netz wählen");
  } else {
    snprintf(wifi_setup_note, sizeof(wifi_setup_note), "%s %s - versuche weiter", saved,
             why == 1 ? "abgelehnt" : "nicht erreichbar");
  }
  console_printf("UI: WLAN selection (%s)\n", wifi_setup_note);
  if (scr_off) screen_on();
  last_activity = now_ms();
  message_open = false;
  hide(message);
  hide(msg_status);
  hide(home_root);
  hide(cat_root);
  hide(wx_root);
  open_settings();
  open_wifi_list();
}

void ui_wifi_setup_close() {
  wifi_setup_note[0] = '\0';
  if (nav == Nav::WifiList || nav == Nav::WifiPass || nav == Nav::Settings) {
    hide_settings();
    nav = Nav::Home;
  }
  ui_show_message(str::kConnectingTitle, str::kConnectingBody, false);
}

void ui_geocode_done(bool ok, const GeoPlace* places, int n) {
  loc_searching = false;
  ui_place_n = n < 0 ? 0 : (n > kMaxPlaces ? kMaxPlaces : n);
  memcpy(ui_places, places, sizeof(GeoPlace) * ui_place_n);
  if (nav != Nav::Location) return;
  lv_label_set_text(loc_status, !ok ? "Suche fehlgeschlagen" : ui_place_n == 0 ? "Kein Ort gefunden" : "Ort antippen");
  fill_loc_list();
}

void ui_set_place(const char* name) {
  snprintf(cur_place, sizeof(cur_place), "%s", name != nullptr ? name : "");
  update_weather_line();
}

void ui_location_done(bool ok, const char* name) {
  ui_set_place(name);
  if (nav == Nav::Location || nav == Nav::Settings) open_settings();
}

void ui_set_wifi_name(const char* ssid) { snprintf(cur_ssid, sizeof(cur_ssid), "%s", ssid != nullptr ? ssid : ""); }

bool ui_find(const char* name, int* x, int* y) {
  for (int i = 0; i < named_n; ++i) {
    if (strcmp(named[i].name, name) == 0 && named[i].obj != nullptr && obj_visible(named[i].obj)) {
      lv_obj_scroll_to_view_recursive(named[i].obj, LV_ANIM_OFF);  // rows in scrolled lists
      lv_obj_update_layout(named[i].obj);
      lv_area_t a;
      lv_obj_get_coords(named[i].obj, &a);
      *x = (a.x1 + a.x2) / 2;
      *y = (a.y1 + a.y2) / 2;
      return true;
    }
  }
  return false;
}

void ui_list_dump() {
  if (cam_list_dump()) return;
  if (info_list_dump()) return;
  if (nav == Nav::WifiList) {
    for (int i = 0; i < ui_ap_n; ++i) console_printf("ITEM %d %d %s\n", i, ui_aps[i].rssi, ui_aps[i].ssid);
  } else if (nav == Nav::Location) {
    for (int i = 0; i < ui_place_n; ++i)
      console_printf("ITEM %d %.4f,%.4f %s|%s\n", i, ui_places[i].lat, ui_places[i].lon, ui_places[i].name,
                     ui_places[i].region);
  }
  console_printf("%s", "ITEM_END\n");
}

bool ui_type(const char* text) {
  lv_obj_t* ta = kb != nullptr ? lv_keyboard_get_textarea(kb) : nullptr;
  if (ta == nullptr) return false;
  lv_textarea_add_text(ta, text);
  return true;
}

bool ui_enter() {
  if (kb == nullptr || lv_keyboard_get_textarea(kb) == nullptr) return false;
  lv_event_send(kb, LV_EVENT_READY, nullptr);
  return true;
}

void ui_reset_location() {
  SettingsRequest r = {};
  r.kind = SettingsRequest::ResetLocation;
  queue_settings(r);
}

// --- Kameras ---

bool ui_take_cam(CamRequest* out) {
  if (nav != Nav::Cameras || scr_off || cam_n == 0 || cam_inflight) return false;
  if (static_cast<int32_t>(now_ms() - cam_next_ms) < 0) return false;
  int target = cam_shown == 0 ? 1 : 0;
  if (cam_frame[target] == nullptr) return false;
  out->id = cams[cam_idx].id;
  out->frame = cam_frame[target];
  cam_inflight = true;
  return true;
}

void ui_cam_done(bool ok, int status, const CamInfo& info, uint16_t* frame, const char* err) {
  cam_inflight = false;
  bool current = cam_n > 0 && info.id == cams[cam_idx].id;
  if (!current) {
    cam_next_ms = now_ms();  // swiped meanwhile: fetch the camera now on screen
    return;
  }
  if (ok) {
    cam_shown = frame == cam_frame[1] ? 1 : 0;
    cam_shown_id = info.id;
    cam_info = info;
    cam_err[0] = '\0';
    // keep the stored title in sync with Windy (lists show it)
    if (info.title[0] != '\0' && strcmp(cams[cam_idx].title, info.title) != 0) {
      snprintf(cams[cam_idx].title, sizeof(cams[cam_idx].title), "%s", info.title);
    }
    lv_obj_invalidate(cam_img);
    cam_next_ms = now_ms() + kCamRefreshMs;
  } else {
    const char* k = key_error(status);
    snprintf(cam_err, sizeof(cam_err), "%s", k != nullptr ? k : (err != nullptr && err[0] != '\0') ? err : "Fehler");
    cam_next_ms = now_ms() + kCamRetryMs;
  }
  if (nav == Nav::Cameras) cam_refresh_view();
}

void ui_cam_lookup_done(bool ok, int status, const CamInfo& info) {
  cam_busy = false;
  if (key_testing) {
    key_testing = false;
    if (camkey_status != nullptr) {
      if (ok) lv_label_set_text(camkey_status, "Gespeichert. Key funktioniert.");
      else lv_label_set_text_fmt(camkey_status, "Gespeichert, aber Windy antwortet HTTP %d", status);
    }
    return;
  }
  if (!ok) {
    const char* k = key_error(status);
    if (k != nullptr) lv_label_set_text(camid_status, k);
    else lv_label_set_text_fmt(camid_status, "Fehler (HTTP %d)", status);
    return;
  }
  if (cam_add(info.id, info.title)) {
    snprintf(cam_added, sizeof(cam_added), "%s", info.title);
    if (nav == Nav::CamId) open_camset();
  } else {
    lv_label_set_text(camid_status, "Schon in der Liste oder Liste voll");
  }
}

void ui_cam_nearby_done(bool ok, int status, const CamInfo* found, int n, const char* place) {
  cam_busy = false;
  if (n > 20) n = 20;
  cam_found_n = (ok && cam_found != nullptr) ? n : 0;
  if (cam_found_n > 0) memcpy(cam_found, found, sizeof(CamInfo) * cam_found_n);
  if (nav != Nav::CamSearch) return;
  fill_found_list();
  if (!ok) {
    const char* k = key_error(status);
    if (status == 404 && place != nullptr) lv_label_set_text_fmt(camsearch_status, "Ort \"%s\" nicht gefunden", place);
    else if (k != nullptr) lv_label_set_text(camsearch_status, k);
    else lv_label_set_text_fmt(camsearch_status, "Fehler (HTTP %d)", status);
  } else {
    lv_label_set_text_fmt(camsearch_status, "%d Webcams um %s (25 km)", cam_found_n, place != nullptr ? place : "");
  }
}

void ui_cams_saved(bool ok) {
  cam_busy = false;
  if (!ok && camset_status != nullptr) lv_label_set_text(camset_status, "Speichern fehlgeschlagen");
}

void ui_windy_key_done(bool ok) {
  cam_busy = false;
  cam_err[0] = '\0';
  cam_next_ms = now_ms();
  if (nav != Nav::CamKey) return;
  char key[72], mask[32];
  bool own = false;
  bool have = settings_load_windy_key(key, sizeof(key), &own);
  settings_mask_key(key, mask, sizeof(mask));
  memset(key, 0, sizeof(key));
  if (!have) lv_label_set_text(camkey_cur, "Kein Key hinterlegt");
  else lv_label_set_text_fmt(camkey_cur, "%s: %s", own ? "Eigener Key" : "Standard-Key (Firmware)", mask);
  if (!ok) {
    lv_label_set_text(camkey_status, "Speichern fehlgeschlagen");
    return;
  }
  if (!have) {
    lv_label_set_text(camkey_status, "Kein Key: Kameras bleiben leer");
    return;
  }
  // Check the key with a lookup of the first camera.
  SettingsRequest r = {};
  r.kind = SettingsRequest::CamLookup;
  r.cam_id = cam_n > 0 ? cams[0].id : 1670606347u;
  key_testing = true;
  cam_busy = true;
  lv_label_set_text(camkey_status, "Gespeichert, pr\xC3\xBC" "fe Key ...");
  queue_settings(r);
}

// --- ÖV / Kalender / MeteoSchweiz / Wartung ---------------------------------

bool ui_take_transit(int* idx) {
  if (tr_inflight || scr_off) return false;
  uint32_t now = now_ms();
  int want = -1;
  if (nav == Nav::Transit && static_cast<int32_t>(now - tr_next_ms[tr_idx]) >= 0) want = tr_idx;
  else if (nav == Nav::Home && static_cast<int32_t>(now - tr_next_ms[0]) >= 0) want = 0;
  if (want < 0) return false;
  *idx = want;
  tr_inflight = true;
  return true;
}

void ui_transit_done(int idx, bool ok, const Board& b) {
  tr_inflight = false;
  if (idx < 0 || idx >= kStationN) return;
  if (ok) boards[idx] = b;
  else boards[idx].ok = false;
  uint32_t wait = !ok ? kTransitRetryMs : (nav == Nav::Transit && tr_idx == idx) ? kTransitViewMs : kTransitHomeMs;
  tr_next_ms[idx] = now_ms() + wait;
  if (nav == Nav::Transit) tr_refresh_view();
  if (idx == 0) home_chips_refresh();
}

void ui_station_search_done(bool ok, const Station* st, int n) {
  st_busy = false;
  st_hit_n = n < 0 ? 0 : (n > kMaxStationHits ? kMaxStationHits : n);
  memcpy(st_hits, st, sizeof(Station) * st_hit_n);
  if (nav != Nav::StationSearch) return;
  lv_label_set_text(st_status, !ok ? "Suche fehlgeschlagen" : st_hit_n == 0 ? "Keine Haltestelle gefunden" : "Haltestelle antippen");
  fill_st_list();
}

void ui_station_saved(bool ok, int idx) {
  st_busy = false;
  if (ok) {
    settings_load_stations(stations);
    boards[idx] = Board();
    tr_next_ms[idx] = now_ms();
  }
  if (nav == Nav::StationSearch) {
    if (ok) open_trset();
    else lv_label_set_text(st_status, "Speichern fehlgeschlagen");
  }
}

void ui_text_saved(bool ok, uint8_t which) {
  txt_busy = false;
  if (ok && (which == kUrlIcal || which == kUrlWaste || which == kTextWasteBfs || which == kTextWasteZone))
    cal_next_ms = now_ms();  // reload the calendar
  if (ok && which == kTextPlz) meteo_next_ms = now_ms();
  if (ok && which >= kTextEnHost) energy_kick();
  if (ok && (which == kTextPvGw || which == kTextPvGwPort || which == kTextPvUnit)) pv_kick();
  if (nav == Nav::TextEdit) {
    if (ok) txt_go_back();
    else lv_label_set_text(txt_status, "Speichern fehlgeschlagen");
  }
}

bool ui_take_calendar() {
  if (cal_inflight || cal == nullptr || !clock_synced()) return false;
  if (static_cast<int32_t>(now_ms() - cal_next_ms) < 0) return false;
  cal_inflight = true;
  return true;
}

void ui_calendar_done(bool ok, const Calendar& c) {
  cal_inflight = false;
  cal_next_ms = now_ms() + (ok ? kCalendarMs : kInfoRetryMs);
  if (ok || !cal_have) {
    memcpy(cal, &c, sizeof(Calendar));
    cal_have = true;
  }
  if (nav == Nav::Calendar) calv_refresh();
  if (nav == Nav::Home) home_chips_refresh();
  rem_tick(true);
}

bool ui_take_meteo() {
  if (meteo_inflight || !clock_synced()) return false;
  if (static_cast<int32_t>(now_ms() - meteo_next_ms) < 0) return false;
  meteo_inflight = true;
  return true;
}

void ui_meteo_done(bool ok, const MeteoNow& m) {
  meteo_inflight = false;
  meteo_next_ms = now_ms() + (ok ? kMeteoMs : kInfoRetryMs);
  if (ok) {
    meteo = m;
    meteo_have = true;
  }
  home_warn_refresh();
  if (nav == Nav::Rain) rain_refresh();
  if (nav == Nav::Radar) rain_dots_set();
  if (nav == Nav::Warn) {
    if (warn_active()) warn_refresh();
    else go_home();  // the last warning ended: the page goes away
  }
}

bool ui_take_ota(char* url, size_t n, bool* check, bool* own) {
  if (ota_check_req) {
    ota_check_req = false;
    bool o = false;
    if (!settings_load_text(kUrlOta, url, n, &o) || url[0] == '\0') {
      ota_state = OtaUi::Error;
      snprintf(ota_msg, sizeof(ota_msg), "Keine Update-URL");
      return false;
    }
    *check = true;
    *own = o;
    ota_running = true;
    return true;
  }
  if (ota_install_req) {
    ota_install_req = false;
    snprintf(url, n, "%s", ota_new_url);
    *check = false;
    *own = false;
    ota_running = true;
    return true;
  }
  return false;
}

void ui_ota_checked(const OtaCheck& c) {
  ota_running = false;
  if (!c.ok) {
    ota_state = OtaUi::Error;
    snprintf(ota_msg, sizeof(ota_msg), "%s", c.err);
  } else if (c.cmp > 0) {
    ota_state = OtaUi::Newer;
    snprintf(ota_new_ver, sizeof(ota_new_ver), "%s", c.version);
    snprintf(ota_new_url, sizeof(ota_new_url), "%s", c.url);
    snprintf(ota_notes, sizeof(ota_notes), "%s", c.notes);
    ota_msg[0] = '\0';
  } else {
    ota_state = OtaUi::Current;
    ota_new_ver[0] = '\0';
    ota_new_url[0] = '\0';
    if (c.cmp < 0) snprintf(ota_msg, sizeof(ota_msg), "Neueste Version online: v%s", c.version);
    else ota_msg[0] = '\0';
  }
  if (nav == Nav::Maint) maint_refresh();
}

void restart_now(lv_timer_t*) {
  disp_save_flush(true);
  esp_restart();
}

void ui_ota_done(bool ok, const char* err) {
  ota_running = false;
  ota_state = ok ? OtaUi::Installing : OtaUi::Error;
  snprintf(ota_msg, sizeof(ota_msg), "%s", ok ? "Installiert. Neustart ..." : err);
  if (nav == Nav::Maint) maint_refresh();
  if (ok) {
    lv_timer_t* t = lv_timer_create(restart_now, 1500, nullptr);
    lv_timer_set_repeat_count(t, 1);
  }
}

// Write pending display choices now (before a restart). UI task only.
void ui_flush_settings() { disp_save_flush(true); }

// --- Test harness: scroll-to-end check -------------------------------------------
// Scrolls every scrollable container of the visible page to its end, then checks
// that the bottom-most clickable widget is fully on screen (above the home bar
// edge), inside all its ancestors and the topmost hit at its centre (tappable).
namespace {
void sc_scroll_all(lv_obj_t* o, bool to_end, int* n) {
  if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
  if (lv_obj_has_flag(o, LV_OBJ_FLAG_SCROLLABLE)) {
    lv_coord_t b = lv_obj_get_scroll_bottom(o);
    if (to_end && b > 0) {
      lv_obj_scroll_to_y(o, lv_obj_get_scroll_y(o) + b, LV_ANIM_OFF);
      ++*n;
    } else if (!to_end && lv_obj_get_scroll_y(o) != 0) {
      lv_obj_scroll_to_y(o, 0, LV_ANIM_OFF);
    }
  }
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); ++i) sc_scroll_all(lv_obj_get_child(o, i), to_end, n);
}
// clickable: bottom-most clickable widget; else bottom-most visible leaf (labels,
// bars) for pages without buttons (lists of departures, events, ...).
void sc_lowest(lv_obj_t* o, lv_obj_t** best, lv_coord_t* best_y, bool clickable) {
  if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
  bool want = clickable ? lv_obj_has_flag(o, LV_OBJ_FLAG_CLICKABLE) && lv_obj_get_height(o) < 400
                        : lv_obj_get_child_cnt(o) == 0 && lv_obj_get_height(o) > 8;  // not the home bar
  if (want && o != lv_scr_act()) {
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    if (a.y2 > *best_y) {
      *best_y = a.y2;
      *best = o;
    }
  }
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); ++i) sc_lowest(lv_obj_get_child(o, i), best, best_y, clickable);
}
const char* sc_name(lv_obj_t* o) {
  for (lv_obj_t* p = o; p != nullptr; p = lv_obj_get_parent(p))
    for (int i = 0; i < named_n; ++i)
      if (named[i].obj == p) return named[i].name;
  return "?";
}
}  // namespace

bool ui_scroll_check(char* out, size_t n) {
  lv_obj_t* scr = lv_scr_act();
  int scrolled = 0;
  sc_scroll_all(scr, true, &scrolled);
  lv_obj_update_layout(scr);
  lv_obj_t* last = nullptr;
  lv_coord_t y2 = -1;
  sc_lowest(scr, &last, &y2, true);
  const bool tappable = last != nullptr;
  if (last == nullptr) sc_lowest(scr, &last, &y2, false);
  bool ok = true;
  const char* why = "ok";
  if (last != nullptr) {
    lv_area_t a;
    lv_obj_get_coords(last, &a);
    lv_point_t c = {static_cast<lv_coord_t>((a.x1 + a.x2) / 2), static_cast<lv_coord_t>((a.y1 + a.y2) / 2)};
    if (a.y2 > kScreen - 1 || a.y1 < 0) {
      ok = false;
      why = "off-screen";
    }
    for (lv_obj_t* p = lv_obj_get_parent(last); ok && p != nullptr; p = lv_obj_get_parent(p)) {
      lv_area_t pa;
      lv_obj_get_coords(p, &pa);
      if (a.y2 > pa.y2 + 1 || a.y1 < pa.y1 - 1) {
        ok = false;
        why = "clipped";
      }
    }
    lv_obj_t* hit = lv_indev_search_obj(scr, &c);
    bool hit_ok = false;
    for (lv_obj_t* h = hit; h != nullptr; h = lv_obj_get_parent(h))
      if (h == last) hit_ok = true;
    if (ok && tappable && !hit_ok) {
      ok = false;
      why = "covered";
    }
    snprintf(out, n, "%s last=%s y=%d..%d scrolled=%d %s%s", ok ? "ok" : "FAIL", sc_name(last), a.y1, a.y2, scrolled, why,
             tappable ? "" : " (no buttons: last visible item)");
  } else {
    snprintf(out, n, "ok none scrolled=%d", scrolled);
  }
  int dummy = 0;
  sc_scroll_all(scr, false, &dummy);  // back to the top
  return ok;
}

bool ui_rain_test(bool on) {
  rain_demo = on;
  rain_icon_refresh();
  if (nav == Nav::Rain) rain_refresh();
  return wx_week != nullptr && wx_count > 0;
}
