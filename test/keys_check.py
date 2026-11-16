#!/usr/bin/env python3
"""keys_check.py HPTEST ROM OUTDIR - every C47-layout key of src/keys.c run on the real ROM, its screen drawn with the
C47 legend it stands for, as contact sheets OUTDIR/keys-N.png. Look at them: the screen must show what the legend
says (make keys). The HP starts from a stack with 1 2 3 on it, so stack commands show what they did."""
import os, re, subprocess, sys
from PIL import Image, ImageDraw, ImageFont

hptest, rom, out = sys.argv[1:4]
extra = sys.argv[4].split() if len(sys.argv) > 4 else []   # e.g. "-cache 24": the same screens through the ROM cache
src = open(os.path.join(os.path.dirname(__file__), '..', 'src', 'keys.c')).read()
table = src[src.index('static const C47Key C47[]'):src.index('/* the C47\'s letters')]
ACT = r'(K1\([^)]*\)|K2\([^)]*\)|K3\([^)]*\)|SP\([^)]*\))'
row = re.compile(r'\{KEY_(\w+),\s*' + ACT + r',\s*' + ACT + r',\s*' + ACT + r',\s*"((?:[^"\\]|\\.)*)",\s*"((?:[^"\\]|\\.)*)",\s*"((?:[^"\\]|\\.)*)"\}')
NAMES = {'LS': 'LSHIFT', 'RS': 'RSHIFT'}

def keys(act):
    if act.startswith('SP'):
        return None
    return [NAMES.get(a.strip(), a.strip().replace('HK_', '')) for a in act[3:-1].split(',')]

BOOT = ['WAIT:3000', 'KEY:F', 'WAIT:800', 'KEY:1', 'KEY:ENTER', 'KEY:2', 'KEY:ENTER', 'KEY:3', 'KEY:ENTER', 'WAIT:300']
os.makedirs(out, exist_ok=True)
cases = []
for m in row.finditer(table):
    key = m.group(1)
    for i, shift in enumerate(('', 'f ', 'g ')):
        ks, label = keys(m.group(2 + i)), m.group(5 + i)
        if not ks or label in '0123456789' or label in ('+', '-', 'x', '/', '.'):
            continue
        n = len(cases)
        shot = os.path.join(out, 'k%03d.pbm' % n)
        cmd = [hptest, rom] + extra + BOOT + ['KEY:' + k for k in ks] + ['WAIT:1200', 'SHOT:' + shot]
        r = subprocess.run(cmd, capture_output=True, text=True)
        cases.append(('%s%s: %s' % (shift, key, label), shot, ' '.join(ks), r.returncode))

font = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf', 13)
per, cols, S = 12, 3, 2
for page in range(0, len(cases), per):
    chunk = cases[page:page + per]
    W, H = 131 * S + 16, 64 * S + 40
    sheet = Image.new('RGB', (cols * W, ((len(chunk) + cols - 1) // cols) * H), 'white')
    d = ImageDraw.Draw(sheet)
    for j, (title, shot, ks, rc) in enumerate(chunk):
        x, y = (j % cols) * W, (j // cols) * H
        d.text((x + 4, y + 2), title[:44], font=font, fill='black')
        d.text((x + 4, y + 16), ('HP: ' + ks + ('  FAILED' if rc else ''))[:44], font=font, fill='#1a5fb4')
        if os.path.exists(shot):
            im = Image.open(shot).convert('L').point(lambda v: 255 - v if False else v)
            sheet.paste(im.resize((131 * S, 64 * S)), (x + 4, y + 34))
    sheet.save(os.path.join(out, 'keys-%d.png' % (page // per + 1)))
print('%d keys -> %s/keys-*.png' % (len(cases), out))
