#pragma once
// UI design tokens. One UiTheme per design; the active one is chosen at boot
// from NVS ("ui_design") and never changes at runtime (a change restarts).
// panel_ui.cpp maps its kXxx colour names and font names onto these fields.

#include <stdint.h>

#include "lvgl.h"

enum UiDesign : uint8_t { kDesignDarkGlass = 0, kDesignCo4 = 1, kDesignCount = 2 };

struct UiTheme {
  const char* name;
  // Background, text, surfaces.
  uint32_t bg_top, bg_bottom, text, text_dim;
  uint32_t card_top, card_bottom, border, off_icon;
  uint32_t pressed_top, pressed_bottom, checked_top, checked_bottom;
  // Accents (Dark Glass: rainbow; Co4: petrol/green family, sun stays yellow).
  uint32_t accent, amber, green, blue, yellow, orange, cyan, purple, teal, red;
  // Home buttons and settings rows: if icon_mono, every icon uses icon,
  // the first home button (Favoriten) uses icon_active.
  bool icon_mono;
  uint32_t icon, icon_active, row_icon;
  // Header bar behind screen titles (Co4 only) and accent gradient.
  bool header_bar;
  uint32_t grad_a, grad_b;
  uint32_t saver_bg;  // digital screensaver background
  // Radii.
  uint8_t r_card, r_tile, r_weather;
  // Fonts (same size slots in both designs).
  const lv_font_t *f14, *f16, *fsb16, *f20, *f40, *f72, *f150;
  bool logo;  // Co4 logo on home and screensaver
};

extern const UiTheme* g_theme;
const UiTheme& ui_theme_get(uint8_t design);
void ui_theme_select(uint8_t design);  // at boot, before the UI is built
