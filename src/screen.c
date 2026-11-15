/* screen.c - HP47: the HP 48's LCD on the DM42n's. See screen.h.
 *
 * Each HP line (131 pixels) is drawn three times as wide into a 400-pixel line (50 bytes, bit 7 = left), which then
 * goes into DMCP's LCD line buffer three times (three LCD lines). The buffer's layout is learnt at start by setting
 * single pixels, as in ZX47 and FPS47: when each group of 8 pixels lands in one byte in the same order, a 256-entry
 * table converts a byte at a time; otherwise pixel by pixel. Only the HP lines that changed are sent, in bands.
 * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#include <main.h>
#include <dmcp.h>
#include <stdlib.h>
#include <string.h>
#include "screen.h"

#define GROUPS (LCD_X / 8)

static uint8_t (*last)[HP_LCD_BPL];     /* the HP lines on the LCD */
static int valid;
static uint8_t xbyte[LCD_X], xbit[LCD_X];
static int ink, fast;
static uint8_t gbyte[GROUPS], lut[256];
static uint32_t *triple;                /* 256: 8 HP pixels -> 24 LCD pixels (bit 23 = left); heap */

static int find_bit(int x, int y, int *byte, int *bit, int *val) {
  uint8_t a[LCD_LINE_BUF_SIZE], b[LCD_LINE_BUF_SIZE];
  lcd_fill_rect((uint32_t)x, (uint32_t)y, 1, 1, 1);
  memcpy(a, lcd_line_addr(y), sizeof a);
  lcd_fill_rect((uint32_t)x, (uint32_t)y, 1, 1, 0);
  memcpy(b, lcd_line_addr(y), sizeof b);
  for (int i = 0; i < (int)sizeof a; i++) {
    uint8_t d = (uint8_t)(a[i] ^ b[i]);
    if (!d) continue;
    for (int k = 0; k < 8; k++)
      if (d == (1u << k)) { *byte = i; *bit = k; *val = (a[i] >> k) & 1; return 1; }
    return 0;
  }
  return 0;
}

static int learn(void) {
  int byte, bit, val;
  for (int x = 0; x < LCD_X; x++) {
    if (!find_bit(x, SCR_Y, &byte, &bit, &val)) return 0;
    xbyte[x] = (uint8_t)byte;
    xbit[x] = (uint8_t)(1u << bit);
    if (x == 0) ink = val;
    else if (val != ink) return 0;
  }
  fast = 1;
  for (int g = 0; g < GROUPS && fast; g++) {
    gbyte[g] = xbyte[g * 8];
    for (int j = 0; j < 8; j++)
      if (xbyte[g * 8 + j] != gbyte[g] || xbit[g * 8 + j] != xbit[j]) fast = 0;
  }
  if (fast)
    for (int v = 0; v < 256; v++) {
      uint8_t o = 0;
      for (int j = 0; j < 8; j++) if ((v >> (7 - j) & 1) == ink) o |= xbit[j];
      lut[v] = o;
    }
  return 1;
}

int screen_init(void) {
  last = malloc(HP_LCD_H * HP_LCD_BPL);
  triple = malloc(256 * sizeof *triple);
  if (!last || !triple) return -1;
  for (int v = 0; v < 256; v++) {
    uint32_t t = 0;
    for (int j = 0; j < 8; j++) if (v >> (7 - j) & 1) t |= 7u << (21 - 3 * j);
    triple[v] = t;
  }
  valid = 0;
  if (!learn()) { fast = 0; return -2; }
  return 0;
}

int screen_fast(void) { return fast; }
void screen_invalidate(void) { valid = 0; }

/* an HP line (17 bytes, 131 pixels) -> 400 LCD pixels, SCR_X from the left, 1 = black */
static void widen(const uint8_t *h, uint8_t out[GROUPS]) {
  memset(out, 0, GROUPS);
  int bitpos = SCR_X;                                 /* the LCD pixel of the next HP pixel */
  for (int i = 0; i < HP_LCD_BPL; i++) {
    uint32_t t = triple[h[i]];
    if (i == HP_LCD_BPL - 1) t &= 0xFF8000u;          /* pixels 128..130 (3 x 3 LCD pixels): only 131 are real */
    for (int b = 16; b >= 0; b -= 8) {                /* 24 bits in three pieces of 8 */
      uint8_t v = (uint8_t)(t >> b);
      int at = bitpos, sh = at & 7, by = at >> 3;
      if (by < GROUPS) out[by] |= (uint8_t)(v >> sh);
      if (sh && by + 1 < GROUPS) out[by + 1] |= (uint8_t)(v << (8 - sh));
      bitpos += 8;
    }
  }
}

static void put_line(int y, const uint8_t m[GROUPS]) {
  uint8_t *p = lcd_line_addr(y);
  if (fast) { for (int g = 0; g < GROUPS; g++) p[gbyte[g]] = lut[m[g]]; return; }
  for (int x = 0; x < LCD_X; x++) {
    if (!xbit[x]) continue;
    int black = m[x >> 3] >> (7 - (x & 7)) & 1;
    if (black == ink) p[xbyte[x]] |= xbit[x]; else p[xbyte[x]] &= (uint8_t)~xbit[x];
  }
}

int screen_render(const uint8_t lcd[HP_LCD_H][HP_LCD_BPL], int on) {
  if (!last) return 0;
  static const uint8_t blank[HP_LCD_BPL];
  uint8_t dirty[HP_LCD_H];
  int any = 0;
  lcd_refresh_wait();
  for (int y = 0; y < HP_LCD_H; y++) {
    const uint8_t *h = on ? lcd[y] : blank;
    dirty[y] = 0;
    if (valid && !memcmp(h, last[y], HP_LCD_BPL)) continue;
    memcpy(last[y], h, HP_LCD_BPL);
    uint8_t m[GROUPS];
    widen(h, m);
    for (int k = 0; k < SCALE; k++) put_line(SCR_Y + y * SCALE + k, m);
    dirty[y] = 1;
    any++;
  }
  valid = 1;
  if (!any) return 0;
  int sent = 0;                                        /* bands closer than 3 HP lines go as one */
  for (int y = 0; y < HP_LCD_H;) {
    if (!dirty[y]) { y++; continue; }
    int s = y, e = y + 1, gap = 0;
    for (int k = e; k < HP_LCD_H && gap < 3; k++) { if (dirty[k]) { e = k + 1; gap = 0; } else gap++; }
    if (sent++) lcd_refresh_wait();
    lcd_refresh_lines(SCR_Y + s * SCALE, (e - s) * SCALE);
    y = e;
  }
  return any;
}
