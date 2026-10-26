#pragma once
// CO4EN v0.4 (gwuno-fw ESP-NOW protocol, DRAFT): pure encoder / decoder for
// the node side (no ESP-IDF, host tested in tools/test_co4en.cpp against the
// co4en_ref.py test vectors). Constants and wire structs come unchanged from
// gwuno's co4en.h (main/co4en_proto.h).
//
// Secrets and derived keys are never formatted into text by this module.

#include <stddef.h>
#include <stdint.h>

#include "co4en_proto.h"

// Gateway-follow limits (menuconfig "Loxone panel"); host builds use the defaults.
#if defined(__has_include)
#if __has_include("sdkconfig.h")
#include "sdkconfig.h"
#endif
#endif
#ifndef CONFIG_CO4EN_LINK_MISS_MAX
#define CONFIG_CO4EN_LINK_MISS_MAX 3
#endif
#ifndef CONFIG_CO4EN_RESWEEP_MIN_S
#define CONFIG_CO4EN_RESWEEP_MIN_S 120
#endif
#ifndef CONFIG_CO4EN_RESWEEP_MAX_S
#define CONFIG_CO4EN_RESWEEP_MAX_S 1800
#endif

// --- crypto (portable SHA-256, HMAC, HKDF) --------------------------------------
void co4en_sha256(const uint8_t* data, size_t n, uint8_t out[32]);
// HMAC-SHA256 over the concatenation of up to 3 parts (nullptr / 0 = unused).
void co4en_hmac(const uint8_t* key, size_t key_n, const uint8_t* a, size_t an, const uint8_t* b, size_t bn,
                const uint8_t* c, size_t cn, uint8_t out[32]);
void co4en_hkdf(const uint8_t* ikm, size_t ikm_n, const char* info, uint8_t* out, size_t n);  // salt "CO4EN1"
void co4en_k_auth(const uint8_t secret[CO4EN_SECRET_LEN], uint8_t out[32]);
void co4en_k_enc(const uint8_t secret[CO4EN_SECRET_LEN], uint8_t out[32]);
void co4en_wipe(void* p, size_t n);

// --- QR / pairing string ----------------------------------------------------------
// "CO4EN1:<MAC hex upper>:<base32 secret, no padding>" (49 chars + NUL).
constexpr size_t kCo4enQrLen = 6 + 1 + 12 + 1 + 26 + 1;
void co4en_qr_make(const uint8_t mac[6], const uint8_t secret[CO4EN_SECRET_LEN], char out[kCo4enQrLen]);

// --- frame writer ---------------------------------------------------------------
struct Co4enFrame {
  uint8_t b[CO4EN_MAX_FRAME];
  size_t n;
  bool overflow;  // a TLV did not fit: the frame must not be sent
};
void co4en_begin(Co4enFrame* f, uint8_t type, uint8_t flags, uint32_t seq, uint32_t ts);
size_t co4en_room(const Co4enFrame& f);  // bytes left
bool co4en_tlv(Co4enFrame* f, uint8_t tag, const uint8_t* v, size_t n);

// Values (vtype bits 0-3); q = quality; ts_ofs only if has_ofs.
struct Co4enValue {
  uint8_t type;  // CO4EN_VT_*
  uint8_t q;
  bool has_ofs;
  int16_t ts_ofs;
  bool b;
  int32_t i32;
  int64_t i64;
  float f32;
  double f64;
  const uint8_t* data;  // string / bytes
  size_t data_n;
};
Co4enValue co4en_f32(float v);
Co4enValue co4en_f64(double v);
Co4enValue co4en_i32(int32_t v);
Co4enValue co4en_i64(int64_t v);
Co4enValue co4en_str(const char* s);

bool co4en_key_ok(const char* key);  // [a-z0-9_.-]{1,32}
size_t co4en_kv_text_size(const char* key, const Co4enValue& v);  // TLV incl. tag+len, 0 if invalid
bool co4en_kv_text(Co4enFrame* f, const char* key, const Co4enValue& v);
bool co4en_kv_id(Co4enFrame* f, uint16_t id, const Co4enValue& v);
bool co4en_keymap(Co4enFrame* f, uint16_t id, const char* key);
bool co4en_status(Co4enFrame* f, uint16_t vstore_mv, uint32_t uptime_s, uint16_t err_bits);

// PAIR_REQ (52 B) with the MIC.
void co4en_pair_req(Co4enFrame* f, const uint8_t secret[CO4EN_SECRET_LEN], const uint8_t mac[6], uint8_t dev_type,
                    const uint8_t fw[3], uint16_t caps, const uint8_t nonce_n[CO4EN_NONCE_LEN], uint32_t seq,
                    uint32_t ts, uint8_t flags);

// --- PAIR_RESP key wrap (exchangeable: v0.4 XOR keystream, AES-CCM planned) -----
struct Co4enKeyWrap {
  const char* name;
  // in: 32 wrapped bytes; out: PMK and LMK. False if the wrap cannot be opened.
  bool (*open)(const uint8_t k_enc[32], const uint8_t nonce_n[CO4EN_NONCE_LEN], const uint8_t nonce_g[CO4EN_NONCE_LEN],
               const uint8_t* in, size_t in_n, uint8_t pmk[16], uint8_t lmk[16]);
};
extern const Co4enKeyWrap kCo4enWrapXorV04;

struct Co4enPairResult {
  uint8_t gw_mac[6];
  uint8_t channel;
  uint32_t unix;
  uint32_t seq;
  uint8_t pmk[16];
  uint8_t lmk[16];
};
enum Co4enPairErr { kPairOk = 0, kPairFormat, kPairMic, kPairNonce, kPairWrap };
// Node side: check format, MIC and the nonce_n echo, then open the keys.
Co4enPairErr co4en_pair_resp_open(const uint8_t* f, size_t n, const uint8_t secret[CO4EN_SECRET_LEN],
                                  const uint8_t nonce_n[CO4EN_NONCE_LEN], const Co4enKeyWrap& wrap,
                                  Co4enPairResult* out);

// --- decoding of what a node receives ------------------------------------------------
struct Co4enHdr {
  uint8_t ver, type, flags, rsv;
  uint32_t seq, ts;
};
bool co4en_hdr(const uint8_t* f, size_t n, Co4enHdr* h);
struct Co4enAck {
  uint32_t seq;        // the ACK's own seq
  uint32_t acked_seq;
  uint8_t channel;
  uint32_t unix;
  uint8_t result;
};
bool co4en_parse_ack(const uint8_t* f, size_t n, Co4enAck* out);

// --- v0.7 BEACON (type 0x04, unencrypted, no MIC: a channel hint, never a trust anchor) ---
constexpr uint8_t kCo4enBeaconPairingOpen = 0x01;  // info flags b0; b1-2 mode
struct Co4enBeacon {
  uint32_t seq;
  uint8_t gw_mac[6];
  uint8_t channel;  // 1..13
  uint8_t flags;    // b0 pairing_open, b1-2 mode (0 fixed, 1 follow_ssid, 2 wifi)
  uint32_t unix;
};
// Walks the TLVs (unknown tags skipped by length); false unless a v1 BEACON_INFO with
// len >= 13 and channel 1..13 is found. Unknown beacon_ver -> false.
bool co4en_parse_beacon(const uint8_t* f, size_t n, Co4enBeacon* out);
// v0.7 WLAN_INFO TLV 0x21 (8 B): primary channel, BSSID, RSSI.
bool co4en_wlan_info(Co4enFrame* f, uint8_t channel, const uint8_t bssid[6], int8_t rssi);

void co4en_hex(const uint8_t* b, size_t n, char* out, size_t out_n);  // "0105..." (upper, no spaces)

// --- seq (monotonic across reboots) -------------------------------------------------
// NVS holds the start of the current block; every seq in use is < nvs + 256.
// The next block is reserved (NVS written) proactively at +224, so no frame ever
// has to wait for a flash write and a cold boot always starts above anything sent.
constexpr uint32_t kCo4enSeqAhead = 224;
struct Co4enSeq {
  uint32_t next;
  uint32_t nvs;  // value that is (or must be) in NVS
};
// Boot: returns true if s->nvs must be written to NVS before the first frame.
bool co4en_seq_boot(Co4enSeq* s, bool nvs_have, uint32_t nvs_base, bool rtc_ok, uint32_t rtc_next);
// Next seq; *persist = true if s->nvs changed and must be written now (before use).
uint32_t co4en_seq_take(Co4enSeq* s, bool* persist);

// --- PAIR_RESP nonce window: latest nonce_n and the one before, max. 10 s old ------
constexpr uint32_t kCo4enNonceMaxAgeMs = 10000;
struct Co4enNonceWin {
  uint8_t n[2][CO4EN_NONCE_LEN];  // [0] latest
  uint32_t t_ms[2];
  bool valid[2];
};
void co4en_nonce_push(Co4enNonceWin* w, const uint8_t nonce[CO4EN_NONCE_LEN], uint32_t now_ms);
void co4en_nonce_clear(Co4enNonceWin* w);
Co4enPairErr co4en_pair_resp_open_win(const uint8_t* f, size_t n, const uint8_t secret[CO4EN_SECRET_LEN],
                                      const Co4enNonceWin& w, uint32_t now_ms, const Co4enKeyWrap& wrap,
                                      Co4enPairResult* out);

// --- default KEYMAP (spec v0.4 §4 proposal); test.* keys get ids from 100 ---------
uint16_t co4en_default_key_id(const char* key);  // 0 = not in the table

// --- link loss + channel sweep (spec v0.5) -------------------------------------------
// Probe = normal encrypted unicast DATA (STATUS, ACK_REQ) to the known gateway; a
// channel counts as found on the ACK-INFO reply (MAC ACK alone is only a hint).
constexpr int kCo4enLinkMissMax = CONFIG_CO4EN_LINK_MISS_MAX;  // consecutive missed ACKs -> link lost
constexpr uint32_t kCo4enProbeWaitMs = 50;  // per channel
constexpr uint32_t kCo4enVstoreMinMv = 15500;
struct Co4enLink {
  uint8_t miss;
};
// ACK received (any result): link alive.
inline void co4en_link_ack(Co4enLink* l) { l->miss = 0; }
// ACK_REQ frame got no ACK (after the app retry): true once the link counts as lost.
inline bool co4en_link_miss(Co4enLink* l) {
  if (l->miss < 255) ++l->miss;
  return l->miss >= kCo4enLinkMissMax;
}

enum class Co4enSweepSt : uint8_t { Idle = 0, Probing, Backoff, Found };
struct Co4enSweep {
  Co4enSweepSt st;
  uint8_t order[13], n, idx;
  uint8_t ch;       // channel of the outstanding probe (0 = none)
  uint8_t found;    // last found channel
  uint8_t fails;    // failed full sweeps in a row (backoff index)
  uint32_t t_next;  // next action due (ms)
};
// Order: last known channel, then 1, 6, 11, then the rest up to 13 (no duplicates).
// single_ch != 0: only that channel (Wi-Fi node: stays on the AP channel).
void co4en_sweep_start(Co4enSweep* s, uint8_t last_ch, uint8_t single_ch, uint32_t now_ms);
// Backoff after the n-th failed search (1-based): RESWEEP_MIN_S doubled per failure,
// capped at RESWEEP_MAX_S (defaults 2, 4, 8, 16, 30, 30 ... min). fails 0 = minimum.
uint32_t co4en_sweep_backoff_ms(uint8_t fails);
// Call periodically. Returns the channel to probe now (switch there, send one probe
// with a new seq), 0 = nothing to do. power_ok: VSTORE >= 15.5 V (CII); otherwise the
// sweep waits.
uint8_t co4en_sweep_step(Co4enSweep* s, uint32_t now_ms, bool power_ok);
// ACK-INFO received for the probe on s->ch: done. Returns true if `found` differs
// from nvs_ch (caller persists it in NVS).
bool co4en_sweep_found(Co4enSweep* s, uint8_t nvs_ch);

// --- runtime re-sweep gate (gateway follow) ----------------------------------------
// A re-search drops Wi-Fi briefly; it may start at most every backoff(fails) after the
// previous one (min 2 min even after a success), so Wi-Fi / Loxone are not interrupted
// continuously. fails counts failed searches in a row; an ACK resets it.
struct Co4enResweep {
  uint32_t n;        // re-searches started since boot
  uint32_t last_ms;  // start of the last one (valid if n != 0)
  uint8_t fails;
};
// ms until the next re-search may start (0 = now).
uint32_t co4en_resweep_wait_ms(const Co4enResweep& r, uint32_t now_ms);
inline void co4en_resweep_started(Co4enResweep* r, uint32_t now_ms) {
  ++r->n;
  r->last_ms = now_ms;
}
inline void co4en_resweep_result(Co4enResweep* r, bool found) {
  if (found) r->fails = 0;
  else if (r->fails < 255) ++r->fails;
}
inline void co4en_resweep_ack(Co4enResweep* r) { r->fails = 0; }
// Channel reported by the gateway (ACK-INFO / BEACON): new gateway channel to adopt, or
// 0 = nothing to do (invalid or unchanged).
inline uint8_t co4en_gw_channel_update(uint8_t gw_ch, uint8_t reported) {
  return reported >= 1 && reported <= 13 && reported != gw_ch ? reported : 0;
}
