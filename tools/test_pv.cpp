// Host test for main/pv.cpp (Growatt datalogger protocol).
//   g++ -std=c++17 -Wall -I main tools/test_pv.cpp main/pv.cpp -o /tmp/tp && /tmp/tp   (repo root)
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "pv.h"

static int fails = 0;
#define CHECK(c, what)                                                 \
  do {                                                                 \
    if (!(c)) {                                                        \
      printf("FAIL %s:%d %s\n", __FILE__, __LINE__, what);             \
      fails++;                                                         \
    }                                                                  \
  } while (0)
#define NEAR(a, b) (fabs((double)(a) - (double)(b)) < 0.051)

static void put(uint8_t* r, int hex, int len, unsigned v) {
  for (int i = len - 1; i >= 0; --i, v >>= 8) r[hex / 2 + i] = static_cast<uint8_t>(v);
}

// Classic T02/T05NNNN data record (6000UE-like, 3-phase), plain.
static size_t make_classic(uint8_t* r, uint8_t proto) {
  const size_t plain = 220;
  memset(r, 0, 300);
  r[0] = 0x00; r[1] = 0x07; r[2] = 0; r[3] = proto;
  size_t len = plain - 6;
  r[4] = static_cast<uint8_t>(len >> 8); r[5] = static_cast<uint8_t>(len); r[6] = 0x01; r[7] = 0x04;
  memcpy(r + 8, "YU34130305", 10);
  memcpy(r + 18, "RW45030008", 10);
  put(r, 78, 2, 1);        // status normal
  put(r, 82, 4, 41230);    // 4123.0 W DC
  put(r, 90, 2, 3512);     // pv1 351.2 V
  put(r, 94, 2, 62);       // 6.2 A
  put(r, 98, 4, 21770);    // 2177.0 W
  put(r, 106, 2, 3398);
  put(r, 110, 2, 57);
  put(r, 114, 4, 19460);
  put(r, 122, 4, 39870);   // AC 3987.0 W
  put(r, 130, 2, 4998);    // 49.98 Hz
  put(r, 134, 2, 2311);
  put(r, 150, 2, 2324);
  put(r, 166, 2, 2298);
  put(r, 182, 4, 187);     // 18.7 kWh today
  put(r, 190, 4, 412345);  // 41234.5 kWh total
  put(r, 206, 2, 412);     // 41.2 C
  if (proto == 0x02) return plain;
  pv_xor(r, plain);
  uint16_t c = pv_crc16(r, plain);
  r[plain] = static_cast<uint8_t>(c >> 8);
  r[plain + 1] = static_cast<uint8_t>(c);
  return plain + 2;
}

static void check_classic(const PvReading& p, const char* tag) {
  char w[80];
  snprintf(w, sizeof(w), "%s: fields", tag);
  CHECK(p.valid && NEAR(p.pv_w, 4123.0) && NEAR(p.ac_w, 3987.0) && NEAR(p.today_kwh, 18.7) &&
            NEAR(p.total_kwh, 41234.5), w);
  snprintf(w, sizeof(w), "%s: strings/grid/temp", tag);
  CHECK(NEAR(p.pv1_v, 351.2) && NEAR(p.pv1_a, 6.2) && NEAR(p.pv2_w, 1946.0) && NEAR(p.grid_hz, 49.98) &&
            NEAR(p.grid_v[2], 229.8) && NEAR(p.temp_c, 41.2) && p.status == 1, w);
  snprintf(w, sizeof(w), "%s: serials", tag);
  CHECK(strcmp(p.logger, "YU34130305") == 0 && strcmp(p.inverter, "RW45030008") == 0, w);
}

int main() {
  // CRC16/Modbus reference: "123456789" -> 0x4B37
  CHECK(pv_crc16(reinterpret_cast<const uint8_t*>("123456789"), 9) == 0x4B37, "crc16 modbus");
  uint8_t r[400], out[128];
  PvFrame f;
  PvReading p;

  // Protocol 02 (plain)
  size_t n = make_classic(r, 0x02);
  CHECK(pv_frame_length(r, n) == static_cast<int>(n) && pv_frame_length(r, n - 1) == 0, "frame length 02");
  int k = pv_handle(r, n, &f, &p, out, sizeof(out));
  CHECK(f.proto == 2 && f.type == 4 && k == 9, "02 ack length");
  const uint8_t ack02[9] = {0x00, 0x07, 0x00, 0x02, 0x00, 0x03, 0x01, 0x04, 0x00};
  CHECK(memcmp(out, ack02, 9) == 0, "02 ack bytes");
  check_classic(p, "02");

  // Protocol 05 (XOR + CRC)
  n = make_classic(r, 0x05);
  CHECK(pv_frame_length(r, n) == static_cast<int>(n), "frame length 05");
  k = pv_handle(r, n, &f, &p, out, sizeof(out));
  CHECK(k == 11 && out[3] == 5 && out[8] == 0x47, "05 ack");
  CHECK(pv_crc16(out, 9) == ((out[9] << 8) | out[10]), "05 ack crc");
  check_classic(p, "05");
  // Bad CRC -> rejected, no reply
  n = make_classic(r, 0x05);
  r[n - 1] ^= 1;
  CHECK(pv_handle(r, n, &f, &p, out, sizeof(out)) == -1, "bad crc rejected");

  // Buffered record (0x50): acked, no live reading
  n = make_classic(r, 0x02);
  r[7] = 0x50;
  k = pv_handle(r, n, &f, &p, out, sizeof(out));
  CHECK(k == 9 && out[7] == 0x50 && !p.valid, "buffered acked, not live");

  // Ping: echoed unchanged (05, scrambled + crc)
  uint8_t ping[40] = {0x00, 0x02, 0x00, 0x05, 0x00, 0x0c, 0x01, 0x16};
  memcpy(ping + 8, "YU34130305", 10);
  pv_xor(ping, 18);
  uint16_t c = pv_crc16(ping, 18);
  ping[18] = c >> 8; ping[19] = c & 0xFF;
  CHECK(pv_frame_length(ping, 20) == 20, "ping frame length");
  uint8_t copy[40];
  memcpy(copy, ping, 20);
  k = pv_handle(ping, 20, &f, &p, out, sizeof(out));
  CHECK(k == 20 && memcmp(out, copy, 20) == 0 && f.type == 0x16, "ping echo");

  // 0x29 and command answers: no reply
  n = make_classic(r, 0x02);
  r[7] = 0x29;
  CHECK(pv_handle(r, n, &f, &p, out, sizeof(out)) == 0, "0x29 no reply");
  n = make_classic(r, 0x02);
  r[7] = 0x19;
  CHECK(pv_handle(r, n, &f, &p, out, sizeof(out)) == 0, "command answer: no reply");

  // Garbage resync
  uint8_t junk[8] = {'G', 'E', 'T', ' ', '/', ' ', 'H', 'T'};
  CHECK(pv_frame_length(junk, 8) == -1, "HTTP garbage rejected");

  // Time command: decodes back to the logger id and the time string
  const char* when = "2026-10-18 16:05:00";
  size_t tl = pv_time_command(0x05, "YU34130305", when, out, sizeof(out));
  CHECK(tl > 0 && pv_frame_length(out, tl) == static_cast<int>(tl), "time cmd framing");
  CHECK(pv_crc16(out, tl - 2) == ((out[tl - 2] << 8) | out[tl - 1]), "time cmd crc");
  pv_xor(out, tl - 2);
  CHECK(out[7] == 0x18 && memcmp(out + 8, "YU34130305", 10) == 0 && out[19] == 31 &&
            memcmp(out + 22, when, strlen(when)) == 0, "time cmd content");
  tl = pv_time_command(0x02, "YU34130305", when, out, sizeof(out));
  CHECK(tl == 8 + 10 + 4 + strlen(when) && pv_frame_length(out, tl) == static_cast<int>(tl), "time cmd 02");

  // Real protocol-06 record from grott examples/grotttest.py
  FILE* fh = fopen("tools/testdata/grott_t06_record.hex", "r");
  CHECK(fh != nullptr, "real sample file");
  if (fh) {
    char hex[1200];
    size_t hl = fread(hex, 1, sizeof(hex) - 1, fh);
    fclose(fh);
    hex[hl] = 0;
    uint8_t rr[600];
    size_t m = 0;
    for (size_t i = 0; i + 1 < hl && hex[i] > ' '; i += 2) {
      unsigned v;
      sscanf(hex + i, "%2x", &v);
      rr[m++] = static_cast<uint8_t>(v);
    }
    CHECK(pv_frame_length(rr, m) == static_cast<int>(m), "real 06 framing");
    k = pv_handle(rr, m, &f, &p, out, sizeof(out));
    printf("real 06: k=%d proto=%d type=%02x pv=%.1f W ac=%.1f W today=%.1f total=%.1f temp=%.1f V1=%.1f logger=%s inv=%s\n",
           k, f.proto, f.type, p.pv_w, p.ac_w, p.today_kwh, p.total_kwh, p.temp_c, p.pv1_v, p.logger, p.inverter);
    CHECK(k == 11 && f.proto == 6 && f.type == 4 && p.valid, "real 06 decoded and acked");
  }
  // Stale: 2.5 x interval, min 150 s, unknown interval = 300 s
  CHECK(pv_stale(1000, 0, 60), "never -> stale");
  CHECK(!pv_stale(1149, 1000, 60) && pv_stale(1151, 1000, 60), "60 s interval -> 150 s");
  CHECK(!pv_stale(1000 + 750, 1000, 0) && pv_stale(1000 + 751, 1000, 0), "unknown -> 750 s");
  CHECK(!pv_stale(1000 + 750, 1000, 300) && pv_stale(1000 + 751, 1000, 300), "5 min -> 12.5 min");
  printf("%s (%d failures)\n", fails ? "FAILED" : "OK", fails);
  return fails ? 1 : 0;
}
