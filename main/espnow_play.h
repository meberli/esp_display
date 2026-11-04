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

// Lost: "Gateway verloren" (backoff), Searching: probing channels (auto-recovery).
enum class PlayState : uint8_t { Off = 0, Pairing, Confirming, Paired, Lost, Searching };

struct PlayCounters {
  PlayState state;
  bool fwd;          // whatwatt forward (persisted)
  bool kv_text;      // KV-Text instead of KV-ID + KEYMAP (persisted, test/dev)
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
  bool cii;             // CII mode: Wi-Fi off, channel sweep (not persisted)
  uint8_t link_miss;    // consecutive missed ACKs
  uint8_t sweep_st;     // Co4enSweepSt
  uint8_t sweep_ch;     // channel of the outstanding probe
  uint8_t sweep_fails;
  uint32_t sweep_next_s;
  uint8_t gw_chan;      // last known gateway channel (NVS)
  uint16_t vstore_mv;   // simulated VSTORE
  // v0.7 "sender follows the gateway" (Wi-Fi drop + BEACON listen / probe, AP pinned to gw_chan)
  uint8_t disc;         // 0 idle, 1 listen, 2 probe, 3 pair
  uint8_t disc_ch;      // channel being listened / probed
  bool pinned;          // Wi-Fi BSSID pinned to an AP on gw_chan
  uint8_t pin_ch;
  uint8_t ap_warn_ch;   // != 0: no AP of the SSID on gateway channel ap_warn_ch
  bool bc_have;         // a BEACON was accepted since boot
  uint8_t bc_ch, bc_flags;
  uint32_t bc_age_s;
  // runtime gateway re-search (link loss -> Wi-Fi drop + sweep), rate limited
  uint32_t keepalives;      // heartbeats sent while lost (between re-searches)
  bool lost_backoff;        // lost, waiting for the next re-search
  uint32_t resweeps;        // started since boot
  uint32_t last_resweep_s;  // seconds since the last one (0 = none)
  uint32_t backoff_s;       // seconds until the next one may start (0 = now)
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
void play_cmd_kvtext(bool on);   // Playground option: KV-Text encoding
void play_cmd_cii(bool on);      // CII mode: Wi-Fi disconnected, real channel sweep (not persisted, 30 min max)
void play_cmd_pair_fast();       // restart the fast 3 s PAIR_REQ phase
void play_cmd_vstore(uint16_t mv);  // test: simulated VSTORE (sweep needs >= 15.5 V)
void play_cmd_offchan(uint8_t ch);  // test: esp_now_switch_channel_tx feasibility
void play_cmd_send(const char* name, const char* value, const char* unit);
// Test harness: feed a received frame as if it came from src (ESP-NOW RX).
void play_cmd_inject(const uint8_t src[6], const uint8_t* f, size_t n);
uint8_t play_gw_chan();             // gateway channel when paired, else 0 (diag; racy read)
void play_diag();                  // DIAGCO4EN: state and counters (no secrets)
// Test harness only: the pairing string (contains the device secret).
void play_print_qr_test_only();
