#pragma once
// Text fonts: Inter 4.0 (SIL Open Font License 1.1, https://rsms.me/inter/),
// converted with lv_font_conv 1.5 (4 bpp). Text sizes cover U+0020-U+007E and
// U+00A0-U+00FF (äöü ÄÖÜ ß, °, ·). The large sizes only hold digits and the
// few signs they need (see the header of each .c file).

#include "lvgl.h"

LV_FONT_DECLARE(font_inter_14)      // Regular
LV_FONT_DECLARE(font_inter_16)      // Regular
LV_FONT_DECLARE(font_inter_sb16)    // SemiBold
LV_FONT_DECLARE(font_inter_20)      // SemiBold
LV_FONT_DECLARE(font_inter_light_40)  // Light: digits, space - / : °
LV_FONT_DECLARE(font_inter_xl_72)   // Display ExtraLight: digits, space - °
LV_FONT_DECLARE(font_clock_150)     // Display Thin: digits, space :

// Co4 design: Montserrat (SIL OFL 1.1, see fonts/OFL-Montserrat.txt), same size
// slots and glyph ranges as the Inter set above (+ U+2013, U+2026 for text).
LV_FONT_DECLARE(font_mont_14)    // Regular
LV_FONT_DECLARE(font_mont_16)    // Regular
LV_FONT_DECLARE(font_mont_sb16)  // SemiBold
LV_FONT_DECLARE(font_mont_20)    // SemiBold
LV_FONT_DECLARE(font_mont_40)    // Regular: digits, space - / : °
LV_FONT_DECLARE(font_mont_72)    // SemiBold: digits, space - °
LV_FONT_DECLARE(font_mont_150)   // SemiBold: digits, space :

LV_IMG_DECLARE(img_co4_logo)     // official Co4 logo, 80x36, transparent
