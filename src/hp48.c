/* hp48.c - HP47: the HP 48GX around x48's Saturn CPU. See hp48.h.
 *
 * The CPU (instructions, registers, CONFIG/UNCNFG/C=ID, the interrupt system) is x48's (third_party/x48). This file is
 * the rest of the machine, written for the calculator:
 *   memory    the ROM and the RAM stay packed, two nibbles a byte (x48 keeps a byte a nibble: 1.25 MB). The six
 *             memory controllers (I/O, RAM, bank switcher, port 1, port 2, ROM) are decoded into a table of 256 pages
 *             of 4 K nibbles: a page that is all ROM or all RAM is read straight, the others (the I/O registers, the
 *             bank switcher, empty ports) go the slow way. No cards in the ports yet.
 *   I/O       the registers at the I/O controller's address (0x100..0x13F): the display, the annunciators, the
 *             timers, the card status (no cards), the serial port (nothing connected).
 *   timers    by HP time, not by instructions: hp_run(ms) moves timer 2 (8192 Hz) and timer 1 (16 Hz) by the ms
 *             given, spreading the instructions between the ticks; when the CPU sleeps (SHUTDN) the rest of the ms
 *             pass at once. The HP's clock (timer 2) so keeps real time even when the emulation is slower than an
 *             HP 48. The interrupt and wake rules follow Emu48's (MSB set = the timer ran out).
 *   keys      x48's keyboard matrix (OUT bit, IN bit), the keyboard interrupt, ON.
 *   LCD       131 x 64 read from the display memory as the ROM set it up (main area, then the menu lines).
 * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#include <stdlib.h>
#include <string.h>
#include "hp48.h"
#include "hp47_x48.h"
#include "hp48_emu.h"

saturn_t saturn;
HpStat hp;
int enter_debugger, in_debugger, opt_gx = 1;
long nibble_masks[16] = { 0xf, 0xf0, 0xf00, 0xf000, 0xf0000, 0xf00000, 0xf000000, 0xf0000000,
                          0xf, 0xf0, 0xf00, 0xf000, 0xf0000, 0xf00000, 0xf000000, 0xf0000000 };

static const uint8_t *rom;
static uint8_t *ram;
#define RAM_NIBS (HP_RAM_BYTES * 2)

/* ------------------------------------------------------------------ packed nibbles */
static inline int nib(const uint8_t *m, uint32_t a) { uint8_t b = m[a >> 1]; return a & 1 ? b >> 4 : b & 15; }
static inline void nib_set(uint8_t *m, uint32_t a, int v) {
  uint8_t *p = &m[a >> 1];
  *p = (uint8_t)(a & 1 ? (*p & 0x0F) | (v << 4) : (*p & 0xF0) | (v & 15));
}

/* ------------------------------------------------------------------ the display registers, decoded */
static struct {
  int on, offset, lines, nibs_per_line;
  uint32_t start, menu;
  int line_counter;
} D;

static void disp_recalc(void) {
  D.on = (saturn.disp_io >> 3) & 1;
  D.offset = saturn.disp_io & 7;
  D.nibs_per_line = (NIBBLES_PER_ROW + saturn.line_offset + (D.offset > 3 ? 2 : 0)) & 0xfff;
  D.start = (uint32_t)saturn.disp_addr & 0xffffe;
  D.lines = saturn.line_count & 0x3f;
  if (D.lines == 0) D.lines = 63;
  D.menu = (uint32_t)saturn.menu_addr;
}

/* ------------------------------------------------------------------ interrupts, timers */
static uint32_t t2_frac, t1_div;        /* ms * 8192 not yet a tick; timer 2 ticks to the next timer 1 tick */

static void irq(void) {                 /* a timer or the card control: as x48's do_kbd_int */
  if (saturn.intenable) {
    push_return_addr(saturn.PC);
    saturn.PC = 0xf;
    saturn.intenable = 0;
  } else saturn.int_pending = 1;
}

static void wake(void) { hp.sleeping = 0; hp.off = 0; }

/* T*_CTRL bits: 1 XTRA (timer 1) / RUN (timer 2), 2 INT, 4 WAKE, 8 SRQ. A timer has run out when its MSB is set. */
static void check_t1(void) {
  if (!(saturn.timer1 & 8)) { saturn.t1_ctrl &= ~8; return; }
  if (saturn.t1_ctrl & 6) saturn.t1_ctrl |= 8;
  int wk = hp.sleeping && (saturn.t1_ctrl & 4);
  if ((!hp.sleeping || wk) && (saturn.t1_ctrl & 2)) { if (wk) wake(); irq(); }
  if (wk) { saturn.t1_ctrl &= ~4; wake(); }
}

static void check_t2(void) {
  if (!(saturn.timer2 & 0x80000000L)) { saturn.t2_ctrl &= ~8; return; }
  if (saturn.t2_ctrl & 6) saturn.t2_ctrl |= 8;
  int wk = hp.sleeping && (saturn.t2_ctrl & 4);
  if ((!hp.sleeping || wk) && (saturn.t2_ctrl & 2)) { if (wk) wake(); irq(); }
  if (wk) { saturn.t2_ctrl &= ~4; wake(); }
}

static void tick(void) {                /* one 1/8192 s */
  if (saturn.t2_ctrl & 1) {
    saturn.timer2 = (word_32)((uint32_t)saturn.timer2 - 1);
    check_t2();
  }
  if (++t1_div >= 512) {                /* 16 Hz */
    t1_div = 0;
    saturn.timer1 = (char)((saturn.timer1 - 1) & 0xf);
    check_t1();
  }
}

/* SHUTDN (x48's do_shutdown waits for X11 events; here the loop below just stops running instructions) */
void do_shutdown(void) {
  if (is_zero_register(saturn.OUT, OUT_FIELD)) {
    saturn.intenable = 1;
    saturn.int_pending = 0;
    hp.off = 1;
  }
  hp.sleeping = 1;
}

/* ------------------------------------------------------------------ the beeper: OUT bit 11 */
static int out_bit;
static uint32_t first_edge;
void hp47_out_changed(void) {
  int b = (saturn.OUT[2] >> 3) & 1;
  if (b == out_bit) return;
  out_bit = b;
  if (!hp.toggles) first_edge = hp.instr;
  hp.toggles++;
  hp.toggle_instr = hp.instr - first_edge;
}

/* ------------------------------------------------------------------ the I/O registers */
static void io_write(int r, int v) {   /* r: 0x00..0x3f from the I/O controller's address */
  int sh;
  switch (r) {
  case 0x00: saturn.disp_io = v; break;
  case 0x01: saturn.contrast_ctrl = v; break;
  case 0x02: case 0x03: sh = (r - 0x02) * 4; saturn.disp_test = (saturn.disp_test & ~(0xf << sh)) | v << sh; break;
  case 0x04: case 0x05: case 0x06: case 0x07:
    sh = (r - 0x04) * 4; saturn.crc = (word_16)((saturn.crc & ~(0xf << sh)) | v << sh); break;
  case 0x08: saturn.power_status = v; break;
  case 0x09: saturn.power_ctrl = v; break;
  case 0x0a: saturn.mode = v; break;
  case 0x0b: case 0x0c: sh = (r - 0x0b) * 4; saturn.annunc = (saturn.annunc & ~(0xf << sh)) | v << sh; break;
  case 0x0d: saturn.baud = v; break;
  case 0x0e: saturn.card_ctrl = v; if (v & 2) saturn.MP = 1; if (v & 1) irq(); break;
  case 0x0f: break;                                       /* card status: read only */
  case 0x10: saturn.io_ctrl = v; break;
  case 0x11: saturn.rcs = v; break;
  case 0x12: saturn.tcs = v; break;
  case 0x13: saturn.rcs &= 0x0b; break;                   /* CRER */
  case 0x14: case 0x15: break;
  case 0x16: case 0x17: sh = (r - 0x16) * 4; saturn.tbr = (saturn.tbr & ~(0xf << sh)) | v << sh;
    saturn.tcs &= ~1; break;                              /* sent at once: nothing is connected */
  case 0x18: case 0x19: sh = (r - 0x18) * 4; saturn.sreq = (saturn.sreq & ~(0xf << sh)) | v << sh; break;
  case 0x1a: saturn.ir_ctrl = v; break;
  case 0x1b: saturn.base_off = v; break;
  case 0x1c: saturn.lcr = v; break;
  case 0x1d: saturn.lbr = v; break;
  case 0x1e: saturn.scratch = v; break;
  case 0x1f: saturn.base_nibble = v; break;
  case 0x20: case 0x21: case 0x22: case 0x23: case 0x24:
    sh = (r - 0x20) * 4; saturn.disp_addr = (saturn.disp_addr & ~(0xfL << sh)) | (long)v << sh; break;
  case 0x25: case 0x26: case 0x27:
    sh = (r - 0x25) * 4; saturn.line_offset = (word_12)((saturn.line_offset & ~(0xf << sh)) | v << sh); break;
  case 0x28: case 0x29:
    sh = (r - 0x28) * 4; saturn.line_count = (saturn.line_count & ~(0xf << sh)) | v << sh; D.line_counter = -1; break;
  case 0x2a: case 0x2b: case 0x2c: case 0x2d:
    sh = (r - 0x2a) * 4; saturn.unknown = (word_16)((saturn.unknown & ~(0xf << sh)) | v << sh); break;
  case 0x2e: saturn.t1_ctrl = v; check_t1(); break;
  case 0x2f: saturn.t2_ctrl = v; check_t2(); break;
  case 0x30: case 0x31: case 0x32: case 0x33: case 0x34:
    sh = (r - 0x30) * 4; saturn.menu_addr = (saturn.menu_addr & ~(0xfL << sh)) | (long)v << sh; break;
  case 0x35: case 0x36: sh = (r - 0x35) * 4; saturn.unknown2 = (saturn.unknown2 & ~(0xf << sh)) | v << sh; break;
  case 0x37: saturn.timer1 = (char)v; check_t1(); break;
  default:                                                /* 0x38..0x3f timer 2 */
    sh = (r - 0x38) * 4;
    saturn.timer2 = (word_32)(((uint32_t)saturn.timer2 & ~(0xfu << sh)) | (uint32_t)v << sh);
    if (r == 0x3f) check_t2();
    break;
  }
  if (r == 0x00 || (r >= 0x20 && r <= 0x29) || (r >= 0x30 && r <= 0x34)) disp_recalc();
}

static int io_read(int r) {
  switch (r) {
  case 0x00: return saturn.disp_io & 15;
  case 0x01: return saturn.contrast_ctrl & 15;
  case 0x02: case 0x03: return (saturn.disp_test >> ((r - 0x02) * 4)) & 15;
  case 0x04: case 0x05: case 0x06: case 0x07: return (saturn.crc >> ((r - 0x04) * 4)) & 15;
  case 0x08: return saturn.power_status & 15;
  case 0x09: return saturn.power_ctrl & 15;
  case 0x0a: return saturn.mode & 15;
  case 0x0b: case 0x0c: return (saturn.annunc >> ((r - 0x0b) * 4)) & 15;
  case 0x0d: return saturn.baud & 15;
  case 0x0e: return saturn.card_ctrl & 15;
  case 0x0f: return saturn.card_status & 15;
  case 0x10: return saturn.io_ctrl & 15;
  case 0x11: return saturn.rcs & 15;
  case 0x12: return saturn.tcs & 15;
  case 0x13: return 0;
  case 0x14: case 0x15: saturn.rcs &= 0x0e; return (saturn.rbr >> ((r - 0x14) * 4)) & 15;
  case 0x16: case 0x17: return 0;
  case 0x18: case 0x19: return (saturn.sreq >> ((r - 0x18) * 4)) & 15;
  case 0x1a: return saturn.ir_ctrl & 15;
  case 0x1b: return saturn.base_off & 15;
  case 0x1c: return saturn.lcr & 15;
  case 0x1d: return saturn.lbr & 15;
  case 0x1e: return saturn.scratch & 15;
  case 0x1f: return saturn.base_nibble & 15;
  case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: return (int)(saturn.disp_addr >> ((r - 0x20) * 4)) & 15;
  case 0x25: case 0x26: case 0x27: return (saturn.line_offset >> ((r - 0x25) * 4)) & 15;
  case 0x28: case 0x29:                                   /* the line being drawn: x48 counts it up on each read */
    if (++D.line_counter > 0x3f) D.line_counter = -1;
    return (((saturn.line_count & 0xc0) | (D.line_counter & 0x3f)) >> ((r - 0x28) * 4)) & 15;
  case 0x2a: case 0x2b: case 0x2c: case 0x2d: return (saturn.unknown >> ((r - 0x2a) * 4)) & 15;
  case 0x2e: return saturn.t1_ctrl & 15;
  case 0x2f: return saturn.t2_ctrl & 15;
  case 0x30: case 0x31: case 0x32: case 0x33: case 0x34: return (int)(saturn.menu_addr >> ((r - 0x30) * 4)) & 15;
  case 0x35: case 0x36: return (saturn.unknown2 >> ((r - 0x35) * 4)) & 15;
  case 0x37: return saturn.timer1 & 15;
  default: return (int)((uint32_t)saturn.timer2 >> ((r - 0x38) * 4)) & 15;
  }
}

/* ------------------------------------------------------------------ the memory controllers */
/* x48's controllers: 0 I/O (one config: the address, 0x40 nibbles), 1 RAM, 2 bank switcher, 3 port 1, 4 port 2
   (two configs: size mask, then address), 5 ROM (always there, everywhere, lowest priority) */
enum { K_NONE, K_IO, K_RAM, K_BANK, K_PORT, K_ROM };
/* the page table: 4096 pages of 256 nibbles (heap, 16 KB). An entry: PG_SLOW (decode it: I/O, the bank switcher,
   ports, a controller's edge), else the nibble index of the page's start in the ROM, or in the RAM with PG_RAM.
   256 nibbles: the I/O registers (64 nibbles at 00100) then slow only 00100..001FF, not the ROM's busy first 4 K. */
#define PG_SLOW 0x80000000u
#define PG_RAM  0x40000000u
#define PG_IX   0x000fffffu
static uint32_t *page;

/* the instruction window (hp47_x48.h): 16 nibbles from the PC, when they lie in one ROM or RAM page */
uint64_t hp47_win;
uint32_t hp47_pc0, hp47_wlen;

static inline void prefetch(uint32_t pc) {
  pc &= 0xfffff;
  if (((pc - hp47_pc0) & 0xfffff) + 8 <= hp47_wlen) return;  /* the window still holds 8 nibbles from here */
  uint32_t e = page[pc >> 8], off = pc & 0xff, i;
  const uint8_t *m;
  hp47_wlen = 0;
  if ((e & PG_SLOW) || off > 256 - 16) return;
  if (e & PG_RAM) { i = (e & PG_IX) + off; if (i + 16 > RAM_NIBS) return; m = ram; }
  else { i = e + off; m = rom; }
  const uint8_t *p = m + (i >> 1);
  uint64_t w;
  memcpy(&w, p, 8);                                       /* little-endian: the low nibble first, as the ROM */
  if (i & 1) w = w >> 4 | (uint64_t)(p[8] & 15) << 60;
  hp47_win = w; hp47_pc0 = pc; hp47_wlen = 16;
}

static int ctl_range(int i, uint32_t *s, uint32_t *e) {
  if (i == 5) { *s = 0; *e = 0xfffff; return 1; }
  if (saturn.mem_cntl[i].unconfigured) return 0;
  if (i == 0) { *s = (uint32_t)saturn.mem_cntl[0].config[0] & 0xfffff; *e = *s + 0x3f; return 1; }
  uint32_t mask = (uint32_t)saturn.mem_cntl[i].config[1] & 0xfffff;
  *s = (uint32_t)saturn.mem_cntl[i].config[0] & mask;
  *e = *s | (~mask & 0xfffff);
  return 1;
}

static int ctl_kind(int i) { static const uint8_t K[6] = { K_IO, K_RAM, K_BANK, K_PORT, K_PORT, K_ROM }; return K[i]; }

/* which controller answers at a: its kind, and the offset into it */
static int decode(uint32_t a, uint32_t *off) {
  for (int i = 0; i < 6; i++) {
    uint32_t s, e;
    if (ctl_range(i, &s, &e) && a >= s && a <= e) { *off = a - s; return ctl_kind(i); }
  }
  return K_NONE;
}

void hp47_mmu_changed(void) {
  hp47_wlen = 0;
  if (!page) return;
  uint32_t S[6], E[6];
  int on[6];
  for (int i = 0; i < 6; i++) on[i] = ctl_range(i, &S[i], &E[i]);
  for (uint32_t p = 0; p < 4096; p++) {
    uint32_t lo = p << 8, hi = lo + 0xff;
    page[p] = PG_SLOW;
    for (int i = 0; i < 6; i++) {
      if (!on[i] || E[i] < lo || S[i] > hi) continue;
      int k = ctl_kind(i);
      if (S[i] <= lo && E[i] >= hi && (k == K_ROM || k == K_RAM))
        page[p] = (k == K_RAM ? PG_RAM : 0) | ((lo - S[i]) & PG_IX);
      break;                                              /* the highest controller there decides */
    }
  }
}

static int slow_read(uint32_t a, int side) {
  uint32_t off;
  switch (decode(a, &off)) {
  case K_IO: return side ? io_read((int)off) : 0;
  case K_RAM: return nib(ram, off & (RAM_NIBS - 1));
  case K_BANK:                                            /* reading here selects port 2's bank (x48) */
    if (side) {
      if (off == 0) saturn.bank_switch = 0;
      else if (off >= 0x40 && off < 0x80) saturn.bank_switch = (short)((off - 0x40) / 2);
    }
    return 0x7;
  case K_PORT: return 0;                                  /* no card */
  case K_ROM: return nib(rom, a);
  default: return 0;
  }
}

int read_nibble(long addr) {
  uint32_t a = (uint32_t)addr & 0xfffff, e = page[a >> 8];
  if (!(e & (PG_SLOW | PG_RAM))) return nib(rom, e + (a & 0xff));
  if (!(e & PG_SLOW)) return nib(ram, ((e & PG_IX) + (a & 0xff)) & (RAM_NIBS - 1));
  return slow_read(a, 1);
}

static int peek(uint32_t a) {           /* no side effects (the LCD) */
  a &= 0xfffff;
  uint32_t e = page[a >> 8];
  if (!(e & (PG_SLOW | PG_RAM))) return nib(rom, e + (a & 0xff));
  if (!(e & PG_SLOW)) return nib(ram, ((e & PG_IX) + (a & 0xff)) & (RAM_NIBS - 1));
  return slow_read(a, 0);
}

int read_nibble_crc(long addr) {
  int v = read_nibble(addr);
  saturn.crc = (word_16)((saturn.crc >> 4) ^ (((saturn.crc ^ v) & 0xf) * 0x1081));
  return v;
}

void write_nibble(long addr, int val) {
  uint32_t a = (uint32_t)addr & 0xfffff, off, e = page[a >> 8];
  if (((a - hp47_pc0) & 0xfffff) < hp47_wlen) hp47_wlen = 0;   /* a write into the instruction window */
  if ((e & (PG_SLOW | PG_RAM)) == PG_RAM) { nib_set(ram, ((e & PG_IX) + (a & 0xff)) & (RAM_NIBS - 1), val & 15); return; }
  if (!(e & PG_SLOW)) return;                             /* the ROM */
  switch (decode(a, &off)) {
  case K_IO: io_write((int)off, val & 15); break;
  case K_RAM: nib_set(ram, off & (RAM_NIBS - 1), val & 15); break;
  default: break;                                         /* ROM, the bank switcher, empty ports */
  }
}

long read_nibbles(long addr, int len) {
  long val = 0;
  addr += len;
  while (len-- > 0) val = (val << 4) | read_nibble(--addr);
  return val;
}

void write_nibbles(long addr, long val, int len) {
  while (len-- > 0) { write_nibble(addr++, (int)(val & 15)); val >>= 4; }
}

void dev_memory_init(void) {}

/* ------------------------------------------------------------------ the machine */
int hp_init(const uint8_t *r, uint8_t *m) {
  if (!page) page = malloc(4096 * sizeof *page);
  if (!page) return -1;
  rom = r;
  ram = m;
  hp_reset();
  return 0;
}

void hp_reset(void) {
  unsigned char *keep_rom = saturn.rom, *keep_ram = saturn.ram;
  memset(&saturn, 0, sizeof saturn);
  saturn.rom = keep_rom; saturn.ram = keep_ram;
  saturn.PC = 0;
  saturn.hexmode = HEX;
  saturn.rstkp = -1;
  saturn.intenable = 1;
  saturn.kbd_ien = 1;
  saturn.timer2 = 0x2000;
  saturn.t1_tick = 8192;
  saturn.t2_tick = 16;
  for (int i = 0; i < NR_MCTL; i++) {
    saturn.mem_cntl[i].unconfigured = (unsigned short)(i == 0 ? 1 : i == 5 ? 0 : 2);
    saturn.mem_cntl[i].config[0] = saturn.mem_cntl[i].config[1] = 0;
  }
  memset(&D, 0, sizeof D);
  D.line_counter = -1;
  disp_recalc();
  memset(&hp, 0, sizeof hp);
  t2_frac = t1_div = 0;
  enter_debugger = 0;
  out_bit = 0;
  hp47_mmu_changed();
}

void hp_clear_ram(void) { memset(ram, 0, HP_RAM_BYTES); }

void hp_run(uint32_t ms, uint32_t max_instr) {
  t2_frac += ms * 8192u;
  uint32_t ticks = t2_frac / 1000u;
  t2_frac %= 1000u;
  int no_tick = !ticks;                                   /* less than a tick of time: instructions only */
  if (no_tick) ticks = 1;
  uint32_t per = max_instr / ticks, extra = max_instr % ticks;
  for (uint32_t k = 0; k < ticks; k++) {
    uint32_t n = per + (k < extra);
    while (n-- && !hp.sleeping && !hp.bad) {
      long pc = saturn.PC;
      prefetch((uint32_t)pc);
      step_instruction();
      hp.instr++;
      if (enter_debugger) {                               /* an unknown opcode: stop, keep its address */
        if (enter_debugger & ILLEGAL_INSTRUCTION) { hp.bad = 1; hp.bad_pc = (uint32_t)pc; }
        enter_debugger = 0;
      }
    }
    if (!no_tick) tick();
  }
}

void hp_key(int code, int down) {
  if (code == HK_NONE) return;
  if (code == HK_ON) {
    for (int i = 0; i < 9; i++) {
      if (down) saturn.keybuf.rows[i] |= 0x8000; else saturn.keybuf.rows[i] &= ~0x8000;
    }
    if (down) { if (hp.sleeping) wake(); do_kbd_int(); }
    return;
  }
  int r = code >> 4, c = 1 << (code & 0xf);
  if (down) {
    if (!(saturn.keybuf.rows[r] & c)) {
      if (hp.sleeping && !hp.off) wake();
      if (saturn.kbd_ien) do_kbd_int();
      saturn.keybuf.rows[r] |= (short)c;
    }
  } else saturn.keybuf.rows[r] &= (short)~c;
}

int hp_lcd(uint8_t out[HP_LCD_H][HP_LCD_BPL]) {
  memset(out, 0, HP_LCD_H * HP_LCD_BPL);
  if (!D.on) return 0;
  uint32_t a = D.start;
  for (int y = 0; y < HP_LCD_H; y++) {
    int main = y <= D.lines, sh = main ? D.offset : 0;
    uint32_t base = main ? a + (uint32_t)y * (uint32_t)D.nibs_per_line : D.menu + (uint32_t)(y - D.lines - 1) * NIBBLES_PER_ROW;
    uint8_t *o = out[y];
    int got = -1, v = 0;
    for (int x = 0; x < HP_LCD_W; x++) {
      int p = x + sh, n = p >> 2;
      if (n != got) { v = peek(base + (uint32_t)n); got = n; }
      if (v >> (p & 3) & 1) o[x >> 3] |= (uint8_t)(0x80 >> (x & 7));
    }
  }
  return 1;
}

int hp_annunc(void) { return saturn.annunc & 0xff; }

const char *hp_rom_version(char s[16]) {
  if (!rom) return NULL;
  static const char want[] = "HP48-";
  for (int i = 0; i < 6; i++) s[i] = (char)(nib(rom, 0x7ffbf + 2 * i) | nib(rom, 0x7ffbf + 2 * i + 1) << 4);
  s[6] = 0;
  return memcmp(s, want, 5) ? NULL : s;
}

/* ------------------------------------------------------------------ the state */
#define STATE_MAGIC 0x48474b31u        /* "HGK1": change it when the layout changes */
typedef struct { uint32_t magic, size; uint32_t t2_frac, t1_div; int out_bit; HpStat hp; } StateHdr;
#define SAT_BYTES (sizeof(saturn_t) - 4 * sizeof(unsigned char *))   /* without the pointers at its end */

uint32_t hp_state_size(void) { return (uint32_t)(sizeof(StateHdr) + SAT_BYTES); }

void hp_state_save(uint8_t *d) {
  StateHdr h = { STATE_MAGIC, hp_state_size(), t2_frac, t1_div, out_bit, hp };
  memcpy(d, &h, sizeof h);
  memcpy(d + sizeof h, &saturn, SAT_BYTES);
}

int hp_state_load(const uint8_t *d, uint32_t n) {
  StateHdr h;
  if (n != hp_state_size()) return -1;
  memcpy(&h, d, sizeof h);
  if (h.magic != STATE_MAGIC || h.size != n) return -1;
  unsigned char *keep[4] = { saturn.rom, saturn.ram, saturn.port1, saturn.port2 };
  memcpy(&saturn, d + sizeof h, SAT_BYTES);
  saturn.rom = keep[0]; saturn.ram = keep[1]; saturn.port1 = keep[2]; saturn.port2 = keep[3];
  t2_frac = h.t2_frac; t1_div = h.t1_div; out_bit = h.out_bit; hp = h.hp;
  hp.toggles = 0;
  memset(&saturn.keybuf, 0, sizeof saturn.keybuf);
  D.line_counter = -1;
  disp_recalc();
  hp47_mmu_changed();
  return 0;
}

void hp_time_passed(uint32_t ms) {     /* timer 2 runs while the HP is off (it keeps the clock) */
  uint64_t t = (uint64_t)ms * 8192u / 1000u;
  if (saturn.t2_ctrl & 1) saturn.timer2 = (word_32)((uint32_t)saturn.timer2 - (uint32_t)t);
  check_t2();
}
