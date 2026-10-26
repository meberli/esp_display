#include "ui_theme.h"

#include "fonts.h"

namespace {

const UiTheme kDarkGlass = {
    "Dark Glass",
    0x1A2438, 0x0B0F1A, 0xE8ECF4, 0x8A94A8,
    0x1B2333, 0x131926, 0x2A3448, 0x5A6478,
    0x2C3954, 0x1F2940, 0x2B4A6B, 0x1F3550,
    0x7CC4FF, 0xFFC857, 0x7EE0A1, 0x7CC4FF, 0xFFC857, 0xFFD27A, 0x7CC4FF, 0xB4A8FF, 0x7FE3D8, 0xFF7A7A,
    false, 0, 0, 0,
    false, 0x7CC4FF, 0x7EE0A1,
    0x000000,
    20, 24, 28,
    &font_inter_14, &font_inter_16, &font_inter_sb16, &font_inter_20,
    &font_inter_light_40, &font_inter_xl_72, &font_clock_150,
    false,
};

// Co4 AG CI (co4_mockups/README.md): petrol #377289, green #69BA93 (logo end),
// Montserrat. Tailwind teal/slate deliberately not used.
const UiTheme kCo4 = {
    "Co4",
    0x13343F, 0x0F2A33, 0xE2E8F0, 0x9FB8C2,
    0x1A4553, 0x1A4553, 0x24596A, 0x5A6E78,
    0x2B5F72, 0x245566, 0x205364, 0x1A4553,
    0x69BA93, 0xFFC857, 0x69BA93, 0x7FB3C4, 0xFFC857, 0xFFD27A, 0x7FB3C4, 0x9FB8C2, 0x9EE0C2, 0xFF7A7A,
    true, 0xE2E8F0, 0x69BA93, 0x69BA93,
    true, 0x377289, 0x69BA93,
    0x0B2129,
    20, 18, 20,
    &font_mont_14, &font_mont_16, &font_mont_sb16, &font_mont_20,
    &font_mont_40, &font_mont_72, &font_mont_150,
    true,
};

}  // namespace

const UiTheme* g_theme = &kDarkGlass;

const UiTheme& ui_theme_get(uint8_t design) { return design == kDesignCo4 ? kCo4 : kDarkGlass; }

void ui_theme_select(uint8_t design) { g_theme = &ui_theme_get(design); }
