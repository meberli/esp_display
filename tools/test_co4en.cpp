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

  // PAIR_RESP nonce window: latest and previous nonce_n, max. 10 s old
  pr[30] ^= 1;  // undo the tamper above
  {
    Co4enNonceWin w;
    co4en_nonce_clear(&w);
    uint8_t n2[8] = {1, 2, 3, 4, 5, 6, 7, 8}, n3[8] = {9, 9, 9, 9, 9, 9, 9, 9};
    CHECK(co4en_pair_resp_open_win(pr, prn, kSecret, w, 1000, kCo4enWrapXorV04, &res) == kPairNonce, "win: empty");
    co4en_nonce_push(&w, kNonceN, 1000);
    CHECK(co4en_pair_resp_open_win(pr, prn, kSecret, w, 1500, kCo4enWrapXorV04, &res) == kPairOk, "win: latest");
    co4en_nonce_push(&w, n2, 4000);
    CHECK(co4en_pair_resp_open_win(pr, prn, kSecret, w, 4100, kCo4enWrapXorV04, &res) == kPairOk, "win: previous");
    CHECK(co4en_pair_resp_open_win(pr, prn, kSecret, w, 1000 + 10000, kCo4enWrapXorV04, &res) == kPairOk, "win: 10 s ok");
    CHECK(co4en_pair_resp_open_win(pr, prn, kSecret, w, 1000 + 10001, kCo4enWrapXorV04, &res) == kPairNonce, "win: >10 s old");
    co4en_nonce_push(&w, n3, 7000);  // kNonceN is now two back
    CHECK(co4en_pair_resp_open_win(pr, prn, kSecret, w, 7100, kCo4enWrapXorV04, &res) == kPairNonce, "win: older than previous");
    co4en_nonce_clear(&w);
    co4en_nonce_push(&w, kNonceN, 0xFFFFF000u);  // millis wrap
    CHECK(co4en_pair_resp_open_win(pr, prn, kSecret, w, 0x00000100u, kCo4enWrapXorV04, &res) == kPairOk, "win: ms wrap");
    pr[30] ^= 1;
    CHECK(co4en_pair_resp_open_win(pr, prn, kSecret, w, 0x00000100u, kCo4enWrapXorV04, &res) == kPairMic, "win: tampered");
    pr[30] ^= 1;
  }

  // seq: monotonic, NVS reserved at +224, never backwards across reboots
  {
    Co4enSeq q;
    bool persist = false, mono = true, early = true;
    uint32_t nvs = 0, last = 0;
    CHECK(co4en_seq_boot(&q, false, 0, false, 0) && q.next == 256 && q.nvs == 256, "seq: first boot 256, persist");
    nvs = q.nvs;
    for (int i = 0; i < 1000; ++i) {
      uint32_t s = co4en_seq_take(&q, &persist);
      if (persist) {
        if (s != nvs + 224) early = false;  // proactive: exactly at +224
        nvs = q.nvs;
      }
      if (i > 0 && s <= last) mono = false;
      if (s >= nvs + 256) early = false;  // NVS (written before use) always covers s
      last = s;
    }
    CHECK(mono, "seq: strictly increasing");
    CHECK(early && q.next < q.nvs + 256, "seq: next block reserved at +224, before use");
    Co4enSeq c;  // cold boot with the stored NVS value
    CHECK(co4en_seq_boot(&c, true, nvs, false, 0) && c.next > last, "seq: cold boot above last sent");
    Co4enSeq wq;  // warm boot (RTC) inside the block
    bool pw = co4en_seq_boot(&wq, true, nvs, true, q.next);
    CHECK(wq.next >= q.next && (pw || wq.nvs == nvs), "seq: warm boot continues");
    Co4enSeq bad;  // RTC garbage below the block: must not go back
    CHECK(co4en_seq_boot(&bad, true, nvs, true, 5) && bad.next > last, "seq: stale RTC ignored");
    Co4enSeq hi;  // RTC beyond the safe range: cold path
    CHECK(co4en_seq_boot(&hi, true, nvs, true, nvs + 230) && hi.next >= nvs + 256, "seq: RTC near block end -> cold");
    // power cut right after the proactive write: the new block start is above everything sent
    Co4enSeq z;
    co4en_seq_boot(&z, true, 1000, false, 0);
    uint32_t zl = 0, zn = z.nvs;
    for (int i = 0; i < 300; ++i) {
      zl = co4en_seq_take(&z, &persist);
      if (persist) zn = z.nvs;
    }
    Co4enSeq z2;
    co4en_seq_boot(&z2, true, zn, false, 0);
    CHECK(z2.next > zl, "seq: cold boot after block switch");
  }

  // Default KEYMAP (spec v0.4 §4)
  CHECK(co4en_default_key_id("e.imp") == 1 && co4en_default_key_id("p.imp") == 2 + 8 && co4en_default_key_id("er.exp") == 8 &&
            co4en_default_key_id("meter.id") == 29 && co4en_default_key_id("w.vol") == 40 && co4en_default_key_id("er.q1") == 30 && co4en_default_key_id("er.q4") == 33 && co4en_default_key_id("test.x") == 0,
        "default key ids");

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


  {  // link loss: 3 consecutive missed ACKs, an ACK resets
    Co4enLink l = {};
    CHECK(!co4en_link_miss(&l) && !co4en_link_miss(&l), "link: 2 misses not lost");
    co4en_link_ack(&l);
    CHECK(!co4en_link_miss(&l) && !co4en_link_miss(&l) && co4en_link_miss(&l), "link: 3 consecutive misses -> lost");
  }
  {  // sweep order: last known, 1, 6, 11, rest
    Co4enSweep s = {};
    co4en_sweep_start(&s, 6, 0, 0);
    const uint8_t want[13] = {6, 1, 11, 2, 3, 4, 5, 7, 8, 9, 10, 12, 13};
    CHECK(s.n == 13 && memcmp(s.order, want, 13) == 0, "sweep: order last,1,6,11,rest");
    co4en_sweep_start(&s, 0, 0, 0);
    const uint8_t want0[4] = {1, 6, 11, 2};
    CHECK(s.n == 13 && memcmp(s.order, want0, 4) == 0, "sweep: no last channel");
    co4en_sweep_start(&s, 9, 6, 0);
    CHECK(s.n == 1 && s.order[0] == 6, "sweep: Wi-Fi node stays on the AP channel");
  }
  {  // found on channel 11 after 1, 6 (last=1), 50 ms each; NVS persisted only on change
    Co4enSweep s = {};
    uint8_t nvs = 1;  // fake NVS
    co4en_sweep_start(&s, nvs, 0, 1000);
    uint32_t t = 1000;
    CHECK(co4en_sweep_step(&s, t, true) == 1, "sweep: probe ch1");
    CHECK(co4en_sweep_step(&s, t + 49, true) == 0, "sweep: waits 50 ms");
    t += 50;
    CHECK(co4en_sweep_step(&s, t, true) == 6, "sweep: then ch6");
    t += 50;
    CHECK(co4en_sweep_step(&s, t, true) == 11, "sweep: then ch11");
    if (co4en_sweep_found(&s, nvs)) nvs = s.found;
    CHECK(s.st == Co4enSweepSt::Found && s.found == 11 && nvs == 11 && s.fails == 0, "sweep: found ch11, NVS updated");
    CHECK(co4en_sweep_step(&s, t + 1000, true) == 0, "sweep: idle after found");
    co4en_sweep_start(&s, nvs, 0, t);
    CHECK(co4en_sweep_step(&s, t, true) == 11, "sweep: next sweep starts on the persisted channel");
    CHECK(!co4en_sweep_found(&s, nvs), "sweep: same channel -> no NVS write");
    CHECK(!co4en_sweep_found(&s, nvs), "sweep: found without probe ignored");
  }
  {  // not found: backoff 2, 4, 8, 16, 30 min (Kconfig defaults); VSTORE low pauses
    Co4enSweep s = {};
    co4en_sweep_start(&s, 0, 0, 0);
    uint32_t t = 0;
    const uint32_t bo[5] = {120000, 240000, 480000, 960000, 1800000};
    bool ok = true;
    for (int round = 0; round < 5; ++round) {
      int probes = 0;
      while (co4en_sweep_step(&s, t, true) != 0) { ++probes; t += 50; }
      ok &= probes == 13 && s.st == Co4enSweepSt::Backoff && s.t_next == t + bo[round];
      ok &= co4en_sweep_step(&s, s.t_next - 1, true) == 0;
      t = s.t_next;
    }
    CHECK(ok, "sweep: 13 probes per sweep, backoff 2/4/8/16/30 min");
    Co4enSweep p = {};
    co4en_sweep_start(&p, 3, 0, 0);
    CHECK(co4en_sweep_step(&p, 0, false) == 0 && p.t_next == 1000 && p.idx == 0, "sweep: VSTORE low -> waits");
    CHECK(co4en_sweep_step(&p, 1000, true) == 3, "sweep: VSTORE ok -> probes");
    co4en_sweep_start(&p, 3, 3, 0);
    CHECK(co4en_sweep_step(&p, 0, true) == 3 && co4en_sweep_step(&p, 50, true) == 0 && p.st == Co4enSweepSt::Backoff &&
              p.t_next == 50 + 120000, "sweep: single channel (Wi-Fi) not found -> backoff");
  }
  {  // backoff curve: minimum for 0/1 failures, doubling, capped, no overflow
    CHECK(co4en_sweep_backoff_ms(0) == 120000 && co4en_sweep_backoff_ms(1) == 120000 &&
              co4en_sweep_backoff_ms(5) == 1800000 && co4en_sweep_backoff_ms(6) == 1800000 &&
              co4en_sweep_backoff_ms(255) == 1800000, "backoff: 2 min .. 30 min cap");
  }
  {  // runtime re-sweep state machine: N missed ACKs -> search, gate, channel change, no AP
    Co4enLink l = {};
    Co4enResweep r = {};
    uint8_t gw = 7, nvs = 7;
    int nvs_writes = 0;
    uint32_t t = 1000;
    CHECK(co4en_resweep_wait_ms(r, t) == 0, "resweep: first search allowed at once");
    bool lost = false;
    for (int i = 0; i < kCo4enLinkMissMax; ++i) lost = co4en_link_miss(&l);
    CHECK(lost && kCo4enLinkMissMax == 3, "resweep: N=3 missed ACKs trigger");
    co4en_resweep_started(&r, t);
    uint8_t c = co4en_gw_channel_update(gw, 6);  // sweep finds the gateway on 6
    if (c) { gw = c; if (gw != nvs) { nvs = gw; ++nvs_writes; } }
    co4en_resweep_result(&r, true);
    CHECK(gw == 6 && nvs == 6 && nvs_writes == 1 && r.fails == 0 && r.n == 1, "resweep: channel change saved once");
    CHECK(co4en_gw_channel_update(gw, 6) == 0 && co4en_gw_channel_update(gw, 0) == 0 &&
              co4en_gw_channel_update(gw, 14) == 0, "resweep: same / invalid channel ignored");
    CHECK(co4en_resweep_wait_ms(r, t + 1000) == 119000, "resweep: min 2 min after a success");
    t += 120000;
    CHECK(co4en_resweep_wait_ms(r, t) == 0, "resweep: allowed after 2 min");
    // no AP of the WLAN on the gateway channel / gateway not found: failures double the gap
    const uint32_t gap[6] = {120000, 240000, 480000, 960000, 1800000, 1800000};
    bool ok = true;
    for (int i = 0; i < 6; ++i) {
      co4en_resweep_started(&r, t);
      co4en_resweep_result(&r, false);
      ok &= co4en_resweep_wait_ms(r, t) == gap[i] && co4en_resweep_wait_ms(r, t + gap[i] - 1) == 1 &&
            co4en_resweep_wait_ms(r, t + gap[i]) == 0;
      t += gap[i];
    }
    CHECK(ok && r.n == 7 && r.fails == 6, "resweep: backoff 2/4/8/16/30/30 min after failures");
    co4en_resweep_ack(&r);
    CHECK(r.fails == 0 && co4en_resweep_wait_ms(r, r.last_ms) == 120000 && co4en_resweep_wait_ms(r, r.last_ms + 120000) == 0, "resweep: ACK resets backoff to 2 min");
    co4en_link_ack(&l);
    CHECK(!co4en_link_miss(&l), "resweep: ACK resets the miss counter");
    Co4enResweep w = {};
    w.n = 1; w.last_ms = 0xFFFFF000u;  // millis wrap
    CHECK(co4en_resweep_wait_ms(w, 0x00001000u) == 120000 - 0x2000, "resweep: ms wrap-around");
  }
  {  // v0.7 BEACON (spec 6.2 vector)
    uint8_t b[64];
    size_t bn = unhex("01040200010000004002DF6A220D01DCDA0C11223306034002DF6A", b);
    Co4enBeacon be = {};
    static const uint8_t gw[6] = {0xDC, 0xDA, 0x0C, 0x11, 0x22, 0x33};
    CHECK(bn == 27 && co4en_parse_beacon(b, bn, &be) && be.seq == 1 && be.channel == 6 && be.flags == 0x03 &&
              be.unix == 1793000000u && memcmp(be.gw_mac, gw, 6) == 0, "beacon: spec vector");
    CHECK(!co4en_parse_beacon(b, bn - 1, &be), "beacon: truncated");
    b[14] = 0x02;
    CHECK(!co4en_parse_beacon(b, bn, &be), "beacon: unknown beacon_ver ignored");
    b[14] = 0x01;
    uint8_t x[80];  // unknown TLV before 0x22, extra bytes inside 0x22
    memcpy(x, b, 12);
    x[12] = 0x7E; x[13] = 2; x[14] = 0xAA; x[15] = 0xBB;
    x[16] = 0x22; x[17] = 15;
    memcpy(x + 18, b + 14, 13);
    x[31] = 0x99; x[32] = 0x99;
    CHECK(co4en_parse_beacon(x, 33, &be) && be.channel == 6, "beacon: unknown TLV skipped, extra bytes ignored");
    b[1] = CO4EN_T_ACK;
    CHECK(!co4en_parse_beacon(b, bn, &be), "beacon: wrong type");
    Co4enFrame f2;
    co4en_begin(&f2, CO4EN_T_DATA, 0, 7, 0);
    CHECK(co4en_wlan_info(&f2, 6, gw, -61) && f2.n == 22 && f2.b[12] == 0x21 && f2.b[13] == 8 && f2.b[14] == 6 &&
              f2.b[21] == 0xC3, "wlan_info TLV 0x21");
  }
  printf(fails ? "RESULT FAIL (%d)\n" : "RESULT PASS\n", fails);
  return fails ? 1 : 0;
}
