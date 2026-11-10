// See scope_rp2040.h.
//
// Buffer: one static 128 KiB ring, either 65536 x uint16 ADC samples or
// 131072 x uint8 logic samples (GPIO20..27, one byte per sample). A data DMA
// channel fills the ring and chains to a control channel that rewrites its
// write address, so capture runs continuously without IRQs; the CPU polls the
// write pointer (lap counting) for pre-trigger, trigger and post-trigger.
//
// Analog: ADC free-running, 500 kS/s max (48 MHz / 96), GPIO26 (ADC0) and/or
//   GPIO27 (ADC1, round robin halves the rate per channel). Software edge
//   trigger with hysteresis on the selected channel.
// Logic: PIO "in pins, 8" at clk_sys / div (div >= 2: 100 MS/s at 200 MHz),
//   autopush 32 bit, RX FIFO joined. Hardware edge trigger: a second state
//   machine waits for the edge at full clk_sys and raises PIO IRQ 0; the exact
//   sample is then found in the data (first matching edge after arming).
// The pins are only read (no pin function change), so I2C on GPIO20/21 keeps
// working and can be sniffed.
#include "scope_rp2040.h"

#include <Arduino.h>
#include <string.h>

#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "rp_scope.h"

namespace {

uint32_t g_buf32[RPS_LOGIC_RING / 4] __attribute__((aligned(4)));
uint16_t* const g_a = reinterpret_cast<uint16_t*>(g_buf32);
uint8_t* const g_l = reinterpret_cast<uint8_t*>(g_buf32);
volatile uint32_t g_reload;  // ring start, source of the control DMA

scope_send_fn g_send = nullptr;
int g_dch = -1, g_cch = -1;
PIO g_pio = nullptr;
int g_sm_cap = -1, g_sm_trig = -1;
int g_off_cap = -1, g_off_trig = -1;
uint16_t g_prog_cap[1];
uint16_t g_prog_trig[4];
pio_program_t g_pp_cap = {};   // filled in logic_start()
pio_program_t g_pp_trig = {};

enum class St : uint8_t { Idle, Pre, Armed, Post };
St g_st = St::Idle;
uint32_t g_last_cmd_ms = 0;
uint8_t g_seq = 0;

struct Cfg {
  bool logic;
  int nch;        // analog 1/2, logic 4
  int chsel;      // analog 0/1/2
  uint32_t rate;  // per channel (actual)
  uint32_t n_tot; // raw samples in the window (all channels)
  uint32_t pre_tot;
  char edge;      // r/f/n
  char tmode;     // a/n
  int src;
  uint16_t lvl;
  uint16_t cols;
  uint32_t timeout_ms;
} g_c;

// Ring progress.
uint32_t g_ring = 0, g_ssize = 1, g_lap = 0, g_lastpos = 0;
uint64_t g_total = 0, g_scanned = 0, g_trig = 0, g_arm_total = 0;
uint32_t g_t0 = 0;
uint8_t g_flags = 0;
rps_atrig_t g_at;

// Last capture (for VIEW).
bool g_have = false;
uint64_t g_win = 0;  // global raw index of the window start

void update_total() {
  uint32_t p = (dma_hw->ch[g_dch].write_addr - reinterpret_cast<uint32_t>(g_buf32)) / g_ssize;
  if (p >= g_ring) p -= g_ring;
  if (p < g_lastpos) ++g_lap;
  g_lastpos = p;
  g_total = static_cast<uint64_t>(g_lap) * g_ring + p;
}

void send_raw(const uint8_t* p, size_t n) {
  if (g_send != nullptr) g_send(p, n);
}

void send_status(uint8_t st, uint8_t flags) {
  uint8_t b[4] = {RPS_T_STATUS, RPS_PROTO_VER, st, flags};
  send_raw(b, sizeof(b));
}

void send_err(const char* t) {
  uint8_t b[64];
  b[0] = RPS_T_ERR;
  size_t n = strnlen(t, sizeof(b) - 1);
  memcpy(b + 1, t, n);
  send_raw(b, n + 1);
}

void dma_setup(uint32_t ring_bytes, enum dma_channel_transfer_size sz, const volatile void* src, uint dreq) {
  g_reload = reinterpret_cast<uint32_t>(g_buf32);
  dma_channel_config d = dma_channel_get_default_config(g_dch);
  channel_config_set_transfer_data_size(&d, sz);
  channel_config_set_read_increment(&d, false);
  channel_config_set_write_increment(&d, true);
  channel_config_set_dreq(&d, dreq);
  channel_config_set_chain_to(&d, g_cch);
  uint32_t unit = sz == DMA_SIZE_32 ? 4 : sz == DMA_SIZE_16 ? 2 : 1;
  dma_channel_configure(g_dch, &d, g_buf32, src, ring_bytes / unit, false);
  dma_channel_config c = dma_channel_get_default_config(g_cch);
  channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
  channel_config_set_read_increment(&c, false);
  channel_config_set_write_increment(&c, false);
  dma_channel_configure(g_cch, &c, &dma_hw->ch[g_dch].al2_write_addr_trig, &g_reload, 1, false);
}

void dma_stop() {
  // Break the chain first so an abort cannot re-trigger the data channel.
  dma_channel_config d = dma_get_channel_config(g_dch);
  channel_config_set_chain_to(&d, g_dch);
  dma_channel_set_config(g_dch, &d, false);
  dma_channel_abort(g_cch);
  dma_channel_abort(g_dch);
}

void reset_progress(uint32_t ring, uint32_t ssize) {
  g_ring = ring;
  g_ssize = ssize;
  g_lap = 0;
  g_lastpos = 0;
  g_total = g_scanned = g_trig = g_arm_total = 0;
  g_flags = 0;
  g_t0 = millis();
}

// ---------------- analog ----------------
void analog_start() {
  adc_run(false);
  adc_fifo_drain();
  adc_gpio_init(26);
  adc_gpio_init(27);
  adc_select_input(g_c.chsel == 1 ? 1 : 0);
  adc_set_round_robin(g_c.nch == 2 ? 0x3 : 0);
  adc_fifo_setup(true, true, 1, false, false);
  uint32_t tot = g_c.rate * g_c.nch;
  uint32_t clk = clock_get_hz(clk_adc);
  float div = static_cast<float>(clk) / static_cast<float>(tot) - 1.0f;
  if (div < 95.0f) div = 0.0f;
  if (div > 65535.0f) div = 65535.0f;
  adc_set_clkdiv(div);
  float period = div == 0.0f ? 96.0f : div + 1.0f;
  g_c.rate = static_cast<uint32_t>(static_cast<float>(clk) / period / g_c.nch + 0.5f);
  reset_progress(RPS_ANALOG_RING, 2);
  dma_setup(RPS_ANALOG_RING * 2, DMA_SIZE_16, &adc_hw->fifo, DREQ_ADC);
  dma_channel_start(g_dch);
  adc_run(true);
}

void analog_stop() {
  adc_run(false);
  update_total();
  dma_stop();
  adc_fifo_setup(false, false, 1, false, false);
  adc_fifo_drain();
  adc_set_round_robin(0);
}

// ---------------- logic ----------------
void pio_release() {
  if (g_pio == nullptr) return;
  if (g_sm_trig >= 0) pio_sm_set_enabled(g_pio, g_sm_trig, false);
  if (g_sm_cap >= 0) pio_sm_set_enabled(g_pio, g_sm_cap, false);
  if (g_off_trig >= 0) pio_remove_program(g_pio, &g_pp_trig, g_off_trig);
  if (g_off_cap >= 0) pio_remove_program(g_pio, &g_pp_cap, g_off_cap);
  g_off_trig = g_off_cap = -1;
  pio_interrupt_clear(g_pio, 0);
}

bool logic_start() {
  if (g_pio == nullptr) {
    PIO pios[2] = {pio1, pio0};
    for (PIO p : pios) {
      int a = pio_claim_unused_sm(p, false);
      if (a < 0) continue;
      int b = pio_claim_unused_sm(p, false);
      if (b < 0) {
        pio_sm_unclaim(p, a);
        continue;
      }
      g_pio = p;
      g_sm_cap = a;
      g_sm_trig = b;
      break;
    }
    if (g_pio == nullptr) return false;
  }
  gpio_set_input_enabled(26, true);
  gpio_set_input_enabled(27, true);
  uint32_t sys = clock_get_hz(clk_sys);
  uint32_t min_div = (sys + 100000000u - 1) / 100000000u;  // <= 100 MS/s
  if (min_div < 1) min_div = 1;
  uint32_t div = (sys + g_c.rate / 2) / (g_c.rate == 0 ? 1 : g_c.rate);
  if (div < min_div) div = min_div;
  if (div > 65535) div = 65535;
  g_c.rate = sys / div;

  g_pp_cap.instructions = g_prog_cap;
  g_pp_cap.length = 1;
  g_pp_cap.origin = -1;
  g_pp_trig.instructions = g_prog_trig;
  g_pp_trig.length = 4;
  g_pp_trig.origin = -1;
  g_prog_cap[0] = pio_encode_in(pio_pins, 8);
  g_off_cap = pio_add_program(g_pio, &g_pp_cap);
  pio_sm_config c = pio_get_default_sm_config();
  sm_config_set_in_pins(&c, RPS_LOGIC_BASE_GPIO);
  sm_config_set_wrap(&c, g_off_cap, g_off_cap);
  sm_config_set_in_shift(&c, true, true, 32);  // first sample in the low byte
  sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
  sm_config_set_clkdiv_int_frac(&c, static_cast<uint16_t>(div), 0);
  pio_sm_init(g_pio, g_sm_cap, g_off_cap, &c);

  if (g_c.edge != 'n') {
    uint gpio = static_cast<uint>(rps_logic_gpio(g_c.src));
    bool rising = g_c.edge == 'r';
    g_prog_trig[0] = pio_encode_wait_gpio(!rising, gpio);
    g_prog_trig[1] = pio_encode_wait_gpio(rising, gpio);
    g_prog_trig[2] = pio_encode_irq_set(false, 0);
    g_prog_trig[3] = pio_encode_jmp(3);  // relocated by pio_add_program: park here
    g_off_trig = pio_add_program(g_pio, &g_pp_trig);
    pio_sm_config t = pio_get_default_sm_config();
    sm_config_set_wrap(&t, g_off_trig, g_off_trig + 3);
    sm_config_set_clkdiv_int_frac(&t, 1, 0);
    pio_sm_init(g_pio, g_sm_trig, g_off_trig, &t);
  }
  pio_interrupt_clear(g_pio, 0);
  reset_progress(RPS_LOGIC_RING, 1);
  dma_setup(RPS_LOGIC_RING, DMA_SIZE_32, &g_pio->rxf[g_sm_cap], pio_get_dreq(g_pio, g_sm_cap, false));
  dma_channel_start(g_dch);
  pio_sm_set_enabled(g_pio, g_sm_cap, true);
  return true;
}

void logic_stop() {
  pio_sm_set_enabled(g_pio, g_sm_cap, false);
  if (g_sm_trig >= 0) pio_sm_set_enabled(g_pio, g_sm_trig, false);
  delayMicroseconds(2);  // let the DMA drain the RX FIFO
  update_total();
  dma_stop();
  pio_sm_clear_fifos(g_pio, g_sm_cap);
  pio_release();
}

// ---------------- frames ----------------
rps_frame_t g_f;
uint8_t g_out[RPS_MAX_FRAME];

void send_capture(uint8_t flags, uint32_t view_off, uint32_t view_n, uint16_t cols) {
  memset(&g_f, 0, offsetof(rps_frame_t, amin));
  g_f.seq = g_seq++;
  g_f.flags = flags;
  uint32_t n_ch = g_c.n_tot / g_c.nch;
  uint32_t pre_ch = static_cast<uint32_t>((g_trig - g_win) / g_c.nch);
  if (view_off >= n_ch) view_off = 0;
  if (view_n == 0 || view_off + view_n > n_ch) view_n = n_ch - view_off;
  g_f.rate = g_c.rate;
  g_f.n = n_ch;
  g_f.pre = pre_ch;
  g_f.view_off = static_cast<int32_t>(view_off);
  g_f.view_n = view_n;
  uint32_t mask = g_ring - 1;
  if (g_c.logic) {
    g_f.type = RPS_T_LOGIC;
    g_f.chmode = RPS_LOGIC_CH;
    g_f.nch = RPS_LOGIC_CH;
    uint32_t s = static_cast<uint32_t>(g_win & mask);
    rps_lmeasure(g_l, mask, s, n_ch, g_c.rate, pre_ch, g_f.lm);
    g_f.cols = rps_ldecimate(g_l, mask, s + view_off, view_n, cols, g_f.lbits);
  } else {
    g_f.type = RPS_T_ANALOG;
    g_f.chmode = static_cast<uint8_t>(g_c.chsel);
    g_f.nch = static_cast<uint8_t>(g_c.nch);
    uint32_t stride = static_cast<uint32_t>(g_c.nch);
    for (int c = 0; c < g_c.nch; ++c) {
      uint32_t s = static_cast<uint32_t>((g_win + c) & mask);
      rps_ameasure(g_a, mask, s, n_ch, stride, g_c.rate, &g_f.am[c]);
      g_f.cols = rps_adecimate(g_a, mask, (s + view_off * stride) & mask, view_n, stride, cols, g_f.amin[c],
                               g_f.amax[c]);
    }
  }
  size_t n = rps_put_frame(&g_f, g_out, sizeof(g_out));
  if (n > 0) send_raw(g_out, n);
}

void stop_capture() {
  if (g_st == St::Idle) return;
  if (g_c.logic) logic_stop();
  else analog_stop();
  g_st = St::Idle;
}

void finish(uint8_t flags) {
  stop_capture();
  // The window must still be in the ring (not overwritten by the tail).
  if (g_total > g_win + g_ring) {
    g_have = false;
    send_err("overrun");
    send_status(0, RPS_F_OVERRUN);
    return;
  }
  g_have = true;
  send_capture(flags, 0, 0, g_c.cols);
  send_status(0, flags);
}

void set_trigger(uint64_t t) {
  g_trig = t;
  uint64_t win = t >= g_c.pre_tot ? t - g_c.pre_tot : 0;
  if (g_c.nch == 2) win &= ~1ull;  // window starts on GPIO26 (round robin order)
  g_win = win;
  g_st = St::Post;
}

void arm(const rps_cmd_t& k) {
  stop_capture();
  g_have = false;
  g_c.logic = k.m == 'l';
  g_c.edge = (k.e == 'r' || k.e == 'f') ? k.e : 'n';
  g_c.tmode = k.t == 'n' ? 'n' : 'a';
  g_c.cols = static_cast<uint16_t>(k.w < 16 ? 16 : k.w > RPS_MAX_COLS ? RPS_MAX_COLS : k.w);
  g_c.timeout_ms = static_cast<uint32_t>(k.o < 10 ? 10 : k.o > 5000 ? 5000 : k.o);
  int32_t pre_pct = k.p < 0 ? 0 : k.p > 90 ? 90 : k.p;
  uint32_t rate = static_cast<uint32_t>(k.r < 1 ? 1 : k.r);
  if (g_c.logic) {
    g_c.nch = 1;  // one byte per sample carries all four channels
    g_c.chsel = 0;
    g_c.src = k.s < 0 ? 0 : k.s > 3 ? 3 : static_cast<int>(k.s);
    uint32_t n = static_cast<uint32_t>(k.n < 64 ? 64 : k.n);
    if (n > RPS_LOGIC_MAX_N) n = RPS_LOGIC_MAX_N;
    g_c.n_tot = n & ~3u;
    g_c.rate = rate;
    g_c.pre_tot = static_cast<uint32_t>((static_cast<uint64_t>(g_c.n_tot) * pre_pct) / 100);
    if (!logic_start()) {
      send_err("no PIO state machine");
      return;
    }
  } else {
    g_c.chsel = k.ch < 0 ? 0 : k.ch > 2 ? 2 : static_cast<int>(k.ch);
    g_c.nch = g_c.chsel == 2 ? 2 : 1;
    g_c.src = g_c.nch == 2 ? (k.s == 1 ? 1 : 0) : 0;
    uint32_t n_ch = static_cast<uint32_t>(k.n < 16 ? 16 : k.n);
    if (n_ch * g_c.nch > RPS_ANALOG_MAX_N) n_ch = RPS_ANALOG_MAX_N / g_c.nch;
    g_c.n_tot = n_ch * g_c.nch;
    if (rate * g_c.nch > RPS_ADC_MAX_RATE) rate = RPS_ADC_MAX_RATE / g_c.nch;
    g_c.rate = rate;
    g_c.pre_tot = static_cast<uint32_t>((static_cast<uint64_t>(n_ch) * pre_pct) / 100) * g_c.nch;
    uint16_t lvl = static_cast<uint16_t>(k.l < 0 ? 0 : k.l > 4095 ? 4095 : k.l);
    rps_atrig_init(&g_at, lvl, 24, g_c.edge != 'f');  // ~20 mV hysteresis
    analog_start();
  }
  g_st = St::Pre;
  send_status(1, 0);
}

void send_caps() {
  rps_caps_t c;
  memset(&c, 0, sizeof(c));
  c.sys_hz = clock_get_hz(clk_sys);
  uint32_t min_div = (c.sys_hz + 100000000u - 1) / 100000000u;
  c.adc_max_rate = RPS_ADC_MAX_RATE;
  c.logic_max_rate = c.sys_hz / (min_div ? min_div : 1);
  c.analog_max_n = RPS_ANALOG_MAX_N;
  c.logic_max_n = RPS_LOGIC_MAX_N;
  c.logic_ch = RPS_LOGIC_CH;
  strncpy(c.fw, "scope-1.0", sizeof(c.fw) - 1);
  uint8_t b[48];
  size_t n = rps_put_caps(&c, b, sizeof(b));
  if (n) send_raw(b, n);
}

// Analog: scan new samples of the trigger channel.
void analog_scan() {
  uint64_t from = g_scanned;
  if (from < g_c.pre_tot) from = g_c.pre_tot;
  // Fell behind (e.g. long USB print): skip ahead, keep a full window possible.
  if (g_total > from + g_ring / 2) {
    from = g_total - g_ring / 4;
    g_flags |= RPS_F_OVERRUN;
  }
  uint32_t mask = g_ring - 1;
  int par = g_c.nch == 2 ? g_c.src : -1;
  for (uint64_t i = from; i < g_total; ++i) {
    if (par >= 0 && static_cast<int>(i & 1) != par) continue;
    if (rps_atrig_step(&g_at, g_a[i & mask] & 0x0FFF)) {
      g_scanned = i + 1;
      g_flags |= RPS_F_TRIGGERED;
      set_trigger(i);
      return;
    }
  }
  g_scanned = g_total;
}

}  // namespace

void scope_begin(scope_send_fn send) {
  g_send = send;
  if (g_dch < 0) g_dch = dma_claim_unused_channel(true);
  if (g_cch < 0) g_cch = dma_claim_unused_channel(true);
}

bool scope_recently_used(uint32_t ms) { return g_last_cmd_ms != 0 && millis() - g_last_cmd_ms < ms; }

void scope_command(const uint8_t* text, size_t len) {
  rps_cmd_t k;
  g_last_cmd_ms = millis();
  if (!rps_cmd_parse(reinterpret_cast<const char*>(text), len, &k)) return;
  if (!strcmp(k.key, "HELLO")) {
    send_caps();
  } else if (!strcmp(k.key, "STOP")) {
    stop_capture();
    send_status(0, 0);
  } else if (!strcmp(k.key, "ARM")) {
    arm(k);
  } else if (!strcmp(k.key, "VIEW")) {
    if (g_st != St::Idle || !g_have) {
      send_err("no capture");
      return;
    }
    uint32_t off = static_cast<uint32_t>(k.o < 0 ? 0 : k.o);
    uint32_t n = static_cast<uint32_t>(k.n < 0 ? 0 : k.n);
    uint16_t cols = static_cast<uint16_t>(k.w < 16 ? 16 : k.w > RPS_MAX_COLS ? RPS_MAX_COLS : k.w);
    send_capture(RPS_F_VIEW, off, n, cols);
  }
}

bool scope_poll() {
  if (g_st == St::Idle) return false;
  update_total();
  uint32_t now = millis();
  if (g_st == St::Pre) {
    if (g_total < g_c.pre_tot) return true;
    if (g_c.edge == 'n') {
      set_trigger(g_total);
    } else {
      g_st = St::Armed;
      g_t0 = now;
      if (g_c.logic) {
        g_arm_total = g_total;  // read before enabling: lower bound of the edge
        pio_sm_set_enabled(g_pio, g_sm_trig, true);
      } else {
        g_scanned = g_total;
      }
    }
    return true;
  }
  if (g_st == St::Armed) {
    if (g_c.logic) {
      if (pio_interrupt_get(g_pio, 0)) {
        update_total();
        g_flags |= RPS_F_TRIGGERED;
        set_trigger(g_total);  // refined after the stop
        return true;
      }
    } else {
      analog_scan();
      if (g_st != St::Armed) return true;
    }
    if (now - g_t0 >= g_c.timeout_ms) {
      if (g_c.tmode == 'a') {
        g_flags |= RPS_F_AUTO;
        set_trigger(g_total);
      } else {
        stop_capture();
        send_status(0, RPS_F_TIMEOUT);
      }
    }
    return true;
  }
  // Post: until the window (plus slack for the logic refinement) is written.
  uint64_t post = g_c.n_tot - g_c.pre_tot;
  uint64_t slack = g_c.logic && (g_flags & RPS_F_TRIGGERED) ? 1024 : 0;
  if (g_total < g_trig + post + slack) return true;
  if (g_c.logic && (g_flags & RPS_F_TRIGGERED)) {
    stop_capture();
    uint64_t lo = g_total > g_ring - 4096 ? g_total - (g_ring - 4096) : 0;
    uint64_t from = g_arm_total > lo ? g_arm_total : lo;
    int64_t e = rps_lfind_edge(g_l, g_ring - 1, static_cast<int64_t>(from), static_cast<int64_t>(g_trig + slack),
                               rps_logic_bit(g_c.src), g_c.edge == 'r');
    if (e >= 0) {
      g_trig = static_cast<uint64_t>(e);
      g_win = g_trig >= g_c.pre_tot ? g_trig - g_c.pre_tot : 0;
    }
    if (g_total > g_win + g_ring || g_win + g_c.n_tot > g_total) {
      g_have = false;
      send_err("overrun");
      send_status(0, RPS_F_OVERRUN);
      return false;
    }
    g_have = true;
    send_capture(g_flags, 0, 0, g_c.cols);
    send_status(0, g_flags);
    return false;
  }
  finish(g_flags);
  return false;
}
