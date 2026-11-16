/*
 * HP47 for the SwissMicros DM42n (DMCP5 native program, .pg5): an HP 48GX running its own ROM.
 * (C) 2026 Victor Valdes (valdes.vj@gmail.com) - GPL-3.0-or-later
 *
 * The screen: the HP 48's 131 x 64 LCD three times as big (393 x 192) in the middle, a status line above it (the HP's
 * annunciators, the C47 shift, the speed), a hint below. Only the lines that changed go to the LCD (screen.c).
 * The ROM is not part of HP47: the user copies a 48G/GX ROM image to /HP47 (HP48.ROM, or gxrom-r as hpcalc.org has it).
 * It is read into the heap, or, when the heap is too small, into DMCP's QSPI user area (once; kept there).
 * The HP's state and its 128 KB of RAM are kept in /HP47/HP47.STA when the calculator turns off, so the HP goes on
 * where it was, as a real one does.
 * The keys (keys.c): the C47 overlay's legends (SHIFT = f, SHIFT SHIFT = g) or the HP 48's keyboard by position. A
 * calculator key may stand for up to three HP keys, pressed in turn by the key sequencer below.
 * The loop: read the two keys held (DMCP's read_key), run the HP for the time that passed (hp48.c), draw, sleep.
 */
#include <main.h>
#include <dmcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hp48.h"
#include "screen.h"
#include "keys.h"

#define DIR        "/HP47"
#define CFG_FILE   DIR "/HP47.CFG"
#define STATE_FILE DIR "/HP47.STA"
#define FRAME_MS   20
#define RUN_MS     16                       /* the HP runs at most this long a frame */
#define SLICE_I    2000u                    /* instructions a slice (then the clock is read again) */
#define HP_IPS     830000u                  /* instructions a second of a real 48GX (its BEEP: test/beep.sh) */

typedef struct { int layout, sound, speed, turbo; } Cfg;
static Cfg cfg = { LAYOUT_C47, 1, 0, 0 };

static uint8_t *ram;                        /* 128 KB, heap */
static const uint8_t *rom;                  /* 512 KB: heap or QSPI */
static int rom_where;                       /* 0 none, 1 heap, 3 built in (ROM_IN), 4 the cache (paged) */
static uint32_t rom_crc;
static char rom_ver[16] = "";
static char msg[64] = "";                   /* the menu's message line */

/* ------------------------------------------------------------------ text */
static void text(int x, int y, const char *s, int inv) {
  t20->inv = inv;
  lcd_setXY(t20, x, y);
  lcd_writeText(t20, s);
  t20->inv = 0;
}

static void bar(const char *s) { lcd_fill_rect(0, 0, LCD_X, 22, 1); text(4, 1, s, 1); }

static void wait_release(void);

static int read_keys(int *k1, int *k2) {
  *k1 = *k2 = 0;
  __sysfn_read_key(k1, k2);
  if (*k1 < 0) *k1 = 0;
  if (*k2 < 0) *k2 = 0;
  return (*k1 > 0) + (*k2 > 0);
}

static void wait_release(void) { int a, b; while (read_keys(&a, &b)) sys_delay(10); key_pop_all(); }

static int wait_key(void) {                 /* a key press (menus), once it is up again; 0 if the program must end */
  for (;;) {
    if (key_empty()) { sys_timer_start(0, 500); CLR_ST(STAT_RUNNING); sys_sleep(); }
    if (ST(STAT_PGM_END) || ST(STAT_SUSPENDED)) return 0;
    int k = key_pop();
    if (k > 0 && k != KEY_DOUBLE_RELEASE) { wait_release(); return k; }
  }
}

/* ------------------------------------------------------------------ crc32 */
static uint32_t crc32_add(uint32_t c, const uint8_t *p, uint32_t n) {
  c = ~c;
  while (n--) { c ^= *p++; for (int k = 0; k < 8; k++) c = c >> 1 ^ (0xEDB88320u & (0u - (c & 1))); }
  return ~c;
}

/* ------------------------------------------------------------------ settings */
static void cfg_save(void) {
  if (!sys_disk_ok()) return;
  char s[96];
  int n = sprintf(s, "layout=%d\nsound=%d\nspeed=%d\nturbo=%d\n", cfg.layout, cfg.sound, cfg.speed, cfg.turbo);
  sys_disk_write_enable(1);
  check_create_dir(DIR);
  FIL f; UINT bw;
  if (f_open(&f, CFG_FILE, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) { f_write(&f, s, (UINT)n, &bw); f_close(&f); }
  sys_disk_write_enable(0);
}

static void cfg_load(void) {
  FIL f; UINT br;
  char s[128];
  if (f_open(&f, CFG_FILE, FA_READ) != FR_OK) return;
  f_read(&f, s, sizeof s - 1, &br);
  f_close(&f);
  s[br] = 0;
  static const struct { const char *k; int *v, max; } T[] = {
    {"layout", &cfg.layout, LAYOUT_CNT - 1}, {"sound", &cfg.sound, 1}, {"speed", &cfg.speed, 1}, {"turbo", &cfg.turbo, 1} };
  for (unsigned i = 0; i < sizeof T / sizeof *T; i++) {
    char key[16];
    sprintf(key, "%s=", T[i].k);
    char *p = strstr(s, key);
    if (p) { int v = atoi(p + strlen(key)); if (v >= 0 && v <= T[i].max) *T[i].v = v; }
  }
}

/* ------------------------------------------------------------------ the ROM
   A 48G/GX ROM image is 512 KB packed (two nibbles a byte, the low nibble first: gxrom-r) or 1 MB unpacked (a nibble
   a byte, as Emu48 keeps it). Both are read as packed bytes, 2 KB at a time. */
static const char *const ROM_NAMES[] = { DIR "/HP48.ROM", DIR "/gxrom-r", DIR "/ROM.48G", DIR "/ROM" };

typedef struct { FIL f; int unpacked; uint8_t tmp[2048]; } RomFile;

static int rom_open(RomFile *r, const char *path) {   /* 0 ok, else a message in msg */
  if (f_open(&r->f, path, FA_READ) != FR_OK) { snprintf(msg, sizeof msg, "cannot read %s", path); return -1; }
  uint32_t n = (uint32_t)f_size(&r->f);
  if (n == HP_ROM_BYTES) r->unpacked = 0;
  else if (n == 2 * HP_ROM_BYTES) r->unpacked = 1;
  else {
    f_close(&r->f);
    snprintf(msg, sizeof msg, n == 256u * 1024u ? "48S/SX ROM: not yet (48G/GX only)" : "not a 48G/GX ROM (%u bytes)",
             (unsigned)n);
    return -1;
  }
  return 0;
}

static int rom_read(RomFile *r, uint8_t *out, uint32_t n) {  /* the next n packed bytes; 0 ok */
  UINT br;
  if (!r->unpacked) return f_read(&r->f, out, n, &br) != FR_OK || br != n;
  while (n) {
    uint32_t k = n > sizeof r->tmp / 2 ? sizeof r->tmp / 2 : n;
    if (f_read(&r->f, r->tmp, k * 2, &br) != FR_OK || br != k * 2) return -1;
    for (uint32_t i = 0; i < k; i++) out[i] = (uint8_t)((r->tmp[2 * i] & 15) | (r->tmp[2 * i + 1] & 15) << 4);
    out += k; n -= k;
  }
  return 0;
}

static int is_48g(const uint8_t *p) {       /* "HP48-" at nibble 0x7ffbf, as hp_rom_version */
  static const char want[] = "HP48-";
  for (int i = 0; i < 5; i++) {
    uint32_t a = 0x7ffbf + 2u * (uint32_t)i;
    int lo = (p[a >> 1] >> ((a & 1) * 4)) & 15, hi = (p[(a + 1) >> 1] >> (((a + 1) & 1) * 4)) & 15;
    if ((char)(lo | hi << 4) != want[i]) return 0;
  }
  return 1;
}

static int confirm(const char *title, const char *l1, const char *l2) {
  lcd_clear_buf();
  bar(title);
  text(4, 40, l1, 0);
  text(4, 64, l2, 0);
  text(4, 110, "ENTER: yes     EXIT: no", 0);
  lcd_refresh();
  wait_release();
  for (;;) {
    int k = wait_key();
    if (k == KEY_ENTER) { wait_release(); return 1; }
    if (k == KEY_EXIT || !k) { wait_release(); return 0; }
  }
}

/* The ROM cache (paged): the ROM file stays open and the HP reads it 2 KB at a time as it needs it (hp48.c).
   A whole HP 48 session touches about 280 KB of the 512; with a cache of 128 KB or more almost every block is read
   only once. The file is closed while the calculator is off or in DMCP's setup (the USB disk), opened again on use. */
#define CACHE_RESERVE (40u * 1024u)         /* the heap left for the rest: state saving, menus, files */
static RomFile *rf;                         /* the open ROM file (paged) */
static char rom_path[64];
static uint8_t *cache;
static int slots;

static void rom_close(void) { if (rf && rf->f.obj.fs) f_close(&rf->f); if (rf) rf->f.obj.fs = 0; }

static int rom_fetch(uint32_t blk, uint8_t *dst) {
  if (!rf) return -1;
  if (!rf->f.obj.fs && rom_open(rf, rom_path)) return -1;
  if (f_lseek(&rf->f, (FSIZE_t)blk * 2048u * (rf->unpacked ? 2u : 1u)) != FR_OK) return -1;
  return rom_read(rf, dst, 2048);
}

static void rom_free(void) {
  if (rom_where == 1) free((void *)rom);
  if (rom_where == 4) { rom_close(); free(rf); rf = NULL; free(cache); cache = NULL; }
  rom = NULL; rom_where = 0;
}

static int hp_start(void) {                 /* the HP on the ROM as loaded */
  return rom_where == 4 ? hp_init_paged(cache, slots, rom_fetch, ram) : hp_init(rom, ram);
}

/* load the ROM at path: 0 ok (rom, rom_where, rom_crc set), else msg says why. The whole ROM in the heap when it
   fits, else the cache with what the heap allows. */
static int rom_load(const char *path) {
  RomFile *r = malloc(sizeof *r);
  if (!r) { strcpy(msg, "no memory"); return -1; }
  memset(r, 0, sizeof *r);
  if (rom_open(r, path)) { free(r); return -1; }
  lcd_clear_buf();
  bar("HP47: reading the ROM");
  text(4, 40, path, 0);
  lcd_refresh();
  rom_free();
  int e = -1;
  uint8_t *heap = (uint32_t)sys_largest_free_mem() >= HP_ROM_BYTES + 48u * 1024u ? malloc(HP_ROM_BYTES) : NULL;
  if (heap) {                                           /* all of it in the heap */
    e = rom_read(r, heap, HP_ROM_BYTES);
    if (!e && !is_48g(heap)) { e = -1; strcpy(msg, "not a 48G/GX ROM"); }
    else if (e) strcpy(msg, "read error");
    if (e) free(heap);
    else { rom = heap; rom_where = 1; rom_crc = crc32_add(0, heap, HP_ROM_BYTES); }
    f_close(&r->f);
    free(r);
    return e;
  }
  uint32_t fr = (uint32_t)sys_largest_free_mem();      /* the cache: as many 2 KB blocks as the heap allows */
  int n = fr > CACHE_RESERVE ? (int)((fr - CACHE_RESERVE) / 2048u) : 0;
  if (n > 255) n = 255;
  if (n < 24 || !(cache = malloc((size_t)n * 2048u))) {
    snprintf(msg, sizeof msg, "no room: heap %u KB (48 KB at least)", (unsigned)(fr / 1024));
    f_close(&r->f); free(r); cache = NULL;
    return -1;
  }
  uint32_t crc = 0;                                     /* the crc (the state file's ROM), block 255 checked */
  e = 0;
  for (uint32_t b = 0; b < 256 && !e; b++) { e = rom_read(r, cache, 2048); crc = crc32_add(crc, cache, 2048); }
  /* "HP48-" at nibble 0x7ffbf = byte 0x3ffdf: in block 127 */
  if (!e) e = f_lseek(&r->f, 127u * 2048u * (r->unpacked ? 2u : 1u)) != FR_OK || rom_read(r, cache, 2048);
  if (!e) {
    static const char want[] = "HP48-";
    for (int i = 0; i < 5 && !e; i++) {
      uint32_t a = 0x7ffbf + 2u * (uint32_t)i - 127u * 4096u;
      int lo = cache[a >> 1] >> ((a & 1) * 4) & 15, hi = cache[(a + 1) >> 1] >> (((a + 1) & 1) * 4) & 15;
      if ((char)(lo | hi << 4) != want[i]) e = -2;
    }
  }
  if (e) {
    strcpy(msg, e == -2 ? "not a 48G/GX ROM" : "read error");
    f_close(&r->f); free(r); free(cache); cache = NULL;
    return -1;
  }
  rf = r;
  strncpy(rom_path, path, sizeof rom_path - 1);
  slots = n;
  rom = cache; rom_where = 4; rom_crc = crc;
  return 0;
}

#ifdef HP47_ROM_IN
extern const uint8_t hp47_rom[];            /* make ROM_IN=...: the ROM in the program's flash (src/rom_in.s) */
#endif

static int rom_find(void) {                 /* the ROM built in, else under one of its usual names */
#ifdef HP47_ROM_IN
  if (is_48g(hp47_rom)) { rom = hp47_rom; rom_where = 3; rom_crc = crc32_add(0, hp47_rom, HP_ROM_BYTES); return 0; }
#endif
  for (unsigned i = 0; i < sizeof ROM_NAMES / sizeof *ROM_NAMES; i++)
    if (file_exists(ROM_NAMES[i])) return rom_load(ROM_NAMES[i]);
  snprintf(msg, sizeof msg, "no ROM: copy one to %s/HP48.ROM", DIR);
  return -1;
}

static int pick_ok;
static int pick_cb(const char *fpath, const char *fname, void *data) {
  (void)fname; (void)data;
  pick_ok = rom_load(fpath) == 0;
  return MRET_EXIT;
}

static int rom_pick(void) {                 /* the file selection screen: any file in /HP47 */
  pick_ok = 0;
  check_create_dir(DIR);
  file_selection_screen("Choose the 48G/GX ROM", DIR, "", pick_cb, 0, 0, NULL);
  wait_for_key_release(-1);
  return pick_ok;
}

/* ------------------------------------------------------------------ the HP's state on the disk */
typedef struct { uint32_t magic, version, rom_crc, state, ram; } StaHdr;
#define STA_MAGIC 0x53345048u               /* "HP4S" */

static uint32_t now_s(void) {               /* seconds, from the RTC (sys_current_ms stops while off) */
  tm_t t; dt_t d;
  rtc_read(&t, &d);
  return (uint32_t)julian_day(&d) * 86400u + t.hour * 3600u + t.min * 60u + t.sec;
}

static int state_save(void) {               /* 0 ok */
  if (!rom || !sys_disk_ok()) return -1;
  uint32_t n = hp_state_size();
  uint8_t *b = malloc(n);
  if (!b) return -1;
  hp_state_save(b);
  StaHdr h = { STA_MAGIC, 1, rom_crc, n, HP_RAM_BYTES };
  sys_disk_write_enable(1);
  check_create_dir(DIR);
  FIL f; UINT w1 = 0, w2 = 0, w3 = 0;
  int ok = f_open(&f, STATE_FILE, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK;
  if (ok) {
    f_write(&f, &h, sizeof h, &w1); f_write(&f, b, n, &w2); f_write(&f, ram, HP_RAM_BYTES, &w3);
    f_close(&f);
  }
  sys_disk_write_enable(0);
  free(b);
  return ok && w1 == sizeof h && w2 == n && w3 == HP_RAM_BYTES ? 0 : -1;
}

static int state_load(void) {               /* 0 ok; otherwise the HP starts afresh */
  FIL f; UINT br;
  StaHdr h;
  if (f_open(&f, STATE_FILE, FA_READ) != FR_OK) return -1;
  int e = f_read(&f, &h, sizeof h, &br) != FR_OK || br != sizeof h || h.magic != STA_MAGIC || h.version != 1 ||
          h.state != hp_state_size() || h.ram != HP_RAM_BYTES;
  if (!e && h.rom_crc != rom_crc) { e = 1; strcpy(msg, "another ROM: the HP starts afresh"); }
  uint8_t *b = e ? NULL : malloc(h.state);
  if (b) {
    e = f_read(&f, b, h.state, &br) != FR_OK || br != h.state || hp_state_load(b, h.state);
    if (!e) e = f_read(&f, ram, HP_RAM_BYTES, &br) != FR_OK || br != HP_RAM_BYTES;
    free(b);
  } else e = 1;
  f_close(&f);
  if (e) { hp_reset(); return -1; }
  return 0;
}

/* ------------------------------------------------------------------ the frame around the HP's screen */
static int shift;                           /* C47: 0, 1 f, 2 g */
static char status_now[96];
static int pct = 100;
static char note[40];
static uint32_t note_until;

static void tag(int *x, const char *s) {    /* an annunciator: white on black */
  int w = lcd_textWidth(t20, s);
  lcd_fill_rect((uint32_t)*x, 0, (uint32_t)w, SCR_Y - 2, 1);
  text(*x, 2, s, 1);
  *x += w + 6;
}

static void status(void) {
  char key[96];
  int a = hp_annunc(), on = a & HP_ANN_ON;
  int fresh = note[0] && (int32_t)(sys_current_ms() - note_until) < 0;
  if (!fresh) note[0] = 0;
  snprintf(key, sizeof key, "%d %02x %d %d %s", cfg.layout, on ? a & 0x3f : 0, shift, cfg.speed ? pct : -1, note);
  if (!strcmp(key, status_now)) return;
  strcpy(status_now, key);
  lcd_refresh_wait();
  lcd_fill_rect(0, 0, LCD_X, SCR_Y - 1, 0);
  int x = 4;
  if (note[0]) text(x, 2, note, 0);
  else {
    text(x, 2, "HP47", 0); x += lcd_textWidth(t20, "HP47") + 12;
    if (on && (a & HP_ANN_LEFT)) tag(&x, " LS ");
    if (on && (a & HP_ANN_RIGHT)) tag(&x, " RS ");
    if (on && (a & HP_ANN_ALPHA)) tag(&x, " ALPHA ");
    if (on && (a & HP_ANN_BUSY)) tag(&x, " BUSY ");
    if (on && (a & HP_ANN_IO)) tag(&x, " I/O ");
    if (on && (a & HP_ANN_BATT)) tag(&x, " BATT ");
    char r[24] = "";
    if (cfg.speed) sprintf(r, pct < 0 ? "idle" : "%d%%", pct);
    if (r[0]) text(LCD_X - 64 - (shift ? 30 : 0), 2, r, 0);
  }
  if (shift && cfg.layout == LAYOUT_C47) {
    lcd_fill_rect(LCD_X - 26, 0, 26, SCR_Y - 2, 1);
    text(LCD_X - 18, 2, shift == 1 ? "f" : "g", 1);
  }
  lcd_refresh_lines(0, SCR_Y - 1);
}

static void hint(void) {
  lcd_fill_rect(0, SCR_Y + SCR_H, LCD_X, LCD_Y - SCR_Y - SCR_H, 0);
  text(4, SCR_Y + SCR_H + 3, cfg.layout == LAYOUT_C47 ? "C47 keys   g . menu   g EXIT screenshot"
                                                      : "HP48 keys   SHIFT held: EXIT menu  . shot", 0);
}

static void frame_ui(void) {
  lcd_refresh_wait();
  lcd_clear_buf();
  status_now[0] = 0;
  hint();
  lcd_forced_refresh();
  lcd_refresh_wait();
  screen_invalidate();
}

static void say(const char *s) { strncpy(note, s, sizeof note - 1); note[sizeof note - 1] = 0; note_until = sys_current_ms() + 2500; }

/* ------------------------------------------------------------------ the key sequencer
   Each calculator key gives up to three HP keys: the first ones are tapped, the last is held as long as the
   calculator key is. A tap is 3 frames down, 2 up: the HP's keyboard scan sees each press. */
#define QN 24
typedef struct { uint16_t hp; uint8_t dm; } Step;   /* dm: the calculator key that holds it (0: a tap) */
static Step q[QN];
static int qh, qt;                          /* queue head, tail */
static int qphase;                          /* frames into the head step: 0 not pressed yet */
static uint16_t held_hp[64];                /* calculator key -> HP key held for it (HK_NONE) */
static uint8_t dm_down[64];

static void q_push(uint16_t hp, uint8_t dm) { int n = (qt + 1) % QN; if (n != qh) { q[qt].hp = hp; q[qt].dm = dm; qt = n; } }

static void q_run(void) {                   /* once a frame */
  if (qh == qt) return;
  Step *s = &q[qh];
  if (qphase == 0) { hp_key(s->hp, 1); qphase = 1; return; }
  qphase++;
  if (s->dm && dm_down[s->dm] && s->hp != HK_NONE) {   /* held: it is released by the key's release */
    if (qphase >= 3) { held_hp[s->dm] = s->hp; qh = (qh + 1) % QN; qphase = 0; }
    return;
  }
  if (qphase == 4) hp_key(s->hp, 0);
  if (qphase >= 6) { qh = (qh + 1) % QN; qphase = 0; }
}

static int q_busy(void) { return qh != qt; }

static void menu_run(void);
static void screenshot(void);

static void do_act(Act a, int dm) {
  if (a.k[0] == SP_MENU) { menu_run(); return; }
  if (a.k[0] == SP_SHOT) { screenshot(); return; }
  int last = -1;
  for (int i = 0; i < ACT_N; i++) if (a.k[i] != HK_NONE) last = i;
  for (int i = 0; i <= last; i++) q_push(a.k[i], (uint8_t)(i == last ? dm : 0));
}

static void hp48_shift_tap(void) {          /* HP48 layout: the HP's own shifts, by what the HP shows now */
  int a = hp_annunc();
  int ls = (a & HP_ANN_ON) && (a & HP_ANN_LEFT), rs = (a & HP_ANN_ON) && (a & HP_ANN_RIGHT);
  q_push(ls || rs ? HK_RSHIFT : HK_LSHIFT, 0);          /* none -> LS -> RS -> none (RS again cancels) */
}

static int shift_down, shift_used;

static void keys_frame(int k1, int k2) {
  int now[2] = { k1, k2 };
  int is_now[64] = { 0 };
  for (int i = 0; i < 2; i++) if (now[i] > 0 && now[i] < 64) is_now[now[i]] = 1;
  for (int k = 1; k < 64; k++) {
    if (dm_down[k] && !is_now[k]) {                     /* released */
      dm_down[k] = 0;
      if (held_hp[k] != HK_NONE) { hp_key(held_hp[k], 0); held_hp[k] = HK_NONE; }
      if (k == KEY_SHIFT) {
        shift_down = 0;
        if (!shift_used) {
          if (cfg.layout == LAYOUT_C47) shift = (shift + 1) % 3;
          else hp48_shift_tap();
        }
      }
    }
  }
  for (int i = 0; i < 2; i++) {
    int k = now[i];
    if (k <= 0 || k >= 64 || dm_down[k]) continue;
    dm_down[k] = 1;                                     /* pressed */
    if (k == KEY_SHIFT) { shift_down = 1; shift_used = 0; continue; }
    if (shift_down) shift_used = 1;
    Act a;
    if (cfg.layout == LAYOUT_C47) {
      int s = shift_down && !shift ? 1 : shift;         /* SHIFT held with a key: f */
      int alpha = (hp_annunc() & HP_ANN_ON) && (hp_annunc() & HP_ANN_ALPHA);
      a = keys_c47(k, s, alpha);
      shift = 0;
    } else a = keys_hp48(k, shift_down);
    do_act(a, k);
  }
}

static void keys_reset(void) {
  for (int k = 0; k < 64; k++) { if (held_hp[k] != HK_NONE) hp_key(held_hp[k], 0); held_hp[k] = HK_NONE; dm_down[k] = 0; }
  if (qh != qt && qphase) hp_key(q[qh].hp, 0);
  qh = qt = qphase = 0;
  shift_down = shift_used = 0;
}

/* ------------------------------------------------------------------ sound: the beeper's edges -> a tone */
static uint32_t tone;
static void sound(void) {
  uint32_t f = 0;
  if (hp.toggles >= 4 && hp.toggle_instr)
    f = (uint32_t)((uint64_t)(hp.toggles - 1) * HP_IPS / 2u / hp.toggle_instr);
  hp.toggles = 0;
  if (!cfg.sound || ST(STAT_BEEP_MUTE) || f < 60 || f > 9000) {
    if (tone) { stop_buzzer(); tone = 0; }
    return;
  }
  uint32_t d = f > tone ? f - tone : tone - f;
  if (!tone || d * 16 > tone) { start_buzzer_freq(f * 1000); tone = f; }
}

/* ------------------------------------------------------------------ screenshot: the LCD as a .bmp in /SCREENS */
static void screenshot(void) {
  lcd_refresh_wait();
  status_now[0] = 0;
  int r = create_screenshot(1);
  say(r < 0 ? "screenshot NOT saved" : "screenshot saved in /SCREENS");
}

/* ------------------------------------------------------------------ the key help: the C47 legends and what they do */
static const struct { int k; const char *n; } KEYNAMES[] = {
  {KEY_SIGMA, "Sig+"}, {KEY_INV, "1/x"}, {KEY_SQRT, "sqrt"}, {KEY_LOG, "LOG"}, {KEY_LN, "LN"}, {KEY_XEQ, "XEQ"},
  {KEY_STO, "STO"}, {KEY_RCL, "RCL"}, {KEY_RDN, "Rdn"}, {KEY_SIN, "SIN"}, {KEY_COS, "COS"}, {KEY_TAN, "TAN"},
  {KEY_ENTER, "ENTER"}, {KEY_SWAP, "x<>y"}, {KEY_CHS, "+/-"}, {KEY_E, "E"}, {KEY_BSP, "<-"},
  {KEY_UP, "up"}, {KEY_7, "7"}, {KEY_8, "8"}, {KEY_9, "9"}, {KEY_DIV, "/"},
  {KEY_DOWN, "down"}, {KEY_4, "4"}, {KEY_5, "5"}, {KEY_6, "6"}, {KEY_MUL, "x"},
  {KEY_SHIFT, "SHIFT"}, {KEY_1, "1"}, {KEY_2, "2"}, {KEY_3, "3"}, {KEY_SUB, "-"},
  {KEY_EXIT, "EXIT"}, {KEY_0, "0"}, {KEY_DOT, "."}, {KEY_RUN, "R/S"}, {KEY_ADD, "+"},
};
#define NKEYS (int)(sizeof KEYNAMES / sizeof *KEYNAMES)

static void keyhelp(void) {
  int page = 0, per = 9, pages = (NKEYS + per - 1) / per;
  wait_release();
  for (;;) {
    char s[80];
    lcd_clear_buf();
    sprintf(s, "C47 keys on the HP  %d/%d   up/down  EXIT", page + 1, pages);
    bar(s);
    text(4, 25, "key     primary / f / g", 0);
    for (int i = 0; i < per && page * per + i < NKEYS; i++) {
      int k = KEYNAMES[page * per + i].k;
      if (k == KEY_SHIFT) snprintf(s, sizeof s, "SHIFT   f, then g, then off");
      else snprintf(s, sizeof s, "%-6s  %s / %s / %s", KEYNAMES[page * per + i].n, keys_c47_label(k, 0),
                    keys_c47_label(k, 1), keys_c47_label(k, 2));
      text(4, 48 + i * 21, s, 0);
    }
    lcd_refresh();
    int k = wait_key();
    if (!k || k == KEY_EXIT) break;
    if (k == KEY_UP && page > 0) page--;
    if ((k == KEY_DOWN || k == KEY_ENTER) && page < pages - 1) page++;
  }
  wait_release();
}

static void keytest(void) {
  lcd_clear_buf();
  bar("Key test: the keys held (EXIT twice ends)");
  lcd_refresh();
  int exits = 0, was = -1;
  for (;;) {
    int a, b;
    read_keys(&a, &b);
    int now = a * 100 + b;
    if (now != was) {
      char s[48];
      sprintf(s, "k1 = %2d   k2 = %2d", a, b);
      lcd_fill_rect(0, 60, LCD_X, 24, 0);
      text(20, 62, s, 0);
      lcd_refresh_lines(60, 24);
      lcd_refresh_wait();
      if (a == KEY_EXIT && was != now) exits++;
      was = now;
    }
    if (exits >= 2 && !a) break;
    sys_delay(20);
    reset_auto_off();
  }
  key_pop_all();
}

/* ------------------------------------------------------------------ About, and DMCP's setup menu (as ZX47) */
static void about(void) {
  char s[80];
  lcd_clear_buf();
  bar("HP47 v" PROGRAM_VERSION "   an HP 48GX for the DM42n");
  int y = 26;
  static const char *L[] = {
    "(C) 2026 Victor Valdes - GPL-3.0-or-later",
    "Saturn CPU: x48 by Eddie C. Dost (GPL-2+)",
    "The ROM is HP's: not part of HP47",
    "DMCP5 SDK (BSD-3, NOMAS)",
  };
  for (unsigned i = 0; i < sizeof L / sizeof *L; i++) { text(4, y, L[i], 0); y += 21; }
  if (rom_where == 4)
    sprintf(s, "ROM: %s, cache %d KB, %u reads", rom_ver[0] ? rom_ver : "none", slots * 2, (unsigned)hp.rom_loads);
  else sprintf(s, "ROM: %s %s (crc %08x)", rom_ver[0] ? rom_ver : "none",
               rom_where == 1 ? "in the heap" : rom_where == 3 ? "built in" : "", (unsigned)rom_crc);
  text(4, y + 4, s, 0); y += 25;
  sprintf(s, "Free heap: %d KB  (largest %d KB)", sys_free_mem() / 1024, sys_largest_free_mem() / 1024);
  text(4, y, s, 0); y += 21;
  sprintf(s, "CPU: %s   LCD: %s", usb_powered() ? "160 MHz (USB)" : "80 MHz (battery)", screen_fast() ? "fast" : "slow path");
  text(4, y, s, 0); y += 21;
  text(4, y + 4, "Written with the help of AI (see README)", 0);
  lcd_refresh();
  wait_release();
  for (;;) { int k = wait_key(); if (!k || k == KEY_EXIT || k == KEY_ENTER) break; }
  wait_release();
}

#define MI_HP47_ABOUT 1
static const uint8_t setup_items[] = {
  MI_SET_TIME, MI_SET_DATE, MI_BEEP_MUTE, MI_SLOW_AUTOREP, MI_MSC, MI_SYSTEM_ENTER, MI_HP47_ABOUT, 0 };
static const smenu_t SETUP_MENU = { "HP47 Setup", setup_items, NULL, NULL };

static int run_menu_item(uint8_t id) {
  if (id == MI_HP47_ABOUT) { about(); return 0; }
  return MRET_UNIMPL;
}

static const char *menu_line_str(uint8_t id, char *s, const int slen) {
  (void)s; (void)slen;
  return id == MI_HP47_ABOUT ? "About HP47 >" : NULL;
}

/* ------------------------------------------------------------------ the HP47 menu */
static void fatal(const char *a, const char *b) {
  lcd_clear_buf();
  bar("HP47 " PROGRAM_VERSION);
  text(4, 40, a, 0);
  text(4, 64, b, 0);
  text(4, 110, "Any key: choose the ROM in " DIR, 0);
  lcd_refresh();
}

static void power_off(void);
static void rom_needed(void) {             /* no ROM: say why, then the file selection screen, until there is one */
  for (;;) {
    fatal(msg, "HP47 needs an HP 48G or 48GX ROM image.");
    wait_release();
    if (!wait_key()) { power_off(); continue; }
    if (rom_pick()) return;
  }
}


static void menu_draw(void) {
  char l[64];
  lcd_clear_buf();
  bar("HP47 " PROGRAM_VERSION "  menu        EXIT: back");
  sprintf(l, "1 keys: %s", LAYOUT_NAME[cfg.layout]);           text(4, 28, l, 0);
  sprintf(l, "2 sound: %s", cfg.sound ? "on" : "off");          text(4, 49, l, 0);
  sprintf(l, "3 speed shown: %s", cfg.speed ? "on" : "off");    text(4, 70, l, 0);
  sprintf(l, "4 speed: %s", cfg.turbo ? "fast (as fast as it goes)" : "an HP 48GX's"); text(4, 91, l, 0);
  text(4, 112, "5 key help  6 key test  7 setup  8 about", 0);
  sprintf(l, "ROM %s   state in " STATE_FILE, rom_ver[0] ? rom_ver : "none");  text(4, 136, l, 0);
  if (msg[0]) text(4, 160, msg, 0);
  static const char *K[6] = { "ROM", "SAVE", "LOAD", "ON+C", "CLEAR", "OFF" };
  lcd_draw_menu_keys(K);
  lcd_refresh();
  lcd_refresh_wait();
}

static int menu_off;                        /* the menu's OFF was chosen */

static void menu_run(void) {
  lcd_refresh_wait();
  stop_buzzer(); tone = 0;
  keys_reset();
  wait_release();
  msg[0] = 0;
  menu_draw();
  for (;;) {
    int k = wait_key();
    if (!k) break;
    switch (k) {
    case KEY_EXIT: case KEY_ENTER: goto out;
    case KEY_F1:
      if (rom_pick() || !rom) {                         /* a failed load may have dropped the old ROM */
        if (!rom && rom_find()) rom_needed();
        hp_start(); hp_rom_version(rom_ver); state_load(); strcpy(msg, "ROM loaded"); goto out;
      }
      break;
    case KEY_F2: strcpy(msg, state_save() ? "state NOT saved" : "state saved in " STATE_FILE); break;
    case KEY_F3: strcpy(msg, state_load() ? "no state: the HP starts afresh" : "state loaded"); break;
    case KEY_F4:                                        /* as ON+C on an HP 48: a restart, the RAM kept */
      hp_reset(); strcpy(msg, "restarted (ON+C)"); goto out;
    case KEY_F5:
      if (confirm("HP47: clear the HP's memory?", "All of the HP's RAM is lost,", "as ON+A+F on an HP 48."))
        { hp_clear_ram(); hp_reset(); strcpy(msg, "memory cleared"); goto out; }
      break;
    case KEY_F6: menu_off = 1; goto out;
    case KEY_1: cfg.layout = (cfg.layout + 1) % LAYOUT_CNT; shift = 0; break;
    case KEY_2: cfg.sound ^= 1; break;
    case KEY_3: cfg.speed ^= 1; break;
    case KEY_4: cfg.turbo ^= 1; break;
    case KEY_5: keyhelp(); break;
    case KEY_6: keytest(); break;
    case KEY_7:                                         /* System may leave HP47: the HP's state first */
      state_save(); rom_close(); wait_release();       /* the USB disk may change the ROM file: read it afresh */
      handle_menu(&SETUP_MENU, MENU_RESET, 0); wait_release(); hp_rom_flush(); break;
    case KEY_8: about(); break;
    default: continue;
    }
    menu_draw();
  }
out:
  cfg_save();
  wait_release();
  frame_ui();
}

/* ------------------------------------------------------------------ power, as the DMCP SDK's template */
static void power_off(void) {               /* the HP's state is saved: it goes on where it was */
  lcd_refresh_wait();
  stop_buzzer(); tone = 0;
  keys_reset();
  state_save();
  rom_close();                                          /* opened again when the HP needs a ROM block */
  uint32_t t0 = now_s();
  SET_ST(STAT_PGM_END);
  if (!ST(STAT_SUSPENDED)) {
    lcd_set_buf_cleared(0);
    draw_power_off_image(1);
    LCD_power_off(0);
    SET_ST(STAT_SUSPENDED);
    SET_ST(STAT_OFF);
  }
  while (ST(STAT_PGM_END) && ST(STAT_SUSPENDED)) { CLR_ST(STAT_RUNNING); sys_sleep(); }   /* ON clears them */
  SET_ST(STAT_RUNNING);
  if (ST(STAT_OFF)) { LCD_power_on(); rtc_wakeup_delay(); CLR_ST(STAT_OFF); }
  uint32_t t1 = now_s();
  if (t1 > t0) hp_time_passed((t1 - t0) * 1000u);       /* the HP's clock moved on while off */
  key_pop_all();
  wait_release();
  if (hp.off) q_push(HK_ON, 0);                         /* the HP was off: ON wakes it, as on a real one */
  frame_ui();
}

/* ------------------------------------------------------------------ start */
void _exit(int code) { (void)code; sys_reset(); for (;;) {} }

void program_main(void) {
  switch_usb_powered_freq();
  run_menu_item_app = run_menu_item;
  menu_line_str_app = menu_line_str;
  for (int k = 0; k < 64; k++) held_hp[k] = HK_NONE;
  ram = calloc(1, HP_RAM_BYTES);
  uint8_t (*lcd)[HP_LCD_BPL] = malloc(HP_LCD_H * HP_LCD_BPL);   /* heap: DMCP has 8 KB of static RAM */
  int s = ram && lcd ? screen_init() : -1;
  if (s == -1) { fatal("HP47: not enough memory", ""); for (;;) wait_for_key_press(); }
  cfg_load();
  if (rom_find()) rom_needed();
  if (hp_start()) { fatal("HP47: not enough memory", ""); for (;;) wait_for_key_press(); }
  hp_rom_version(rom_ver);
  if (state_load() == 0) say("the HP goes on where it was");
  frame_ui();
  if (s == -2) say("LCD layout not learnt: slow drawing");

  uint32_t last = sys_current_ms(), last_auto = last, sec = last, busy_i = 0, busy_ms = 0, owed = 0, frame_t0 = last;
  int busy_prev = 0;
  for (;;) {
    if (ST(STAT_PGM_END) || ST(STAT_SUSPENDED)) power_off();
    if (ST(STAT_POWER_CHANGE)) { switch_usb_powered_freq(); CLR_ST(STAT_POWER_CHANGE); }
    SET_ST(STAT_RUNNING);

    int k1, k2;
    read_keys(&k1, &k2);
    int shot = 0;
    for (int qk = key_pop(); qk >= 0; qk = key_pop()) if (qk == KEY_SCREENSHOT) shot = 1;
    if (shot) screenshot();
    keys_frame(k1, k2);
    if (menu_off) { menu_off = 0; power_off(); last = sys_current_ms(); continue; }
    q_run();

    /* run the HP: in slices, by the wall clock. Its timers get the real time that passed; its CPU gets the
       instructions a real 48GX would run in that time (owed), or in turbo as many as fit. A frame runs at most
       RUN_MS, so the keys and the screen stay live however slow or fast the CPU is. */
    uint32_t t0 = sys_current_ms(), now = t0;
    if (busy_prev) busy_ms += t0 - frame_t0;            /* the last frame (its run and its sleep) was busy */
    frame_t0 = t0;
    for (;;) {
      uint32_t r = now - last;
      last = now;
      if (r > 200) r = 200;                             /* a long wait (a menu): the HP's time does not jump */
      owed += HP_IPS / 1000u * r;
      if (owed > HP_IPS / 20u) owed = HP_IPS / 20u;     /* too slow: at most 50 ms of work carried over */
      uint32_t n = cfg.turbo ? SLICE_I : (owed < SLICE_I ? owed : SLICE_I);
      uint32_t i0 = hp.instr;
      hp_run(r, n);
      uint32_t done = hp.instr - i0;
      owed = hp.sleeping ? 0 : owed > done ? owed - done : 0;
      busy_i += done;
      now = sys_current_ms();
      if (hp.sleeping || hp.bad || now - t0 >= RUN_MS || (!cfg.turbo && !owed)) break;
    }
    busy_prev = !hp.sleeping;
    sound();

    int on = hp_lcd(lcd);
    screen_render(lcd, on);
    status();

    if (hp.bad) {                                       /* an opcode x48 does not know: say where */
      char s2[40];
      if (hp.bad == 2) sprintf(s2, "ROM file read failed (%05x)", (unsigned)hp.bad_pc);
      else sprintf(s2, "HP stopped: opcode at %05x", (unsigned)hp.bad_pc);
      say(s2);
      hp.bad = 0;
      hp_reset();
    }
    if (hp.off && !q_busy() && !dm_down[KEY_EXIT]) { power_off(); last = sys_current_ms(); continue; }

    now = sys_current_ms();
    if (now - sec >= 1000) {                            /* busy: as fast as a real 48GX = 100% */
      pct = busy_ms >= 100 ? (int)((uint64_t)busy_i * 100000u / ((uint64_t)busy_ms * HP_IPS)) : -1;   /* wall time */
      if (pct > 999) pct = 999;
      busy_i = busy_ms = 0; sec = now;
    }
    if (now - last_auto > 10000) { reset_auto_off(); last_auto = now; }

    uint32_t spent = sys_current_ms() - frame_t0;       /* sleep the rest of the frame; longer while the HP sleeps */
    uint32_t want = hp.sleeping && !q_busy() && !k1 && !k2 && !tone ? 50 : FRAME_MS;
    if (spent < want && key_empty()) {
      sys_timer_start(0, want - spent);
      CLR_ST(STAT_RUNNING);
      sys_sleep();
      sys_timer_disable(0);
    }
  }
}
