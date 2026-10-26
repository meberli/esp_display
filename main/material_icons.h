#pragma once
// Google Material Symbols Rounded, instanced at FILL 1, wght 500, opsz 48,
// GRAD 0 (fontTools varLib.instancer), only the glyphs below, converted with
// lv_font_conv 1.5 (4 bpp):
//   font_icons_80: weather glyphs (home and day view)
//   font_icons_38: home buttons + weather glyphs
//   font_icons_32: control tiles (lightbulb, blinds_2, toggle_on/off, star,
//                  arrow_upward/downward)
//   font_icons_24: weather glyphs, water_drop, air, wb_twilight, wifi bars,
//                  lock, location_on, search, check, chevron_right, bedtime,
//                  schedule, lightbulb, blinds_2, toggle_on, star,
//                  keyboard_arrow_up, public, desktop_windows, videocam,
//                  add, delete, key, train, directions_bus, event, recycling,
//                  warning, system_update, memory, calendar_month, backup,
//                  router, info, notifications
//   font_icons_38 also: videocam
//   font_icons_16: water_drop, air, wb_twilight, nights_stay, bedtime, warning,
//                  schedule, train, directions_bus, event, recycling
// Material Symbols: Copyright Google LLC, Apache License 2.0
// (https://github.com/google/material-design-icons/blob/master/LICENSE).

#include "lvgl.h"

LV_FONT_DECLARE(font_icons_80)
LV_FONT_DECLARE(font_icons_38)
LV_FONT_DECLARE(font_icons_32)
LV_FONT_DECLARE(font_icons_24)
LV_FONT_DECLARE(font_icon_bolt_24)  // only ICON_BOLT (U+EA0B "bolt"), Energie
#define ICON_BOLT "\xEE\xA8\x8B"
LV_FONT_DECLARE(font_icons_16)

#define ICON_STAR "\xEF\x82\x9A"          // U+F09A
#define ICON_BULB "\xEE\xA4\x8F"          // U+E90F
#define ICON_BLINDS "\xF3\xBF\xBD\xB8"    // U+FFF78
#define ICON_TOGGLE "\xEE\xA7\xB6"        // U+E9F6 toggle_on
#define ICON_TOGGLE_OFF "\xEE\xA7\xB5"    // U+E9F5
#define ICON_SETTINGS "\xEE\xA2\xB8"      // U+E8B8
#define ICON_ARROW_UP "\xEE\x97\x98"      // U+E5D8
#define ICON_ARROW_DOWN "\xEE\x97\x9B"    // U+E5DB
#define ICON_WIFI3 "\xEE\x98\xBE"         // U+E63E
#define ICON_WIFI2 "\xEE\x93\x99"         // U+E4D9
#define ICON_WIFI1 "\xEE\x93\x8A"         // U+E4CA
#define ICON_WIFI0 "\xEF\x82\xB0"         // U+F0B0
#define ICON_LOCK "\xEE\xA2\x99"          // U+E899
#define ICON_PLACE "\xEF\x87\x9B"         // U+F1DB
#define ICON_SEARCH "\xEE\xBD\xBA"        // U+EF7A
#define ICON_CHECK "\xEE\x99\xA8"         // U+E668
#define ICON_CHEVRON "\xEE\x97\x8C"       // U+E5CC
#define ICON_SCREEN "\xEF\x85\x99"        // U+F159 bedtime
#define ICON_SCHEDULE "\xEE\xBF\x96"      // U+EFD6
#define ICON_UP "\xEE\x8C\x96"            // U+E316 keyboard_arrow_up
#define ICON_PUBLIC "\xEE\xA0\x8B"        // U+E80B
#define ICON_MONITOR "\xEE\x8C\x8C"       // U+E30C desktop_windows
#define ICON_DROP "\xEE\x9E\x98"          // U+E798 water_drop
#define ICON_AIR "\xEE\xBF\x98"           // U+EFD8
#define ICON_SUNRISE "\xEE\x87\x86"       // U+E1C6 wb_twilight
#define ICON_SUNSET "\xEF\x85\xB4"        // U+F174 nights_stay
#define ICON_VIDEOCAM "\xEE\x81\x8B"     // U+E04B (38, 24)
#define ICON_ADD "\xEE\x85\x85"          // U+E145 (24)
#define ICON_DELETE "\xEE\xA1\xB2"       // U+E872 (24)
#define ICON_KEY "\xEE\x9C\xBC"          // U+E73C (24)
#define ICON_TRAIN "\xEE\x95\xB0" // U+E570 (24, 16)
#define ICON_BUS "\xEE\xBF\xB6"   // U+EFF6 (24, 16)
#define ICON_EVENT "\xEE\xA1\xB8" // U+E878 (24, 16)
#define ICON_RECYCLE "\xEE\x9D\xA0" // U+E760 (24, 16)
#define ICON_WARNING "\xEF\x82\x83" // U+F083 (24, 16)
#define ICON_NOTIFY "\xEE\x9F\xB4" // U+E7F4 (24)
#define ICON_UPDATE "\xEF\x8B\x8D" // U+F2CD (24)
#define ICON_MEMORY "\xEE\x8C\xA2" // U+E322 (24)
#define ICON_CALENDAR "\xEE\xAF\x8C" // U+EBCC (24)
#define ICON_BACKUP "\xEE\xA1\xA4" // U+E864 (24)
#define ICON_ROUTER "\xEE\x8C\xA8" // U+E328 (24)
#define ICON_INFO "\xEE\xA2\x8E"  // U+E88E (24)
// Weather
#define ICON_W_SUN "\xEE\xA0\x9A"         // U+E81A sunny
#define ICON_W_PARTLY "\xEF\x85\xB2"      // U+F172 partly_cloudy_day
#define ICON_W_CLOUD "\xEF\x85\x9C"       // U+F15C cloud
#define ICON_W_RAIN "\xEF\x85\xB6"        // U+F176 rainy
#define ICON_W_SHOWERS "\xEF\x98\x9E"     // U+F61E rainy_light
#define ICON_W_SNOW "\xEE\x8B\x8D"        // U+E2CD weather_snowy
#define ICON_W_THUNDER "\xEE\xAF\x9B"     // U+EBDB thunderstorm
#define ICON_W_FOG "\xEE\xA0\x98"         // U+E818 foggy
