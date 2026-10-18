#pragma once
// Minimal Growatt datalogger server protocol (old ShineLan / ShineWiFi), pure
// logic, host tested in tools/test_pv.cpp. Follows johanmeijer/grott
// (grottserver.py, grottdata.py, grottconf.py layouts T02NNNN / T05NNNN[X] / T06NNNN[X]).
//
// Record: 8-byte header  seq(2) 00 proto(1) len(2) dev(1) type(1), then payload.
//  proto 02: plain, no CRC.  proto 05/06: payload XOR "Growatt" (from byte 8),
//  CRC16/Modbus (big endian) appended. Total length = 6 + len (+2 CRC if not 02).
// Types: 0x16 ping (echoed), 0x03 announce, 0x04 data, 0x50 buffered data,
//  0x1b/0x20 other data (acked), 0x29 (no answer).

#include <stddef.h>
#include <stdint.h>

struct PvReading {
  bool valid;            // fields decoded from a 0x04 live record
  float pv_w;            // DC input power (pvpowerin)
  float pv1_v, pv1_a, pv1_w;
  float pv2_v, pv2_a, pv2_w;
  float ac_w;            // AC output power (pvpowerout)
  float grid_hz;
  float grid_v[3];       // L1..L3
  float today_kwh, total_kwh;
  float temp_c;
  int status;            // pvstatus: 0 waiting, 1 normal, 3 fault
  char logger[11];       // datalogger serial (ascii)
  char inverter[11];     // inverter serial (ascii)
};

enum PvRecType : uint8_t {
  kPvPing = 0x16, kPvAnnounce = 0x03, kPvData = 0x04, kPvBuffered = 0x50, kPvNoAnswer = 0x29,
};

struct PvFrame {
  uint8_t proto;   // 0x02 / 0x05 / 0x06
  uint8_t dev, type;
  size_t total;    // bytes of this frame in the stream
};

// Length of the first complete frame in buf, 0 = need more, -1 = garbage (resync).
int pv_frame_length(const uint8_t* buf, size_t n);
uint16_t pv_crc16(const uint8_t* d, size_t n);  // Modbus
void pv_xor(uint8_t* d, size_t n);              // XOR "Growatt" from byte 8 (in place, symmetric)

// Handle one complete frame (in place: may be descrambled). Writes the reply
// into out (cap >= 64) and returns its length (0 = no reply). Sets *frame and,
// for live data records, *reading (valid = true). Returns -1 if the CRC is bad.
int pv_handle(uint8_t* rec, size_t n, PvFrame* frame, PvReading* reading, uint8_t* out, size_t cap);

// Time-sync command (register 31 = "YYYY-MM-DD HH:MM:SS" local time) after 0x03.
size_t pv_time_command(uint8_t proto, const char* logger, const char* datetime, uint8_t* out, size_t cap);

// Stale: no live record for more than 2.5 x the observed send interval
// (interval unknown: 5 min assumed; at least 150 s).
bool pv_stale(int64_t now, int64_t last, int32_t interval_s);
