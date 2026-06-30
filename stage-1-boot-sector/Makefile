# Makefile - Build the boot sector for QEMU
#
# Targets:
#   make          - Build boot.bin
#   make run      - Build and run in QEMU
#   make debug    - Build and run in QEMU with debug server (-s -S)
#   make clean    - Remove build artifacts
#
# O=build/        - Use an out-of-tree build directory (default: build/)
#                   e.g. "make O=build/ run"

O ?= build/

.PHONY: all run debug clean

all: $(O)boot.bin

# Assemble: 16-bit (--32) AT&T syntax -> ELF32 object file
$(O)boot.o: boot.S $(O)
	as --32 -o $@ $<

# Link: place .text at physical address 0x7C00 (where BIOS loads us)
#   -m elf_i386   -> 32-bit ELF output (even though target is 16-bit real mode)
#   -N            -> make .text readable+writable (for raw binary conversion)
#   -Ttext 0x7C00 -> set the load/run address to 0x7C00
$(O)boot.elf: $(O)boot.o
	ld -m elf_i386 -N -o $@ $< -Ttext 0x7C00

# Strip ELF headers: produce raw flat binary (exactly 512 bytes)
#   -O binary     -> output format: raw binary (no ELF headers, symbol table, etc.)
$(O)boot.bin: $(O)boot.elf
	objcopy -O binary $< $@

# Create build directory if it doesn't exist
$(O):
	mkdir -p $(O)

# Run in QEMU
#   -drive format=raw,... -> treat boot.bin as a raw disk image
#   Ctrl-A then X         -> exit QEMU
run: all
	qemu-system-x86_64 -drive format=raw,file=$(O)boot.bin

# Debug: same as run, but freeze CPU and wait for GDB on port 1234
#   -s  -> shorthand for -gdb tcp::1234
#   -S  -> freeze CPU at startup (don't execute any instruction until GDB says continue)
debug: all
	qemu-system-x86_64 -s -S -drive format=raw,file=$(O)boot.bin

clean:
	rm -rf build/
