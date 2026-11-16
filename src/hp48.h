/* hp48.h - HP47: the HP 48GX around x48's Saturn CPU (third_party/x48): the memory controller with the ROM and the RAM
 * packed two nibbles a byte, the I/O registers, the timers, the keyboard and the LCD.
 * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#ifndef HP47_HP48_H
#define HP47_HP48_H
#include <stdint.h>

#define HP_ROM_BYTES  (512u * 1024u)    /* a 48G/GX ROM image: 1 M nibbles, low nibble first (as gxrom-r) */
#define HP_RAM_BYTES  (128u * 1024u)    /* the 48GX's RAM */
#define HP_LCD_W 131
#define HP_LCD_H 64
#define HP_LCD_BPL 17                   /* bytes a line in hp_lcd(): bit 7 of byte 0 = the left pixel */

/* annunciators (hp_annunc): the bits are on only when bit 7 (all on) is set */
#define HP_ANN_ON    0x80
#define HP_ANN_LEFT  0x01
#define HP_ANN_RIGHT 0x02
#define HP_ANN_ALPHA 0x04
#define HP_ANN_BATT  0x08
#define HP_ANN_BUSY  0x10
#define HP_ANN_IO    0x20

/* the keys: (OUT bit) * 16 + IN bit, as x48; ON is 0x8000 */
enum {
  HK_A = 0x14, HK_B = 0x84, HK_C = 0x83, HK_D = 0x82, HK_E = 0x81, HK_F = 0x80,
  HK_MTH = 0x24, HK_PRG = 0x74, HK_CST = 0x73, HK_VAR = 0x72, HK_UP = 0x71, HK_NXT = 0x70,
  HK_QUOTE = 0x04, HK_STO = 0x64, HK_EVAL = 0x63, HK_LEFT = 0x62, HK_DOWN = 0x61, HK_RIGHT = 0x60,
  HK_SIN = 0x34, HK_COS = 0x54, HK_TAN = 0x53, HK_SQRT = 0x52, HK_POW = 0x51, HK_INV = 0x50,
  HK_ENTER = 0x44, HK_NEG = 0x43, HK_EEX = 0x42, HK_DEL = 0x41, HK_BS = 0x40,
  HK_ALPHA = 0x35, HK_7 = 0x33, HK_8 = 0x32, HK_9 = 0x31, HK_DIV = 0x30,
  HK_LSHIFT = 0x25, HK_4 = 0x23, HK_5 = 0x22, HK_6 = 0x21, HK_MUL = 0x20,
  HK_RSHIFT = 0x15, HK_1 = 0x13, HK_2 = 0x12, HK_3 = 0x11, HK_SUB = 0x10,
  HK_ON = 0x8000, HK_0 = 0x03, HK_DOT = 0x02, HK_SPC = 0x01, HK_ADD = 0x00,
  HK_NONE = 0xFFFF
};

typedef struct {
  uint32_t instr;                       /* instructions run */
  uint32_t toggles;                     /* beeper edges (OUT bit 11) since hp_toggles_take() */
  uint32_t toggle_instr;                /* instructions between the first and last of those edges */
  int sleeping;                         /* SHUTDN: waiting for a timer or a key */
  int off;                              /* sleeping with OUT = 0: the HP is off (ON wakes it) */
  int bad;                              /* an unknown opcode stopped the CPU (its PC in bad_pc) */
  uint32_t bad_pc;
} HpStat;
extern HpStat hp;

/* rom: HP_ROM_BYTES (stays where it is: flash, QSPI or heap); ram: HP_RAM_BYTES */
int  hp_init(const uint8_t *rom, uint8_t *ram);
void hp_reset(void);                    /* the CPU and the chips (the RAM is kept: the ROM checks it, as ON+C) */
void hp_clear_ram(void);                /* all of the RAM to zero: the next boot is a "Memory Clear" */
void hp_run(uint32_t ms, uint32_t max_instr);   /* ms of HP time: the timers by the ms, at most max_instr instructions */
void hp_key(int code, int down);
int  hp_lcd(uint8_t out[HP_LCD_H][HP_LCD_BPL]);   /* the screen as bits; 0 when the display is off */
int  hp_annunc(void);
const char *hp_rom_version(char s[16]); /* "HPHP48-R" -> "R" ... from the ROM; NULL if not a 48G ROM */

/* the state to keep while the calculator is off (with the RAM): hp_state_size() bytes */
uint32_t hp_state_size(void);
void hp_state_save(uint8_t *d);
int  hp_state_load(const uint8_t *d, uint32_t n);   /* 0 ok */
void hp_time_passed(uint32_t ms);       /* the HP was not running (off): its timer 2 (the clock) moves on */
#endif
