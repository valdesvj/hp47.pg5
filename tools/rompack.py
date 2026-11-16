#!/usr/bin/env python3
"""rompack.py ROM OUT.bin - an HP 48G/GX ROM image as the packed 512 KB HP47 builds into its program (make ROM_IN=...).
The ROM may be packed (512 KB, the low nibble first: gxrom-r) or unpacked (1 MB, a nibble a byte, as Emu48's)."""
import sys

rom, out = sys.argv[1:3]
b = open(rom, 'rb').read()
if len(b) == 2 * 524288:
    b = bytes((b[2 * i] & 15) | (b[2 * i + 1] & 15) << 4 for i in range(524288))
if len(b) != 524288:
    sys.exit('rompack: %s is %d bytes: not a 48G/GX ROM (512 KB or 1 MB)' % (rom, len(b)))
nib = lambda a: b[a >> 1] >> (a & 1) * 4 & 15
if bytes(nib(0x7ffbf + 2 * i) | nib(0x7ffc0 + 2 * i) << 4 for i in range(5)) != b'HP48-':
    sys.exit('rompack: %s is not a 48G/GX ROM (no "HP48-" at nibble 7FFBF)' % rom)
open(out, 'wb').write(b)
print('rompack: %s -> %s (built into the program: do not share this HP47.pg5)' % (rom, out))
