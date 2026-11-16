/* hp47_x48.h - what x48's emulate.c and actions.c need from the parts of x48 HP47 does not use.
 * HP47 (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#ifndef HP47_X48_H
#define HP47_X48_H
#include "hp48.h"
#define ILLEGAL_INSTRUCTION 0x01
#define TRAP_INSTRUCTION    0x02
extern int enter_debugger;              /* set by an unknown opcode or the TRAP (src/hp48.c reports it) */
extern int in_debugger;
extern int opt_gx;
extern unsigned long instructions;
extern void hp47_out_changed(void);     /* OUT written: the beeper (src/hp48.c) */
extern void hp47_mmu_changed(void);     /* CONFIG, UNCNFG, RESET: src/hp48.c rebuilds its page table */

/* HP47: the instruction's first 16 nibbles, fetched at once before each instruction (src/hp48.c, hp_run): x48
   reads the opcode a nibble at a time, often the same nibble again; these come from the window instead. Any other
   address goes to read_nibble as before; a write into the window empties it. */
#include <stdint.h>
extern uint64_t hp47_win;
extern uint32_t hp47_pc0, hp47_wlen;
extern int read_nibble(long addr);
extern long read_nibbles(long addr, int len);
static inline int fetch_nib(long a) {
  uint32_t d = ((uint32_t)a - hp47_pc0) & 0xfffff;
  return d < hp47_wlen ? (int)(hp47_win >> (d * 4)) & 15 : read_nibble(a);
}
static inline long fetch_nibs(long a, int n) {
  uint32_t d = ((uint32_t)a - hp47_pc0) & 0xfffff;
  if (n <= 8 && d + (uint32_t)n <= hp47_wlen) return (long)((hp47_win >> (d * 4)) & ((1ull << (4 * n)) - 1));
  return read_nibbles(a, n);
}
#endif
