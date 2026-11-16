######################################
# HP47 - an HP 48GX for the SwissMicros DM42n (DMCP5 native program, .pg5)
# Based on the SwissMicros DMCP5_SDK Makefile (BSD-3), as ZX47, Chess 47, NAV47, TERM47 and GB47
######################################
# make              build/HP47.pg5 (loads on DMCP5 3.57 and later: interface 3.17); it reads the ROM from /HP47
# make ROM_IN=roms/gxrom-r   the ROM built into the program's flash: needs no heap for it. For your own
#                   calculator only: HP's ROM may not be shared, so never publish a .pg5 built this way.
# The tests need an HP 48G/GX ROM, which is not in the repo: roms/gxrom-r (hpcalc.org), or ROM=path.
# make test         the PC tests: boot, keys, arithmetic, the state round trip, the BEEP's pitch, the C47 keys
# make sim          the PC simulator (build/sim/sim); make shots: its screens -> build/sim/run/*.png
# make keys         every C47-layout key run on the ROM -> build/keys/keys-*.png: look that each does what it says

TARGET    = HP47
BUILD_DIR = build
PYTHON   ?= python3
IFC      ?= 17
ROM      ?= roms/gxrom-r

CC      = arm-none-eabi-gcc
AS      = arm-none-eabi-gcc -x assembler-with-cpp
OBJCOPY = arm-none-eabi-objcopy
SIZE    = arm-none-eabi-size

X48       = third_party/x48/emulate.c third_party/x48/actions.c third_party/x48/register.c
C_SOURCES = dmcp/sys/pgm_syscalls.c src/main.c src/hp48.c src/screen.c src/keys.c $(X48)
ASM_SOURCES = dmcp/startup_pgm.s
C_INCLUDES = -Idmcp -Isrc -Ithird_party/x48

C_DEFS   = -D__weak="__attribute__((weak))" -D__packed="__attribute__((__packed__))" -DPLATFORM_IFC_VER=$(IFC)
CPUFLAGS = -mthumb -march=armv7e-m -mfloat-abi=hard -mfpu=fpv4-sp-d16
CFLAGS   = -std=gnu11 $(CPUFLAGS) $(C_DEFS) $(C_INCLUDES) -Wall -fdata-sections -ffunction-sections -O2 -g
CFLAGS  += -MD -MP -MF $(BUILD_DIR)/.dep/$(@F).d
LDSCRIPT = stm32_program.ld
LDFLAGS  = $(CPUFLAGS) -T$(LDSCRIPT) -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref -Wl,--gc-sections \
           -Wl,--wrap=_malloc_r -g

all: $(BUILD_DIR)/$(TARGET).pg5

OBJECTS  = $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o)))
vpath %.c $(sort $(dir $(C_SOURCES)))
OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(ASM_SOURCES:.s=.o)))
vpath %.s $(sort $(dir $(ASM_SOURCES)))

$(BUILD_DIR)/%.o: %.c Makefile | $(BUILD_DIR)/.dep
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/%.o: %.s Makefile | $(BUILD_DIR)/.dep
	$(AS) -c $(CFLAGS) $< -o $@

# the ROM built in (ROM_IN=path); main.o is rebuilt when ROM_IN changes
ROMCFG := $(BUILD_DIR)/rom_in.cfg
$(shell mkdir -p $(BUILD_DIR); echo "$(ROM_IN)" | cmp -s - $(ROMCFG) 2>/dev/null || echo "$(ROM_IN)" > $(ROMCFG))
$(BUILD_DIR)/main.o: $(ROMCFG)
ifneq ($(ROM_IN),)
C_DEFS  += -DHP47_ROM_IN
OBJECTS += $(BUILD_DIR)/rom_in.o
$(BUILD_DIR)/rom.bin: $(ROM_IN) tools/rompack.py
	$(PYTHON) tools/rompack.py $< $@
$(BUILD_DIR)/rom_in.o: src/rom_in.s $(BUILD_DIR)/rom.bin | $(BUILD_DIR)/.dep
	$(AS) -c $(CFLAGS) -DHP47_ROM_BIN='"$(BUILD_DIR)/rom.bin"' $< -o $@
endif

$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS) Makefile
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

# QSPI CRC is compiled into the program header: if it changes, rebuild once.
$(BUILD_DIR)/$(TARGET).pg5: $(BUILD_DIR)/$(TARGET).elf tools/pg5tool.py
	$(OBJCOPY) --remove-section .qspi -O binary $< $(BUILD_DIR)/$(TARGET)_flash.bin
	$(OBJCOPY) --only-section   .qspi -O binary $< $(BUILD_DIR)/$(TARGET)_qspi.bin
	@if ! $(PYTHON) tools/pg5tool.py qspi $(BUILD_DIR)/$(TARGET)_qspi.bin src/qspi_crc.h; then \
	   rm -f $(BUILD_DIR)/pgm_syscalls.o $<; $(MAKE) --no-print-directory $@; exit $$?; fi
	$(PYTHON) tools/pg5tool.py pg5 $(BUILD_DIR)/$(TARGET)_flash.bin $@
	$(SIZE) $<

$(BUILD_DIR)/.dep:
	mkdir -p $@

# ------------------------------------------------------------------ PC tests
T = $(BUILD_DIR)/test
HOSTCC = gcc -O2 -Wall -Isrc -Ithird_party/x48
test: $(T)/hptest
	@test -f $(ROM) || { echo "make test: no ROM at $(ROM) (roms/README.md)"; exit 1; }
	sh test/run.sh $(T)/hptest $(ROM)

$(T)/hptest: test/hptest.c src/hp48.c src/hp48.h $(X48)
	mkdir -p $(T)
	$(HOSTCC) -o $@ test/hptest.c src/hp48.c $(X48)

keys: $(T)/hptest
	$(PYTHON) test/keys_check.py $(T)/hptest $(ROM) $(BUILD_DIR)/keys

# ------------------------------------------------------------------ the PC simulator
S = $(BUILD_DIR)/sim
sim:
	mkdir -p $(S)
	gcc -O2 -g -w '-D__packed=__attribute__((__packed__))' '-D__weak=__attribute__((weak))' -DHP47_SIM \
	    -Itest/sim -Isrc -Ithird_party/x48 -o $(S)/sim test/sim/sim.c src/main.c src/hp48.c src/screen.c \
	    src/keys.c $(X48)

shots: sim
	@test -f $(ROM) || { echo "make shots: no ROM at $(ROM) (roms/README.md)"; exit 1; }
	sh test/shots.sh $(S) $(ROM) $(PYTHON)

clean:
	-rm -fR $(BUILD_DIR)

-include $(wildcard $(BUILD_DIR)/.dep/*)

.PHONY: all test keys sim shots clean
