# Makefile — Build my-kernel objects and link with stage-3 boot code
#
# Targets:
#   make          — Build kernel objects
#   make run      — Build full disk image and run in QEMU
#   make debug    — Same, but with GDB server (-s -S)
#   make clean    — Remove build artifacts

O ?= build/

MY_CFLAGS = -ffreestanding -m64 -mno-red-zone -Wall -Wextra -I include

# C sources
KERNEL_C   = $(wildcard kernel/*.c)
KERNEL_O   = $(patsubst kernel/%.c, $(O)%.o, $(KERNEL_C))

# Assembly sources (idt_handlers.S etc.)
KERNEL_ASM = $(wildcard kernel/*.S)
KERNEL_ASM_O = $(patsubst kernel/%.S, $(O)%.o, $(KERNEL_ASM))

.PHONY: all run debug clean

all: $(O)kernel.o

# Compile each kernel C file
$(O)%.o: kernel/%.c include/*.h $(O)
	gcc $(MY_CFLAGS) -c -o $@ $<

# Assemble each .S file (pure assembly, not CPP)
$(O)%.o: kernel/%.S $(O)
	as --64 -o $@ $<

# Link all objects into one relocatable kernel.o
$(O)kernel.o: $(KERNEL_O) $(KERNEL_ASM_O)
	ld -r -o $@ $^

# Directory
$(O):
	mkdir -p $(O)

# -------------------------------------------------------------------
#  QEMU targets (reuse stage-3 boot code)
# -------------------------------------------------------------------

run: $(O)kernel.o
	cp $(O)kernel.o ../stage-3-long-mode/build/kernel.o
	$(MAKE) -C ../stage-3-long-mode run

debug: $(O)kernel.o
	cp $(O)kernel.o ../stage-3-long-mode/build/kernel.o
	$(MAKE) -C ../stage-3-long-mode debug

clean:
	rm -rf $(O)
