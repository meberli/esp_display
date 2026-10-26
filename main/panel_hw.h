#pragma once

#include <stdint.h>

// Bring up the ST7701 RGB panel, GT911 touch, and LVGL display driver.
// Relay 1 (GPIO40) is a separate output. GPIO1 and GPIO2 stay untouched.
bool panel_display_begin();
uint8_t panel_touch_begin();
bool panel_touch_ok();
bool panel_lvgl_start();

// Drive relay 1 off. Call once at boot. Later changes come only from the UI.
void panel_relay_begin();
bool panel_relay_is_on();
void panel_relay_set(bool on);

// Backlight (GPIO38, LEDC PWM). percent 0..100, optional fade.
void panel_backlight(uint8_t percent, uint32_t fade_ms);
uint8_t panel_backlight_level();
void panel_fb_stats(uint32_t* black, uint32_t* white, uint32_t* red, uint32_t* other);  // sampled framebuffer
void panel_bl_describe(char* out, size_t n);
void panel_hw_dump();  // DIAGHW: read-only register readback (D1 only; Guition prints nothing)  // applied LEDC duty/frequency (DIAGFB)
