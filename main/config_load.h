#pragma once

// Credentials come only from include/panel_config.h when that file exists.
// A checkout without it compiles and boots the not-configured screen.

#if defined(__has_include)
#if __has_include("panel_config.h")
#include "panel_config.h"
#define PANEL_CONFIG_PRESENT 1
#else
#include "panel_config.example.h"
#define PANEL_CONFIG_PRESENT 0
#endif
#else
#include "panel_config.example.h"
#define PANEL_CONFIG_PRESENT 0
#endif

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif
#ifndef LOXONE_HOST
#define LOXONE_HOST ""
#endif
#ifndef LOXONE_PORT
#define LOXONE_PORT 80
#endif
#ifndef LOXONE_USER
#define LOXONE_USER ""
#endif
#ifndef LOXONE_PASSWORD
#define LOXONE_PASSWORD ""
#endif
#ifndef LOXONE_AUTH
#define LOXONE_AUTH "basic"
#endif

inline bool panel_is_configured() {
  return WIFI_SSID[0] != '\0' && LOXONE_HOST[0] != '\0' && LOXONE_USER[0] != '\0';
}
