#include "pv.h"

#include <stdio.h>
#include <string.h>

namespace {

const char kMask[] = "Growatt";

uint32_t be(const uint8_t* p, int len) {
  uint32_t v = 0;
  for (int i = 0; i < len; ++i) v = (v << 8) | p[i];
  return v;
}

// Grott layout offsets are in hex characters from the record start: byte = value / 2.
struct Field {
  uint16_t hex;   // grottconf "value"
  uint8_t len;    // bytes
  uint16_t div;
};
struct Layout {
  Field pvstatus, pvpowerin, pv1v, pv1a, pv1w, pv2v, pv2a, pv2w, pac, hz, gv1, gv2, gv3, today, total, temp;
  uint16_t serial;  // pvserial hex offset (datalogserial is always 16)
};
// T02NNNN / T05NNNN (classic, also 3-phase grid fields)
const Layout kClassic = {{78, 2, 1},   {82, 4, 10},  {90, 2, 10},  {94, 2, 10},  {98, 4, 10},  {106, 2, 10},
                         {110, 2, 10}, {114, 4, 10}, {122, 4, 10}, {130, 2, 100}, {134, 2, 10}, {150, 2, 10},
                         {166, 2, 10}, {182, 4, 10}, {190, 4, 10}, {206, 2, 10}, 36};
// T05NNNNX (records > 375 bytes)
const Layout kClassicX = {{78, 2, 1},   {82, 4, 10},  {90, 2, 10},  {94, 2, 10},  {98, 4, 10},  {106, 2, 10},
                          {110, 2, 10}, {114, 4, 10}, {170, 4, 10}, {178, 2, 100}, {182, 2, 10}, {198, 2, 10},
                          {214, 2, 10}, {274, 4, 10}, {282, 4, 10}, {450, 2, 10}, 36};
// T06NNNN (protocol 06: 20 bytes padding after the logger serial)
const Layout k06 = {{158, 2, 1},   {162, 4, 10}, {170, 2, 10}, {174, 2, 10}, {178, 4, 10}, {186, 2, 10},
                    {190, 2, 10},  {194, 4, 10}, {202, 4, 10}, {210, 2, 100}, {214, 2, 10}, {230, 2, 10},
                    {246, 2, 10},  {262, 4, 10}, {270, 4, 10}, {286, 2, 10}, 76};
const Layout k06X = {{158, 2, 1},  {162, 4, 10}, {170, 2, 10}, {174, 2, 10}, {178, 4, 10}, {186, 2, 10},
                     {190, 2, 10}, {194, 4, 10}, {250, 4, 10}, {258, 2, 100}, {262, 2, 10}, {278, 2, 10},
                     {294, 2, 10}, {354, 4, 10}, {362, 4, 10}, {530, 2, 10}, 76};

bool get(const uint8_t* r, size_t n, const Field& f, float* out) {
  size_t off = f.hex / 2;
  if (off + f.len > n) return false;
  *out = static_cast<float>(be(r + off, f.len)) / f.div;
  return true;
}

void text(const uint8_t* r, size_t n, size_t off, char* out) {
  size_t k = 0;
  for (; k < 10 && off + k < n; ++k) {
    char c = static_cast<char>(r[off + k]);
    out[k] = (c >= 0x20 && c < 0x7F) ? c : '?';
  }
  out[k] = '\0';
}

}  // namespace

uint16_t pv_crc16(const uint8_t* d, size_t n) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < n; ++i) {
    crc ^= d[i];
    for (int b = 0; b < 8; ++b) crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
  }
  return crc;
}

void pv_xor(uint8_t* d, size_t n) {
  for (size_t i = 8, j = 0; i < n; ++i, j = (j + 1) % 7) d[i] ^= static_cast<uint8_t>(kMask[j]);
}

int pv_frame_length(const uint8_t* buf, size_t n) {
  if (n < 8) return 0;
  if (buf[2] != 0x00 || (buf[3] != 0x02 && buf[3] != 0x05 && buf[3] != 0x06)) return -1;
  size_t len = (static_cast<size_t>(buf[4]) << 8) | buf[5];
  size_t total = 6 + len + (buf[3] == 0x02 ? 0 : 2);
  if (len < 2 || total > 2048) return -1;
  return n >= total ? static_cast<int>(total) : 0;
}

int pv_handle(uint8_t* rec, size_t n, PvFrame* frame, PvReading* reading, uint8_t* out, size_t cap) {
  memset(frame, 0, sizeof(*frame));
  if (reading != nullptr) reading->valid = false;
  if (n < 8 || cap < 16) return 0;
  frame->proto = rec[3];
  frame->dev = rec[6];
  frame->type = rec[7];
  frame->total = n;
  const bool enc = frame->proto != 0x02;
  if (enc) {
    if (n < 10) return -1;
    uint16_t crc = static_cast<uint16_t>((rec[n - 2] << 8) | rec[n - 1]);
    if (pv_crc16(rec, n - 2) != crc) return -1;
  }
  // Ping: echo the record unchanged (still scrambled, with its CRC).
  if (frame->type == kPvPing) {
    if (n > cap) return 0;
    memcpy(out, rec, n);
    return static_cast<int>(n);
  }
  const size_t plain = enc ? n - 2 : n;
  if (enc) pv_xor(rec, plain);
  const uint8_t t = frame->type;
  if (t == kPvAnnounce || t == kPvData || t == kPvBuffered || t == 0x1b || t == 0x20) {
    if (reading != nullptr && t == kPvData) {
      const bool x = plain > 375;
      const Layout& L = frame->proto == 0x06 ? (x ? k06X : k06) : (x ? kClassicX : kClassic);
      PvReading r = {};
      float st = 0;
      bool ok = get(rec, plain, L.pvpowerin, &r.pv_w) && get(rec, plain, L.pac, &r.ac_w) &&
                get(rec, plain, L.today, &r.today_kwh) && get(rec, plain, L.total, &r.total_kwh);
      get(rec, plain, L.pvstatus, &st);
      get(rec, plain, L.pv1v, &r.pv1_v);
      get(rec, plain, L.pv1a, &r.pv1_a);
      get(rec, plain, L.pv1w, &r.pv1_w);
      get(rec, plain, L.pv2v, &r.pv2_v);
      get(rec, plain, L.pv2a, &r.pv2_a);
      get(rec, plain, L.pv2w, &r.pv2_w);
      get(rec, plain, L.hz, &r.grid_hz);
      get(rec, plain, L.gv1, &r.grid_v[0]);
      get(rec, plain, L.gv2, &r.grid_v[1]);
      get(rec, plain, L.gv3, &r.grid_v[2]);
      get(rec, plain, L.temp, &r.temp_c);
      r.status = static_cast<int>(st);
      text(rec, plain, 8, r.logger);
      text(rec, plain, L.serial / 2, r.inverter);
      // Plausibility (6 kVA class): reject obviously misaligned layouts.
      r.valid = ok && r.pv_w < 30000 && r.ac_w < 30000 && r.total_kwh < 10000000;
      *reading = r;
    }
    // Ack: header with length 3, same dev/type, payload 00 (02) or 47 + CRC (05/06).
    out[0] = rec[0];
    out[1] = rec[1];
    out[2] = 0x00;
    out[3] = frame->proto;
    out[4] = 0x00;
    out[5] = 0x03;
    out[6] = rec[6];
    out[7] = rec[7];
    out[8] = enc ? 0x47 : 0x00;
    if (!enc) return 9;
    uint16_t c = pv_crc16(out, 9);
    out[9] = static_cast<uint8_t>(c >> 8);
    out[10] = static_cast<uint8_t>(c);
    return 11;
  }
  return 0;  // 0x29, command answers (0x05/0x06/0x18/0x19), unknown: no reply
}

size_t pv_time_command(uint8_t proto, const char* logger, const char* datetime, uint8_t* out, size_t cap) {
  uint8_t body[96];
  size_t b = 0;
  for (int i = 0; i < 10; ++i) body[b++] = static_cast<uint8_t>(logger[i] ? logger[i] : '0');
  if (proto == 0x06) {
    memset(body + b, 0, 20);
    b += 20;
  }
  body[b++] = 0x00;
  body[b++] = 31;  // register 31: date/time
  size_t tl = strlen(datetime);
  body[b++] = static_cast<uint8_t>(tl >> 8);
  body[b++] = static_cast<uint8_t>(tl);
  memcpy(body + b, datetime, tl);
  b += tl;
  size_t len = b + 2;  // + dev + type
  size_t total = 8 + b + (proto == 0x02 ? 0 : 2);
  if (total > cap) return 0;
  out[0] = 0x00;
  out[1] = 0x01;
  out[2] = 0x00;
  out[3] = proto;
  out[4] = static_cast<uint8_t>(len >> 8);
  out[5] = static_cast<uint8_t>(len);
  out[6] = 0x01;
  out[7] = 0x18;
  memcpy(out + 8, body, b);
  if (proto == 0x02) return 8 + b;
  pv_xor(out, 8 + b);
  uint16_t c = pv_crc16(out, 8 + b);
  out[8 + b] = static_cast<uint8_t>(c >> 8);
  out[9 + b] = static_cast<uint8_t>(c);
  return total;
}

bool pv_stale(int64_t now, int64_t last, int32_t interval_s) {
  if (last <= 0) return true;
  int64_t iv = interval_s > 0 ? interval_s : 300;
  int64_t lim = iv * 5 / 2;
  if (lim < 150) lim = 150;
  return now - last > lim;
}
