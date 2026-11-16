/* The HP 48G/GX ROM built into the program's flash (make ROM_IN=roms/gxrom-r): for the user's own build only.
   HP's ROM may not be shared, so a .pg5 built this way stays on its owner's calculator. */
  .section .rodata.hp47rom,"a"
  .balign 4
  .global hp47_rom
hp47_rom:
  .incbin HP47_ROM_BIN
