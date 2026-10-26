#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "net.h"
#include "panel_ui.h"
#include "retry_logic.h"
#include "strings_de.h"
#include "waste_window.h"

#include "sys.h"
#include "lvgl.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>

#include "fonts.h"
#include "ui_theme.h"
// UI text fonts follow the design (same size slots; Inter for Dark Glass,
// Montserrat for Co4). Declarations above keep the real symbol names.
#define font_inter_14 (*g_theme->f14)
#define font_inter_16 (*g_theme->f16)
#define font_inter_sb16 (*g_theme->fsb16)
#define font_inter_20 (*g_theme->f20)
#define font_inter_light_40 (*g_theme->f40)
#define font_inter_xl_72 (*g_theme->f72)
#define font_clock_150 (*g_theme->f150)
#include "clock_sync.h"
#include <math.h>
#include <sys/time.h>
#include <time.h>
#include "material_icons.h"
#include "settings.h"
#include "energy_task.h"
#include "espnow_play.h"
#include "pv_task.h"
#include "energy_mix.h"
#include "panel_hw.h"
#include "board_pins.h"
#include "weather_derive.h"
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <esp_system.h>

namespace {

constexpr int kScreen = 480;
constexpr int kEdge = 32;
// Touch is sampled every LV_INDEV_DEF_READ_PERIOD ms, so a quick 80 px flick
// can register as ~40 px. Taps stay at <= kTapSlop (12 px).
constexpr int kSwipe = 40;
constexpr int kMargin = 20;
constexpr int kGap = 12;
constexpr int kTileW = 214;
constexpr int kTileH = 156;
constexpr int kContentY = 88;
constexpr int kRowsPerPage = 2;
constexpr int kPerPage = 4;
constexpr int kMaxPages = 32;
constexpr uint32_t kShutterRereadMs = 3000;
constexpr int kLocalRelay = -2;

// Colours come from the active design (ui_theme.h: Dark Glass or Co4), chosen at boot.
#define kBgTop (g_theme->bg_top)
#define kBgBottom (g_theme->bg_bottom)
#define kText (g_theme->text)
#define kTextDim (g_theme->text_dim)
#define kCardTop (g_theme->card_top)
#define kCardBottom (g_theme->card_bottom)
#define kBorder (g_theme->border)
#define kOffIcon (g_theme->off_icon)
#define kAccent (g_theme->accent)
#define kAmber (g_theme->amber)
#define kGreen (g_theme->green)
#define kBlue (g_theme->blue)
#define kYellow (g_theme->yellow)
#define kOrange (g_theme->orange)
#define kCyan (g_theme->cyan)
#define kPurple (g_theme->purple)
#define kTeal (g_theme->teal)
#define kRed (g_theme->red)

enum Section : uint8_t { kSecFav = 0, kSecLight = 1, kSecShutter = 2, kSecOther = 3 };
enum class Nav : uint8_t {
  Home, Category, Weather, Settings, WifiList, WifiPass, Location, Display, Time,
  Cameras, CamSettings, CamId, CamSearch, CamKey,
  Transit, Calendar, Rain, TransitSet, StationSearch, CalSet, TextEdit, Maint, RemSet, Energy, EnSet,
  Playground
};

const char* kShutterLabel[2] = {"Auf", "Ab"};
const char* kShutterCommand[2] = {"FullUp", "FullDown"};
const char* kShutterIcon[2] = {ICON_ARROW_UP, ICON_ARROW_DOWN};
const char* kSectionIcon[4] = {ICON_STAR, ICON_BULB, ICON_BLINDS, ICON_TOGGLE};
uint32_t kSectionAccent[4] = {};  // set from the design in the UI build

struct RowPlan {
  bool shutter;
  int a;
  int b;
};

struct PagePlan {
  Section section;
  int nrows;
  RowPlan rows[kRowsPerPage];
};

lv_obj_t* home_root = nullptr;
lv_obj_t* cat_root = nullptr;
lv_obj_t* weather_temp = nullptr;
lv_obj_t* weather_big = nullptr;  // large icon in the weather card
lv_obj_t* home_date = nullptr;
lv_obj_t* home_time = nullptr;
char cur_cond[48] = "Wetter wird geladen";
// Forecast panels inside the weather card: [0] Heute, [1] Morgen.
lv_obj_t* fc_icon[2] = {nullptr, nullptr};
// Weather day detail screen.
lv_obj_t* wx_root = nullptr;
lv_obj_t* wx_title = nullptr;
lv_obj_t* wx_page = nullptr;
lv_obj_t* wx_icon = nullptr;
lv_obj_t* wx_temp = nullptr;
lv_obj_t* wx_cond = nullptr;
lv_obj_t* wx_info[4] = {nullptr, nullptr, nullptr, nullptr};
lv_obj_t* wx_dot[kWxSlots];
lv_obj_t* wx_hour[kWxSlots];
lv_obj_t* wx_sicon[kWxSlots];
lv_obj_t* wx_stemp[kWxSlots];
lv_obj_t* wx_smm[kWxSlots];
lv_obj_t* wx_sprob[kWxSlots];
lv_obj_t* wx_swind[kWxSlots];
lv_obj_t* wx_curve = nullptr;
lv_point_t wx_curve_pts[kWxSlots];
lv_obj_t* wx_prev_hint = nullptr;
lv_obj_t* wx_next_hint = nullptr;
lv_obj_t* wx_empty = nullptr;
WeatherDetailDay* wx_week = nullptr;  // PSRAM copy
int wx_count = 0;
int wx_day = 0;

lv_obj_t* fc_temp[2] = {nullptr, nullptr};
lv_obj_t* fc_rain[2] = {nullptr, nullptr};
bool fc_have = false;
lv_obj_t* card_btn[6] = {};
lv_obj_t* weather_place = nullptr;
// Settings screens
lv_obj_t* set_root = nullptr;
lv_obj_t* set_wifi_val = nullptr;
lv_obj_t* set_loc_val = nullptr;
lv_obj_t* set_disp_val = nullptr;
lv_obj_t* set_time_val = nullptr;
// Uhrzeit (NTP) screen
lv_obj_t* time_root = nullptr;
lv_obj_t* time_srv_btn[4] = {};
lv_obj_t* time_tz_btn[4] = {};
lv_obj_t* time_ta = nullptr;
lv_obj_t* time_status = nullptr;
TimeSettings tset = {};
bool time_saving = false;
struct TzChoice {
  const char* label;
  const char* name;
  const char* posix;
};
const char* kNtpServers[4] = {"ch.pool.ntp.org", "pool.ntp.org", "ntp.metas.ch", "time.google.com"};
const TzChoice kTzChoices[4] = {
    {"Z\xC3\xBCrich", "Europe/Zurich", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"London", "Europe/London", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"UTC", "UTC", "UTC0"},
    {"New York", "America/New_York", "EST5EDT,M3.2.0,M11.1.0"},
};
// Screensaver choices (Bildschirm)
lv_obj_t* disp_saver_btn[3] = {};
// Display settings screen and the screen-off layer
lv_obj_t* disp_root = nullptr;
lv_obj_t* disp_to_btn[kTimeoutChoiceN] = {};
lv_obj_t* disp_hint_sw = nullptr;
DisplaySettings disp = {120, true, kSaverBlack};
uint16_t scr_timeout_s = 120;  // effective timeout (harness SCRTO may override)
bool scr_off = false;
bool wake_swallow = false;
uint32_t last_activity = 0;
lv_obj_t* sleep_layer = nullptr;
lv_obj_t* sleep_hint = nullptr;
lv_obj_t* main_screen = nullptr;
// Clock screensavers (children of sleep_layer, a screen of its own)
lv_obj_t* saver_digital = nullptr;
lv_obj_t* dig_time = nullptr;
lv_obj_t* dig_date = nullptr;
lv_obj_t* saver_analog = nullptr;
lv_timer_t* saver_timer = nullptr;
int saver_last_min = -1;
float sec_angle = -1.f;  // second hand angle drawn last (deg)
lv_area_t sec_area = {};  // its bounding box (absolute coords)
constexpr uint8_t kClockBacklight = 40;  // percent while a clock saver shows
constexpr int kClockD = 440;             // analog clock object size
uint32_t hint_moved_ms = 0;
constexpr uint32_t kHintMoveMs = 45000;
constexpr uint8_t kHintBacklight = 4;  // percent
lv_obj_t* wl_root = nullptr;
lv_obj_t* wl_list = nullptr;
lv_obj_t* wl_status = nullptr;
char wifi_setup_note[96] = "";  // set while the firmware opened the WLAN selection
lv_obj_t* wp_root = nullptr;
lv_obj_t* wp_title = nullptr;
lv_obj_t* wp_ta = nullptr;
lv_obj_t* wp_status = nullptr;
lv_obj_t* loc_root = nullptr;
lv_obj_t* loc_ta = nullptr;
lv_obj_t* loc_list = nullptr;
lv_obj_t* loc_status = nullptr;
lv_obj_t* kb = nullptr;
lv_obj_t* msg_wifi_btn = nullptr;
WifiAp ui_aps[kMaxAps];
int ui_ap_n = 0;
bool wl_scanning = false;
ScanGen wl_gen;              // scan generation: "Abbrechen" makes a late result stale
lv_obj_t* wl_left = nullptr;   // "Abbrechen"
lv_obj_t* wl_right = nullptr;  // "Nochmals versuchen" (firmware opened the list) / "Neu suchen"
void wifi_list_leave();        // panel_ui_settings.inc
GeoPlace ui_places[kMaxPlaces];
int ui_place_n = 0;
bool loc_searching = false;
bool wp_connecting = false;
char wp_ssid[33] = "";
char cur_ssid[33] = "";
char cur_place[48] = "Buchs SG";
EXT_RAM_BSS_ATTR SettingsRequest settings_req;
bool settings_pending = false;

// Kameras (implementation at the end of the namespace).
lv_obj_t* cam_root = nullptr;
lv_obj_t* camset_root = nullptr;
lv_obj_t* camid_root = nullptr;
lv_obj_t* camsearch_root = nullptr;
lv_obj_t* camkey_root = nullptr;
lv_obj_t* set_cam_val = nullptr;
void cam_hide_all();
void cam_enter();
void cam_swipe(int dir);
void cam_tick();
void open_camset();
void build_camera_screens(lv_obj_t* screen);
void camset_value();
bool cam_kb_ready(lv_obj_t* ta);
bool cam_describe(char* out, size_t n);
bool cam_list_dump();
// panel_ui_info.inc (ÖV, Kalender, MeteoSchweiz, Wartung)
lv_obj_t* set_tr_val = nullptr;
lv_obj_t* set_cal_val = nullptr;
lv_obj_t* set_maint_val = nullptr;
lv_obj_t* set_rem_val = nullptr;
lv_obj_t* set_en_val = nullptr;
// Energie (panel_ui_energy.inc)
lv_obj_t* en_root[3] = {nullptr, nullptr, nullptr};  // 0 Strom, 1 Bilanz (PV), 2 Wasser
int en_page = 0;
lv_obj_t* enset_root = nullptr;
lv_obj_t* chip_en_top = nullptr;
lv_obj_t* chip_en_main = nullptr;
lv_obj_t* chip_en_sub = nullptr;  // energy tile: "Wasser gestern: N L"
void energy_swipe(int dir);
void energy_enter(int page);
void energy_refresh();
void energy_chip_refresh();
void open_enset();
void enset_refresh();
void enset_summary();
lv_obj_t* remset_root = nullptr;
void open_remset();
void rem_tick(bool force);
void disp_save_flush(bool force);
bool rem_describe(char* out, size_t n);
void build_reminders(lv_obj_t* screen);
bool rem_shown_now();
bool rem_ok_hit(int x, int y);
bool rem_ok_pressing();
void info_hide_all();
// Playground (panel_ui_play.inc)
lv_obj_t* set_play_val = nullptr;
PlayState play_last_state = PlayState::Off;
bool play_last_fwd = false;
void play_hide();
void play_tick();
void play_kb_ready(lv_obj_t* ta);
void open_play();
void info_hide_views();
void home_chips_refresh();
void home_warn_refresh();
void tr_swipe(int dir);
void rain_enter();
void open_trset();
void open_calset();
void open_maint();
void txt_go_back();
void trset_refresh();
void calset_refresh();
void maint_refresh();
bool st_kb_ready(lv_obj_t* ta);
bool txt_kb_ready(lv_obj_t* ta);
bool info_describe(char* out, size_t n);
bool info_list_dump();
void info_tick();
void build_home_info(lv_obj_t* weather_card);
void build_info_screens(lv_obj_t* screen);

// Named widgets for the test harness (POS <name> -> centre coordinates).
struct Named {
  char name[16];
  lv_obj_t* obj;
};
EXT_RAM_BSS_ATTR Named named[192];
int named_n = 0;
void reg(const char* name, lv_obj_t* obj) {
  for (int i = 0; i < named_n; ++i) {
    if (strcmp(named[i].name, name) == 0) {
      named[i].obj = obj;
      return;
    }
  }
  if (named_n < 192) {
    snprintf(named[named_n].name, sizeof(named[named_n].name), "%s", name);
    named[named_n].obj = obj;
    named_n++;
  }
}
void unreg_prefix(const char* prefix) {
  size_t n = strlen(prefix);
  for (int i = 0; i < named_n; ++i) {
    if (strncmp(named[i].name, prefix, n) == 0) named[i].obj = nullptr;
  }
}
lv_obj_t* cat_title = nullptr;
lv_obj_t* cat_page = nullptr;
lv_obj_t* prev_hint = nullptr;
lv_obj_t* next_hint = nullptr;
lv_obj_t* cat_status = nullptr;
lv_obj_t* msg_status = nullptr;
lv_obj_t* tiles[kPerPage] = {};
lv_obj_t* tile_name[kPerPage] = {};
lv_obj_t* tile_icon[kPerPage] = {};
lv_obj_t* cat_icon = nullptr;
lv_obj_t* tile_sub[kPerPage] = {};
lv_obj_t* shutter_box[kRowsPerPage] = {};
lv_obj_t* shutter_title[kRowsPerPage] = {};
lv_obj_t* shutter_pos[kRowsPerPage] = {};
lv_obj_t* empty_hint = nullptr;
lv_obj_t* message = nullptr;
lv_obj_t* message_title = nullptr;
lv_obj_t* message_body = nullptr;
lv_obj_t* retry_btn = nullptr;

EXT_RAM_BSS_ATTR LoxoneControl controls[kLoxoneControlCap];
size_t control_count = 0;
char server_name[48] = "";
PagePlan pages[kMaxPages];
int page_n = 0;
int section_origin[4] = {-1, -1, -1, -1};
int section_pages[4] = {};
int page = -1;
int section_page = 0;
Section current_section = kSecFav;
Nav nav = Nav::Home;
bool message_open = false;

bool known_on[kLoxoneControlCap] = {};
bool known[kLoxoneControlCap] = {};
bool needs_read[kLoxoneControlCap] = {};
bool prefer[kLoxoneControlCap] = {};
bool moving[kLoxoneControlCap] = {};
int position_pct[kLoxoneControlCap] = {};
uint32_t reread_after[kLoxoneControlCap] = {};
bool command_pending = false;
LoxoneCommandRequest pending = {};
bool retry_pending = false;

bool gesture_consumed = false;
lv_coord_t gesture_x0 = 0;
lv_coord_t gesture_y0 = 0;

lv_obj_t* make_label(lv_obj_t* parent, const lv_font_t* font, lv_color_t color) {
  lv_obj_t* label = lv_label_create(parent);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, color, 0);
  lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
  return label;
}

void hide(lv_obj_t* obj) {
  if (obj != nullptr) {
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  }
}

void show(lv_obj_t* obj) {
  if (obj != nullptr) {
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
  }
}

// Glass card: dark navy gradient with a thin light border.
void style_glass(lv_obj_t* obj, lv_coord_t radius) {
  lv_obj_set_style_bg_color(obj, lv_color_hex(kCardTop), 0);
  lv_obj_set_style_bg_grad_color(obj, lv_color_hex(kCardBottom), 0);
  lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(obj, lv_color_hex(g_theme->pressed_top), LV_STATE_PRESSED);
  lv_obj_set_style_bg_grad_color(obj, lv_color_hex(g_theme->pressed_bottom), LV_STATE_PRESSED);
  lv_obj_set_style_radius(obj, radius, 0);
  lv_obj_set_style_border_width(obj, 1, 0);
  lv_obj_set_style_border_color(obj, lv_color_hex(kBorder), 0);
  lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);
  lv_obj_set_style_shadow_width(obj, 0, 0);
  lv_obj_set_style_outline_width(obj, 0, 0);
  lv_obj_set_style_pad_all(obj, 0, 0);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

// Checked state (selected choice): accent-tinted glass with an accent border.
void style_checked(lv_obj_t* obj) {
  lv_obj_set_style_bg_color(obj, lv_color_hex(g_theme->checked_top), LV_STATE_CHECKED);
  lv_obj_set_style_bg_grad_color(obj, lv_color_hex(g_theme->checked_bottom), LV_STATE_CHECKED);
  lv_obj_set_style_border_color(obj, lv_color_hex(kAccent), LV_STATE_CHECKED);
}

// Home button: glass squircle with a coloured icon. The tap flash
// (LV_STATE_PRESSED) tints it with the accent.
void style_tile(lv_obj_t* obj, uint32_t accent) {
  lv_color_t a = lv_color_hex(accent);
  style_glass(obj, g_theme->r_tile);
  lv_obj_set_style_bg_color(obj, lv_color_mix(a, lv_color_hex(kCardTop), 90), LV_STATE_PRESSED);
  lv_obj_set_style_bg_grad_color(obj, lv_color_mix(a, lv_color_hex(kCardBottom), 50), LV_STATE_PRESSED);
  lv_obj_set_style_border_color(obj, a, LV_STATE_PRESSED);
}

void style_plain(lv_obj_t* obj) {
  lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(obj, 0, 0);
  lv_obj_set_style_shadow_width(obj, 0, 0);
  lv_obj_set_style_pad_all(obj, 0, 0);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
}

const char* section_name(Section section) {
  switch (section) {
    case kSecFav:
      return "Favoriten";
    case kSecLight:
      return "Licht";
    case kSecShutter:
      return "Storen";
    default:
      return "Schalter";
  }
}

const char* empty_text(Section section) {
  switch (section) {
    case kSecFav:
      return "Keine Favoriten";
    case kSecLight:
      return "Keine Lichter";
    case kSecShutter:
      return "Keine Storen";
    default:
      return "Keine weiteren Steuerungen";
  }
}

int round_c(float v) { return static_cast<int>(v >= 0.f ? v + 0.5f : v - 0.5f); }

#include "panel_ui_weather.inc"
#include "panel_ui_settings.inc"
#include "panel_ui_cams.inc"

#include "panel_ui_info.inc"
#include "panel_ui_rem.inc"
#include "panel_ui_energy.inc"
#include "panel_ui_play.inc"

}  // namespace

void ui_set_week(const WeatherDetailDay* days, int count) {
  if (count > kWxDays) count = kWxDays;
  if (wx_week == nullptr) {
    wx_week = static_cast<WeatherDetailDay*>(
        heap_caps_malloc(sizeof(WeatherDetailDay) * kWxDays, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (wx_week == nullptr) return;
  }
  memcpy(wx_week, days, sizeof(WeatherDetailDay) * count);
  wx_count = count;
  if (nav == Nav::Weather) {
    refresh_weather_day();
  }
}

void ui_describe(char* out, size_t n) {
  if (!message_open && cam_describe(out, n)) return;
  if (!message_open && (info_describe(out, n) || rem_describe(out, n))) return;
  if (message_open) {
    snprintf(out, n, "message");
  } else if (nav == Nav::Home) {
    snprintf(out, n, "home");
  } else if (nav == Nav::Weather) {
    snprintf(out, n, "weather %d/%d", wx_day + 1, wx_count);
  } else if (nav == Nav::Settings) {
    snprintf(out, n, "settings");
  } else if (nav == Nav::WifiList) {
    snprintf(out, n, "wifi %d scanning=%d", ui_ap_n, wl_scanning ? 1 : 0);
  } else if (nav == Nav::WifiPass) {
    snprintf(out, n, "wifipass connecting=%d", wp_connecting ? 1 : 0);
  } else if (nav == Nav::Time) {
    snprintf(out, n, "time server=%s tz=%s synced=%d", tset.server, tset.tz_name, clock_synced() ? 1 : 0);
  } else if (nav == Nav::Display) {
    snprintf(out, n, "display timeout=%u hint=%d saver=%d", static_cast<unsigned>(disp.timeout_s), disp.hint ? 1 : 0,
             disp.saver);
  } else if (nav == Nav::Location) {
    snprintf(out, n, "location %d searching=%d", ui_place_n, (loc_searching || settings_pending) ? 1 : 0);
  } else {
    snprintf(out, n, "category %s %d/%d", section_name(current_section), section_page + 1,
             section_pages[current_section]);
  }
}

void ui_touch_sample(bool pressed, int x, int y) {
  if (pressed) {
    if (!touch_down) {
      touch_down = true;
      touch_t0 = now_ms();
      touch_max_move = 0;
      gesture_x0 = x;
      gesture_y0 = y;
      gesture_consumed = false;
    }
    int m = abs(x - gesture_x0);
    int my = abs(y - gesture_y0);
    if (my > m) {
      m = my;
    }
    if (m > touch_max_move) {
      touch_max_move = m;
    }
    if (touch_max_move > kTapSlop) {
      gesture_consumed = true;  // moved: no widget may click from here on
    }
    touch_x = x;
    touch_y = y;
  } else if (touch_down) {
    touch_down = false;
    finish_touch();
  }
}

// LVGL heap attribution (LV_MEM_CUSTOM: LVGL allocates from the system heap):
// what building all screens took, internal and PSRAM (printed by DIAG mem).
size_t g_ui_build_int = 0, g_ui_build_psram = 0;
static void ui_init_impl();
void ui_init() {
  size_t i0 = heap_caps_get_free_size(MALLOC_CAP_INTERNAL), p0 = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  ui_init_impl();
  size_t i1 = heap_caps_get_free_size(MALLOC_CAP_INTERNAL), p1 = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  g_ui_build_int = i0 > i1 ? i0 - i1 : 0;
  g_ui_build_psram = p0 > p1 ? p0 - p1 : 0;
  heap_note("ui-build");
}
void style_header(lv_obj_t* t);  // panel_ui_settings.inc

static void ui_init_impl() {
  // Design is fixed for this boot (a change in Einstellungen restarts).
  ui_theme_select(settings_load_design());
  kSectionAccent[0] = kAmber;
  kSectionAccent[1] = kOrange;
  kSectionAccent[2] = kAccent;
  kSectionAccent[3] = kGreen;
  lv_obj_t* screen = lv_scr_act();
  lv_obj_set_style_bg_color(screen, lv_color_hex(kBgTop), 0);
  lv_obj_set_style_bg_grad_color(screen, lv_color_hex(kBgBottom), 0);
  lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  watch(screen);

  main_screen = screen;
  tset = settings_load_time();
  clock_apply_tz(tset.tz);

  home_root = lv_obj_create(screen);
  lv_obj_set_pos(home_root, 0, 0);
  lv_obj_set_size(home_root, kScreen, kScreen);
  style_plain(home_root);

  home_date = make_label(home_root, &font_inter_16, lv_color_hex(kTextDim));
  lv_obj_set_pos(home_date, 24, 24);
  home_time = make_label(home_root, &font_inter_16, lv_color_hex(kTextDim));
  lv_label_set_text(home_time, "--:--");
  lv_obj_align(home_time, LV_ALIGN_TOP_RIGHT, -24, 24);
  if (g_theme->logo) {
    lv_obj_t* logo = lv_img_create(home_root);
    lv_img_set_src(logo, &img_co4_logo);
    lv_obj_align(logo, LV_ALIGN_TOP_MID, 0, 12);
  }

  // One weather card: now (icon, temperature, place and condition), then
  // today and tomorrow side by side under a divider. Tap a day for details.
  lv_obj_t* weather = lv_obj_create(home_root);
  lv_obj_set_pos(weather, 24, 56);
  lv_obj_set_size(weather, kScreen - 48, 222);
  style_glass(weather, g_theme->r_weather);
  // Tapping the current-weather block opens today's details; the day panels
  // below are children and keep their own taps.
  lv_obj_clear_flag(weather, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(weather, on_day_panel, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<intptr_t>(0)));
  watch(weather);
  reg("wx:now", weather);

  weather_big = make_label(weather, &font_icons_80, lv_color_hex(kAmber));
  lv_obj_set_pos(weather_big, 18, 18);
  weather_icon(weather_big, -1);

  weather_temp = make_label(weather, &font_inter_xl_72, lv_color_hex(kText));
  lv_obj_set_pos(weather_temp, 120, 10);
  lv_label_set_text(weather_temp, "--");

  weather_place = make_label(weather, &font_inter_16, lv_color_hex(kTextDim));
  lv_obj_set_pos(weather_place, 124, 96);
  lv_obj_set_width(weather_place, 290);
  lv_label_set_long_mode(weather_place, LV_LABEL_LONG_DOT);

  make_divider(weather, 20, 130, kScreen - 48 - 40, 1);
  make_divider(weather, (kScreen - 48) / 2, 144, 1, 62);

  static const char* kDayName[2] = {"Heute", "Morgen"};
  for (int d = 0; d < 2; ++d) {
    lv_obj_t* panel = lv_obj_create(weather);
    lv_obj_set_pos(panel, 8 + d * 208, 136);
    lv_obj_set_size(panel, 200, 78);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_10, LV_STATE_PRESSED);
    lv_obj_set_style_radius(panel, 16, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_set_style_shadow_width(panel, 0, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(panel, on_day_panel, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<intptr_t>(d)));
    watch(panel);
    reg(d == 0 ? "day:0" : "day:1", panel);

    fc_icon[d] = make_label(panel, &font_icons_38, lv_color_hex(kAmber));
    lv_obj_align(fc_icon[d], LV_ALIGN_LEFT_MID, 12, 0);
    weather_icon(fc_icon[d], -1);

    lv_obj_t* name = make_label(panel, &font_inter_14, lv_color_hex(kTextDim));
    lv_label_set_text(name, kDayName[d]);
    lv_obj_set_pos(name, 64, 8);

    fc_temp[d] = make_label(panel, &font_inter_20, lv_color_hex(kText));
    lv_label_set_recolor(fc_temp[d], true);
    lv_label_set_text(fc_temp[d], "--");
    lv_obj_set_pos(fc_temp[d], 64, 26);

    lv_obj_t* drop = make_label(panel, &font_icons_16, lv_color_hex(kAccent));
    lv_label_set_text(drop, ICON_DROP);
    lv_obj_set_pos(drop, 63, 54);
    fc_rain[d] = make_label(panel, &font_inter_14, lv_color_hex(kAccent));
    lv_label_set_text(fc_rain[d], "");
    lv_obj_set_pos(fc_rain[d], 82, 53);
  }
  update_weather_line();

  // Five icon-only buttons in one row.
  struct HomeButton {
    const char* icon;
    const char* name;
    uint32_t color;
  };
  // Six icon-only buttons in one row (432 px: 6 x 64 + 5 x 9.6).
  static const HomeButton kButtons[6] = {
      {ICON_STAR, "cat:fav", kSectionAccent[0]},      {ICON_BULB, "cat:licht", kSectionAccent[1]},
      {ICON_BLINDS, "cat:storen", kSectionAccent[2]}, {ICON_TOGGLE, "cat:schalter", kSectionAccent[3]},
      {ICON_VIDEOCAM, "cat:kameras", kTeal},          {ICON_SETTINGS, "cat:settings", kPurple},
  };
  static const int kWhich[6] = {0, 1, 2, 3, 5, 4};  // on_category: 4 = settings, 5 = cameras
  for (int i = 0; i < 6; ++i) {
    const HomeButton& b = kButtons[i];
    lv_obj_t* btn = lv_btn_create(home_root);
    lv_obj_set_pos(btn, 24 + (i * 368) / 5, 300);
    lv_obj_set_size(btn, 64, 64);
    style_tile(btn, b.color);
    lv_obj_add_event_cb(btn, on_category, LV_EVENT_CLICKED,
                        reinterpret_cast<void*>(static_cast<intptr_t>(kWhich[i])));
    watch(btn);
    reg(b.name, btn);
    card_btn[i] = btn;
    uint32_t ic = b.color;
    if (g_theme->icon_mono) ic = i == 0 ? g_theme->icon_active : g_theme->icon;
    lv_obj_t* icon = make_label(btn, &font_icons_38, lv_color_hex(ic));
    lv_label_set_text(icon, b.icon);
    lv_obj_center(icon);
  }
  build_home_info(weather);  // warning pill in the card, ÖV + Kalender chips below the buttons

  cat_root = lv_obj_create(screen);
  lv_obj_set_pos(cat_root, 0, 0);
  lv_obj_set_size(cat_root, kScreen, kScreen);
  style_plain(cat_root);

  if (g_theme->header_bar) {
    lv_obj_t* bar = lv_obj_create(cat_root);
    style_plain(bar);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_size(bar, kScreen, 60);
    lv_obj_set_style_bg_color(bar, lv_color_hex(g_theme->grad_a), 0);
    lv_obj_set_style_bg_grad_color(bar, lv_color_hex(g_theme->grad_b), 0);
    lv_obj_set_style_bg_grad_dir(bar, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);
  }
  cat_icon = make_label(cat_root, &font_icons_24, lv_color_hex(kAmber));
  lv_obj_set_pos(cat_icon, 22, 20);
  cat_title = make_label(cat_root, &font_inter_20, lv_color_hex(kText));
  lv_obj_set_pos(cat_title, 54, 21);
  lv_label_set_text(cat_title, "");

  cat_page = make_label(cat_root, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_align(cat_page, LV_ALIGN_TOP_RIGHT, -24, 25);
  hide(cat_page);

  cat_status = make_label(cat_root, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_set_pos(cat_status, 24, 54);
  lv_obj_set_width(cat_status, 432);
  lv_label_set_long_mode(cat_status, LV_LABEL_LONG_DOT);
  lv_label_set_text(cat_status, "");

  for (int slot = 0; slot < kPerPage; ++slot) {
    int col = slot % 2;
    int row = slot / 2;
    int x = kMargin + col * (kTileW + kGap);
    int y = kContentY + row * (kTileH + kGap);
    lv_obj_t* btn = lv_btn_create(cat_root);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, kTileW, kTileH);
    style_glass(btn, 22);
    lv_obj_add_event_cb(btn, on_tile, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<intptr_t>(slot)));
    watch(btn);
    tiles[slot] = btn;

    tile_icon[slot] = make_label(btn, &font_icons_32, lv_color_hex(kOffIcon));
    lv_obj_set_pos(tile_icon[slot], 16, 14);

    tile_name[slot] = make_label(btn, &font_inter_sb16, lv_color_hex(kText));
    lv_obj_set_width(tile_name[slot], kTileW - 32);
    lv_label_set_long_mode(tile_name[slot], LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(tile_name[slot], 16, 66);

    tile_sub[slot] = make_label(btn, &font_inter_14, lv_color_hex(kTextDim));
    lv_obj_set_width(tile_sub[slot], kTileW - 32);
    lv_label_set_long_mode(tile_sub[slot], LV_LABEL_LONG_DOT);
    lv_obj_align(tile_sub[slot], LV_ALIGN_BOTTOM_LEFT, 16, -14);
    hide(btn);
  }

  for (int row_slot = 0; row_slot < kRowsPerPage; ++row_slot) {
    lv_obj_t* box = lv_obj_create(cat_root);
    lv_obj_set_pos(box, kMargin, kContentY + row_slot * (kTileH + kGap));
    lv_obj_set_size(box, kScreen - 2 * kMargin, kTileH);
    style_glass(box, 22);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_CLICKABLE);
    shutter_box[row_slot] = box;

    shutter_title[row_slot] = make_label(box, &font_inter_sb16, lv_color_hex(kText));
    lv_obj_set_pos(shutter_title[row_slot], 18, 16);
    lv_obj_set_width(shutter_title[row_slot], 300);
    lv_label_set_long_mode(shutter_title[row_slot], LV_LABEL_LONG_DOT);

    shutter_pos[row_slot] = make_label(box, &font_inter_14, lv_color_hex(kAccent));
    lv_obj_align(shutter_pos[row_slot], LV_ALIGN_TOP_RIGHT, -18, 18);

    for (int which = 0; which < 2; ++which) {
      lv_obj_t* btn = lv_btn_create(box);
      lv_obj_set_pos(btn, 14 + which * 208, 52);
      lv_obj_set_size(btn, 198, 88);
      style_shutter_button(btn);
      intptr_t tag = static_cast<intptr_t>(row_slot * 2 + which);
      lv_obj_add_event_cb(btn, on_shutter, LV_EVENT_CLICKED, reinterpret_cast<void*>(tag));
      watch(btn);
      lv_obj_t* arrow = make_label(btn, &font_icons_32, lv_color_hex(kAccent));
      lv_label_set_text(arrow, kShutterIcon[which]);
      lv_obj_align(arrow, LV_ALIGN_CENTER, -28, 0);
      lv_obj_t* label = make_label(btn, &font_inter_20, lv_color_hex(kText));
      lv_label_set_text(label, kShutterLabel[which]);
      lv_obj_align(label, LV_ALIGN_CENTER, 18, 0);
    }
    hide(box);
  }

  empty_hint = make_label(cat_root, &font_inter_20, lv_color_hex(kTextDim));
  lv_obj_set_width(empty_hint, 400);
  lv_label_set_long_mode(empty_hint, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(empty_hint, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(empty_hint, LV_ALIGN_CENTER, 0, 20);
  hide(empty_hint);

  // Right-edge handle: shown by refresh_category() only when a next page
  // exists. (It used to mean "swipe back to home" and was always shown.)
  next_hint = wx_handle(cat_root, 470);
  hide(next_hint);
  prev_hint = wx_handle(cat_root, 5);
  hide(prev_hint);
  make_home_bar(cat_root);

  hide(cat_root);

  build_weather_screen(screen);

  msg_status = make_label(screen, &font_inter_14, lv_color_hex(kTextDim));
  lv_obj_set_pos(msg_status, 24, 28);
  lv_obj_set_width(msg_status, 432);
  lv_label_set_long_mode(msg_status, LV_LABEL_LONG_DOT);
  lv_label_set_text(msg_status, "");
  hide(msg_status);

  message = lv_obj_create(screen);
  lv_obj_set_pos(message, 20, 88);
  lv_obj_set_size(message, 440, 360);
  style_glass(message, 28);
  lv_obj_set_style_bg_opa(message, 210, 0);
  watch(message);

  message_title = make_label(message, &font_inter_20, lv_color_hex(kOrange));
  lv_obj_set_width(message_title, 392);
  lv_label_set_long_mode(message_title, LV_LABEL_LONG_WRAP);
  lv_obj_align(message_title, LV_ALIGN_TOP_LEFT, 20, 16);

  message_body = make_label(message, &font_inter_20, lv_color_hex(kText));
  lv_obj_set_width(message_body, 392);
  lv_label_set_long_mode(message_body, LV_LABEL_LONG_WRAP);
  lv_obj_align(message_body, LV_ALIGN_TOP_LEFT, 20, 80);

  msg_wifi_btn = lv_btn_create(message);
  lv_obj_set_size(msg_wifi_btn, 188, 80);
  lv_obj_align(msg_wifi_btn, LV_ALIGN_BOTTOM_LEFT, 4, -16);
  style_glass(msg_wifi_btn, 20);
  lv_obj_add_event_cb(msg_wifi_btn, on_msg_wifi, LV_EVENT_CLICKED, nullptr);
  watch(msg_wifi_btn);
  reg("msg:wifi", msg_wifi_btn);
  lv_obj_t* wifi_label = make_label(msg_wifi_btn, &font_inter_20, lv_color_hex(kText));
  lv_label_set_text(wifi_label, "WLAN");
  lv_obj_center(wifi_label);

  retry_btn = lv_btn_create(message);
  lv_obj_set_size(retry_btn, 188, 80);
  lv_obj_align(retry_btn, LV_ALIGN_BOTTOM_RIGHT, -4, -16);
  reg("msg:retry", retry_btn);
  style_glass(retry_btn, 20);
  lv_obj_set_style_bg_color(retry_btn, lv_color_hex(kGreen), 0);
  lv_obj_set_style_bg_opa(retry_btn, LV_OPA_50, 0);
  lv_obj_add_event_cb(retry_btn, on_retry, LV_EVENT_CLICKED, nullptr);
  watch(retry_btn);
  lv_obj_t* retry_label = make_label(retry_btn, &font_inter_20, lv_color_hex(kText));
  lv_label_set_text(retry_label, str::kWifiRetry);
  lv_obj_set_width(retry_label, 172);
  lv_label_set_long_mode(retry_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(retry_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(retry_label);
  hide(message);
  hide(home_root);
  build_settings_screens(screen);
  build_camera_screens(screen);
  build_info_screens(screen);
  build_energy_screens(screen);
  build_enset_screen(screen);
  build_play_screen(screen);
  lv_obj_move_foreground(kb);
  build_sleep_layer();
  build_reminders(screen);
  lv_timer_create(clock_tick, 1000, nullptr);
  clock_tick(nullptr);
}

bool ui_power_touch(bool pressed, int x, int y) {
  if (pressed) last_activity = now_ms();
  // Screen off: every touch wakes the panel and is swallowed, except a press
  // on the reminder's OK button, which acknowledges in one tap.
  if (scr_off && pressed && !wake_swallow && rem_ok_hit(x, y)) return true;
  if (scr_off && pressed && rem_ok_pressing()) return true;
  if (scr_off && pressed) {
    wake_swallow = true;
    screen_on();
    console_puts("Wake touch swallowed");
    return false;
  }
  if (wake_swallow) {
    if (!pressed) wake_swallow = false;
    return false;
  }
  return true;
}

void ui_power_describe(char* out, size_t n) {
  snprintf(out, n, "scr=%s bl=%u to=%u saver=%d rem=%d", scr_off ? "off" : "on",
           static_cast<unsigned>(panel_backlight_level()), static_cast<unsigned>(scr_timeout_s), disp.saver,
           ui_rem_pending());
}

lv_obj_t* ui_overlay() { return scr_off ? sleep_layer : nullptr; }

void ui_saver_runtime(int saver) { disp.saver = static_cast<uint8_t>(saver); }

void ui_saver_describe(char* out, size_t n) {
  struct tm t;
  bool valid = local_now(&t, nullptr);
  snprintf(out, n, "scr_off=%d act_sleep=%d saver=%d analog_vis=%d digital_vis=%d timer_paused=%d bl=%u time_valid=%d %02d:%02d",
           scr_off ? 1 : 0, lv_scr_act() == sleep_layer ? 1 : 0, disp.saver,
           saver_analog != nullptr && !lv_obj_has_flag(saver_analog, LV_OBJ_FLAG_HIDDEN) ? 1 : 0,
           saver_digital != nullptr && !lv_obj_has_flag(saver_digital, LV_OBJ_FLAG_HIDDEN) ? 1 : 0,
           saver_timer != nullptr && saver_timer->paused ? 1 : 0, static_cast<unsigned>(panel_backlight_level()),
           valid ? 1 : 0, t.tm_hour, t.tm_min);
}

void ui_set_screen_timeout(uint16_t seconds) {
  scr_timeout_s = seconds;
  last_activity = now_ms();
}

void ui_show_message(const char* title, const char* body, bool retry) {
  lv_label_set_text(message_title, title != nullptr ? title : "");
  lv_label_set_text(message_body, body != nullptr ? body : "");
  if (retry) {
    show(retry_btn);
    show(msg_wifi_btn);
  } else {
    hide(retry_btn);
    hide(msg_wifi_btn);
  }
  hide_settings();
  message_open = true;
  hide(home_root);
  hide(cat_root);
  hide(wx_root);
  show(msg_status);
  show(message);
}

void ui_show_controls(const LoxoneControl* items, size_t count, size_t supported, const char* server) {
  if (count > kLoxoneControlCap) {
    count = kLoxoneControlCap;
  }
  control_count = count;
  page = -1;
  section_page = 0;
  command_pending = false;
  snprintf(server_name, sizeof(server_name), "%s", (server != nullptr && server[0] != '\0') ? server : "");
  memset(known, 0, sizeof(known));
  memset(known_on, 0, sizeof(known_on));
  memset(needs_read, 0, sizeof(needs_read));
  memset(prefer, 0, sizeof(prefer));
  memset(moving, 0, sizeof(moving));
  memset(position_pct, 0, sizeof(position_pct));
  memset(reread_after, 0, sizeof(reread_after));
  for (size_t i = 0; i < count; ++i) {
    controls[i] = items[i];
    needs_read[i] = has_status(controls[i]);
  }
  rebuild_pages();
  console_printf("Home categories ready, Schalter pages %d\n", section_pages[kSecOther]);
  hide(msg_status);
  show_home();
  ui_set_status(server_name);
  (void)supported;
}

void ui_set_status(const char* text) {
  const char* value = text != nullptr ? text : "";
  if (cat_status != nullptr) {
    lv_label_set_text(cat_status, value);
  }
  if (msg_status != nullptr) {
    lv_label_set_text(msg_status, value);
  }
}

void ui_set_forecast(bool ok, const WeatherDay* days, int count) {
  if (fc_temp[0] == nullptr) {
    return;
  }
  if (!ok) {
    if (!fc_have) {
      for (int d = 0; d < 2; ++d) {
        lv_label_set_text(fc_temp[d], "--");
        lv_label_set_text(fc_rain[d], "");
        weather_icon(fc_icon[d], -1);
      }
    }
    return;  // keep the last good forecast
  }
  fc_have = true;
  for (int d = 0; d < 2 && d < count; ++d) {
    char buf[48];
    snprintf(buf, sizeof(buf), "%d\xC2\xB0 #8A94A8 / %d\xC2\xB0#", round_c(days[d].t_max), round_c(days[d].t_min));
    lv_label_set_text(fc_temp[d], buf);
    snprintf(buf, sizeof(buf), "%.1f mm", static_cast<double>(days[d].precip));
    lv_label_set_text(fc_rain[d], buf);
    day_icon(fc_icon[d], days[d].kind);
  }
}

void ui_set_weather(bool ok, int temp_c, int code) {
  if (weather_temp == nullptr) {
    return;
  }
  if (!ok) {
    lv_label_set_text(weather_temp, "--");
    snprintf(cur_cond, sizeof(cur_cond), "Wetter nicht verf\xC3\xBCgbar");
  } else {
    char buf[12];
    snprintf(buf, sizeof(buf), "%d\xC2\xB0", temp_c);
    lv_label_set_text(weather_temp, buf);
    weather_icon(weather_big, code);
    snprintf(cur_cond, sizeof(cur_cond), "%s", weather_name(code));
  }
  update_weather_line();
}

void ui_time_done(bool ok) {
  time_saving = false;
  console_printf("Time settings saved=%d\n", ok ? 1 : 0);
  refresh_time_settings();
}

bool ui_take_command(LoxoneCommandRequest* out) {
  if (!command_pending || out == nullptr) {
    return false;
  }
  *out = pending;
  command_pending = false;
  return true;
}

bool ui_take_retry() {
  if (!retry_pending) {
    return false;
  }
  retry_pending = false;
  return true;
}

void ui_command_finished(int index, bool ok, const char* command, const char* detail) {
  if (index >= 0 && static_cast<size_t>(index) < control_count && ok && command != nullptr) {
    if (strcmp(command, "On") == 0) {
      known[index] = true;
      known_on[index] = true;
      needs_read[index] = false;
    } else if (strcmp(command, "Off") == 0) {
      known[index] = true;
      known_on[index] = false;
      needs_read[index] = false;
    } else if (controls[index].kind == kLoxoneShutter) {
      moving[index] = true;
      needs_read[index] = true;
      prefer[index] = true;
      reread_after[index] = now_ms() + kShutterRereadMs;
    }
  }
  if (!visible_pending()) {
    if (ok) {
      if (command != nullptr && strcmp(command, "Pulse") == 0) {
        ui_set_status("Impuls gesendet");
      } else if (command != nullptr && strcmp(command, "FullUp") == 0) {
        ui_set_status("Storen auf");
      } else if (command != nullptr && strcmp(command, "FullDown") == 0) {
        ui_set_status("Storen ab");
      } else {
        ui_set_status("Gesendet");
      }
    } else {
      ui_set_status(detail != nullptr && detail[0] != '\0' ? detail : "Befehl fehlgeschlagen");
    }
  }
  refresh_visible();
}

bool ui_take_state(LoxoneStateRequest* out) {
  if (out == nullptr || command_pending) {
    return false;
  }
  uint32_t now = now_ms();
  int index = pick_state(true, now);
  if (index < 0) {
    index = pick_state(false, now);
  }
  if (index < 0) {
    return false;
  }
  out->index = index;
  snprintf(out->action, sizeof(out->action), "%s", controls[index].action);
  snprintf(out->state, sizeof(out->state), "%s", controls[index].state);
  out->kind = controls[index].kind;
  return true;
}

void ui_state_finished(int index, bool ok, const char* value) {
  if (index < 0 || static_cast<size_t>(index) >= control_count) {
    return;
  }
  needs_read[index] = false;
  reread_after[index] = 0;
  moving[index] = false;
  if (ok) {
    apply_state_value(index, value);
  }
  publish_status();
  refresh_visible();
}

#include "panel_ui_api.inc"
