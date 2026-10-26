#pragma once

// Board selection: idf.py -DPANEL_BOARD=sensecap_d1 (see CMakeLists.txt and
// sdkconfig.defaults.sensecap). Default is the Guition ESP32-4848S040C.
#include <stdint.h>

#include "sdkconfig.h"

#if CONFIG_PANEL_BOARD_SENSECAP_D1
#include "board_pins_sensecap_d1.h"
#else
#include "board_pins_guition.h"
#endif
