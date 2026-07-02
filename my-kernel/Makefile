# Makefile — Build my-kernel objects for stage-3-protected-mode
#
# Targets:
#   make          — Build kernel.o
#   make clean    — Remove build artifacts

O ?= build/

MY_CFLAGS = -m32 -ffreestanding -nostdlib -Wall -Wextra -I include

# C sources
KERNEL_C   = $(wildcard kernel/*.c)
KERNEL_O   = $(patsubst kernel/%.c, $(O)%.o, $(KERNEL_C))

# Assembly sources
KERNEL_ASM = $(wildcard kernel/*.S)
KERNEL_ASM_O = $(patsubst kernel/%.S, $(O)%.o, $(KERNEL_ASM))

.PHONY: all clean

all: $(O)kernel.o

# Compile each kernel C file (32-bit)
$(O)%.o: kernel/%.c include/*.h $(O)
	gcc $(MY_CFLAGS) -c -o $@ $<

# Assemble each .S file (32-bit)
$(O)%.o: kernel/%.S $(O)
	as --32 -o $@ $<

# Link all objects into one relocatable kernel.o
$(O)kernel.o: $(KERNEL_O) $(KERNEL_ASM_O)
	ld -r -o $@ $^

# Directory
$(O):
	mkdir -p $(O)

clean:
	rm -rf $(O)
