/* keys.c - HP47: the DM42n's keys as the HP 48's. See keys.h.
 *
 * C47 layout: the C47 overlay's legends (gitlab rpncalculators/c43 src/c47/assign.c, kbd_std_C47) do the HP 48's
 * function of the same meaning, as the HP key sequence that does it on a 48G (ROM R): f x^2 = LS SQRT. Where the C47
 * function has no HP 48 match, an HP 48 essential that has no key here takes its place (marked "HP" below). The HP's
 * shifted functions were read off the ROM in the PC tests (test/keys.sh draws them all).
 * In the HP's alpha mode the C47's letters type (A..O on the top keys, P..Z on the digit block); f + a letter key types
 * it in lower case, f + a digit-block key the digit.
 *
 * HP48 layout: the HP 48's keys by position (F1..F6 = A..F, the bottom block as the HP's), SHIFT tapped = the HP's
 * left shift, tapped again = right shift, again = none. The six HP keys that do not fit are on SHIFT held down + a key.
 * Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later. */
#include <main.h>
#include <dmcp.h>
#include <stddef.h>
#include "keys.h"
#include "hp48.h"

const char *const LAYOUT_NAME[LAYOUT_CNT] = { "C47", "HP48" };

#define N HK_NONE
#define LS HK_LSHIFT
#define RS HK_RSHIFT
#define K1(a) { { a, N, N } }
#define K2(a, b) { { a, b, N } }
#define K3(a, b, c) { { a, b, c } }
#define SP(x) { { x, N, N } }

typedef struct { uint8_t key; Act p, f, g; const char *lp, *lf, *lg; } C47Key;

static const C47Key C47[] = {
  /* key        primary              f                       g                     the legends (help screen) */
  {KEY_SIGMA, K1(HK_QUOTE),        K1(HK_MTH),             K1(HK_PRG),            "' (HP)", "MTH (HP)", "PRG (HP)"},
  {KEY_INV,   K1(HK_INV),          K1(HK_POW),             K1(HK_CST),            "1/x", "y^x", "CST (HP)"},
  {KEY_SQRT,  K1(HK_SQRT),         K2(LS, HK_SQRT),        K1(HK_VAR),            "sqrt x", "x^2", "VAR (HP)"},
  {KEY_LOG,   K2(RS, HK_POW),      K2(LS, HK_POW),         K1(HK_DEL),            "LOG", "10^x", "DEL (HP)"},
  {KEY_LN,    K2(RS, HK_INV),      K2(LS, HK_INV),         K2(LS, HK_QUOTE),      "LN", "e^x", "UP dir (HP)"},
  {KEY_XEQ,   K1(HK_EVAL),         K1(HK_ALPHA),           K2(LS, HK_NXT),        "EVAL", "alpha", "PREV menu (HP)"},
  {KEY_STO,   K1(HK_STO),          K2(LS, HK_STO),         K2(LS, HK_EEX),        "STO", "DEF (HP)", "PURGE (HP)"},
  {KEY_RCL,   K2(RS, HK_STO),      K2(LS, HK_EVAL),        K2(LS, HK_VAR),        "RCL", "->NUM (HP)", "MEMORY (HP)"},
  {KEY_RDN,   K1(HK_NXT),          K2(LS, HK_SPC),         K2(RS, HK_SQRT),       "NXT menu (HP)", "pi", "x root y"},
  {KEY_SIN,   K1(HK_SIN),          K2(LS, HK_SIN),         K2(RS, HK_SIN),        "SIN", "ASIN", "d/dx (HP)"},
  {KEY_COS,   K1(HK_COS),          K2(LS, HK_COS),         K2(RS, HK_COS),        "COS", "ACOS", "integral (HP)"},
  {KEY_TAN,   K1(HK_TAN),          K2(LS, HK_TAN),         K2(RS, HK_TAN),        "TAN", "ATAN", "sum (HP)"},
  {KEY_ENTER, K1(HK_ENTER),        K2(LS, HK_DIV),         K2(RS, HK_SPC),        "ENTER", "( ) complex", "angle (CPX)"},
  {KEY_SWAP,  K1(HK_RIGHT),        K2(RS, HK_EEX),         K2(RS, HK_UP),         "x<>y = right", "LAST ARG", "STACK view"},
  {KEY_CHS,   K1(HK_NEG),          K2(RS, HK_CST),         K2(LS, HK_MTH),        "+/-", "MODES", "DEG/RAD"},
  {KEY_E,     K1(HK_EEX),          K2(LS, HK_NEG),         K2(RS, HK_NEG),        "EEX", "EDIT (HP)", "CMD (HP)"},
  {KEY_BSP,   K1(HK_BS),           K2(RS, HK_EVAL),        K2(LS, HK_DEL),        "<-", "UNDO", "CLEAR"},
  {KEY_UP,    K1(HK_UP),           K1(HK_LEFT),            K2(LS, HK_UP),         "up", "left (BST)", "STACK menu"},
  {KEY_DOWN,  K1(HK_DOWN),         K2(RS, HK_DOWN),        K2(LS, HK_LEFT),       "down", "REVIEW", "GRAPH (HP)"},
  {KEY_7,     K1(HK_7),            K2(LS, HK_ENTER),       K2(RS, HK_QUOTE),      "7", "EQUATION (EQN)", "HOME"},
  {KEY_8,     K1(HK_8),            K2(RS, HK_7),           K2(LS, HK_7),          "8", "SOLVE (ADV)", "SOLVE menu (FIN)"},
  {KEY_9,     K1(HK_9),            K2(RS, HK_ENTER),       K2(RS, HK_9),          "9", "MATRIX (MATX)", "SYMBOLIC (XFN)"},
  {KEY_DIV,   K1(HK_DIV),          K2(RS, HK_5),           K2(RS, HK_8),          "/", "STAT", "PLOT"},
  {KEY_4,     K1(HK_4),            K2(RS, HK_DIV),         K2(HK_MTH, HK_F),      "4", "# (BASE)", "BASE menu (BITS)"},
  {KEY_5,     K1(HK_5),            K2(LS, HK_6),           K2(RS, HK_4),          "5", "UNITS", "TIME (CLK)"},
  {KEY_6,     K1(HK_6),            K2(LS, HK_CST),         K2(RS, HK_6),          "6", "MODES menu (FLAGS)", "UNITS catalog"},
  {KEY_MUL,   K1(HK_MUL),          K3(HK_MTH, HK_NXT, HK_A), K2(LS, HK_MUL),      "x", "PROB", "[ ] (HP)"},
  {KEY_1,     K1(HK_1),            K2(RS, HK_MUL),         K2(RS, HK_ALPHA),      "1", "_ units (HP)", "program ENTRY (HP)"},
  {KEY_2,     K1(HK_2),            K2(LS, HK_ALPHA),       K2(RS, HK_PRG),        "2", "USER", "CHARS"},
  {KEY_3,     K1(HK_3),            K1(HK_PRG),             K2(LS, HK_2),          "3", "PRG (PFN)", "LIBRARY (HP)"},
  {KEY_SUB,   K1(HK_SUB),          K2(LS, HK_1),           K2(RS, HK_1),          "-", "I/O menu (PRINT)", "I/O"},
  {KEY_EXIT,  K1(HK_ON),           K2(RS, HK_ON),          SP(SP_SHOT),           "ON / CANCEL", "OFF", "screenshot"},
  {KEY_0,     K1(HK_0),            K2(LS, HK_0),           K2(RS, HK_0),          "0", "= (HP)", "-> (HP)"},
  {KEY_DOT,   K1(HK_DOT),          K2(LS, HK_DOT),         SP(SP_MENU),           ".", ", (HP)", "HP47 menu (INFO)"},
  {KEY_RUN,   K1(HK_SPC),          K2(LS, HK_SUB),         K2(RS, HK_SUB),        "SPC (HP)", "<< >> (PR)", "\" \" (HP)"},
  {KEY_ADD,   K1(HK_ADD),          K2(LS, HK_ADD),         K2(RS, HK_3),          "+", "{ } (HP)", "EQ LIB (CONST)"},
};

/* the C47's letters on the HP's letter keys (the HP 48: A..F soft keys, G..L MTH..NXT, M..R '..right,
   S..X SIN..1/x, Y +/-, Z EEX) */
static const struct { uint8_t key; uint16_t hp; } LETTER[] = {
  {KEY_SIGMA, HK_A}, {KEY_INV, HK_B}, {KEY_SQRT, HK_C}, {KEY_LOG, HK_D}, {KEY_LN, HK_E}, {KEY_XEQ, HK_F},
  {KEY_STO, HK_MTH}, {KEY_RCL, HK_PRG}, {KEY_RDN, HK_CST}, {KEY_SIN, HK_VAR}, {KEY_COS, HK_UP}, {KEY_TAN, HK_NXT},
  {KEY_SWAP, HK_QUOTE}, {KEY_CHS, HK_STO}, {KEY_E, HK_EVAL},
  {KEY_7, HK_LEFT}, {KEY_8, HK_DOWN}, {KEY_9, HK_RIGHT}, {KEY_DIV, HK_SIN},
  {KEY_4, HK_COS}, {KEY_5, HK_TAN}, {KEY_6, HK_SQRT}, {KEY_MUL, HK_POW},
  {KEY_1, HK_INV}, {KEY_2, HK_NEG}, {KEY_3, HK_EEX},
};

static const struct { uint8_t key; uint16_t hp; } DIGIT[] = {
  {KEY_7, HK_7}, {KEY_8, HK_8}, {KEY_9, HK_9}, {KEY_4, HK_4}, {KEY_5, HK_5}, {KEY_6, HK_6},
  {KEY_1, HK_1}, {KEY_2, HK_2}, {KEY_3, HK_3}, {KEY_DIV, HK_DIV}, {KEY_MUL, HK_MUL},
};

static Act none(void) { Act a = K1(N); return a; }

static int softkey(int k) { return k >= KEY_F1 && k <= KEY_F6; }
static const uint16_t SOFT[6] = { HK_A, HK_B, HK_C, HK_D, HK_E, HK_F };

Act keys_c47(int k, int shift, int alpha) {
  if (softkey(k)) {
    Act a = K1(SOFT[k - KEY_F1]);
    if (shift == 1) { a.k[0] = LS; a.k[1] = SOFT[k - KEY_F1]; }        /* the HP's own shifted soft keys */
    if (shift == 2) { a.k[0] = RS; a.k[1] = SOFT[k - KEY_F1]; }
    return a;
  }
  if (alpha && shift != 2) {
    if (shift == 1)
      for (unsigned i = 0; i < sizeof DIGIT / sizeof *DIGIT; i++)
        if (DIGIT[i].key == k) { Act a = K1(DIGIT[i].hp); return a; }
    for (unsigned i = 0; i < sizeof LETTER / sizeof *LETTER; i++)
      if (LETTER[i].key == k) {
        Act a = K1(LETTER[i].hp);
        if (shift == 1) { a.k[0] = LS; a.k[1] = LETTER[i].hp; }      /* lower case */
        return a;
      }
  }
  for (unsigned i = 0; i < sizeof C47 / sizeof *C47; i++)
    if (C47[i].key == k) return shift == 1 ? C47[i].f : shift == 2 ? C47[i].g : C47[i].p;
  return none();
}

const char *keys_c47_label(int k, int shift) {
  for (unsigned i = 0; i < sizeof C47 / sizeof *C47; i++)
    if (C47[i].key == k) return shift == 1 ? C47[i].lf : shift == 2 ? C47[i].lg : C47[i].lp;
  return NULL;
}

/* ------------------------------------------------------------------ HP48: by position */
static const struct { uint8_t key; uint16_t hp, held; } HP48[] = {
  /* key        the HP key      SHIFT held + the key */
  {KEY_SIGMA, HK_QUOTE,       HK_MTH},
  {KEY_INV,   HK_INV,         HK_PRG},
  {KEY_SQRT,  HK_SQRT,        HK_CST},
  {KEY_LOG,   HK_POW,         N},
  {KEY_LN,    HK_ALPHA,       N},
  {KEY_XEQ,   HK_EVAL,        N},
  {KEY_STO,   HK_STO,         N},
  {KEY_RCL,   HK_VAR,         N},
  {KEY_RDN,   HK_NXT,         N},
  {KEY_SIN,   HK_SIN,         N}, {KEY_COS, HK_COS, N}, {KEY_TAN, HK_TAN, N},
  {KEY_ENTER, HK_ENTER,       N},
  {KEY_SWAP,  HK_RIGHT,       HK_LEFT},
  {KEY_CHS,   HK_NEG,         N},
  {KEY_E,     HK_EEX,         N},
  {KEY_BSP,   HK_BS,          HK_DEL},
  {KEY_UP,    HK_UP,          N}, {KEY_DOWN, HK_DOWN, N},
  {KEY_7, HK_7, N}, {KEY_8, HK_8, N}, {KEY_9, HK_9, N}, {KEY_DIV, HK_DIV, N},
  {KEY_4, HK_4, N}, {KEY_5, HK_5, N}, {KEY_6, HK_6, N}, {KEY_MUL, HK_MUL, N},
  {KEY_1, HK_1, N}, {KEY_2, HK_2, N}, {KEY_3, HK_3, N}, {KEY_SUB, HK_SUB, N},
  {KEY_EXIT,  HK_ON,          SP_MENU},
  {KEY_0, HK_0, N}, {KEY_DOT, HK_DOT, SP_SHOT}, {KEY_RUN, HK_SPC, N}, {KEY_ADD, HK_ADD, N},
};

Act keys_hp48(int k, int held) {
  if (softkey(k)) { Act a = K1(SOFT[k - KEY_F1]); return a; }
  for (unsigned i = 0; i < sizeof HP48 / sizeof *HP48; i++)
    if (HP48[i].key == k) { Act a = K1(held ? HP48[i].held : HP48[i].hp); return a; }
  return none();
}
