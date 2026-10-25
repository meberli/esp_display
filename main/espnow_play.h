#pragma once
// Einstellungen > Playground: CO4EN (gwuno-fw ESP-NOW, spec v0.4 DRAFT) test
// sender. Behaves like the planned CII module for pairing and protocol
// (dev_type 0x01 + CAP_TEST). Runs in its own task on core 0; ESP-NOW is only
// initialised once the Playground is used (pairing started, paired, or the
// whatwatt forward switched on), so a panel that never opens it is unchanged.
//
// UI <-> task only through queues: play_cmd_*() post commands, the UI drains
// play_ui_take() (status / log / last frame) from its own tick.
// The device secret, PMK and LMK are never logged.

#include <stddef.h>
#include <stdint.h>

#include "co4en.h"

enum class PlayState : uint8_t { Off = 0, Pairing, Confirming, Paired };

struct PlayCounters {
  PlayState state;
  bool fwd;          // whatwatt forward (persisted)
  bool radio;        // ESP-NOW initialised
  uint8_t channel;   // Wi-Fi primary channel
  uint8_t gw_mac[6];
  uint32_t pair_req; // PAIR_REQ broadcasts
  uint32_t sent;     // frames handed to esp_now_send
  uint32_t mac_ok;   // send callback: delivered (MAC ACK / broadcast sent)
  uint32_t acked;    // CO4EN ACK result 0
  uint32_t failed;   // send error, MAC failure, or app ACK timeout
  uint32_t dry;      // encoded and logged but not transmitted (not paired)
  uint32_t last_seq;
};

enum class PlayMsgKind : uint8_t { Status, Log, Frame, Qr };
struct PlayUiMsg {
  PlayMsgKind kind;
  PlayCounters c;
  char text[CO4EN_MAX_FRAME * 2 + 48];  // Log line / frame hex / pairing string
};

void play_start();                 // create the task (idle until used)
bool play_ui_take(PlayUiMsg* out); // UI task: next message, false if none

// Commands (any task; non-blocking).
void play_cmd_open();              // Playground page opened: send status + QR
void play_cmd_pair(bool start);
void play_cmd_reset();             // drop keys, new device secret (new QR)
void play_cmd_fwd(bool on);
void play_cmd_send(const char* name, const char* value, const char* unit);
// Test harness: feed a received frame as if it came from src (ESP-NOW RX).
void play_cmd_inject(const uint8_t src[6], const uint8_t* f, size_t n);
void play_diag();                  // DIAGCO4EN: state and counters (no secrets)
// Test harness only: the pairing string (contains the device secret).
void play_print_qr_test_only();
