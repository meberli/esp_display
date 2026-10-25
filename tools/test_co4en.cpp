// Host test of main/co4en.cpp against the CO4EN v0.4 test vectors
// (tools/co4en_ref.py vectors, gwuno-fw test_vectors.md). TEST secret/keys only.
//   g++ -std=c++17 -Wall -I main tools/test_co4en.cpp main/co4en.cpp -o /tmp/tc && /tmp/tc
#include <stdio.h>
#include <string.h>

#include "co4en.h"

static int fails = 0;
#define CHECK(c, what)                         \
  do {                                         \
    if (c) printf("ok   %s\n", what);          \
    else { printf("FAIL %s\n", what); ++fails; } \
  } while (0)

static size_t unhex(const char* s, uint8_t* out) {
  size_t n = 0;
  for (; s[0] && s[1]; s += 2) {
    unsigned v;
    sscanf(s, "%2x", &v);
    out[n++] = (uint8_t)v;
  }
  return n;
}

static bool same(const Co4enFrame& f, const char* hex, const char* what) {
  uint8_t want[256];
  size_t n = unhex(hex, want);
  bool ok = !f.overflow && f.n == n && memcmp(f.b, want, n) == 0;
  if (!ok) {
    char got[520];
    co4en_hex(f.b, f.n, got, sizeof(got));
    printf("  %s got  %s\n  %s want %s\n", what, got, what, hex);
  }
  return ok;
}

static const uint8_t kSecret[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};  // TEST ONLY
static const uint8_t kNodeMac[6] = {0xF0, 0xF5, 0xBD, 0x01, 0x02, 0x03};
static const uint8_t kNonceN[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
static const uint32_t kTs = 1793000000;

int main() {
  char buf[600];
  uint8_t k[32];

  co4en_qr_make(kNodeMac, kSecret, buf);
  CHECK(strcmp(buf, "CO4EN1:F0F5BD010203:AAAQEAYEAUDAOCAJBIFQYDIOB4") == 0, "qr");
  co4en_k_auth(kSecret, k);
  co4en_hex(k, 32, buf, sizeof(buf));
  CHECK(strcmp(buf, "DD602E9A020EF7B59D1910FBE215B1293BB3720619177BF6A0512B924272A92B") == 0, "k_auth");
  co4en_k_enc(kSecret, k);
  co4en_hex(k, 32, buf, sizeof(buf));
  CHECK(strcmp(buf, "A7F35D57B1E843EFFA1444B750EC98D334752651E963CE04A07F6B9315DAD343") == 0, "k_enc");

  // SHA-256 known answers (empty, "abc", 2-block message)
  co4en_sha256(nullptr, 0, k);
  co4en_hex(k, 32, buf, sizeof(buf));
  CHECK(strcmp(buf, "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855") == 0, "sha256 empty");
  const char* m2 = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  co4en_sha256(reinterpret_cast<const uint8_t*>(m2), strlen(m2), k);
  co4en_hex(k, 32, buf, sizeof(buf));
  CHECK(strcmp(buf, "248D6A61D20638B8E5C026930C3E6039A33CE45964FF2167F6ECEDD419DB06C1") == 0, "sha256 448 bit");

  Co4enFrame f;
  const uint8_t fw[3] = {0, 4, 0};
  co4en_pair_req(&f, kSecret, kNodeMac, CO4EN_DEV_CII, fw,
                 CO4EN_CAP_KV_TEXT | CO4EN_CAP_KV_ID | CO4EN_CAP_CMD | CO4EN_CAP_MAINS | CO4EN_CAP_TEST, kNonceN, 1, 0, 0);
  CHECK(f.n == CO4EN_PAIR_REQ_LEN && same(f, "0105000001000000000000005014F0F5BD01020301000400D30011223344556677885F105ADFA4B4DF1F5B2318D722901650BCDB", "pair_req"), "pair_req (52 B)");

  co4en_begin(&f, CO4EN_T_DATA, CO4EN_F_ACK_REQ | CO4EN_F_TIME_VALID, 2, kTs);
  co4en_kv_text(&f, "p.imp", co4en_f32(1234.5f));
  co4en_kv_text(&f, "u.l1", co4en_f32(230.1f));
  co4en_kv_text(&f, "f", co4en_f32(50.0f));
  CHECK(same(f, "01010300020000004002DF6A130B05702E696D700300509A44130A04752E6C31039A196643130701660300004842", "kv_text"), "kv_text (46 B)");
  CHECK(co4en_kv_text_size("p.imp", co4en_f32(1)) == 13 && co4en_kv_text_size("p.l1", co4en_f32(1)) == 12, "kv_text size");

  co4en_begin(&f, CO4EN_T_DATA, CO4EN_F_ACK_REQ, 3, 0);
  co4en_keymap(&f, 1, "e.imp");
  co4en_keymap(&f, 2, "p.imp");
  co4en_keymap(&f, 200, "u.l1");
  CHECK(same(f, "01010100030000000000000015070105652E696D7015070205702E696D70150780C804752E6C31", "keymap"), "keymap (39 B)");

  co4en_begin(&f, CO4EN_T_DATA, CO4EN_F_TIME_VALID, 4, kTs);
  co4en_kv_id(&f, 1, co4en_i64(12345678));
  co4en_kv_id(&f, 2, co4en_f32(1234.5f));
  Co4enValue v = co4en_f32(230.1f);
  v.has_ofs = true;
  v.ts_ofs = -5;
  co4en_kv_id(&f, 200, v);
  CHECK(same(f, "01010200040000004002DF6A140A01024E61BC00000000001406020300509A44140980C8439A196643FBFF", "kv_id"), "kv_id (43 B)");

  // PAIR_RESP opened by the node
  uint8_t pr[256];
  size_t prn = unhex("01060200F40100004002DF6A513BDCDA0C112233112233445566778888776655443322110B4002DF6A2A68FC476EAF394FB945DFCF20299EDFEF665CC09F7169DD24C162182AF4105F5F10FA1BE1BB24B42927ADBBB03E84718F3D", pr);
  Co4enPairResult res;
  CHECK(co4en_pair_resp_open(pr, prn, kSecret, kNonceN, kCo4enWrapXorV04, &res) == kPairOk, "pair_resp open");
  co4en_hex(res.pmk, 16, buf, sizeof(buf));
  CHECK(strcmp(buf, "A0A1A2A3A4A5A6A7A8A9AAABACADAEAF") == 0, "pair_resp pmk");
  co4en_hex(res.lmk, 16, buf, sizeof(buf));
  CHECK(strcmp(buf, "B0B1B2B3B4B5B6B7B8B9BABBBCBDBEBF") == 0, "pair_resp lmk");
  CHECK(res.channel == 11 && res.unix == kTs && res.seq == 500 && res.gw_mac[0] == 0xDC && res.gw_mac[5] == 0x33, "pair_resp fields");
  uint8_t bad_n[8] = {0};
  CHECK(co4en_pair_resp_open(pr, prn, kSecret, bad_n, kCo4enWrapXorV04, &res) == kPairNonce, "pair_resp wrong nonce");
  pr[30] ^= 1;
  CHECK(co4en_pair_resp_open(pr, prn, kSecret, kNonceN, kCo4enWrapXorV04, &res) == kPairMic, "pair_resp tampered");
  CHECK(co4en_pair_resp_open(pr, prn - 1, kSecret, kNonceN, kCo4enWrapXorV04, &res) == kPairFormat, "pair_resp short");

  uint8_t ak[64];
  size_t akn = unhex("01030200F50100004102DF6A410A020000000B4102DF6A00", ak);
  Co4enAck a;
  CHECK(co4en_parse_ack(ak, akn, &a) && a.seq == 501 && a.acked_seq == 2 && a.channel == 11 && a.unix == kTs + 1 &&
            a.result == 0, "ack parse");

  // Limits
  CHECK(!co4en_key_ok("P.imp") && !co4en_key_ok("") && !co4en_key_ok("abcdefghijabcdefghijabcdefghijabc") &&
            co4en_key_ok("test.temp_c-1"), "key charset/length");
  co4en_begin(&f, CO4EN_T_DATA, 0, 9, 0);
  int n = 0;
  while (co4en_kv_text(&f, "p.l1", co4en_f32(1))) ++n;
  CHECK(n == 19 && f.overflow && f.n <= CO4EN_MAX_FRAME, "frame limit 250 B");

  printf(fails ? "RESULT FAIL (%d)\n" : "RESULT PASS\n", fails);
  return fails ? 1 : 0;
}
