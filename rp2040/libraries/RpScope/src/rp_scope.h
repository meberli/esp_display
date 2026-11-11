// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Michael Eberli
// Playground scope: ESP32-S3 <-> RP2040 protocol and capture helpers.
// Shared verbatim by the RP2040 sketch (rp2040/indicator_rp2040_scope), the
// ESP32 firmware (main/rp_link.cpp, main/scope_view.cpp) and the host tests
// (tools/test_scope.cpp). Pure C99, no hardware access, no allocation.
//
// Link (SenseCAP Indicator): ESP32-S3 GPIO19 (TX) / GPIO20 (RX) <-> RP2040
// GPIO17 (RX) / GPIO16 (TX), UART 115200 8N1, COBS frames each followed by
// one 0x00 delimiter (PacketSerial / Seeed reference framing). The first
// decoded byte is the packet type. Seeed's sensor packets 0xB0..0xB5 (float)
// stay unchanged.
//
// ESP32 -> RP2040 scope commands are ASCII text with type 'S' (0x53), e.g.
//   "S HELLO" | "S STOP" | "S ARM m=a ch=0 r=500000 n=4800 p=10 e=r l=2048 s=0 t=a w=456 o=200"
//   "S VIEW o=<first sample> n=<samples> w=<cols>"
// A printable command shorter than 160 bytes COBS-encodes without any byte
// >= 0xA1 (code byte = length + 1), so the scope never puts 0xA3 (Seeed's
// PKT_TYPE_CMD_SHUTDOWN) on the wire. rps_cmd_frame() enforces this.
//
// RP2040 -> ESP32 replies are binary little-endian (see rps_put_* / rps_parse_*).
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RPS_PROTO_VER 1

#define RPS_T_CMD 0x53     /* 'S': ESP32 -> RP2040 ASCII command */
#define RPS_T_CAPS 0xC0    /* RP2040 -> ESP32: capabilities (reply to HELLO) */
#define RPS_T_STATUS 0xC1  /* capture state changes */
#define RPS_T_ANALOG 0xC2  /* analog capture (decimated) */
#define RPS_T_LOGIC 0xC3   /* logic capture (decimated) */
#define RPS_T_ERR 0xC4     /* text error */
#define RPS_FORBIDDEN_BYTE 0xA3 /* never sent by the ESP32 scope code */

/* Hardware limits of the RP2040 implementation (also sent in CAPS). */
#define RPS_ADC_CLK_HZ 48000000u
#define RPS_ADC_MAX_RATE 500000u      /* conversions/s, shared by the channels */
#define RPS_ANALOG_RING 65536u        /* uint16 samples (128 KiB, power of two) */
#define RPS_ANALOG_MAX_N 61440u       /* samples per capture (all channels) */
#define RPS_LOGIC_RING 131072u        /* uint8 samples (same 128 KiB buffer) */
#define RPS_LOGIC_MAX_N 126976u
#define RPS_LOGIC_CH 4                /* D0 GPIO20, D1 GPIO21, D2 GPIO26, D3 GPIO27 */
#define RPS_LOGIC_BASE_GPIO 20        /* PIO samples GPIO20..27, one byte per sample */
#define RPS_MAX_COLS 480
#define RPS_MAX_FRAME (1 + 27 + 2 * 10 + 2 * RPS_MAX_COLS * 2 + 16)
#define RPS_MAX_CMD 159

/* Bit of channel c (0..3) inside one logic sample byte (GPIO20..27). */
static inline uint8_t rps_logic_bit(int c) { return (uint8_t)(c < 2 ? (1u << c) : (1u << (c + 4))); }
static inline int rps_logic_gpio(int c) { return c < 2 ? 20 + c : 24 + c; }

/* ---- COBS ---- */
size_t rps_cobs_encode(const uint8_t* in, size_t n, uint8_t* out, size_t cap); /* no trailing 0; 0 = no room */
size_t rps_cobs_decode(const uint8_t* in, size_t n, uint8_t* out, size_t cap); /* 0 = malformed / no room */

/* ESP32 side: wrap an ASCII command (without the 'S') into a wire frame
 * (COBS + 0x00). Returns the frame length, 0 if the text is too long, not
 * printable ASCII, or the frame would contain 0xA3. */
size_t rps_cmd_frame(const char* text, uint8_t* out, size_t cap);

/* RP2040 side: parse "KEY a=1 b=x ..." into key + integer/char fields. */
typedef struct {
  char key[8];   /* HELLO, ARM, VIEW, STOP */
  char m, e, t;  /* mode a/l, edge r/f/n, trigger mode a/n */
  int32_t ch, r, n, p, l, s, w, o;
} rps_cmd_t;
int rps_cmd_parse(const char* text, size_t len, rps_cmd_t* out); /* 1 ok */

/* ---- capture frames ---- */
#define RPS_F_TRIGGERED 0x01
#define RPS_F_AUTO 0x02     /* auto mode, no trigger in time: free-running capture */
#define RPS_F_OVERRUN 0x04  /* samples were lost while scanning (trigger search skipped ahead) */
#define RPS_F_VIEW 0x08     /* re-sent from the stored capture (zoom / pan) */
#define RPS_F_TIMEOUT 0x10  /* normal mode, no trigger in time: no data */

typedef struct {
  uint16_t vmin, vmax, vmean; /* 12-bit ADC counts */
  float freq_hz;              /* 0 = not measurable */
} rps_ameas_t;

typedef struct {
  uint32_t rising;   /* rising edges in the capture */
  float freq_hz;     /* 0 = not measurable */
  uint16_t duty_pm;  /* high time per mille between first and last rising edge */
  uint8_t level0;    /* level of the first sample */
  uint8_t level_t;   /* level at the trigger sample */
} rps_lmeas_t;

typedef struct {
  uint8_t type;      /* RPS_T_ANALOG / RPS_T_LOGIC */
  uint8_t ver, seq, flags;
  uint8_t chmode;    /* analog: 0 GPIO26, 1 GPIO27, 2 both; logic: RPS_LOGIC_CH */
  uint16_t cols;     /* data columns */
  uint32_t rate;     /* samples/s per channel */
  uint32_t n;        /* samples per channel in the capture */
  uint32_t pre;      /* trigger position (sample index per channel) */
  int32_t view_off;  /* first sample of the columns (per channel) */
  uint32_t view_n;   /* samples covered by the columns */
  uint8_t nch;       /* analog channels in this frame (1/2), logic 4 */
  rps_ameas_t am[2];
  rps_lmeas_t lm[RPS_LOGIC_CH];
  /* analog: amin/amax[ch][col] (12-bit >> 4); logic: lbits[col], bit 2c = low seen, 2c+1 = high seen */
  uint8_t amin[2][RPS_MAX_COLS], amax[2][RPS_MAX_COLS];
  uint8_t lbits[RPS_MAX_COLS];
} rps_frame_t;

size_t rps_put_frame(const rps_frame_t* f, uint8_t* out, size_t cap);   /* raw (un-COBSed) packet */
int rps_parse_frame(const uint8_t* p, size_t n, rps_frame_t* f);        /* 1 ok */

typedef struct {
  uint8_t ver;
  uint32_t sys_hz, adc_max_rate, logic_max_rate, analog_max_n, logic_max_n;
  uint8_t logic_ch;
  char fw[13];
} rps_caps_t;
size_t rps_put_caps(const rps_caps_t* c, uint8_t* out, size_t cap);
int rps_parse_caps(const uint8_t* p, size_t n, rps_caps_t* c);

/* ---- capture helpers (ring buffers, power-of-two size) ---- */
/* Analog edge trigger with hysteresis: arm below lvl-hyst (rising) /
 * above lvl+hyst (falling), fire on reaching lvl. */
typedef struct {
  uint16_t lvl, hyst;
  uint8_t rising, armed;
} rps_atrig_t;
void rps_atrig_init(rps_atrig_t* t, uint16_t lvl, uint16_t hyst, int rising);
static inline int rps_atrig_step(rps_atrig_t* t, uint16_t v) {
  if (t->rising) {
    if (v + t->hyst < t->lvl) t->armed = 1;
    else if (t->armed && v >= t->lvl) { t->armed = 0; return 1; }
  } else {
    if (v > (uint32_t)t->lvl + t->hyst) t->armed = 1;
    else if (t->armed && v <= t->lvl) { t->armed = 0; return 1; }
  }
  return 0;
}

/* Samples are ring[(start + i*stride) & mask], i in [0, n). */
void rps_ameasure(const uint16_t* ring, uint32_t mask, uint32_t start, uint32_t n, uint32_t stride, uint32_t rate,
                  rps_ameas_t* out);
/* Decimate to cols min/max columns (cols <= n, else one column per sample). Returns cols used. */
uint16_t rps_adecimate(const uint16_t* ring, uint32_t mask, uint32_t start, uint32_t n, uint32_t stride,
                       uint16_t cols, uint8_t* mn, uint8_t* mx);
void rps_lmeasure(const uint8_t* ring, uint32_t mask, uint32_t start, uint32_t n, uint32_t rate, uint32_t trig,
                  rps_lmeas_t out[RPS_LOGIC_CH]);
uint16_t rps_ldecimate(const uint8_t* ring, uint32_t mask, uint32_t start, uint32_t n, uint16_t cols, uint8_t* bits);
/* Exact logic trigger: the first matching edge (sample i with i-1 on the
 * other level) of channel bit `bit` in [from, to) (global sample indices,
 * ring masked); -1 if none. The PIO trigger fires on the first such edge
 * after it was armed, so `from` = total written just before arming. */
int64_t rps_lfind_edge(const uint8_t* ring, uint32_t mask, int64_t from, int64_t to, uint8_t bit, int rising);

#ifdef __cplusplus
}
#endif
