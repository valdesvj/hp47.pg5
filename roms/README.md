# The HP 48G/GX ROM

HP47 runs HP's own ROM, which is **not part of HP47** and is not in this repository. HP's 48-series ROM images are
freely downloadable (since 2000), but there is no licence to redistribute them, so, like Emu48 and x48, HP47 does not
ship one.

- For the calculator: copy a 48G or 48GX ROM image to the folder `HP47` on the DM42n's disk, as `HP48.ROM`
  (or keep its name `gxrom-r`). The 48G and the 48GX have the same ROM; HP47 is always a 48GX (128 KB of RAM).
  `gxrom-r` (revision R, the last one) is at hpcalc.org: hp48/pc/emulators/gxrom-r.zip.
- For the PC tests (`make test`, `make keys`, `make shots`): put it here as `roms/gxrom-r`, or `make test ROM=path`.

Both forms work: 512 KB (two nibbles a byte, the low one first: gxrom-r) and 1 MB (a nibble a byte, as Emu48 keeps
them). 48S/SX ROMs (256 KB) are not supported yet.
