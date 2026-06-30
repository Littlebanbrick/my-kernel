# Makefile - Build stage 2 (protected mode boot sector)
#
# Targets:
#   make          - Build boot.bin
#   make run      - Build and run in QEMU
#   make debug    - Build and run in QEMU with debug server (-s -S)
#   make clean    - Remove build artifacts

O ?= build/

.PHONY: all run debug clean

all: $(O)boot.bin

$(O)boot.o: boot.S $(O)
	as --32 -o $@ $<

$(O)boot.elf: $(O)boot.o
	ld -m elf_i386 -N -o $@ $< -Ttext 0x7C00

$(O)boot.bin: $(O)boot.elf
	objcopy -O binary $< $@

$(O):
	mkdir -p $(O)

run: all
	qemu-system-x86_64 -drive format=raw,file=$(O)boot.bin

debug: all
	qemu-system-x86_64 -s -S -drive format=raw,file=$(O)boot.bin

clean:
	rm -rf build/
