#pragma once
// Wall clock via SNTP (esp_netif_sntp). Runs in the network task; the time
// zone is applied by the UI task (TZ is process-wide, localtime_r reads it).

#include <stdbool.h>

// (Re)starts SNTP with this server. Safe to call again with a new server.
void clock_sntp_start(const char* server);
// true once SNTP has set the clock at least once since boot.
bool clock_synced();
// Seconds since boot of the last successful sync (0 = never).
unsigned clock_last_sync_s();
// setenv("TZ") + tzset(). Call from the UI task.
void clock_apply_tz(const char* posix_tz);

void clock_sntp_kick();  // after Wi-Fi got an IP: send the first request now
