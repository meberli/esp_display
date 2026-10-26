# ESP-IDF project for the Loxone panel.
#   Guition ESP32-4848S040C (default):
#     idf.py build flash monitor -p COM14
#   SenseCAP Indicator D1 (own build dir, own project name = board ID):
#     idf.py -B build-d1 -DSDKCONFIG=build-d1/sdkconfig -DPANEL_BOARD=sensecap_d1 build
cmake_minimum_required(VERSION 3.22)
if(NOT PANEL_BOARD)
  set(PANEL_BOARD "guition")
endif()
if(PANEL_BOARD STREQUAL "sensecap_d1")
  if(NOT SDKCONFIG_DEFAULTS)
    set(SDKCONFIG_DEFAULTS "sdkconfig.defaults")
  endif()
  set(SDKCONFIG_DEFAULTS "${SDKCONFIG_DEFAULTS};sdkconfig.defaults.sensecap")
elseif(NOT PANEL_BOARD STREQUAL "guition")
  message(FATAL_ERROR "PANEL_BOARD must be guition or sensecap_d1")
endif()
# Local secrets (Windy API key) live in the gitignored sdkconfig.defaults.local
# and are merged after the regular defaults (and after sdkconfig.test when the
# test build passes -D SDKCONFIG_DEFAULTS=...).
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.local")
  if(NOT SDKCONFIG_DEFAULTS)
    set(SDKCONFIG_DEFAULTS "sdkconfig.defaults")
  endif()
  set(SDKCONFIG_DEFAULTS "${SDKCONFIG_DEFAULTS};sdkconfig.defaults.local")
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# The project name is the board ID: it is embedded in esp_app_desc_t
# (project_name), names the release asset (<name>.bin) and OTA refuses an
# image whose project_name differs from the running one (ota_board.cpp).
if(PANEL_BOARD STREQUAL "sensecap_d1")
  project(loxone_panel_sensecap_d1)
else()
  project(loxone_panel)
endif()
