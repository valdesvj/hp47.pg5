#!/usr/bin/env python3
"""keymap.py OUT.png - HP47's C47 layout drawn on the DM42n's keyboard, from src/keys.c (the legends there).
Each key: the C47 key (small, top), what it does on the HP (big), f and g (small, bottom).
Usage: python3 tools/keymap.py docs/keymap.png"""
import os
import re
import sys
from PIL import Image, ImageDraw, ImageFont

F = '/usr/share/fonts/truetype/dejavu/'
small = ImageFont.truetype(F + 'DejaVuSansCondensed.ttf', 13)
big = ImageFont.truetype(F + 'DejaVuSansCondensed-Bold.ttf', 17)
title = ImageFont.truetype(F + 'DejaVuSans-Bold.ttf', 24)

src = open(os.path.join(os.path.dirname(__file__), '..', 'src', 'keys.c')).read()
Q = r'"((?:[^"\\]|\\.)*)"'
LAB = {m.group(1): [x.replace('\\"', '"').replace(' (HP)', '') for x in m.group(2, 3, 4)]
       for m in re.finditer(r'\{KEY_(\w+),[^"]*' + Q + r',\s*' + Q + r',\s*' + Q + r'\}', src)}

# the DM42n: (DMCP name, the key's own legend, width in columns)
ROWS = [
    [('F1', 'F1', 1), ('F2', 'F2', 1), ('F3', 'F3', 1), ('F4', 'F4', 1), ('F5', 'F5', 1), ('F6', 'F6', 1)],
    [('SIGMA', 'Σ+', 1), ('INV', '1/x', 1), ('SQRT', '√x', 1), ('LOG', 'LOG', 1), ('LN', 'LN', 1), ('XEQ', 'XEQ', 1)],
    [('STO', 'STO', 1), ('RCL', 'RCL', 1), ('RDN', 'R↓', 1), ('SIN', 'SIN', 1), ('COS', 'COS', 1), ('TAN', 'TAN', 1)],
    [('ENTER', 'ENTER', 2), ('SWAP', 'x≷y', 1), ('CHS', '+/−', 1), ('E', 'E', 1), ('BSP', '←', 1)],
    [('UP', '▲', 1), ('7', '7', 1), ('8', '8', 1), ('9', '9', 1), ('DIV', '÷', 1)],
    [('DOWN', '▼', 1), ('4', '4', 1), ('5', '5', 1), ('6', '6', 1), ('MUL', '×', 1)],
    [('SHIFT', 'SHIFT', 1), ('1', '1', 1), ('2', '2', 1), ('3', '3', 1), ('SUB', '−', 1)],
    [('EXIT', 'EXIT', 1), ('0', '0', 1), ('DOT', '.', 1), ('RUN', 'R/S', 1), ('ADD', '+', 1)],
]
SOFT = {'F%d' % i: ['HP soft key ' + 'ABCDEF'[i - 1], '', ''] for i in range(1, 7)}
SOFT['SHIFT'] = ['f / g', 'tap: f', 'tap twice: g']

W, KH, GAP, M, TOP = 1100, 96, 10, 24, 70
im = Image.new('RGB', (W, TOP + len(ROWS) * (KH + GAP) + 90), 'white')
d = ImageDraw.Draw(im)
d.text((M, 18), 'HP47 keys on the DM42n (C47 layout)', font=title, fill='black')
d.text((M + 520, 26), 'big: the key   ·   orange: f (SHIFT)   ·   blue: g (SHIFT SHIFT)', font=small, fill='#444')


def fit(s, font, w):
    while s and d.textlength(s, font=font) > w:
        s = s[:-1]
    return s


for r, row in enumerate(ROWS):
    units = sum(k[2] for k in row)
    unit = (W - 2 * M - (units - 1) * GAP) / units
    x, y = M, TOP + r * (KH + GAP)
    for name, leg, w in row:
        kw = unit * w + GAP * (w - 1)
        p, f, g = SOFT.get(name) or LAB.get(name, ['', '', ''])
        d.rounded_rectangle([x, y, x + kw, y + KH], radius=10, fill='#e9ece6', outline='#20262a', width=2)
        d.text((x + 8, y + 4), leg, font=small, fill='#555')
        p = fit(p, big, kw - 12)
        d.text((x + (kw - d.textlength(p, font=big)) / 2, y + 26), p, font=big, fill='black')
        if f:
            d.text((x + 6, y + KH - 38), fit('f ' + f, small, kw - 10), font=small, fill='#c05a00')
        if g:
            d.text((x + 6, y + KH - 20), fit('g ' + g, small, kw - 10), font=small, fill='#1a5fb4')
        x += kw + GAP

y = TOP + len(ROWS) * (KH + GAP) + 6
for i, line in enumerate([
        'In the HP\'s alpha mode (f XEQ) the keys type the C47\'s letters: Σ+ … E = A … O, the digit block P … Z.',
        'The HP draws its menu labels at the bottom of its screen, right above F1 … F6.   g .: the HP47 menu.',
        'HP48 layout (menu 1): the HP 48\'s keys by position, SHIFT = the HP\'s own shifts.']):
    d.text((M, y + i * 22), line, font=small, fill='black')
im.save(sys.argv[1])
