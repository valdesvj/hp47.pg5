/* screen.h - HP47: the HP 48's 131 x 64 LCD three times as big (393 x 192) on the DM42n's LCD, the lines that
 * changed only. Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#ifndef HP47_SCREEN_H
#define HP47_SCREEN_H
#include <stdint.h>
#include "hp48.h"

#define SCALE 3
#define SCR_X 4                         /* (400 - 393) / 2 */
#define SCR_Y 24                        /* below the status line */
#define SCR_W (HP_LCD_W * SCALE)
#define SCR_H (HP_LCD_H * SCALE)

int  screen_init(void);                 /* learns the LCD buffer's layout; 0 ok, -1 no memory, -2 slow path */
void screen_invalidate(void);           /* the next render sends every line */
int  screen_render(const uint8_t lcd[HP_LCD_H][HP_LCD_BPL], int on);   /* returns the HP lines sent */
int  screen_fast(void);
#endif
