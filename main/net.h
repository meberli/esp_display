#pragma once
// Wi-Fi station and a small blocking HTTP GET over esp_http_client.

#include <stddef.h>
#include <stdint.h>

#include "settings.h"

void wifi_start(const char* ssid, const char* password, const char* hostname);
bool wifi_connected();
// Playground CII mode: hold = disconnect and stop reconnecting (credentials kept);
// release = reconnect normally.
void wifi_hold(bool on);
// true once the driver reported a final failure (wrong password, no AP).
bool wifi_failed();
int wifi_auth_fails();   // auth / 4-way handshake rejections since wifi_start
int wifi_noap_fails();   // "no AP found" since wifi_start
int wifi_last_reason();  // last WIFI_REASON_* of a disconnect
// Blocks (event group, no polling) until connected, a disconnect event arrives
// or ms elapse. Returns wifi_connected(). Network task only.
bool wifi_wait(uint32_t ms);
// Scan (call from the network task). Returns the number of APs, strongest
// first, duplicates merged; -1 if generation gen was cancelled meanwhile.
int wifi_scan(WifiAp* out, int cap, uint32_t gen = 0);
// UI ("Abbrechen"): cancel scan generation gen (and older). Only stores an atomic;
// the net task stops the driver scan itself.
void wifi_scan_cancel(uint32_t gen);
// Playground "sender follows the gateway" (CO4EN v0.7 6.1). Generic: any SSID, any vendor.
// Scans the configured SSID on channel ch only and pins the strongest BSSID whose primary
// channel is ch (bssid_set + channel, RAM only). Returns 1 pinned, 0 no AP of the SSID on
// ch (pin cleared, normal connect), -1 scan failed (pin cleared). Does not connect while held.
int wifi_pin_select(uint8_t ch, uint8_t bssid_out[6] = nullptr, int8_t* rssi_out = nullptr);
void wifi_unpin();  // back to "any AP of the SSID" on the next connect
bool wifi_pinned(uint8_t bssid[6], uint8_t* ch);
// true once after the pinned AP was not found 6 x in a row (net unpinned itself and
// reconnects to any AP); the Playground then re-runs discovery.
bool wifi_pin_lost_take();
// Drop the association so the reconnect uses the current (pinned) config.
void wifi_reassoc();
// SSID of the configured network.
const char* wifi_ssid();

struct HttpGetResult {
  bool ok;         // transport ok and the whole body fit into the buffer
  int status;      // HTTP status, 0 if no response
  size_t length;   // body bytes stored
  char error[64];  // transport error text when !ok
};

// TLS serialization: all HTTPS traffic (http_get to https://, OTA check and
// OTA install) runs one request at a time so the TLS handshakes don't stack up
// in internal RAM. Recursive (an OTA check may call http_get). wait_ms 0 =
// wait forever. tls_unlock() records the internal-heap minimum under label.
bool tls_lock(const char* label, uint32_t wait_ms = 60000);
void tls_unlock(const char* label);
struct TlsGuard {
  explicit TlsGuard(const char* l, uint32_t wait_ms = 60000) : label(l), ok(tls_lock(l, wait_ms)) {}
  ~TlsGuard() { if (ok) tls_unlock(label); }
  const char* label;
  bool ok;
};
// Internal heap low-water attribution: heap_note(label) checks whether the
// all-time internal minimum dropped since the last note and, if so, blames
// label (logged "HEAP: ..."). heap_min_label() returns the current culprit.
void heap_note(const char* label);
const char* heap_min_label();
size_t heap_min_value();

HttpGetResult http_get_raw(const char* url, const char* user, const char* password, uint8_t* body, size_t cap,
                           int timeout_ms, const char* hdr_name = nullptr, const char* hdr_value = nullptr);
// GET url into body (cap bytes). user != nullptr adds HTTP basic auth.
// HTTPS uses the IDF certificate bundle.
// hdr_name/hdr_value add one request header (e.g. x-windy-api-key).
HttpGetResult http_get(const char* url, const char* user, const char* password, uint8_t* body, size_t cap,
                       int timeout_ms, const char* hdr_name = nullptr, const char* hdr_value = nullptr);
