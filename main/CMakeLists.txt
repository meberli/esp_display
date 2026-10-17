idf_component_register(
    SRCS "app_main.cpp" "sys.cpp" "net.cpp" "main.cpp" "panel_hw.cpp" "panel_ui.cpp"
         "loxone_client.cpp" "loxone_structure.cpp" "test_harness.cpp" "weather_derive.cpp"
         "settings.cpp" "settings_schema.cpp" "clock_sync.cpp" "webcams.cpp" "ical.cpp" "waste_api.cpp" "waste_window.cpp" "energy.cpp" "energy_task.cpp" "info_net.cpp" "maint.cpp" "ota_check.cpp" "diag.cpp" "font_inter_14.c" "font_inter_16.c" "font_inter_sb16.c"
         "font_inter_20.c" "font_inter_light_40.c" "font_inter_xl_72.c" "font_clock_150.c" "font_icons_80.c"
         "font_icons_38.c" "font_icons_32.c" "font_icons_24.c" "font_icon_bolt_24.c" "font_icons_16.c"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_netif lwip esp_event nvs_flash esp_http_client esp-tls esp_lcd esp_driver_gpio
             esp_driver_i2c esp_driver_ledc esp_driver_uart esp_timer esp_psram spi_flash esp_mm mbedtls esp_http_server app_update esp_https_ota esp_app_format ArduinoJson)
# The fonts and LVGL use designated initialisers in C++ headers.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers -Wno-unused-variable
                       -Wno-format)
