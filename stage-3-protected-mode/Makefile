# Makefile — Build stage 3 (32-bit protected mode)
#
# Build process:
#   1. boot.S   → boot.bin   (512-byte boot sector at 0x7C00)
#   2. stage3.S + kernel.c   → stage3.bin (kernel at 0xB000)
#   3. Concatenate into disk.img and pad to 62 sectors
#
# Targets:
#   make          — Build disk.img
#   make run      — Build and run in QEMU
#   make debug    — Build and run with GDB server (-s -S)
#   make clean    — Remove build artifacts

O ?= build/

SECTORS   = 62
PAD_SIZE  = $(shell echo $$(($(SECTORS) * 512)))

.PHONY: all run debug clean

all: $(O)disk.img

# -------------------------------------------------------------------
#  Boot sector (512 bytes, loaded at 0x7C00 by BIOS)
# -------------------------------------------------------------------

$(O)boot.o: boot.S $(O)
	as --32 -o $@ $<

$(O)boot.elf: $(O)boot.o
	ld -m elf_i386 -N -o $@ $< -Ttext 0x7C00

$(O)boot.bin: $(O)boot.elf
	objcopy -O binary $< $@
	@test `stat -c%s $@` -eq 512 || \
		(echo "*** Error: boot.bin is not 512 bytes"; exit 1)

# -------------------------------------------------------------------
#  Stage 3 payload — 32-bit kernel
# -------------------------------------------------------------------

$(O)stage3.o: stage3.S $(O)
	as --32 -o $@ $<

$(O)kernel.o: kernel.c $(O)
	gcc -m32 -ffreestanding -nostdlib -c -o $@ $<

$(O)stage3.elf: linker.ld $(O)stage3.o $(O)kernel.o
	ld -m elf_i386 -T linker.ld -N -o $@ $(O)stage3.o $(O)kernel.o

$(O)stage3.bin: $(O)stage3.elf
	objcopy -O binary $< $@

$(O)stage3.pad: $(O)stage3.bin
	cp $< $@
	@test `stat -c%s $<` -le $(PAD_SIZE) || \
		(echo "*** Error: stage3.bin too large for $(SECTORS) sectors"; \
		 rm -f $@; exit 1)
	@truncate -s $(PAD_SIZE) $@

# -------------------------------------------------------------------
#  Combined disk image
# -------------------------------------------------------------------

$(O)disk.img: $(O)boot.bin $(O)stage3.pad
	cat $^ > $@
	@echo "  disk.img: $$(stat -c%s $@) bytes ($$(( $$(stat -c%s $@) / 512 )) sectors)"

# -------------------------------------------------------------------
#  Directory
# -------------------------------------------------------------------

$(O):
	mkdir -p $(O)

# -------------------------------------------------------------------
#  QEMU targets
# -------------------------------------------------------------------

run: all
	qemu-system-x86_64 -accel tcg -cpu qemu64 -drive format=raw,file=$(O)disk.img

debug: all
	qemu-system-x86_64 -accel tcg -cpu qemu64 -s -S -drive format=raw,file=$(O)disk.img

# -------------------------------------------------------------------
#  Clean
# -------------------------------------------------------------------

clean:
	rm -rf build/
