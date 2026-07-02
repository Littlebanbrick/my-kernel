# Makefile — Build stage 3 (32-bit protected mode)
#
# Build process:
#   1. boot.S   → boot.bin   (512-byte boot sector at 0x7C00)
#   2. stage3.S + kernel.c + my-kernel sources → stage3.bin
#   3. Concatenate into disk.img and pad to 62 sectors
#
# Targets:
#   make          — Build disk.img
#   make run      — Build and run in QEMU
#   make debug    — Build and run with GDB server (-s -S)
#   make clean    — Remove build artifacts

O ?= build/
MY_INC = -I ../my-kernel/include

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

MY_CFLAGS = -m32 -ffreestanding -nostdlib -Wall -Wextra $(MY_INC)

$(O)kernel.o: kernel.c $(O)
	gcc $(MY_CFLAGS) -c -o $@ $<

# Pull in printf/putchar from my-kernel (compiled as 32-bit)
$(O)printf.o: ../my-kernel/kernel/printf.c $(O)
	gcc $(MY_CFLAGS) -c -o $@ $<

$(O)putchar.o: ../my-kernel/kernel/putchar.c $(O)
	gcc $(MY_CFLAGS) -c -o $@ $<

# IDT from my-kernel
$(O)idt.o: ../my-kernel/kernel/idt.c $(O)
	gcc $(MY_CFLAGS) -c -o $@ $<

$(O)idt_handlers.o: ../my-kernel/kernel/idt_handlers.S $(O)
	as --32 -o $@ $<

KERNEL_OBJS = $(O)stage3.o $(O)kernel.o $(O)printf.o $(O)putchar.o \
	      $(O)idt.o $(O)idt_handlers.o

$(O)stage3.elf: linker.ld $(KERNEL_OBJS)
	ld -m elf_i386 -T linker.ld -N -o $@ $(KERNEL_OBJS)

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
