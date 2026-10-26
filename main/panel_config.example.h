#pragma once

// Copy this file to include/panel_config.h and fill it in.
// include/panel_config.h is gitignored. Do not commit it.
//
// Leave WIFI_SSID, LOXONE_HOST, or LOXONE_USER empty to boot the
// not-configured screen. An empty WIFI_PASSWORD is an open network.
// An empty LOXONE_PASSWORD is sent as an empty basic-auth password.

// Wi-Fi
#define WIFI_SSID ""
#define WIFI_PASSWORD ""

// Local Loxone Miniserver. Host or IP only: no scheme, no path.
// The client speaks HTTP. HTTPS is not implemented.
#define LOXONE_HOST ""
#define LOXONE_PORT 80
#define LOXONE_USER ""
#define LOXONE_PASSWORD ""

// "basic" is implemented (HTTP basic auth).
// "token" is reserved for a later Miniserver token flow and will not connect.
#define LOXONE_AUTH "basic"
