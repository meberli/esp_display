#include "co4en.h"

#include <string.h>

// --- SHA-256 ---------------------------------------------------------------------
namespace {

constexpr uint32_t kK[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98,
    0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8,
    0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819,
    0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
    0xc67178f2};

struct Sha {
  uint32_t h[8];
  uint8_t buf[64];
  size_t used;
  uint64_t total;
};

inline uint32_t ror(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

void block(Sha* s, const uint8_t* p) {
  uint32_t w[64];
  for (int i = 0; i < 16; ++i)
    w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
  for (int i = 16; i < 64; ++i) {
    uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
    uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint32_t a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4], f = s->h[5], g = s->h[6], h = s->h[7];
  for (int i = 0; i < 64; ++i) {
    uint32_t t1 = h + (ror(e, 6) ^ ror(e, 11) ^ ror(e, 25)) + ((e & f) ^ (~e & g)) + kK[i] + w[i];
    uint32_t t2 = (ror(a, 2) ^ ror(a, 13) ^ ror(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
    h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
  }
  s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d; s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += h;
  co4en_wipe(w, sizeof(w));
}

void sha_init(Sha* s) {
  static const uint32_t iv[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  memcpy(s->h, iv, sizeof(iv));
  s->used = 0;
  s->total = 0;
}

void sha_add(Sha* s, const uint8_t* p, size_t n) {
  if (p == nullptr) return;
  s->total += n;
  while (n > 0) {
    size_t k = 64 - s->used;
    if (k > n) k = n;
    memcpy(s->buf + s->used, p, k);
    s->used += k;
    p += k;
    n -= k;
    if (s->used == 64) {
      block(s, s->buf);
      s->used = 0;
    }
  }
}

void sha_done(Sha* s, uint8_t out[32]) {
  uint64_t bits = s->total * 8;
  uint8_t pad = 0x80;
  sha_add(s, &pad, 1);
  pad = 0;
  while (s->used != 56) sha_add(s, &pad, 1);
  uint8_t len[8];
  for (int i = 0; i < 8; ++i) len[i] = (uint8_t)(bits >> (56 - 8 * i));
  sha_add(s, len, 8);
  for (int i = 0; i < 8; ++i) {
    out[4 * i] = (uint8_t)(s->h[i] >> 24);
    out[4 * i + 1] = (uint8_t)(s->h[i] >> 16);
    out[4 * i + 2] = (uint8_t)(s->h[i] >> 8);
    out[4 * i + 3] = (uint8_t)s->h[i];
  }
  co4en_wipe(s, sizeof(*s));
}

void put16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
void put32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8 * i)); }
void put64(uint8_t* p, uint64_t v) { for (int i = 0; i < 8; ++i) p[i] = (uint8_t)(v >> (8 * i)); }
uint32_t get32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

bool ct_equal(const uint8_t* a, const uint8_t* b, size_t n) {
  uint8_t d = 0;
  for (size_t i = 0; i < n; ++i) d |= a[i] ^ b[i];
  return d == 0;
}

size_t keyid_len(uint16_t id) { return id < 0x80 ? 1 : 2; }
size_t put_keyid(uint8_t* p, uint16_t id) {
  if (id < 0x80) { p[0] = (uint8_t)id; return 1; }
  p[0] = (uint8_t)(0x80 | (id >> 8));
  p[1] = (uint8_t)id;
  return 2;
}

size_t value_len(const Co4enValue& v) {
  size_t n;
  switch (v.type) {
    case CO4EN_VT_BOOL: n = 1; break;
    case CO4EN_VT_I32: case CO4EN_VT_F32: n = 4; break;
    case CO4EN_VT_I64: case CO4EN_VT_F64: n = 8; break;
    case CO4EN_VT_STR: if (v.data_n > CO4EN_STR_MAX) return 0; n = 1 + v.data_n; break;
    case CO4EN_VT_BYTES: if (v.data_n > 255) return 0; n = 1 + v.data_n; break;
    default: return 0;
  }
  return 1 + n + (v.has_ofs ? 2 : 0);  // vtype + value [+ ts_ofs]
}

size_t put_value(uint8_t* p, const Co4enValue& v) {
  uint8_t* s = p;
  *p++ = (uint8_t)(CO4EN_VT(v.type, v.q) | (v.has_ofs ? CO4EN_VT_TS_OFS : 0));
  switch (v.type) {
    case CO4EN_VT_BOOL: *p++ = v.b ? 1 : 0; break;
    case CO4EN_VT_I32: put32(p, (uint32_t)v.i32); p += 4; break;
    case CO4EN_VT_I64: put64(p, (uint64_t)v.i64); p += 8; break;
    case CO4EN_VT_F32: { uint32_t u; memcpy(&u, &v.f32, 4); put32(p, u); p += 4; break; }
    case CO4EN_VT_F64: { uint64_t u; memcpy(&u, &v.f64, 8); put64(p, u); p += 8; break; }
    default: *p++ = (uint8_t)v.data_n; if (v.data_n) memcpy(p, v.data, v.data_n); p += v.data_n; break;
  }
  if (v.has_ofs) { put16(p, (uint16_t)v.ts_ofs); p += 2; }
  return (size_t)(p - s);
}

void add_mic(Co4enFrame* f, const uint8_t secret[CO4EN_SECRET_LEN]) {
  uint8_t ka[32], m[32];
  co4en_k_auth(secret, ka);
  co4en_hmac(ka, 32, f->b, f->n, nullptr, 0, nullptr, 0, m);
  co4en_tlv(f, CO4EN_TAG_MIC, m, CO4EN_MIC_LEN);
  co4en_wipe(ka, sizeof(ka));
  co4en_wipe(m, sizeof(m));
}

bool xor_open(const uint8_t k_enc[32], const uint8_t nn[CO4EN_NONCE_LEN], const uint8_t ng[CO4EN_NONCE_LEN],
              const uint8_t* in, size_t in_n, uint8_t pmk[16], uint8_t lmk[16]) {
  if (in_n != 32) return false;
  static const uint8_t ks_label[2] = {'k', 's'};
  uint8_t ks[32];
  co4en_hmac(k_enc, 32, ks_label, 2, nn, CO4EN_NONCE_LEN, ng, CO4EN_NONCE_LEN, ks);
  for (int i = 0; i < 16; ++i) {
    pmk[i] = in[i] ^ ks[i];
    lmk[i] = in[16 + i] ^ ks[16 + i];
  }
  co4en_wipe(ks, sizeof(ks));
  return true;
}

}  // namespace

const Co4enKeyWrap kCo4enWrapXorV04 = {"xor-hmac-v0.4", xor_open};

void co4en_wipe(void* p, size_t n) {
  volatile uint8_t* v = static_cast<volatile uint8_t*>(p);
  while (n--) *v++ = 0;
}

void co4en_sha256(const uint8_t* data, size_t n, uint8_t out[32]) {
  Sha s;
  sha_init(&s);
  sha_add(&s, data, n);
  sha_done(&s, out);
}

void co4en_hmac(const uint8_t* key, size_t key_n, const uint8_t* a, size_t an, const uint8_t* b, size_t bn,
                const uint8_t* c, size_t cn, uint8_t out[32]) {
  uint8_t k[64] = {0}, pad[64], inner[32];
  if (key_n > 64) co4en_sha256(key, key_n, k);
  else memcpy(k, key, key_n);
  Sha s;
  for (int i = 0; i < 64; ++i) pad[i] = k[i] ^ 0x36;
  sha_init(&s);
  sha_add(&s, pad, 64);
  sha_add(&s, a, an);
  sha_add(&s, b, bn);
  sha_add(&s, c, cn);
  sha_done(&s, inner);
  for (int i = 0; i < 64; ++i) pad[i] = k[i] ^ 0x5c;
  sha_init(&s);
  sha_add(&s, pad, 64);
  sha_add(&s, inner, 32);
  sha_done(&s, out);
  co4en_wipe(k, sizeof(k));
  co4en_wipe(pad, sizeof(pad));
  co4en_wipe(inner, sizeof(inner));
}

void co4en_hkdf(const uint8_t* ikm, size_t ikm_n, const char* info, uint8_t* out, size_t n) {
  uint8_t prk[32], t[32];
  co4en_hmac(reinterpret_cast<const uint8_t*>(CO4EN_HKDF_SALT), strlen(CO4EN_HKDF_SALT), ikm, ikm_n, nullptr, 0,
             nullptr, 0, prk);
  size_t done = 0, tn = 0;
  for (uint8_t i = 1; done < n; ++i) {
    co4en_hmac(prk, 32, t, tn, reinterpret_cast<const uint8_t*>(info), strlen(info), &i, 1, t);
    tn = 32;
    size_t k = n - done < 32 ? n - done : 32;
    memcpy(out + done, t, k);
    done += k;
  }
  co4en_wipe(prk, sizeof(prk));
  co4en_wipe(t, sizeof(t));
}

void co4en_k_auth(const uint8_t secret[CO4EN_SECRET_LEN], uint8_t out[32]) {
  co4en_hkdf(secret, CO4EN_SECRET_LEN, CO4EN_HKDF_INFO_AUTH, out, 32);
}
void co4en_k_enc(const uint8_t secret[CO4EN_SECRET_LEN], uint8_t out[32]) {
  co4en_hkdf(secret, CO4EN_SECRET_LEN, CO4EN_HKDF_INFO_ENC, out, 32);
}

void co4en_qr_make(const uint8_t mac[6], const uint8_t secret[CO4EN_SECRET_LEN], char out[kCo4enQrLen]) {
  static const char hx[] = "0123456789ABCDEF";
  static const char b32[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
  char* p = out;
  memcpy(p, CO4EN_QR_PREFIX ":", 7);
  p += 7;
  for (int i = 0; i < 6; ++i) { *p++ = hx[mac[i] >> 4]; *p++ = hx[mac[i] & 15]; }
  *p++ = ':';
  uint32_t acc = 0;
  int bits = 0;
  for (int i = 0; i < CO4EN_SECRET_LEN; ++i) {
    acc = (acc << 8) | secret[i];
    bits += 8;
    while (bits >= 5) { *p++ = b32[(acc >> (bits - 5)) & 31]; bits -= 5; }
  }
  if (bits > 0) *p++ = b32[(acc << (5 - bits)) & 31];
  *p = '\0';
  acc = 0;
}

void co4en_begin(Co4enFrame* f, uint8_t type, uint8_t flags, uint32_t seq, uint32_t ts) {
  memset(f, 0, sizeof(*f));
  f->b[0] = CO4EN_VER;
  f->b[1] = type;
  f->b[2] = flags;
  f->b[3] = 0;
  put32(f->b + 4, seq);
  put32(f->b + 8, ts);
  f->n = CO4EN_HDR_LEN;
}

size_t co4en_room(const Co4enFrame& f) { return f.n >= CO4EN_MAX_FRAME ? 0 : CO4EN_MAX_FRAME - f.n; }

bool co4en_tlv(Co4enFrame* f, uint8_t tag, const uint8_t* v, size_t n) {
  if (n > 255 || f->n + 2 + n > CO4EN_MAX_FRAME) {
    f->overflow = true;
    return false;
  }
  f->b[f->n++] = tag;
  f->b[f->n++] = (uint8_t)n;
  if (n) memcpy(f->b + f->n, v, n);
  f->n += n;
  return true;
}

Co4enValue co4en_f32(float v) { Co4enValue x = {}; x.type = CO4EN_VT_F32; x.f32 = v; return x; }
Co4enValue co4en_f64(double v) { Co4enValue x = {}; x.type = CO4EN_VT_F64; x.f64 = v; return x; }
Co4enValue co4en_i32(int32_t v) { Co4enValue x = {}; x.type = CO4EN_VT_I32; x.i32 = v; return x; }
Co4enValue co4en_i64(int64_t v) { Co4enValue x = {}; x.type = CO4EN_VT_I64; x.i64 = v; return x; }
Co4enValue co4en_str(const char* s) {
  Co4enValue x = {};
  x.type = CO4EN_VT_STR;
  x.data = reinterpret_cast<const uint8_t*>(s);
  x.data_n = strlen(s);
  return x;
}

bool co4en_key_ok(const char* key) {
  size_t n = key ? strlen(key) : 0;
  if (n < 1 || n > CO4EN_KEY_MAX) return false;
  for (size_t i = 0; i < n; ++i) {
    char c = key[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-')) return false;
  }
  return true;
}

size_t co4en_kv_text_size(const char* key, const Co4enValue& v) {
  size_t vl = value_len(v);
  if (!co4en_key_ok(key) || vl == 0) return 0;
  size_t n = 1 + strlen(key) + vl;
  return n > 255 ? 0 : 2 + n;
}

bool co4en_kv_text(Co4enFrame* f, const char* key, const Co4enValue& v) {
  if (co4en_kv_text_size(key, v) == 0) { f->overflow = true; return false; }
  uint8_t tmp[255];
  size_t kn = strlen(key), n = 0;
  tmp[n++] = (uint8_t)kn;
  memcpy(tmp + n, key, kn);
  n += kn;
  n += put_value(tmp + n, v);
  return co4en_tlv(f, CO4EN_TAG_KV_TEXT, tmp, n);
}

bool co4en_kv_id(Co4enFrame* f, uint16_t id, const Co4enValue& v) {
  size_t vl = value_len(v);
  if (id > 0x7FFF || vl == 0 || keyid_len(id) + vl > 255) { f->overflow = true; return false; }
  uint8_t tmp[255];
  size_t n = put_keyid(tmp, id);
  n += put_value(tmp + n, v);
  return co4en_tlv(f, CO4EN_TAG_KV_ID, tmp, n);
}

bool co4en_keymap(Co4enFrame* f, uint16_t id, const char* key) {
  if (id > 0x7FFF || !co4en_key_ok(key)) { f->overflow = true; return false; }
  uint8_t tmp[40];
  size_t n = put_keyid(tmp, id), kn = strlen(key);
  tmp[n++] = (uint8_t)kn;
  memcpy(tmp + n, key, kn);
  return co4en_tlv(f, CO4EN_TAG_KEYMAP, tmp, n + kn);
}

bool co4en_status(Co4enFrame* f, uint16_t vstore_mv, uint32_t uptime_s, uint16_t err_bits) {
  uint8_t t[8];
  put16(t, vstore_mv);
  put32(t + 2, uptime_s);
  put16(t + 6, err_bits);
  return co4en_tlv(f, CO4EN_TAG_STATUS, t, 8);
}

void co4en_pair_req(Co4enFrame* f, const uint8_t secret[CO4EN_SECRET_LEN], const uint8_t mac[6], uint8_t dev_type,
                    const uint8_t fw[3], uint16_t caps, const uint8_t nonce_n[CO4EN_NONCE_LEN], uint32_t seq,
                    uint32_t ts, uint8_t flags) {
  co4en_begin(f, CO4EN_T_PAIR_REQ, flags, seq, ts);
  uint8_t body[20];
  memcpy(body, mac, 6);
  body[6] = dev_type;
  memcpy(body + 7, fw, 3);
  put16(body + 10, caps);
  memcpy(body + 12, nonce_n, CO4EN_NONCE_LEN);
  co4en_tlv(f, CO4EN_TAG_PAIR_REQ, body, sizeof(body));
  add_mic(f, secret);
}

Co4enPairErr co4en_pair_resp_open(const uint8_t* f, size_t n, const uint8_t secret[CO4EN_SECRET_LEN],
                                  const uint8_t nonce_n[CO4EN_NONCE_LEN], const Co4enKeyWrap& wrap,
                                  Co4enPairResult* out) {
  memset(out, 0, sizeof(*out));
  if (n != CO4EN_PAIR_RESP_LEN || f[0] != CO4EN_VER || f[1] != CO4EN_T_PAIR_RESP || f[12] != CO4EN_TAG_PAIR_RESP ||
      f[13] != 59 || f[14 + 59] != CO4EN_TAG_MIC || f[15 + 59] != CO4EN_MIC_LEN)
    return kPairFormat;
  uint8_t ka[32], m[32];
  co4en_k_auth(secret, ka);
  co4en_hmac(ka, 32, f, n - CO4EN_MIC_LEN - 2, nullptr, 0, nullptr, 0, m);
  bool mic_ok = ct_equal(m, f + n - CO4EN_MIC_LEN, CO4EN_MIC_LEN);
  co4en_wipe(ka, sizeof(ka));
  co4en_wipe(m, sizeof(m));
  if (!mic_ok) return kPairMic;
  const uint8_t* b = f + 14;
  if (!ct_equal(b + 6, nonce_n, CO4EN_NONCE_LEN)) return kPairNonce;
  uint8_t ke[32];
  co4en_k_enc(secret, ke);
  bool ok = wrap.open(ke, b + 6, b + 14, b + 27, 32, out->pmk, out->lmk);
  co4en_wipe(ke, sizeof(ke));
  if (!ok) { co4en_wipe(out, sizeof(*out)); return kPairWrap; }
  memcpy(out->gw_mac, b, 6);
  out->channel = b[22];
  out->unix = get32(b + 23);
  out->seq = get32(f + 4);
  return kPairOk;
}

bool co4en_hdr(const uint8_t* f, size_t n, Co4enHdr* h) {
  if (n < CO4EN_HDR_LEN || n > CO4EN_MAX_FRAME || f[0] != CO4EN_VER) return false;
  h->ver = f[0];
  h->type = f[1];
  h->flags = f[2];
  h->rsv = f[3];
  h->seq = get32(f + 4);
  h->ts = get32(f + 8);
  return true;
}

bool co4en_parse_ack(const uint8_t* f, size_t n, Co4enAck* out) {
  Co4enHdr h;
  if (!co4en_hdr(f, n, &h) || h.type != CO4EN_T_ACK) return false;
  for (size_t o = CO4EN_HDR_LEN; o + 2 <= n;) {
    uint8_t tag = f[o], len = f[o + 1];
    if (o + 2 + len > n) return false;
    if (tag == CO4EN_TAG_ACK_INFO && len >= 10) {
      const uint8_t* v = f + o + 2;
      out->seq = h.seq;
      out->acked_seq = get32(v);
      out->channel = v[4];
      out->unix = get32(v + 5);
      out->result = v[9];
      return true;
    }
    o += 2 + len;
  }
  return false;
}

void co4en_hex(const uint8_t* b, size_t n, char* out, size_t out_n) {
  static const char hx[] = "0123456789ABCDEF";
  size_t o = 0;
  for (size_t i = 0; i < n && o + 2 < out_n; ++i) {
    out[o++] = hx[b[i] >> 4];
    out[o++] = hx[b[i] & 15];
  }
  if (out_n) out[o] = '\0';
}

// --- seq -------------------------------------------------------------------------------
bool co4en_seq_boot(Co4enSeq* s, bool nvs_have, uint32_t nvs_base, bool rtc_ok, uint32_t rtc_next) {
  if (nvs_have && rtc_ok && rtc_next >= nvs_base && rtc_next < nvs_base + kCo4enSeqAhead) {
    s->next = rtc_next;  // warm reboot inside the reserved block
    s->nvs = nvs_base;
    return false;
  }
  // Cold boot (or RTC outside the safe range): skip the whole block that may be in use.
  s->nvs = (nvs_have ? nvs_base : 0) + CO4EN_SEQ_NVS_STEP;  // seq 0 is never used
  s->next = s->nvs;
  return true;
}

uint32_t co4en_seq_take(Co4enSeq* s, bool* persist) {
  *persist = false;
  if (s->next >= s->nvs + kCo4enSeqAhead) {  // reserve the next block before using this seq
    s->nvs += CO4EN_SEQ_NVS_STEP;
    *persist = true;
  }
  return s->next++;
}

// --- nonce window --------------------------------------------------------------------------
void co4en_nonce_push(Co4enNonceWin* w, const uint8_t nonce[CO4EN_NONCE_LEN], uint32_t now_ms) {
  memcpy(w->n[1], w->n[0], CO4EN_NONCE_LEN);
  w->t_ms[1] = w->t_ms[0];
  w->valid[1] = w->valid[0];
  memcpy(w->n[0], nonce, CO4EN_NONCE_LEN);
  w->t_ms[0] = now_ms;
  w->valid[0] = true;
}

void co4en_nonce_clear(Co4enNonceWin* w) { co4en_wipe(w, sizeof(*w)); }

Co4enPairErr co4en_pair_resp_open_win(const uint8_t* f, size_t n, const uint8_t secret[CO4EN_SECRET_LEN],
                                      const Co4enNonceWin& w, uint32_t now_ms, const Co4enKeyWrap& wrap,
                                      Co4enPairResult* out) {
  Co4enPairErr last = kPairNonce;
  for (int i = 0; i < 2; ++i) {
    if (!w.valid[i] || now_ms - w.t_ms[i] > kCo4enNonceMaxAgeMs) continue;
    Co4enPairErr e = co4en_pair_resp_open(f, n, secret, w.n[i], wrap, out);
    if (e == kPairOk) return e;
    if (e != kPairNonce) return e;  // format / MIC / wrap: same for both nonces
    last = e;
  }
  return last;
}

// --- default KEYMAP --------------------------------------------------------------------------
uint16_t co4en_default_key_id(const char* key) {
  static const struct {
    const char* k;
    uint16_t id;
  } t[] = {{"e.imp", 1},     {"e.imp.t1", 2},  {"e.imp.t2", 3},  {"e.exp", 4},     {"e.exp.t1", 5},  {"e.exp.t2", 6},
           {"er.imp", 7},    {"er.exp", 8},    {"p.imp", 10},    {"p.exp", 11},    {"p.l1", 12},     {"p.l2", 13},
           {"p.l3", 14},     {"p.exp.l1", 15}, {"p.exp.l2", 16}, {"p.exp.l3", 17}, {"q.imp", 18},    {"q.exp", 19},
           {"u.l1", 20},     {"u.l2", 21},     {"u.l3", 22},     {"i.l1", 23},     {"i.l2", 24},     {"i.l3", 25},
           {"f", 26},        {"pf", 27},       {"tariff", 28},   {"meter.id", 29}};
  for (const auto& e : t)
    if (strcmp(e.k, key) == 0) return e.id;
  return 0;
}
