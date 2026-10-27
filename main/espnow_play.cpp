#include "espnow_play.h"

#include <atomic>
#include <stdarg.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "clock_sync.h"
#include "energy_task.h"
#include "esp_app_desc.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "net.h"
#include "nvs.h"
#include "sys.h"

namespace {

constexpr uint8_t kBcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
constexpr uint32_t kRtcMagic = 0xC04E0001;
constexpr uint32_t kConfirmPeriodMs = 5000;
constexpr int kConfirmFrames = 3;
constexpr uint32_t kHeartbeatMs = 60000;
constexpr uint32_t kFwdMinMs = 5000;
constexpr int kAckEvery = 6;
constexpr uint8_t kCaps = CO4EN_CAP_KV_TEXT | CO4EN_CAP_KV_ID | CO4EN_CAP_MAINS | CO4EN_CAP_TEST;

enum class CmdKind : uint8_t { Open, Pair, Reset, Fwd, Send, Rx, KvText, Cii, PairFast, Vstore, OffChan };
constexpr uint32_t kCiiMaxMs = 30 * 60000;  // CII mode switches itself off: Wi-Fi always comes back
struct Cmd {
  CmdKind kind;
  bool on;
  uint8_t src[6];
  uint16_t n;
  uint8_t data[CO4EN_MAX_FRAME];
  char name[24], value[24], unit[12];
};

RTC_NOINIT_ATTR uint32_t rtc_magic;
RTC_NOINIT_ATTR uint32_t rtc_seq;

QueueHandle_t g_cmd = nullptr;
QueueHandle_t g_ui = nullptr;
TaskHandle_t g_task = nullptr;

// Callback counters (Wi-Fi task -> play task).
std::atomic<uint32_t> a_mac_ok{0}, a_mac_fail{0};

// --- play task state (only touched by the play task) ---------------------------
PlayState st = PlayState::Off;
bool fwd = false, radio = false, have_secret = false, first_data = true;
uint8_t secret[CO4EN_SECRET_LEN];
uint8_t my_mac[6];
uint8_t gw_mac[6], pmk[16], lmk[16], gw_chan = 0;
bool kv_text_mode = false;  // NVS "kvtext"
bool km_done[128];          // KEYMAP announced since pairing / boot / ACK result 1
char test_keys[16][CO4EN_KEY_MAX + 1];  // test.* keys -> ids 100..115 (this boot)
Co4enNonceWin nwin;  // latest PAIR_REQ nonce and the one before
// Link loss + channel recovery (spec v0.5).
bool cii_mode = false;      // Wi-Fi disconnected, ESP-NOW hops channels (not persisted)
uint32_t cii_until = 0;
uint16_t vstore_mv = 16000; // simulated VSTORE (test parameter; mains display)
Co4enLink link = {};
Co4enSweep sw = {};
bool probe_out = false;     // pend is a sweep probe
uint8_t pair_hop = 0;       // CII pairing: next channel index of the PAIR_REQ round
uint8_t hint_ch = 0;        // last "gateway channel != Wi-Fi channel" hint logged
// v0.7 6.1: a Wi-Fi sender drops Wi-Fi briefly to find the gateway channel, then pins an
// AP of its own SSID on that channel. The gateway is never moved.
enum class Disc : uint8_t { Idle = 0, Listen, Probe, Pair };
constexpr uint32_t kDiscDwellMs = 1100;     // > default beacon_ms 1000
constexpr uint32_t kDiscListenMaxMs = 15000;
constexpr uint32_t kDiscMaxMs = 18000;      // hard cap Wi-Fi down (plus <= ~1 s AP scan)
constexpr uint32_t kDiscPairMs = 2500;      // PAIR_REQ phase after the listen
Disc disc = Disc::Idle;
bool disc_pairing = false;
uint8_t disc_order[13], disc_n = 0, disc_i = 0, disc_ch = 0, disc_bc_ch = 0;
uint32_t disc_t0 = 0, disc_t_next = 0, disc_phase_t0 = 0;
uint8_t ap_warn_ch = 0;      // no AP of our SSID on this gateway channel
uint8_t noap_n = 0;          // discoveries in a row that found the gateway but no AP on its channel
bool boot_aligned = false;
uint32_t keepalive_n = 0;
bool keepalive_out = false;  // outstanding heartbeat sent while lost
Co4enResweep rs = {};        // runtime re-search gate (count, last start, failures -> backoff)
Co4enBeacon last_bc = {};
bool bc_have = false;
uint32_t bc_ms = 0;
bool nonce_valid = false;
uint32_t pair_t0 = 0, next_pair = 0;
uint32_t pair_req_n = 0, sent_n = 0, acked_n = 0, failed_n = 0, dry_n = 0, last_seq_v = 0;
uint32_t data_n = 0, last_data_ms = 0, last_status_ms = 0, last_status_post = 0;
int confirm_n = 0;
uint32_t ack_seqs[8];
int ack_seq_i = 0;
// One outstanding ACK_REQ frame (CII: 20 ms wait, 1 app retry with a new seq).
struct Pending {
  bool active;
  uint32_t seq, deadline;
  int retries;
  Co4enFrame f;
} pend;
uint32_t ui_open_until = 0;
int64_t fwd_last_fetched = 0;
uint32_t fwd_last_ms = 0, fwd_poll_ms = 0;
EnergyView* ev = nullptr;
char qr[kCo4enQrLen];

// --- UI messages ---------------------------------------------------------------
void ui_post(PlayMsgKind k, const char* text);

PlayCounters counters() {
  PlayCounters c = {};
  c.state = st;
  c.fwd = fwd;
  c.kv_text = kv_text_mode;
  c.radio = radio;
  uint8_t ch = 0;
  wifi_second_chan_t sc;
  if (esp_wifi_get_channel(&ch, &sc) == ESP_OK) c.channel = ch;
  if (st != PlayState::Off && st != PlayState::Pairing) memcpy(c.gw_mac, gw_mac, 6);
  c.pair_req = pair_req_n;
  c.sent = sent_n;
  c.mac_ok = a_mac_ok.load();
  c.acked = acked_n;
  c.failed = failed_n + a_mac_fail.load();
  c.dry = dry_n;
  c.last_seq = last_seq_v;
  c.cii = cii_mode;
  c.link_miss = link.miss;
  c.sweep_st = static_cast<uint8_t>(sw.st);
  c.sweep_ch = sw.ch;
  c.sweep_fails = sw.fails;
  c.sweep_next_s = sw.st == Co4enSweepSt::Backoff ? (sw.t_next - now_ms()) / 1000 : 0;
  c.gw_chan = gw_chan;
  c.vstore_mv = vstore_mv;
  c.disc = static_cast<uint8_t>(disc);
  c.disc_ch = disc_ch;
  c.pinned = wifi_pinned(nullptr, &c.pin_ch);
  c.ap_warn_ch = ap_warn_ch;
  c.resweeps = rs.n;
  c.keepalives = keepalive_n;
  c.lost_backoff = st == PlayState::Lost && sw.st == Co4enSweepSt::Backoff;
  c.last_resweep_s = rs.n != 0 ? (now_ms() - rs.last_ms) / 1000 : 0;
  c.backoff_s = (co4en_resweep_wait_ms(rs, now_ms()) + 999) / 1000;
  c.bc_have = bc_have;
  c.bc_ch = last_bc.channel;
  c.bc_flags = last_bc.flags;
  c.bc_age_s = bc_have ? (now_ms() - bc_ms) / 1000 : 0;
  return c;
}

void ui_post(PlayMsgKind k, const char* text) {
  if (g_ui == nullptr) return;
  static PlayUiMsg m;  // play task only
  m.kind = k;
  m.c = counters();
  snprintf(m.text, sizeof(m.text), "%s", text != nullptr ? text : "");
  if (xQueueSend(g_ui, &m, 0) != pdTRUE) {
    static PlayUiMsg drop;
    xQueueReceive(g_ui, &drop, 0);  // drop the oldest, keep the newest
    xQueueSend(g_ui, &m, 0);
  }
}

void logf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void logf(const char* fmt, ...) {
  char b[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(b, sizeof(b), fmt, ap);
  va_end(ap);
  console_printf("CO4EN %s\n", b);
  ui_post(PlayMsgKind::Log, b);
}

const char* state_name(PlayState s) {
  switch (s) {
    case PlayState::Pairing: return "pairing";
    case PlayState::Confirming: return "confirming";
    case PlayState::Paired: return "paired";
    case PlayState::Lost: return "lost";
    case PlayState::Searching: return "searching";
    default: return "off";
  }
}

// --- NVS -----------------------------------------------------------------------
void nvs_save_blob(const char* k, const void* v, size_t n) {
  nvs_handle_t h;
  if (nvs_open(CO4EN_NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
  nvs_set_blob(h, k, v, n);
  nvs_commit(h);
  nvs_close(h);
}
void nvs_save_u8(const char* k, uint8_t v) {
  nvs_handle_t h;
  if (nvs_open(CO4EN_NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
  nvs_set_u8(h, k, v);
  nvs_commit(h);
  nvs_close(h);
}
void nvs_save_u32(const char* k, uint32_t v) {
  nvs_handle_t h;
  if (nvs_open(CO4EN_NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
  nvs_set_u32(h, k, v);
  nvs_commit(h);
  nvs_close(h);
}

void new_secret() {
  // Per-device random secret (never the public test vector secret).
  esp_fill_random(secret, sizeof(secret));
  nvs_save_blob(CO4EN_NVS_SECRET, secret, sizeof(secret));
  have_secret = true;
  co4en_qr_make(my_mac, secret, qr);
}

void drop_keys() {
  co4en_wipe(pmk, sizeof(pmk));
  co4en_wipe(lmk, sizeof(lmk));
  memset(gw_mac, 0, sizeof(gw_mac));
  gw_chan = 0;
  nvs_handle_t h;
  if (nvs_open(CO4EN_NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
    nvs_erase_key(h, CO4EN_NVS_PAIRED);
    nvs_erase_key(h, CO4EN_NVS_GW_MAC);
    nvs_erase_key(h, CO4EN_NVS_PMK);
    nvs_erase_key(h, CO4EN_NVS_LMK);
    nvs_erase_key(h, CO4EN_NVS_CHANNEL);
    nvs_erase_key(h, CO4EN_NVS_KM_SENT);
    nvs_commit(h);
    nvs_close(h);
  }
}

void load() {
  esp_read_mac(my_mac, ESP_MAC_WIFI_STA);
  nvs_handle_t h;
  uint8_t paired = 0, f = 0;
  if (nvs_open(CO4EN_NVS_NS, NVS_READONLY, &h) == ESP_OK) {
    size_t n = sizeof(secret);
    have_secret = nvs_get_blob(h, CO4EN_NVS_SECRET, secret, &n) == ESP_OK && n == sizeof(secret);
    nvs_get_u8(h, "fwd", &f);
    uint8_t kt = 0;
    nvs_get_u8(h, "kvtext", &kt);
    kv_text_mode = kt != 0;
    nvs_get_u8(h, CO4EN_NVS_PAIRED, &paired);
    size_t a = 6, b = 16, c = 16;
    bool keys = nvs_get_blob(h, CO4EN_NVS_GW_MAC, gw_mac, &a) == ESP_OK && a == 6 &&
                nvs_get_blob(h, CO4EN_NVS_PMK, pmk, &b) == ESP_OK && b == 16 &&
                nvs_get_blob(h, CO4EN_NVS_LMK, lmk, &c) == ESP_OK && c == 16;
    nvs_get_u8(h, CO4EN_NVS_CHANNEL, &gw_chan);
    nvs_close(h);
    if (paired == 1 && keys) st = PlayState::Paired;
  }
  fwd = f != 0;
  if (!have_secret) new_secret();
  else co4en_qr_make(my_mac, secret, qr);
  if (st != PlayState::Paired && paired != 0) drop_keys();  // unconfirmed keys from before a reboot
}

// seq: RTC RAM across warm reboots, NVS block of 256 across power cycles; the
// next block is reserved at +224 (co4en_seq_*), so seq never goes backwards.
Co4enSeq seqst;
bool seq_ready = false;
uint32_t next_seq() {
  if (!seq_ready) {
    uint32_t base = 0;
    bool have = false;
    nvs_handle_t h;
    if (nvs_open(CO4EN_NVS_NS, NVS_READONLY, &h) == ESP_OK) {
      have = nvs_get_u32(h, CO4EN_NVS_SEQ_BASE, &base) == ESP_OK;
      nvs_close(h);
    }
    if (co4en_seq_boot(&seqst, have, base, rtc_magic == kRtcMagic, rtc_seq)) nvs_save_u32(CO4EN_NVS_SEQ_BASE, seqst.nvs);
    seq_ready = true;
  }
  bool persist = false;
  uint32_t s = co4en_seq_take(&seqst, &persist);
  if (persist) nvs_save_u32(CO4EN_NVS_SEQ_BASE, seqst.nvs);  // before the frame leaves
  rtc_seq = seqst.next;
  rtc_magic = kRtcMagic;
  last_seq_v = s;
  return s;
}

uint32_t unix_now(uint8_t* flags) {
  if (!clock_synced()) return 0;
  if (flags != nullptr) *flags |= CO4EN_F_TIME_VALID;
  return static_cast<uint32_t>(time(nullptr));
}

// --- radio ---------------------------------------------------------------------
void on_send(const esp_now_send_info_t* info, esp_now_send_status_t s) {
  (void)info;
  if (s == ESP_NOW_SEND_SUCCESS) a_mac_ok.fetch_add(1);
  else a_mac_fail.fetch_add(1);
}

void on_recv(const esp_now_recv_info_t* info, const uint8_t* data, int n) {
  if (g_cmd == nullptr || info == nullptr || data == nullptr || n < CO4EN_HDR_LEN || n > CO4EN_MAX_FRAME) return;
  // Only the types a node consumes; anything else is dropped here.
  if (data[1] != CO4EN_T_PAIR_RESP && data[1] != CO4EN_T_ACK && data[1] != CO4EN_T_BEACON && data[1] != CO4EN_T_CMD)
    return;
  static Cmd c;  // Wi-Fi task only
  c.kind = CmdKind::Rx;
  memcpy(c.src, info->src_addr, 6);
  c.n = static_cast<uint16_t>(n);
  memcpy(c.data, data, n);
  xQueueSend(g_cmd, &c, 0);
}

bool add_gw_peer() {
  esp_now_del_peer(gw_mac);
  esp_now_peer_info_t p = {};
  memcpy(p.peer_addr, gw_mac, 6);
  p.channel = 0;  // current channel
  p.ifidx = WIFI_IF_STA;
  p.encrypt = true;
  memcpy(p.lmk, lmk, 16);
  esp_err_t e = esp_now_add_peer(&p);
  co4en_wipe(p.lmk, sizeof(p.lmk));
  return e == ESP_OK;
}

// Wi-Fi held by the Playground: CII mode or a gateway discovery. Only then may the channel move.
bool offair() { return cii_mode || disc != Disc::Idle; }

bool radio_up() {
  if (radio) return true;
  if (!wifi_connected() && !offair()) return false;
  if (esp_now_init() != ESP_OK) {
    logf("esp_now_init failed");
    return false;
  }
  esp_now_register_send_cb(on_send);
  esp_now_register_recv_cb(on_recv);
  esp_now_peer_info_t p = {};
  memcpy(p.peer_addr, kBcast, 6);
  p.channel = 0;
  p.ifidx = WIFI_IF_STA;
  esp_now_add_peer(&p);
  if (st != PlayState::Off && st != PlayState::Pairing) {
    esp_now_set_pmk(pmk);
    add_gw_peer();
  }
  radio = true;
  logf("radio on");
  return true;
}

void radio_down() {
  if (!radio) return;
  esp_now_unregister_recv_cb();
  esp_now_unregister_send_cb();
  esp_now_deinit();
  radio = false;
  logf("radio off");
}

// Hand a finished frame to ESP-NOW (or just log it: "dry").
void tx(const Co4enFrame& f, const uint8_t* dst, const char* mode) {
  static char hex[CO4EN_MAX_FRAME * 2 + 1];
  co4en_hex(f.b, f.n, hex, sizeof(hex));
  Co4enHdr h = {};
  co4en_hdr(f.b, f.n, &h);
  bool dry = dst == nullptr;
  console_printf("CO4EN TX seq=%lu type=%u flags=%02X len=%u to=%s hex=%s\n", static_cast<unsigned long>(h.seq),
                 h.type, h.flags, static_cast<unsigned>(f.n), mode, hex);
  if (dry) {
    ++dry_n;
  } else if (!radio || esp_now_send(dst, f.b, f.n) != ESP_OK) {
    ++failed_n;
  } else {
    ++sent_n;
  }
  char line[CO4EN_MAX_FRAME * 2 + 48];
  snprintf(line, sizeof(line), "seq=%lu t=%u %uB %s\n%s", static_cast<unsigned long>(h.seq), h.type,
           static_cast<unsigned>(f.n), mode, hex);
  ui_post(PlayMsgKind::Frame, line);
}

void remember_ack_seq(uint32_t s) {
  ack_seqs[ack_seq_i] = s;
  ack_seq_i = (ack_seq_i + 1) % 8;
}

// DATA frame: header is rewritten here (seq, flags) and the frame sent per CII
// rules. Not paired: logged only.
void send_data(Co4enFrame* f, uint8_t flags, bool force_ack) {
  bool linked = st == PlayState::Paired || st == PlayState::Confirming;
  bool ack = false;
  if (linked) {
    ack = force_ack || st == PlayState::Confirming || first_data || (data_n % kAckEvery) == 0;
    ++data_n;
  }
  if (ack) flags |= CO4EN_F_ACK_REQ;
  if (!linked) {  // unpaired / gateway lost: data stays local (logged, seq 0, not sent)
    f->b[2] = flags;
    memset(&f->b[4], 0, 4);
    tx(*f, nullptr, st == PlayState::Lost || st == PlayState::Searching ? "dry-lost" : "dry");
    return;
  }
  uint32_t s = next_seq();
  f->b[2] = flags;
  memcpy(&f->b[4], &s, 4);  // LE target
  radio_up();
  tx(*f, gw_mac, "gw");
  first_data = false;
  last_data_ms = now_ms();
  if (ack) {
    remember_ack_seq(s);
    pend.active = true;
    pend.seq = s;
    pend.deadline = now_ms() + CO4EN_ACK_WAIT_MS;
    pend.retries = 1;
    pend.f = *f;
  }
}

void begin_data(Co4enFrame* f, uint8_t* flags) {
  *flags = 0;
  uint32_t ts = unix_now(flags);
  co4en_begin(f, CO4EN_T_DATA, *flags, 0, ts);
}

void add_status(Co4enFrame* f) {
  co4en_status(f, 0 /* mains: no storage */, now_ms() / 1000, 0);
  last_status_ms = now_ms();
  // v0.7 WLAN_INFO (0x21): only while associated; the gateway stores it, never adopts it.
  wifi_ap_record_t ap;
  if (!offair() && wifi_connected() && co4en_room(*f) >= 10 && esp_wifi_sta_get_ap_info(&ap) == ESP_OK)
    co4en_wlan_info(f, ap.primary, ap.bssid, ap.rssi);
}

void send_status_frame(bool force_ack) {
  Co4enFrame f;
  uint8_t flags;
  begin_data(&f, &flags);
  add_status(&f);
  send_data(&f, flags, force_ack);
}

// --- link loss + channel recovery ---------------------------------------------------
uint8_t cur_channel() {
  uint8_t ch = 0;
  wifi_second_chan_t sc;
  esp_wifi_get_channel(&ch, &sc);
  return ch;
}

// Only without Wi-Fi: an associated STA stays on the AP channel.
void set_channel(uint8_t ch) {
  if (!offair() || ch < 1 || ch > 13) return;
  esp_err_t e = esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  if (e != ESP_OK) logf("set channel %u failed (%d)", ch, static_cast<int>(e));
}

// Pin an AP of the configured WLAN on gateway channel c (scan, BSSID pin, reassociate).
// No AP there: warn (log + UI queue), Wi-Fi stays where it is.
void pin_to_gw(uint8_t c, const char* why) {
  int8_t rssi = 0;
  int rc = wifi_pin_select(c, nullptr, &rssi);
  if (rc == 1) {
    ap_warn_ch = 0;
    logf("%s: WLAN ch %u != gateway ch %u: reconnecting to an AP on ch %u (rssi %d)", why, cur_channel(), c, c, rssi);
    wifi_reassoc();
  } else {
    ap_warn_ch = c;
    char m[64];
    snprintf(m, sizeof(m), "Kein Access Point auf Gateway-Kanal %u", c);
    logf("%s%s", m, rc < 0 ? " (scan failed)" : "");
    ui_post(PlayMsgKind::Status, m);
  }
}

// Gateway reported its channel (ACK-INFO / BEACON while associated): adopt it (NVS only on
// change) and move the WLAN to an AP on it. Once per distinct reported channel (hint_ch).
void channel_hint(uint8_t gw_ch) {
  if (offair() || gw_ch < 1 || gw_ch > 13 || gw_ch == hint_ch) return;
  bool paired = st == PlayState::Paired;
  uint8_t nc = paired ? co4en_gw_channel_update(gw_chan, gw_ch) : 0;
  if (nc != 0) {
    logf("gateway channel %u -> %u (reported by the gateway, saved)", gw_chan, nc);
    gw_chan = nc;
    nvs_save_u8(CO4EN_NVS_CHANNEL, gw_chan);
  }
  uint8_t ch = cur_channel();
  if (gw_ch == ch) return;
  hint_ch = gw_ch;
  if (!paired || !wifi_connected()) {
    logf("gateway channel %u != WLAN channel %u (AP re-selection on the next discovery)", gw_ch, ch);
    return;
  }
  pin_to_gw(gw_ch, "gateway channel hint");
}

// --- v0.7 discovery: Wi-Fi down (bounded), BEACON listen 1..13, then probe / PAIR_REQ ------
bool disc_possible() { return !cii_mode && wifi_ssid()[0] != '\0'; }

void disc_begin(bool pairing);
void disc_finish(uint8_t ch);

void start_search(const char* why) {
  pend.active = false;
  probe_out = false;
  st = PlayState::Lost;
  if (disc_possible()) {
    logf("%s: searching gateway (WLAN briefly off, beacon listen + sweep)", why);
    disc_begin(false);
    return;
  }
  co4en_sweep_start(&sw, gw_chan, cii_mode ? 0 : cur_channel(), now_ms());
  logf("%s: searching gateway (%s)", why, cii_mode ? "sweep ch 1-13" : "WLAN channel only");
}

// Probe = normal encrypted unicast DATA with STATUS + ACK_REQ, new seq every time.
void send_probe(uint8_t ch) {
  set_channel(ch);
  if (!radio_up()) return;
  Co4enFrame f;
  uint8_t flags;
  begin_data(&f, &flags);
  add_status(&f);
  flags |= CO4EN_F_ACK_REQ;
  uint32_t s = next_seq();
  f.b[2] = flags;
  memcpy(&f.b[4], &s, 4);
  char mode[16];
  snprintf(mode, sizeof(mode), "probe-ch%u", ch);
  tx(f, gw_mac, mode);
  remember_ack_seq(s);
  pend.active = true;
  pend.seq = s;
  pend.deadline = now_ms() + kCo4enProbeWaitMs;
  pend.retries = 0;
  pend.f = f;
  probe_out = true;
}

void search_tick(uint32_t t) {
  if (st != PlayState::Lost && st != PlayState::Searching) return;
  if (pend.active) return;
  if (disc == Disc::Listen || disc == Disc::Pair) return;
  if (disc == Disc::Idle && disc_possible() && sw.st == Co4enSweepSt::Backoff &&
      static_cast<int32_t>(t - sw.t_next) >= 0) {
    disc_begin(false);  // re-alignment after the 1/5/15/60 min backoff: again with Wi-Fi down
    return;
  }
  bool power_ok = !cii_mode || vstore_mv >= kCo4enVstoreMinMv;
  uint8_t ch = co4en_sweep_step(&sw, t, power_ok);
  if (ch != 0) {
    st = PlayState::Searching;
    send_probe(ch);
  } else if (sw.st == Co4enSweepSt::Backoff && st == PlayState::Searching) {
    st = PlayState::Lost;
    if (disc == Disc::Idle)  // inside a discovery disc_finish() logs the final backoff once
      logf("gateway not found: next search in %lu min", static_cast<unsigned long>(co4en_sweep_backoff_ms(sw.fails) / 60000));
  }
}

// Paired gateway heard again while lost (keepalive ACK / beacon): link ok, cancel the
// scheduled re-search, reset backoff + miss counter. No Wi-Fi change here.
void lost_recover(const char* why) {
  co4en_lost_recover(&link, &rs, &sw);
  keepalive_out = false;
  probe_out = false;
  st = PlayState::Paired;
  noap_n = 0;
  last_status_ms = now_ms();
  logf("gateway back (%s): link ok, re-search cancelled", why);
  ui_post(PlayMsgKind::Status, state_name(st));
}

void send_keepalive() {
  if (!radio_up()) return;
  Co4enFrame f;
  uint8_t flags;
  begin_data(&f, &flags);
  add_status(&f);
  flags |= CO4EN_F_ACK_REQ;
  uint32_t s = next_seq();
  f.b[2] = flags;
  memcpy(&f.b[4], &s, 4);
  tx(f, gw_mac, "keepalive");
  remember_ack_seq(s);
  pend.active = true;
  pend.seq = s;
  pend.deadline = now_ms() + CO4EN_ACK_WAIT_MS;
  pend.retries = 0;  // one frame per period; a miss only waits for the next one
  pend.f = f;
  keepalive_out = true;
  ++keepalive_n;
  last_status_ms = last_data_ms = now_ms();
}

void probe_found(const Co4enAck& a) {
  uint8_t ch = offair() ? sw.ch : cur_channel();
  bool changed = co4en_sweep_found(&sw, gw_chan);
  probe_out = false;
  st = PlayState::Paired;
  co4en_link_ack(&link);
  if (changed && offair()) {
    gw_chan = ch;
    nvs_save_u8(CO4EN_NVS_CHANNEL, gw_chan);
  }
  logf("gateway found on ch %u (ACK-INFO ch %u)%s", ch, a.channel, changed && offair() ? ", saved" : "");
  channel_hint(a.channel);
}

void cii_set(bool on) {
  if (on == cii_mode) return;
  if (on) {
    if (disc != Disc::Idle) {  // CII takes over the held Wi-Fi; discovery ends without AP selection
      disc = Disc::Idle;
      pend.active = false;
      probe_out = false;
    }
    cii_mode = true;
    cii_until = now_ms() + kCiiMaxMs;
    wifi_hold(true);
    radio_up();
    set_channel(gw_chan != 0 ? gw_chan : 1);
    logf("CII mode on: WLAN disconnected, no whatwatt forward (manual values only)");
    if (st == PlayState::Paired || st == PlayState::Lost || st == PlayState::Searching) start_search("CII mode");
    if (st == PlayState::Pairing) next_pair = now_ms();
  } else {
    cii_mode = false;
    pair_hop = 0;
    wifi_hold(false);
    logf("CII mode off: WLAN reconnecting");
    if (st == PlayState::Lost || st == PlayState::Searching) {
      pend.active = false;
      st = PlayState::Lost;
      sw.st = Co4enSweepSt::Idle;  // restarted on the AP channel once Wi-Fi is back
    }
  }
}

// Experimental: IDF 6.1 esp_now_switch_channel_tx (off-channel TX while associated).
void offchan_test(uint8_t ch) {
  if (!radio_up()) {
    logf("offchan: radio not up");
    return;
  }
  Co4enFrame f;
  uint8_t flags;
  begin_data(&f, &flags);
  add_status(&f);
  uint32_t s = next_seq();
  memcpy(&f.b[4], &s, 4);
  size_t sz = sizeof(esp_now_switch_channel_t) + f.n;
  auto* c = static_cast<esp_now_switch_channel_t*>(calloc(1, sz));
  if (c == nullptr) return;
  c->type = WIFI_OFFCHAN_TX_REQ;
  c->channel = ch;
  c->sec_channel = WIFI_SECOND_CHAN_NONE;
  c->wait_time_ms = kCo4enProbeWaitMs;
  memcpy(c->dest_mac, kBcast, 6);
  c->data_len = f.n;
  memcpy(c->data, f.b, f.n);
  uint32_t t0 = now_ms();
  esp_err_t e = esp_now_switch_channel_tx(c);
  free(c);
  logf("offchan ch %u seq %lu: %s (%d) in %lu ms, wifi=%d", ch, static_cast<unsigned long>(s), esp_err_to_name(e),
       static_cast<int>(e), static_cast<unsigned long>(now_ms() - t0), wifi_connected() ? 1 : 0);
}

// --- KV encoding: KV-ID + KEYMAP (CII default) or KV-Text (Playground option) ------
void km_reset() { memset(km_done, 0, sizeof(km_done)); }

uint16_t key_id(const char* key) {
  uint16_t id = co4en_default_key_id(key);
  if (id != 0 || strncmp(key, "test.", 5) != 0) return id;
  for (int i = 0; i < 16; ++i) {
    if (test_keys[i][0] == '\0') {
      snprintf(test_keys[i], sizeof(test_keys[i]), "%s", key);
      return static_cast<uint16_t>(100 + i);
    }
    if (strcmp(test_keys[i], key) == 0) return static_cast<uint16_t>(100 + i);
  }
  return 0;  // table full: KV-Text
}

// Appends one value; false (frame unchanged) if it does not fit.
bool put_kv(Co4enFrame* f, const char* key, const Co4enValue& v) {
  Co4enFrame t = *f;
  uint16_t id = kv_text_mode ? 0 : key_id(key);
  bool ok;
  if (id == 0) {
    ok = co4en_kv_text(&t, key, v);  // KV-Text mode, or a key without a KV-ID (e.g. w.vol)
  } else {
    ok = (km_done[id] || co4en_keymap(&t, id, key)) && co4en_kv_id(&t, id, v);
  }
  if (!ok || t.overflow) return false;
  *f = t;
  if (id != 0) km_done[id] = true;
  return true;
}

// --- pairing -------------------------------------------------------------------
void send_pair_req() {
  if (!radio_up()) {
    logf("pairing: waiting for Wi-Fi");
    return;
  }
  uint8_t nonce[CO4EN_NONCE_LEN];
  esp_fill_random(nonce, sizeof(nonce));
  co4en_nonce_push(&nwin, nonce, now_ms());
  nonce_valid = true;
  unsigned a = 0, b = 0, c = 0;
  sscanf(esp_app_get_description()->version, "%u.%u.%u", &a, &b, &c);
  const uint8_t fw[3] = {static_cast<uint8_t>(a), static_cast<uint8_t>(b), static_cast<uint8_t>(c)};
  uint8_t flags = 0;
  uint32_t ts = unix_now(&flags);
  Co4enFrame f;
  co4en_pair_req(&f, secret, my_mac, CO4EN_DEV_CII, fw, kCaps, nonce, next_seq(), ts, flags);
  co4en_wipe(nonce, sizeof(nonce));
  ++pair_req_n;
  tx(f, kBcast, offair() ? "bcast-hop" : "bcast");
}

void start_pairing() {
  if (st == PlayState::Paired || st == PlayState::Confirming) {
    logf("already paired: reset first");
    return;
  }
  st = PlayState::Pairing;
  pair_t0 = now_ms();
  next_pair = pair_t0;
  logf("pairing started (%s)", radio ? "radio on" : "waiting for Wi-Fi");
  if (disc_possible() && disc == Disc::Idle) disc_begin(true);
}

void stop_pairing() {
  if (st != PlayState::Pairing) return;
  st = PlayState::Off;
  nonce_valid = false;
  co4en_nonce_clear(&nwin);
  logf("pairing stopped");
  radio_down();
}

void on_pair_resp(const uint8_t src[6], const uint8_t* d, size_t n) {
  if (st != PlayState::Pairing || !nonce_valid) return;
  Co4enPairResult r;
  Co4enPairErr e = co4en_pair_resp_open_win(d, n, secret, nwin, now_ms(), kCo4enWrapXorV04, &r);
  if (e != kPairOk) {
    static const char* names[] = {"ok", "format", "mic", "nonce", "wrap"};
    logf("PAIR_RESP from %02X:%02X:%02X:%02X:%02X:%02X rejected: %s", src[0], src[1], src[2], src[3], src[4], src[5],
         names[e]);
    co4en_wipe(&r, sizeof(r));
    return;
  }
  memcpy(gw_mac, r.gw_mac, 6);
  memcpy(pmk, r.pmk, 16);
  memcpy(lmk, r.lmk, 16);
  gw_chan = r.channel;
  co4en_wipe(&r, sizeof(r));
  nonce_valid = false;
  co4en_nonce_clear(&nwin);
  km_reset();
  hint_ch = 0;
  if (offair()) gw_chan = cur_channel();  // found by the PAIR_REQ hop / beacon channel
  else channel_hint(gw_chan);
  pair_hop = 0;
  co4en_link_ack(&link);
  nvs_save_blob(CO4EN_NVS_GW_MAC, gw_mac, 6);
  nvs_save_blob(CO4EN_NVS_PMK, pmk, 16);
  nvs_save_blob(CO4EN_NVS_LMK, lmk, 16);
  nvs_save_u8(CO4EN_NVS_CHANNEL, gw_chan);
  if (radio) {
    esp_now_set_pmk(pmk);
    if (!add_gw_peer()) logf("add_peer failed");
  }
  st = PlayState::Confirming;
  confirm_n = 0;
  first_data = true;
  logf("PAIR_RESP ok from %02X:%02X:%02X:%02X:%02X:%02X ch=%u: confirming", gw_mac[0], gw_mac[1], gw_mac[2],
       gw_mac[3], gw_mac[4], gw_mac[5], gw_chan);
  ++confirm_n;
  send_status_frame(true);
}

void on_ack(const uint8_t src[6], const uint8_t* d, size_t n) {
  if (st == PlayState::Off || st == PlayState::Pairing) return;
  if (memcmp(src, gw_mac, 6) != 0) return;
  Co4enAck a;
  if (!co4en_parse_ack(d, n, &a)) {
    logf("ACK malformed");
    return;
  }
  bool ours = false;
  for (uint32_t s : ack_seqs) ours |= s == a.acked_seq && s != 0;
  if (!ours) {
    logf("ACK for unknown seq %lu", static_cast<unsigned long>(a.acked_seq));
    return;
  }
  bool probe_hit = probe_out && pend.active && a.acked_seq == pend.seq;
  if (pend.active && (a.acked_seq == pend.seq)) pend.active = false;
  co4en_link_ack(&link);  // any ACK-INFO: the gateway hears us
  co4en_resweep_ack(&rs);  // backoff back to the minimum
  if (probe_hit && st == PlayState::Searching) probe_found(a);
  else if (st == PlayState::Lost && disc == Disc::Idle) lost_recover("ACK");  // e.g. keepalive between re-searches
  keepalive_out = false;
  if (a.result != CO4EN_ACK_OK) {
    ++failed_n;
    if (a.result == CO4EN_ACK_ERR_DECODE) {  // e.g. unknown KV-ID: announce the KEYMAP again
      km_reset();
      logf("gateway: unknown id / decode error -> KEYMAP re-announced with the next frame");
    }
    logf("ACK seq=%lu result=%u", static_cast<unsigned long>(a.acked_seq), a.result);
    return;
  }
  ++acked_n;
  logf("ACK seq=%lu ok ch=%u", static_cast<unsigned long>(a.acked_seq), a.channel);
  channel_hint(a.channel);
  if (st == PlayState::Confirming) {
    st = PlayState::Paired;
    nvs_save_u8(CO4EN_NVS_PAIRED, 1);
    logf("pairing confirmed");
  }
}

// Unencrypted, no MIC: paired -> only from our gateway (src and gw_mac); before pairing only
// a channel hint (pairing_open), never a trust anchor (keys come from PAIR_RESP only).
void on_beacon(const uint8_t src[6], const uint8_t* d, size_t n) {
  Co4enBeacon b;
  if (!co4en_parse_beacon(d, n, &b)) return;
  bool linked = st != PlayState::Off && st != PlayState::Pairing;
  if (linked && (memcmp(src, gw_mac, 6) != 0 || memcmp(b.gw_mac, gw_mac, 6) != 0)) return;
  if (!linked && (memcmp(src, b.gw_mac, 6) != 0 || !(b.flags & kCo4enBeaconPairingOpen))) return;
  last_bc = b;
  bc_have = true;
  bc_ms = now_ms();
  if (disc == Disc::Listen && disc_bc_ch == 0) {
    disc_bc_ch = b.channel;
    logf("beacon %02X:%02X:%02X:%02X:%02X:%02X ch %u flags %02X (heard on ch %u)", b.gw_mac[0], b.gw_mac[1],
         b.gw_mac[2], b.gw_mac[3], b.gw_mac[4], b.gw_mac[5], b.channel, b.flags, cur_channel());
  } else if (linked && !offair()) {
    if (st == PlayState::Lost && disc == Disc::Idle) lost_recover("beacon");
    if (b.channel != gw_chan) channel_hint(b.channel);
  }
}

void disc_begin(bool pairing) {
  if (!disc_possible() || disc != Disc::Idle) return;
  if (!pairing) {  // re-search drops Wi-Fi: rate limited (2 min min, doubling to 30 min on failures)
    uint32_t w = co4en_resweep_wait_ms(rs, now_ms());
    if (w != 0) {
      sw.st = Co4enSweepSt::Backoff;
      sw.t_next = now_ms() + w;
      logf("gateway re-search in %lu s (backoff, WLAN stays on)", static_cast<unsigned long>(w / 1000));
      return;
    }
    co4en_resweep_started(&rs, now_ms());
  }
  disc_pairing = pairing;
  disc_bc_ch = 0;
  disc_n = disc_i = disc_ch = 0;
  auto add = [](uint8_t c) {
    if (c < 1 || c > 13) return;
    for (uint8_t i = 0; i < disc_n; ++i)
      if (disc_order[i] == c) return;
    disc_order[disc_n++] = c;
  };
  if (!pairing) add(gw_chan);  // last known channel first, then 1..13 (no channel assumed)
  for (uint8_t c = 1; c <= 13; ++c) add(c);
  pend.active = false;
  probe_out = false;
  disc = Disc::Listen;
  disc_t0 = disc_phase_t0 = now_ms();
  disc_t_next = disc_t0 + 200;  // let the disconnect settle before the first set_channel
  wifi_hold(true);
  radio_up();
  logf("discovery: WLAN off (max %lu s), listening for the gateway beacon on ch 1-13",
       static_cast<unsigned long>(kDiscMaxMs / 1000));
}

// ch != 0: gateway channel found. Selects an AP on it (paired only), then Wi-Fi comes back.
void disc_finish(uint8_t ch) {
  if (disc == Disc::Idle) return;
  disc = Disc::Idle;
  disc_ch = 0;
  pend.active = false;
  probe_out = false;
  bool linked = st == PlayState::Paired || st == PlayState::Confirming;
  if (ch != 0 && linked) {
    if (ch != gw_chan) {
      gw_chan = ch;
      nvs_save_u8(CO4EN_NVS_CHANNEL, gw_chan);
    }
    int8_t rssi = 0;
    int rc = wifi_pin_select(ch, nullptr, &rssi);
    if (rc == 1) {
      ap_warn_ch = 0;
      noap_n = 0;
      if (!disc_pairing) co4en_resweep_result(&rs, true);
      logf("WLAN: AP on gateway channel %u (rssi %d) selected", ch, rssi);
    } else {
      ap_warn_ch = ch;
      if (st == PlayState::Paired) {  // unreachable over this WLAN: retry with backoff, no Wi-Fi drop loop
        if (noap_n < 255) ++noap_n;
        if (!disc_pairing) co4en_resweep_result(&rs, false);
        st = PlayState::Lost;
        sw.fails = rs.fails > noap_n ? rs.fails : noap_n;
        sw.st = Co4enSweepSt::Backoff;
        sw.t_next = now_ms() + co4en_sweep_backoff_ms(sw.fails);
      }
      char m[64];
      snprintf(m, sizeof(m), "Kein Access Point auf Gateway-Kanal %u", ch);
      logf("%s%s", m, rc < 0 ? " (scan failed)" : "");
      ui_post(PlayMsgKind::Status, m);
    }
  }
  if (!cii_mode) wifi_hold(false);  // Wi-Fi always comes back
  hint_ch = 0;
  logf("discovery done: %s, WLAN was off %lu ms", ch != 0 ? "gateway channel found" : "gateway not found",
       static_cast<unsigned long>(now_ms() - disc_t0));
  if (ch == 0 && (st == PlayState::Lost || st == PlayState::Searching)) {
    st = PlayState::Lost;
    if (!disc_pairing) co4en_resweep_result(&rs, false);
    if (sw.st != Co4enSweepSt::Backoff) {  // listen-only failure: count it like a failed sweep
      if (sw.fails < 255) ++sw.fails;
    }
    if (sw.fails < rs.fails) sw.fails = rs.fails;
    sw.st = Co4enSweepSt::Backoff;
    sw.t_next = now_ms() + co4en_sweep_backoff_ms(sw.fails);
    logf("gateway not found: next search in %lu min", static_cast<unsigned long>(co4en_sweep_backoff_ms(sw.fails) / 60000));
  }
}

void disc_tick(uint32_t t) {
  if (disc == Disc::Idle) return;
  bool linked = st == PlayState::Paired || st == PlayState::Confirming;
  if (static_cast<int32_t>(t - disc_t0) >= static_cast<int32_t>(kDiscMaxMs)) {
    disc_finish(disc == Disc::Pair && linked ? gw_chan : 0);
    return;
  }
  if (disc_pairing && st != PlayState::Pairing && st != PlayState::Confirming) {  // stopped / reset
    disc_finish(0);
    return;
  }
  switch (disc) {
    case Disc::Listen: {
      bool done = disc_bc_ch != 0 || disc_i >= disc_n ||
                  static_cast<int32_t>(t - disc_t0) >= static_cast<int32_t>(kDiscListenMaxMs);
      if (!done) {
        if (static_cast<int32_t>(t - disc_t_next) < 0) return;
        disc_ch = disc_order[disc_i++];
        set_channel(disc_ch);
        disc_t_next = t + kDiscDwellMs;
        return;
      }
      disc_phase_t0 = t;
      disc_i = 0;
      if (disc_pairing) {
        disc = Disc::Pair;  // beacon channel only, else a PAIR_REQ hop over 1..13
        disc_t_next = t;
      } else {
        disc = Disc::Probe;  // encrypted STATUS ACK_REQ, beacon channel first; first ACK wins
        st = PlayState::Searching;
        co4en_sweep_start(&sw, disc_bc_ch != 0 ? disc_bc_ch : gw_chan, 0, t);
      }
      return;
    }
    case Disc::Probe:
      if (st == PlayState::Paired) disc_finish(gw_chan);  // probe_found() stored the channel
      else if (sw.st == Co4enSweepSt::Backoff) disc_finish(0);
      else disc_ch = sw.ch != 0 ? sw.ch : disc_ch;
      return;
    case Disc::Pair:
      if (st == PlayState::Confirming || st == PlayState::Paired) {  // PAIR_RESP ok: wait for the confirm ACK off-air
        if (!pend.active) disc_finish(gw_chan);
        return;
      }
      if (static_cast<int32_t>(t - disc_phase_t0) >= static_cast<int32_t>(kDiscPairMs)) {
        logf("discovery: no PAIR_RESP, pairing continues on the WLAN channel");
        disc_finish(0);
        return;
      }
      if (static_cast<int32_t>(t - disc_t_next) < 0) return;
      disc_ch = disc_bc_ch != 0 ? disc_bc_ch : static_cast<uint8_t>(disc_i % 13 + 1);
      ++disc_i;
      set_channel(disc_ch);
      send_pair_req();
      disc_t_next = t + (disc_bc_ch != 0 ? 250 : kCo4enProbeWaitMs);
      return;
    default: return;
  }
}

void on_rx(const Cmd& c) {
  Co4enHdr h;
  if (!co4en_hdr(c.data, c.n, &h) || h.ver != CO4EN_VER) return;
  console_printf("CO4EN RX type=%u len=%u from=%02X%02X%02X%02X%02X%02X\n", h.type, c.n, c.src[0], c.src[1], c.src[2],
                 c.src[3], c.src[4], c.src[5]);
  if (h.type == CO4EN_T_PAIR_RESP) on_pair_resp(c.src, c.data, c.n);
  else if (h.type == CO4EN_T_ACK) on_ack(c.src, c.data, c.n);
  else if (h.type == CO4EN_T_BEACON) on_beacon(c.src, c.data, c.n);
}

void do_reset() {
  disc_finish(0);
  wifi_unpin();
  ap_warn_ch = 0;
  noap_n = 0;
  rs = {};
  keepalive_n = 0;
  keepalive_out = false;
  boot_aligned = false;
  pend.active = false;
  probe_out = false;
  sw.st = Co4enSweepSt::Idle;
  link.miss = 0;
  drop_keys();
  st = PlayState::Off;
  nonce_valid = false;
  co4en_nonce_clear(&nwin);
  radio_down();
  new_secret();
  logf("reset: keys dropped, new device secret");
  ui_post(PlayMsgKind::Qr, qr);
}

// --- values ----------------------------------------------------------------------
void sanitize(const char* in, char* out, size_t n) {
  size_t j = 0;
  for (size_t i = 0; in[i] != '\0' && j + 1 < n; ++i) {
    unsigned char ch = static_cast<unsigned char>(in[i]);
    if (ch == '%') {
      const char* p = "pct";
      while (*p != '\0' && j + 1 < n) out[j++] = *p++;
      continue;
    }
    ch = static_cast<unsigned char>(tolower(ch));
    if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '.' || ch == '-') out[j++] = ch;
    else if (ch == ' ' && j > 0 && out[j - 1] != '_') out[j++] = '_';
  }
  out[j] = '\0';
}

void do_send(const Cmd& c) {
  char nm[24], un[12], key[CO4EN_KEY_MAX + 8];
  sanitize(c.name, nm, sizeof(nm));
  sanitize(c.unit, un, sizeof(un));
  if (un[0] != '\0') snprintf(key, sizeof(key), "test.%s_%s", nm, un);
  else snprintf(key, sizeof(key), "test.%s", nm);
  if (nm[0] == '\0' || strlen(key) > CO4EN_KEY_MAX || !co4en_key_ok(key)) {
    logf("invalid key '%s'", key);
    return;
  }
  char v[24];
  snprintf(v, sizeof(v), "%s", c.value);
  for (char* p = v; *p != '\0'; ++p)
    if (*p == ',') *p = '.';
  char* end = nullptr;
  bool real = strpbrk(v, ".eE") != nullptr;
  Co4enValue val;
  if (real) {
    double d = strtod(v, &end);
    val = co4en_f32(static_cast<float>(d));
  } else {
    long long q = strtoll(v, &end, 10);
    val = (q >= INT32_MIN && q <= INT32_MAX) ? co4en_i32(static_cast<int32_t>(q)) : co4en_i64(q);
  }
  if (v[0] == '\0' || end == nullptr || *end != '\0') {
    logf("invalid number '%s'", c.value);
    return;
  }
  Co4enFrame f;
  uint8_t flags;
  begin_data(&f, &flags);
  if (!put_kv(&f, key, val)) {
    logf("encode failed");
    return;
  }
  if (now_ms() - last_status_ms >= kHeartbeatMs && co4en_room(f) >= 12) add_status(&f);
  logf("send %s=%s%s", key, v, (st == PlayState::Paired || st == PlayState::Confirming) ? "" : " (dry: not paired)");
  send_data(&f, flags, true);  // manual value: always ACK_REQ (user feedback, link check)
}

// Greedy KV-Text packing: a value that does not fit closes the frame.
struct Packer {
  Co4enFrame f;
  uint8_t flags;
  uint32_t ts;
  bool open = false;
  int frames = 0;
  void start() {
    flags = 0;
    if (ts != 0) flags |= CO4EN_F_TIME_VALID;
    co4en_begin(&f, CO4EN_T_DATA, flags, 0, ts);
    open = true;
  }
  void flush() {
    if (open && f.n > CO4EN_HDR_LEN) {
      send_data(&f, flags, false);
      ++frames;
    }
    open = false;
  }
  void put(const char* key, const Co4enValue& v) {
    if (!open) start();
    if (!put_kv(&f, key, v)) {
      flush();
      start();
      put_kv(&f, key, v);
    }
  }
};

void forward(const EnergyReading& r) {
  Packer p;
  p.ts = r.ts > 0 ? static_cast<uint32_t>(r.ts) : 0;
  p.put("p.imp", co4en_f32(r.p_in_kw * 1000.0f));
  p.put("p.exp", co4en_f32(r.p_out_kw * 1000.0f));
  p.put("e.imp", co4en_i64(static_cast<int64_t>(r.e_in * 1000.0 + 0.5)));
  p.put("e.exp", co4en_i64(static_cast<int64_t>(r.e_out * 1000.0 + 0.5)));
  if (r.e_in_t1 >= 0) p.put("e.imp.t1", co4en_i64(static_cast<int64_t>(r.e_in_t1 * 1000.0 + 0.5)));
  if (r.e_in_t2 >= 0) p.put("e.imp.t2", co4en_i64(static_cast<int64_t>(r.e_in_t2 * 1000.0 + 0.5)));
  if (r.e_out_t1 >= 0) p.put("e.exp.t1", co4en_i64(static_cast<int64_t>(r.e_out_t1 * 1000.0 + 0.5)));
  if (r.e_out_t2 >= 0) p.put("e.exp.t2", co4en_i64(static_cast<int64_t>(r.e_out_t2 * 1000.0 + 0.5)));
  if (r.er_in >= 0) p.put("er.imp", co4en_i64(static_cast<int64_t>(r.er_in * 1000.0 + 0.5)));
  if (r.er_out >= 0) p.put("er.exp", co4en_i64(static_cast<int64_t>(r.er_out * 1000.0 + 0.5)));
  static const char* const erq[4] = {"er.q1", "er.q2", "er.q3", "er.q4"};  // varh, OBIS 5.8.0..8.8.0
  for (int q = 0; q < 4; ++q)
    if (r.er_q[q] >= 0) p.put(erq[q], co4en_i64(static_cast<int64_t>(r.er_q[q] * 1000.0 + 0.5)));
  const char* il[3] = {"i.l1", "i.l2", "i.l3"};
  for (int i = 0; i < 3; ++i)
    if (r.i_a[i] >= 0) p.put(il[i], co4en_f32(r.i_a[i]));
  if (r.tariff > 0) p.put("tariff", co4en_i32(r.tariff));
  static uint32_t id_sent = 0;  // meter.id only now and then (every 10 min)
  if (r.meter_id[0] != '\0' && (id_sent == 0 || now_ms() - id_sent > 600000)) {
    p.put("meter.id", co4en_str(r.meter_id));
    id_sent = now_ms() | 1;
  }
  if (p.open && now_ms() - last_status_ms >= kHeartbeatMs && co4en_room(p.f) >= 12) add_status(&p.f);
  p.flush();
  if (r.has_water) {  // own frame: the M-Bus reading has its own timestamp
    Packer w;
    w.ts = r.water_ts > 0 ? static_cast<uint32_t>(r.water_ts) : 0;
    w.put("w.vol", co4en_f64(r.water_m3));
    w.flush();
  }
}

void fwd_tick() {
  if (offair()) return;  // no WLAN: no whatwatt data
  if (!fwd || now_ms() - fwd_poll_ms < 1000) return;
  fwd_poll_ms = now_ms();
  if (ev == nullptr) ev = static_cast<EnergyView*>(heap_caps_calloc(1, sizeof(EnergyView), MALLOC_CAP_SPIRAM));
  if (ev == nullptr) return;
  energy_get(ev);
  if (!ev->have || ev->stale || ev->fetched == fwd_last_fetched) return;
  if (fwd_last_ms != 0 && now_ms() - fwd_last_ms < kFwdMinMs) return;
  fwd_last_fetched = ev->fetched;
  fwd_last_ms = now_ms();
  forward(ev->r);
}

void tick() {
  uint32_t t = now_ms();
  if (cii_mode && static_cast<int32_t>(t - cii_until) >= 0) cii_set(false);
  disc_tick(t);
  if (st == PlayState::Pairing && disc == Disc::Idle && static_cast<int32_t>(t - next_pair) >= 0) {
    // CII: one PAIR_REQ per channel 1..13, 50 ms apart, then the normal period.
    if (cii_mode) set_channel(static_cast<uint8_t>(pair_hop + 1));
    send_pair_req();
    uint32_t period = t - pair_t0 < CO4EN_PAIR_FAST_DURATION_MS ? CO4EN_PAIR_FAST_PERIOD_MS : 30000;
    if (cii_mode && ++pair_hop < 13) period = kCo4enProbeWaitMs;
    else pair_hop = 0;
    next_pair = t + period;
  }
  if ((st == PlayState::Lost || st == PlayState::Searching) && sw.st == Co4enSweepSt::Idle && disc == Disc::Idle &&
      (cii_mode || wifi_connected()) && radio_up()) {  // after leaving CII mode
    if (disc_possible()) disc_begin(false);
    else co4en_sweep_start(&sw, gw_chan, cii_mode ? 0 : cur_channel(), t);
  }
  // Paired: keep the WLAN on an AP of the gateway channel (once per boot, no Wi-Fi drop needed).
  if (st == PlayState::Paired && !boot_aligned && disc == Disc::Idle && !cii_mode && gw_chan != 0 &&
      wifi_connected()) {
    boot_aligned = true;
    if (cur_channel() != gw_chan) pin_to_gw(gw_chan, "boot");
  }
  // Pinned AP gone (net fell back to any AP): find the gateway channel again.
  if (wifi_pin_lost_take() && st == PlayState::Paired && disc == Disc::Idle) start_search("pinned AP gone");
  if (pend.active && probe_out && static_cast<int32_t>(t - pend.deadline) >= 0) {
    pend.active = false;  // one probe per channel, no retry
    probe_out = false;
    ++failed_n;
  }
  if (pend.active && static_cast<int32_t>(t - pend.deadline) >= 0) {
    if (pend.retries > 0 && st != PlayState::Off) {
      // App retry with a new seq (spec: no duplicate seq).
      pend.retries = 0;
      uint32_t s = next_seq();
      memcpy(&pend.f.b[4], &s, 4);
      remember_ack_seq(s);
      pend.seq = s;
      pend.deadline = t + CO4EN_ACK_WAIT_MS;
      tx(pend.f, gw_mac, "gw-retry");
    } else {
      pend.active = false;
      ++failed_n;
      keepalive_out = false;
      if (st == PlayState::Paired && co4en_link_miss(&link)) {
        char why[48];
        snprintf(why, sizeof(why), "Gateway verloren (%d ACKs missed)", kCo4enLinkMissMax);
        start_search(why);
      }
    }
  }
  search_tick(t);
  if (co4en_keepalive_due(st == PlayState::Lost, sw.st == Co4enSweepSt::Backoff, disc == Disc::Idle,
                          !offair() && wifi_connected(), pend.active, t, last_status_ms, kHeartbeatMs))
    send_keepalive();
  if (st == PlayState::Confirming && !pend.active && t - last_data_ms >= kConfirmPeriodMs &&
      (offair() || wifi_connected())) {  // not while Wi-Fi reconnects after a discovery
    if (confirm_n >= kConfirmFrames) {
      logf("no ACK after %d frames: back to pairing", confirm_n);
      drop_keys();
      if (radio) esp_now_del_peer(gw_mac);
      st = PlayState::Pairing;
      next_pair = t;
    } else {
      ++confirm_n;
      send_status_frame(true);
    }
  }
  if (st == PlayState::Paired && t - last_status_ms >= kHeartbeatMs && t - last_data_ms >= 1000) {
    if (disc != Disc::Idle) last_status_ms = t;  // off-air: the discovery probes instead
    else if (radio_up()) send_status_frame(true);  // heartbeat with ACK_REQ: feeds the link-loss detection
    else last_status_ms = t;  // try again in a minute
  }
  if (st == PlayState::Paired && !radio) radio_up();
  fwd_tick();
  if (t < ui_open_until && t - last_status_post >= 1000) {
    last_status_post = t;
    ui_post(PlayMsgKind::Status, state_name(st));
  }
}

void task(void*) {
  load();
  static Cmd c;
  for (;;) {
    // ACK wait needs ms resolution; otherwise 100 ms is plenty.
    TickType_t wait = pend.active ? pdMS_TO_TICKS(2) : pdMS_TO_TICKS(100);
    if (xQueueReceive(g_cmd, &c, wait) == pdTRUE) {
      switch (c.kind) {
        case CmdKind::Open:
          ui_open_until = now_ms() + 30000;
          ui_post(PlayMsgKind::Qr, qr);
          ui_post(PlayMsgKind::Status, state_name(st));
          break;
        case CmdKind::Pair:
          if (c.on) start_pairing();
          else stop_pairing();
          break;
        case CmdKind::Reset: do_reset(); break;
        case CmdKind::Fwd:
          fwd = c.on;
          nvs_save_u8("fwd", fwd ? 1 : 0);
          fwd_last_fetched = 0;
          logf("whatwatt forward %s", fwd ? "on" : "off");
          break;
        case CmdKind::Send: do_send(c); break;
        case CmdKind::KvText:
          kv_text_mode = c.on;
          nvs_save_u8("kvtext", kv_text_mode ? 1 : 0);
          logf("encoding %s", kv_text_mode ? "KV-Text (test/dev)" : "KV-ID + KEYMAP");
          break;
        case CmdKind::Rx: on_rx(c); break;
        case CmdKind::Cii: cii_set(c.on); break;
        case CmdKind::PairFast:
          if (st == PlayState::Off) start_pairing();
          if (st == PlayState::Pairing) {
            pair_t0 = next_pair = now_ms();
            pair_hop = 0;
            if (disc_possible() && disc == Disc::Idle) disc_begin(true);
            logf("pairing: fast 3 s phase restarted");
          }
          break;
        case CmdKind::Vstore:
          vstore_mv = c.n;
          logf("VSTORE (simulated) %u mV", vstore_mv);
          break;
        case CmdKind::OffChan: offchan_test(static_cast<uint8_t>(c.n)); break;
      }
      ui_post(PlayMsgKind::Status, state_name(st));
    }
    tick();
  }
}

void post(const Cmd& c) {
  if (g_cmd != nullptr) xQueueSend(g_cmd, &c, 0);
}

}  // namespace

void play_start() {
  if (g_task != nullptr) return;
  g_cmd = xQueueCreateWithCaps(6, sizeof(Cmd), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  g_ui = xQueueCreateWithCaps(8, sizeof(PlayUiMsg), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (g_cmd == nullptr || g_ui == nullptr) return;
  // Core 0 with the network, lowest app priority (below energy 3, net 4, Loxone 5).
  xTaskCreatePinnedToCoreWithCaps(task, "co4en", 8 * 1024, nullptr, 2, &g_task, 0, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

bool play_ui_take(PlayUiMsg* out) { return g_ui != nullptr && xQueueReceive(g_ui, out, 0) == pdTRUE; }

static Cmd* cmd_buf() {
  static Cmd c;  // callers: UI task / console task, one at a time in practice
  memset(&c, 0, sizeof(c));
  return &c;
}

void play_cmd_open() {
  Cmd* c = cmd_buf();
  c->kind = CmdKind::Open;
  post(*c);
}
void play_cmd_pair(bool start) {
  Cmd* c = cmd_buf();
  c->kind = CmdKind::Pair;
  c->on = start;
  post(*c);
}
void play_cmd_reset() {
  Cmd* c = cmd_buf();
  c->kind = CmdKind::Reset;
  post(*c);
}
static void post_n(CmdKind k, bool on, uint16_t n);
void play_cmd_cii(bool on) { post_n(CmdKind::Cii, on, 0); }
void play_cmd_pair_fast() { post_n(CmdKind::PairFast, true, 0); }
void play_cmd_vstore(uint16_t mv) { post_n(CmdKind::Vstore, true, mv); }
void play_cmd_offchan(uint8_t ch) { post_n(CmdKind::OffChan, true, ch); }
static void post_n(CmdKind k, bool on, uint16_t n) {
  Cmd* c = cmd_buf();
  c->kind = k;
  c->on = on;
  c->n = n;
  post(*c);
}
void play_cmd_kvtext(bool on) {
  Cmd* c = cmd_buf();
  c->kind = CmdKind::KvText;
  c->on = on;
  post(*c);
}

void play_cmd_fwd(bool on) {
  Cmd* c = cmd_buf();
  c->kind = CmdKind::Fwd;
  c->on = on;
  post(*c);
}
void play_cmd_send(const char* name, const char* value, const char* unit) {
  Cmd* c = cmd_buf();
  c->kind = CmdKind::Send;
  snprintf(c->name, sizeof(c->name), "%s", name != nullptr ? name : "");
  snprintf(c->value, sizeof(c->value), "%s", value != nullptr ? value : "");
  snprintf(c->unit, sizeof(c->unit), "%s", unit != nullptr ? unit : "");
  post(*c);
}
void play_cmd_inject(const uint8_t src[6], const uint8_t* f, size_t n) {
  if (n > CO4EN_MAX_FRAME) return;
  Cmd* c = cmd_buf();
  c->kind = CmdKind::Rx;
  memcpy(c->src, src, 6);
  c->n = static_cast<uint16_t>(n);
  memcpy(c->data, f, n);
  post(*c);
}

uint8_t play_gw_chan() { return (st == PlayState::Off || st == PlayState::Pairing) ? 0 : gw_chan; }

static const char* const kSweepNames[4] = {"idle", "probing", "backoff", "found"};
void play_diag() {
  PlayCounters c = counters();  // read-only snapshot; racy but harmless for diagnostics
  console_printf("CO4EN state=%s fwd=%d radio=%d ch=%u gw=%02X%02X%02X%02X%02X%02X pair_req=%lu sent=%lu mac_ok=%lu "
                 "acked=%lu failed=%lu dry=%lu seq=%lu enc=%s cii=%d gw_ch=%u link_miss=%u sweep=%s sweep_ch=%u "
                 "sweep_fails=%u next=%lus vstore=%u disc=%u disc_ch=%u pinned=%d pin_ch=%u ap_warn_ch=%u "
                 "beacon=%s beacon_ch=%u beacon_flags=%02X beacon_age=%lus resweeps=%lu last_resweep_s=%lu backoff_s=%lu keepalives=%lu lost_backoff=%d\n",
                 state_name(c.state), c.fwd ? 1 : 0, c.radio ? 1 : 0, c.channel, c.gw_mac[0], c.gw_mac[1], c.gw_mac[2],
                 c.gw_mac[3], c.gw_mac[4], c.gw_mac[5], static_cast<unsigned long>(c.pair_req),
                 static_cast<unsigned long>(c.sent), static_cast<unsigned long>(c.mac_ok),
                 static_cast<unsigned long>(c.acked), static_cast<unsigned long>(c.failed),
                 static_cast<unsigned long>(c.dry), static_cast<unsigned long>(c.last_seq), c.kv_text ? "kvtext" : "kvid", c.cii ? 1 : 0,
                 c.gw_chan, c.link_miss, kSweepNames[c.sweep_st & 3], c.sweep_ch, c.sweep_fails,
                 static_cast<unsigned long>(c.sweep_next_s), c.vstore_mv, c.disc, c.disc_ch, c.pinned ? 1 : 0, c.pin_ch,
                 c.ap_warn_ch, c.bc_have ? "yes" : "no", c.bc_ch, c.bc_flags, static_cast<unsigned long>(c.bc_age_s),
                 static_cast<unsigned long>(c.resweeps), static_cast<unsigned long>(c.last_resweep_s),
                 static_cast<unsigned long>(c.backoff_s), static_cast<unsigned long>(c.keepalives),
                 c.lost_backoff ? 1 : 0);
}

void play_print_qr_test_only() { console_printf("CO4ENQR %s\n", qr); }
