// See rp_scope.h. Pure C99: shared by the RP2040 sketch, the ESP32 firmware
// and the host tests.
#include "rp_scope.h"

#include <stdlib.h>
#include <string.h>

/* ---------------- COBS ---------------- */
size_t rps_cobs_encode(const uint8_t* in, size_t n, uint8_t* out, size_t cap) {
  size_t o = 1, code_at = 0;
  uint8_t code = 1;
  if (cap == 0) return 0;
  for (size_t i = 0; i < n; ++i) {
    if (in[i] == 0) {
      out[code_at] = code;
      code_at = o++;
      if (o > cap) return 0;
      code = 1;
    } else {
      if (o >= cap) return 0;
      out[o++] = in[i];
      if (++code == 0xFF) {
        out[code_at] = code;
        code_at = o++;
        if (o > cap) return 0;
        code = 1;
      }
    }
  }
  out[code_at] = code;
  return o;
}

size_t rps_cobs_decode(const uint8_t* in, size_t n, uint8_t* out, size_t cap) {
  size_t i = 0, o = 0;
  while (i < n) {
    uint8_t code = in[i++];
    if (code == 0) return 0;
    for (uint8_t k = 1; k < code; ++k) {
      if (i >= n || o >= cap || in[i] == 0) return 0;
      out[o++] = in[i++];
    }
    if (code != 0xFF && i < n) {
      if (o >= cap) return 0;
      out[o++] = 0;
    }
  }
  return o;
}

size_t rps_cmd_frame(const char* text, uint8_t* out, size_t cap) {
  uint8_t raw[RPS_MAX_CMD + 1];
  size_t len = strlen(text);
  if (len + 2 > RPS_MAX_CMD) return 0;
  raw[0] = RPS_T_CMD;
  raw[1] = ' ';
  for (size_t i = 0; i < len; ++i) {
    unsigned char c = (unsigned char)text[i];
    if (c < 0x20 || c > 0x7E) return 0;
    raw[2 + i] = c;
  }
  if (cap < len + 4) return 0;
  size_t n = rps_cobs_encode(raw, len + 2, out, cap - 1);
  if (n == 0) return 0;
  out[n++] = 0;
  for (size_t i = 0; i < n; ++i)
    if (out[i] == RPS_FORBIDDEN_BYTE) return 0;
  return n;
}

int rps_cmd_parse(const char* text, size_t len, rps_cmd_t* c) {
  memset(c, 0, sizeof(*c));
  c->m = 'a'; c->e = 'r'; c->t = 'a';
  c->r = 500000; c->n = 4800; c->p = 10; c->l = 2048; c->w = 456; c->o = 200;
  size_t i = 0;
  while (i < len && text[i] == ' ') ++i;
  size_t k = 0;
  while (i < len && text[i] != ' ' && k + 1 < sizeof(c->key)) c->key[k++] = text[i++];
  c->key[k] = 0;
  if (k == 0) return 0;
  while (i < len) {
    while (i < len && text[i] == ' ') ++i;
    if (i + 2 > len) break;
    char name[4] = {0};
    size_t nk = 0;
    while (i < len && text[i] != '=' && text[i] != ' ' && nk < 3) name[nk++] = text[i++];
    if (i >= len || text[i] != '=') { while (i < len && text[i] != ' ') ++i; continue; }
    ++i;
    char val[16] = {0};
    size_t nv = 0;
    while (i < len && text[i] != ' ' && nv + 1 < sizeof(val)) val[nv++] = text[i++];
    while (i < len && text[i] != ' ') ++i;
    long v = strtol(val, NULL, 10);
    if (!strcmp(name, "m")) c->m = val[0];
    else if (!strcmp(name, "e")) c->e = val[0];
    else if (!strcmp(name, "t")) c->t = val[0];
    else if (!strcmp(name, "ch")) c->ch = (int32_t)v;
    else if (!strcmp(name, "r")) c->r = (int32_t)v;
    else if (!strcmp(name, "n")) c->n = (int32_t)v;
    else if (!strcmp(name, "p")) c->p = (int32_t)v;
    else if (!strcmp(name, "l")) c->l = (int32_t)v;
    else if (!strcmp(name, "s")) c->s = (int32_t)v;
    else if (!strcmp(name, "w")) c->w = (int32_t)v;
    else if (!strcmp(name, "o")) c->o = (int32_t)v;
  }
  return 1;
}

/* ---------------- little-endian helpers ---------------- */
static uint8_t* put16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); return p + 2; }
static uint8_t* put32(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
  return p + 4;
}
static uint8_t* putf(uint8_t* p, float f) {
  uint32_t v;
  memcpy(&v, &f, 4);
  return put32(p, v);
}
static uint16_t get16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t get32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static float getf(const uint8_t* p) {
  uint32_t v = get32(p);
  float f;
  memcpy(&f, &v, 4);
  return f;
}

#define RPS_HDR 27 /* bytes after the type byte, up to and including nch */
size_t rps_put_frame(const rps_frame_t* f, uint8_t* out, size_t cap) {
  int logic = f->type == RPS_T_LOGIC;
  int nch = logic ? RPS_LOGIC_CH : f->nch;
  uint16_t cols = f->cols > RPS_MAX_COLS ? RPS_MAX_COLS : f->cols;
  size_t need = 1 + RPS_HDR + (logic ? (size_t)nch * 12 + cols : (size_t)nch * 10 + (size_t)nch * cols * 2);
  if (need > cap || (!logic && (nch < 1 || nch > 2))) return 0;
  uint8_t* p = out;
  *p++ = f->type;
  *p++ = RPS_PROTO_VER;
  *p++ = f->seq;
  *p++ = f->flags;
  *p++ = f->chmode;
  p = put16(p, cols);
  p = put32(p, f->rate);
  p = put32(p, f->n);
  p = put32(p, f->pre);
  p = put32(p, (uint32_t)f->view_off);
  p = put32(p, f->view_n);
  *p++ = (uint8_t)nch;
  if (logic) {
    for (int c = 0; c < nch; ++c) {
      p = put32(p, f->lm[c].rising);
      p = putf(p, f->lm[c].freq_hz);
      p = put16(p, f->lm[c].duty_pm);
      *p++ = f->lm[c].level0;
      *p++ = f->lm[c].level_t;
    }
    memcpy(p, f->lbits, cols);
    p += cols;
  } else {
    for (int c = 0; c < nch; ++c) {
      p = put16(p, f->am[c].vmin);
      p = put16(p, f->am[c].vmax);
      p = put16(p, f->am[c].vmean);
      p = putf(p, f->am[c].freq_hz);
    }
    for (int c = 0; c < nch; ++c) {
      memcpy(p, f->amin[c], cols); p += cols;
      memcpy(p, f->amax[c], cols); p += cols;
    }
  }
  return (size_t)(p - out);
}

int rps_parse_frame(const uint8_t* p, size_t n, rps_frame_t* f) {
  if (n < 1 + RPS_HDR) return 0;
  if (p[0] != RPS_T_ANALOG && p[0] != RPS_T_LOGIC) return 0;
  memset(f, 0, offsetof(rps_frame_t, amin));
  f->type = p[0];
  f->ver = p[1];
  if (f->ver != RPS_PROTO_VER) return 0;
  f->seq = p[2];
  f->flags = p[3];
  f->chmode = p[4];
  f->cols = get16(p + 5);
  f->rate = get32(p + 7);
  f->n = get32(p + 11);
  f->pre = get32(p + 15);
  f->view_off = (int32_t)get32(p + 19);
  f->view_n = get32(p + 23);
  f->nch = p[27];
  const uint8_t* q = p + 1 + RPS_HDR;
  const uint8_t* end = p + n;
  if (f->cols > RPS_MAX_COLS) return 0;
  if (f->type == RPS_T_LOGIC) {
    if (f->nch != RPS_LOGIC_CH || (size_t)(end - q) < (size_t)RPS_LOGIC_CH * 12 + f->cols) return 0;
    for (int c = 0; c < RPS_LOGIC_CH; ++c, q += 12) {
      f->lm[c].rising = get32(q);
      f->lm[c].freq_hz = getf(q + 4);
      f->lm[c].duty_pm = get16(q + 8);
      f->lm[c].level0 = q[10];
      f->lm[c].level_t = q[11];
    }
    memcpy(f->lbits, q, f->cols);
  } else {
    if (f->nch < 1 || f->nch > 2 || (size_t)(end - q) < (size_t)f->nch * (10 + 2u * f->cols)) return 0;
    for (int c = 0; c < f->nch; ++c, q += 10) {
      f->am[c].vmin = get16(q);
      f->am[c].vmax = get16(q + 2);
      f->am[c].vmean = get16(q + 4);
      f->am[c].freq_hz = getf(q + 6);
    }
    for (int c = 0; c < f->nch; ++c) {
      memcpy(f->amin[c], q, f->cols); q += f->cols;
      memcpy(f->amax[c], q, f->cols); q += f->cols;
    }
  }
  return 1;
}

size_t rps_put_caps(const rps_caps_t* c, uint8_t* out, size_t cap) {
  if (cap < 1 + 1 + 20 + 1 + 12) return 0;
  uint8_t* p = out;
  *p++ = RPS_T_CAPS;
  *p++ = RPS_PROTO_VER;
  p = put32(p, c->sys_hz);
  p = put32(p, c->adc_max_rate);
  p = put32(p, c->logic_max_rate);
  p = put32(p, c->analog_max_n);
  p = put32(p, c->logic_max_n);
  *p++ = c->logic_ch;
  memset(p, 0, 12);
  for (size_t i = 0; i < 12 && c->fw[i]; ++i) p[i] = (uint8_t)c->fw[i];
  p += 12;
  return (size_t)(p - out);
}

int rps_parse_caps(const uint8_t* p, size_t n, rps_caps_t* c) {
  if (n < 35 || p[0] != RPS_T_CAPS) return 0;
  memset(c, 0, sizeof(*c));
  c->ver = p[1];
  c->sys_hz = get32(p + 2);
  c->adc_max_rate = get32(p + 6);
  c->logic_max_rate = get32(p + 10);
  c->analog_max_n = get32(p + 14);
  c->logic_max_n = get32(p + 18);
  c->logic_ch = p[22];
  memcpy(c->fw, p + 23, 12);
  c->fw[12] = 0;
  return c->ver == RPS_PROTO_VER;
}

/* ---------------- capture helpers ---------------- */
void rps_atrig_init(rps_atrig_t* t, uint16_t lvl, uint16_t hyst, int rising) {
  t->lvl = lvl;
  t->hyst = hyst;
  t->rising = rising ? 1 : 0;
  t->armed = 0;
}

void rps_ameasure(const uint16_t* ring, uint32_t mask, uint32_t start, uint32_t n, uint32_t stride, uint32_t rate,
                  rps_ameas_t* out) {
  memset(out, 0, sizeof(*out));
  if (n == 0) return;
  uint16_t mn = 0xFFFF, mx = 0;
  uint64_t sum = 0;
  for (uint32_t i = 0; i < n; ++i) {
    uint16_t v = ring[(start + i * stride) & mask] & 0x0FFF;
    if (v < mn) mn = v;
    if (v > mx) mx = v;
    sum += v;
  }
  out->vmin = mn;
  out->vmax = mx;
  out->vmean = (uint16_t)(sum / n);
  uint16_t pp = (uint16_t)(mx - mn);
  if (pp < 40 || rate == 0) return; /* < ~32 mV: noise, no frequency */
  rps_atrig_t t;
  rps_atrig_init(&t, (uint16_t)(mn + pp / 2), (uint16_t)(pp / 10), 1);
  int64_t first = -1, last = -1;
  uint32_t k = 0;
  for (uint32_t i = 0; i < n; ++i) {
    if (rps_atrig_step(&t, ring[(start + i * stride) & mask] & 0x0FFF)) {
      if (first < 0) first = i;
      last = i;
      ++k;
    }
  }
  if (k >= 2 && last > first)
    out->freq_hz = (float)((double)(k - 1) * (double)rate / (double)(last - first));
}

uint16_t rps_adecimate(const uint16_t* ring, uint32_t mask, uint32_t start, uint32_t n, uint32_t stride,
                       uint16_t cols, uint8_t* mn, uint8_t* mx) {
  if (cols > RPS_MAX_COLS) cols = RPS_MAX_COLS;
  if (n < cols) cols = (uint16_t)n;
  for (uint16_t c = 0; c < cols; ++c) {
    uint32_t a = (uint32_t)(((uint64_t)c * n) / cols), b = (uint32_t)(((uint64_t)(c + 1) * n) / cols);
    if (b <= a) b = a + 1;
    uint16_t lo = 0xFFFF, hi = 0;
    for (uint32_t i = a; i < b; ++i) {
      uint16_t v = ring[(start + i * stride) & mask] & 0x0FFF;
      if (v < lo) lo = v;
      if (v > hi) hi = v;
    }
    mn[c] = (uint8_t)(lo >> 4);
    mx[c] = (uint8_t)(hi >> 4);
  }
  return cols;
}

void rps_lmeasure(const uint8_t* ring, uint32_t mask, uint32_t start, uint32_t n, uint32_t rate, uint32_t trig,
                  rps_lmeas_t out[RPS_LOGIC_CH]) {
  for (int c = 0; c < RPS_LOGIC_CH; ++c) {
    rps_lmeas_t* m = &out[c];
    memset(m, 0, sizeof(*m));
    if (n == 0) continue;
    uint8_t bit = rps_logic_bit(c);
    uint8_t prev = ring[start & mask] & bit;
    m->level0 = prev ? 1 : 0;
    m->level_t = (ring[(start + (trig < n ? trig : 0)) & mask] & bit) ? 1 : 0;
    int64_t first = -1, last = -1;
    uint64_t high = 0;
    uint64_t high_at_last = 0;
    for (uint32_t i = 1; i < n; ++i) {
      uint8_t v = ring[(start + i) & mask] & bit;
      if (first >= 0 && prev) ++high;
      if (v && !prev) {
        if (first < 0) first = i;
        last = i;
        high_at_last = high;
        ++m->rising;
      }
      prev = v;
    }
    if (m->rising >= 2 && last > first) {
      m->freq_hz = (float)((double)(m->rising - 1) * (double)rate / (double)(last - first));
      m->duty_pm = (uint16_t)((high_at_last * 1000u) / (uint64_t)(last - first));
    }
  }
}

uint16_t rps_ldecimate(const uint8_t* ring, uint32_t mask, uint32_t start, uint32_t n, uint16_t cols, uint8_t* bits) {
  if (cols > RPS_MAX_COLS) cols = RPS_MAX_COLS;
  if (n < cols) cols = (uint16_t)n;
  for (uint16_t c = 0; c < cols; ++c) {
    uint32_t a = (uint32_t)(((uint64_t)c * n) / cols), b = (uint32_t)(((uint64_t)(c + 1) * n) / cols);
    if (b <= a) b = a + 1;
    uint8_t any = 0, all = 0xFF;
    for (uint32_t i = a; i < b; ++i) {
      uint8_t v = ring[(start + i) & mask];
      any |= v;
      all &= v;
    }
    uint8_t o = 0;
    for (int ch = 0; ch < RPS_LOGIC_CH; ++ch) {
      uint8_t bit = rps_logic_bit(ch);
      if (!(all & bit)) o |= (uint8_t)(1u << (2 * ch));      /* low seen */
      if (any & bit) o |= (uint8_t)(1u << (2 * ch + 1));     /* high seen */
    }
    bits[c] = o;
  }
  return cols;
}

int64_t rps_lfind_edge(const uint8_t* ring, uint32_t mask, int64_t from, int64_t to, uint8_t bit, int rising) {
  if (from < 1) from = 1;
  for (int64_t i = from; i < to; ++i) {
    uint8_t a = ring[(uint64_t)(i - 1) & mask] & bit, b = ring[(uint64_t)i & mask] & bit;
    if (rising ? (b && !a) : (a && !b)) return i;
  }
  return -1;
}
