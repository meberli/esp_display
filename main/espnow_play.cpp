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

enum class CmdKind : uint8_t { Open, Pair, Reset, Fwd, Send, Rx, KvText };
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
  if (st == PlayState::Confirming || st == PlayState::Paired) memcpy(c.gw_mac, gw_mac, 6);
  c.pair_req = pair_req_n;
  c.sent = sent_n;
  c.mac_ok = a_mac_ok.load();
  c.acked = acked_n;
  c.failed = failed_n + a_mac_fail.load();
  c.dry = dry_n;
  c.last_seq = last_seq_v;
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

bool radio_up() {
  if (radio) return true;
  if (!wifi_connected()) return false;
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
  if (st == PlayState::Paired || st == PlayState::Confirming) {
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
  if (!linked) {  // unpaired: only PAIR_REQ goes on air; data stays local (logged, seq 0, not sent)
    f->b[2] = flags;
    memset(&f->b[4], 0, 4);
    tx(*f, nullptr, "dry");
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
}

void send_status_frame(bool force_ack) {
  Co4enFrame f;
  uint8_t flags;
  begin_data(&f, &flags);
  add_status(&f);
  send_data(&f, flags, force_ack);
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
  tx(f, kBcast, "bcast");
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
  uint8_t ch = 0;
  wifi_second_chan_t sc;
  esp_wifi_get_channel(&ch, &sc);
  if (gw_chan != 0 && gw_chan != ch)
    logf("gateway channel %u != Wi-Fi channel %u (panel stays on Wi-Fi channel)", gw_chan, ch);
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
  if (st != PlayState::Paired && st != PlayState::Confirming) return;
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
  if (pend.active && (a.acked_seq == pend.seq)) pend.active = false;
  if (a.result != CO4EN_ACK_OK) {
    ++failed_n;
    if (a.result == 1) km_reset();  // gateway could not decode: announce the KEYMAP again
    logf("ACK seq=%lu result=%u", static_cast<unsigned long>(a.acked_seq), a.result);
    return;
  }
  ++acked_n;
  logf("ACK seq=%lu ok ch=%u", static_cast<unsigned long>(a.acked_seq), a.channel);
  if (st == PlayState::Confirming) {
    st = PlayState::Paired;
    nvs_save_u8(CO4EN_NVS_PAIRED, 1);
    logf("pairing confirmed");
  }
}

void on_rx(const Cmd& c) {
  Co4enHdr h;
  if (!co4en_hdr(c.data, c.n, &h) || h.ver != CO4EN_VER) return;
  console_printf("CO4EN RX type=%u len=%u from=%02X%02X%02X%02X%02X%02X\n", h.type, c.n, c.src[0], c.src[1], c.src[2],
                 c.src[3], c.src[4], c.src[5]);
  if (h.type == CO4EN_T_PAIR_RESP) on_pair_resp(c.src, c.data, c.n);
  else if (h.type == CO4EN_T_ACK) on_ack(c.src, c.data, c.n);
}

void do_reset() {
  pend.active = false;
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
  send_data(&f, flags, false);
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
  if (st == PlayState::Pairing && static_cast<int32_t>(t - next_pair) >= 0) {
    send_pair_req();
    uint32_t period = t - pair_t0 < CO4EN_PAIR_FAST_DURATION_MS ? CO4EN_PAIR_FAST_PERIOD_MS : 30000;
    next_pair = t + period;
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
    }
  }
  if (st == PlayState::Confirming && !pend.active && t - last_data_ms >= kConfirmPeriodMs) {
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
    if (radio_up()) send_status_frame(false);
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

void play_diag() {
  PlayCounters c = counters();  // read-only snapshot; racy but harmless for diagnostics
  console_printf("CO4EN state=%s fwd=%d radio=%d ch=%u gw=%02X%02X%02X%02X%02X%02X pair_req=%lu sent=%lu mac_ok=%lu "
                 "acked=%lu failed=%lu dry=%lu seq=%lu enc=%s\n",
                 state_name(c.state), c.fwd ? 1 : 0, c.radio ? 1 : 0, c.channel, c.gw_mac[0], c.gw_mac[1], c.gw_mac[2],
                 c.gw_mac[3], c.gw_mac[4], c.gw_mac[5], static_cast<unsigned long>(c.pair_req),
                 static_cast<unsigned long>(c.sent), static_cast<unsigned long>(c.mac_ok),
                 static_cast<unsigned long>(c.acked), static_cast<unsigned long>(c.failed),
                 static_cast<unsigned long>(c.dry), static_cast<unsigned long>(c.last_seq), c.kv_text ? "kvtext" : "kvid");
}

void play_print_qr_test_only() { console_printf("CO4ENQR %s\n", qr); }
