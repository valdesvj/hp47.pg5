/* keys.h - HP47: the DM42n's keys as the HP 48's, in two layouts:
 *   C47    the C47 overlay's legends (primary, f, g on the one SHIFT key) do the HP 48's function of the same meaning
 *   HP48   the HP 48's keyboard by position, SHIFT as the HP's own left/right shift
 * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#ifndef HP47_KEYS_H
#define HP47_KEYS_H
#include <stdint.h>

enum { LAYOUT_C47, LAYOUT_HP48, LAYOUT_CNT };
extern const char *const LAYOUT_NAME[LAYOUT_CNT];

/* what a calculator key does: up to 3 HP keys pressed in turn (the last one held while the key is), or one of HP47's own */
#define ACT_N 3
typedef struct { uint16_t k[ACT_N]; } Act;
enum { SP_MENU = 0xF001, SP_SHOT = 0xF002, SP_NONE = 0xFFFF };

/* shift: 0 none, 1 f, 2 g (C47); alpha: the HP is in alpha mode; held: SHIFT held down (HP48's hold layer) */
Act keys_c47(int dmcp_key, int shift, int alpha);
Act keys_hp48(int dmcp_key, int held);
const char *keys_c47_label(int dmcp_key, int shift);   /* for the key help screen (NULL: nothing) */
#endif
