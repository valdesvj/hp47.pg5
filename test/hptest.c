/* hptest.c - HP47 on the PC: boots a 48G/GX ROM and types on it, checking the screen.
 *   hptest ROM [-v] [script...]
 * The script: words run in order, each a frame of 20 ms of HP time unless WAIT:
 *   WAIT:ms       run that long        KEY:name       press and release a key (as hp48.h without HK_: 7 ENTER LSHIFT ...)
 *   SHOT:file     the LCD as a .pbm (test/run.sh compares them with test/ref)
 *   STATE:file    save the state and RAM, then load them back (the round trip)
 *   BEEPSTAT      print the beeper's edges since the last BEEPSTAT
 * With no script: boot, then show the screen. -v prints the screen as text after each SHOT.
 * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hp48.h"

#define BUDGET 12000                    /* instructions a 20 ms frame (about 600 000 a second) */

static uint8_t *rom, *ram;
static int verbose;
static uint8_t lcd[HP_LCD_H][HP_LCD_BPL];

static void frames(int ms) { for (int t = 0; t < ms; t += 20) hp_run(20, BUDGET); }

static void ascii(void) {
  hp_lcd(lcd);
  int a = hp_annunc();
  printf("+%.*s+  ann %02x%s%s%s%s  PC %05lx%s\n", HP_LCD_W / 2, "--------------------------------------------------------------------",
         a, a & HP_ANN_LEFT ? " LS" : "", a & HP_ANN_RIGHT ? " RS" : "", a & HP_ANN_ALPHA ? " ALPHA" : "",
         a & HP_ANN_BUSY ? " BUSY" : "", 0L, hp.sleeping ? " (sleeping)" : "");
  for (int y = 0; y < HP_LCD_H; y += 2) {
    putchar('|');
    for (int x = 0; x < HP_LCD_W; x += 2) {
      int n = 0;
      for (int dy = 0; dy < 2; dy++) for (int dx = 0; dx < 2 && x + dx < HP_LCD_W; dx++)
        n += lcd[y + dy][(x + dx) >> 3] >> (7 - ((x + dx) & 7)) & 1;
      putchar(n >= 2 ? '#' : n ? '+' : ' ');
    }
    printf("|\n");
  }
}

static void shot(const char *fn) {
  hp_lcd(lcd);
  FILE *f = fopen(fn, "w");
  if (!f) { perror(fn); exit(2); }
  fprintf(f, "P1\n%d %d\n", HP_LCD_W, HP_LCD_H);
  for (int y = 0; y < HP_LCD_H; y++) {
    for (int x = 0; x < HP_LCD_W; x++) fputc(lcd[y][x >> 3] >> (7 - (x & 7)) & 1 ? '1' : '0', f);
    fputc('\n', f);
  }
  fclose(f);
  if (verbose) ascii();
}

static const struct { const char *n; int k; } KEYS[] = {
  {"A",HK_A},{"B",HK_B},{"C",HK_C},{"D",HK_D},{"E",HK_E},{"F",HK_F},{"MTH",HK_MTH},{"PRG",HK_PRG},{"CST",HK_CST},
  {"VAR",HK_VAR},{"UP",HK_UP},{"NXT",HK_NXT},{"QUOTE",HK_QUOTE},{"STO",HK_STO},{"EVAL",HK_EVAL},{"LEFT",HK_LEFT},
  {"DOWN",HK_DOWN},{"RIGHT",HK_RIGHT},{"SIN",HK_SIN},{"COS",HK_COS},{"TAN",HK_TAN},{"SQRT",HK_SQRT},{"POW",HK_POW},
  {"INV",HK_INV},{"ENTER",HK_ENTER},{"NEG",HK_NEG},{"EEX",HK_EEX},{"DEL",HK_DEL},{"BS",HK_BS},{"ALPHA",HK_ALPHA},
  {"7",HK_7},{"8",HK_8},{"9",HK_9},{"DIV",HK_DIV},{"LSHIFT",HK_LSHIFT},{"4",HK_4},{"5",HK_5},{"6",HK_6},
  {"MUL",HK_MUL},{"RSHIFT",HK_RSHIFT},{"1",HK_1},{"2",HK_2},{"3",HK_3},{"SUB",HK_SUB},{"ON",HK_ON},{"0",HK_0},
  {"DOT",HK_DOT},{"SPC",HK_SPC},{"ADD",HK_ADD},
};

static int key(const char *n) {
  for (unsigned i = 0; i < sizeof KEYS / sizeof *KEYS; i++) if (!strcmp(KEYS[i].n, n)) return KEYS[i].k;
  fprintf(stderr, "hptest: no key %s\n", n);
  exit(2);
}

static void state_round_trip(const char *fn) {
  uint32_t n = hp_state_size();
  uint8_t *b = malloc(n + HP_RAM_BYTES);
  hp_state_save(b);
  memcpy(b + n, ram, HP_RAM_BYTES);
  FILE *f = fopen(fn, "wb"); fwrite(b, 1, n + HP_RAM_BYTES, f); fclose(f);
  memset(ram, 0x55, HP_RAM_BYTES);
  hp_reset();
  f = fopen(fn, "rb"); size_t got = fread(b, 1, n + HP_RAM_BYTES, f); fclose(f);
  memcpy(ram, b + n, HP_RAM_BYTES);
  if (got != n + HP_RAM_BYTES || hp_state_load(b, n)) { fprintf(stderr, "hptest: state did not load\n"); exit(1); }
  free(b);
}

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: hptest ROM [-v] [script]\n"); return 2; }
  FILE *f = fopen(argv[1], "rb");
  if (!f) { perror(argv[1]); return 2; }
  rom = malloc(HP_ROM_BYTES);
  ram = calloc(1, HP_RAM_BYTES);
  size_t n = fread(rom, 1, HP_ROM_BYTES, f);
  fclose(f);
  if (n != HP_ROM_BYTES) { fprintf(stderr, "hptest: %s is %zu bytes, not %u\n", argv[1], n, HP_ROM_BYTES); return 2; }
  hp_init(rom, ram);
  char v[16];
  printf("ROM: %s\n", hp_rom_version(v) ? v : "not a 48G ROM");
  int i = 2;
  if (i < argc && !strcmp(argv[i], "-v")) { verbose = 1; i++; }
  if (i >= argc) { frames(4000); ascii(); }
  int fails = 0;
  for (; i < argc; i++) {
    const char *w = argv[i];
    if (!strncmp(w, "WAIT:", 5)) frames(atoi(w + 5));
    else if (!strncmp(w, "KEY:", 4)) { int k = key(w + 4); hp_key(k, 1); frames(100); hp_key(k, 0); frames(100); }
    else if (!strncmp(w, "SHOT:", 5)) shot(w + 5);
    else if (!strncmp(w, "STATE:", 6)) state_round_trip(w + 6);
    else if (!strcmp(w, "BEEPSTAT")) { printf("beeper: %u edges in %u instructions\n", hp.toggles, hp.toggle_instr); hp.toggles = 0; }
    else { fprintf(stderr, "hptest: what is %s\n", w); return 2; }
    if (hp.bad) { printf("unknown opcode at %05x\n", hp.bad_pc); fails++; break; }
  }
  printf("%u instructions, %s\n", hp.instr, hp.sleeping ? "sleeping" : "running");
  return fails ? 1 : 0;
}
