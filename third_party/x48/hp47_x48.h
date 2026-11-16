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
#endif
