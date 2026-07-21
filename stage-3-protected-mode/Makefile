# Makefile — Build stage 3 (32-bit protected mode)
#
# Build process:
#   1. boot.S   → boot.bin   (512-byte boot sector at 0x7C00)
#   2. stage3.S + kernel.c + my-kernel sources → stage3.bin (padded to SECTORS sectors)
#   3. my-kernel/programs/*.c → flat binaries → mkimage-wrapped program images
#   4. Concatenate boot + padded kernel + programs into disk.img
#
# Targets:
#   make          — Build disk.img
#   make run      — Build and run in QEMU
#   make debug    — Build and run with GDB server (-s -S)
#   make clean    — Remove build artifacts

O ?= build/
MY_INC = -I ../my-kernel/include

# Number of 512-byte sectors the kernel (stage3.pad) occupies on disk.
# This is the SINGLE SOURCE OF TRUTH for the kernel's disk footprint:
#   - boot.S receives it via  `as --defsym LOAD_SECTORS=$(SECTORS)`
#   - the kernel C receives it via -DKERNEL_SECTORS=$(SECTORS), and
#     exec.h derives EXEC_DEFAULT_LBA = 1 + KERNEL_SECTORS from it.
# Change ONLY this line and both the boot read size and the program
# region's start LBA follow automatically — no manual three-way sync.
# Sized with headroom so the kernel can grow without rebuilding the
# pad-size wall each time (the boot loader reads exactly SECTORS).
SECTORS   = 128
PAD_SIZE  = $(shell echo $$(($(SECTORS) * 512)))

.PHONY: all run debug clean

all: $(O)disk.img

# -------------------------------------------------------------------
#  Boot sector (512 bytes, loaded at 0x7C00 by BIOS)
# -------------------------------------------------------------------

$(O)boot.o: boot.S $(O)
	as --32 --defsym LOAD_SECTORS=$(SECTORS) -o $@ $<

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

MY_CFLAGS = -m32 -ffreestanding -nostdlib -Wall -Wextra $(MY_INC) \
	    -DKERNEL_SECTORS=$(SECTORS)

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

# PIC from my-kernel
$(O)pic.o: ../my-kernel/kernel/pic.c $(O)
	gcc $(MY_CFLAGS) -c -o $@ $<

# Physical memory buddy allocator from my-kernel
$(O)memory.o: ../my-kernel/kernel/memory.c $(O)
	gcc $(MY_CFLAGS) -c -o $@ $<

# Paging from my-kernel
$(O)paging.o: ../my-kernel/kernel/paging.c $(O)
	gcc $(MY_CFLAGS) -c -o $@ $<

# Ring 3 setup from my-kernel
$(O)ring3.o: ../my-kernel/kernel/ring3.c $(O)
	gcc $(MY_CFLAGS) -c -o $@ $<

# Scheduler from my-kernel
$(O)sched.o: ../my-kernel/kernel/sched.c $(O)
	gcc $(MY_CFLAGS) -c -o $@ $<

# PS/2 keyboard driver (ring buffer) from my-kernel
$(O)kbd.o: ../my-kernel/kernel/kbd.c $(O)
	gcc $(MY_CFLAGS) -c -o $@ $<

# Line-buffered input (readline) from my-kernel
$(O)readline.o: ../my-kernel/kernel/readline.c $(O)
	gcc $(MY_CFLAGS) -c -o $@ $<

# Minimal command interpreter (shell) from my-kernel
$(O)shell.o: ../my-kernel/kernel/shell.c $(O)
	gcc $(MY_CFLAGS) -c -o $@ $<

# PIT timer driver from my-kernel
$(O)pit.o: ../my-kernel/kernel/pit.c $(O)
	gcc $(MY_CFLAGS) -c -o $@ $<

# ATA/IDE PIO disk driver from my-kernel
$(O)ata.o: ../my-kernel/kernel/ata.c $(O)
	gcc $(MY_CFLAGS) -c -o $@ $<

# exec — load a disk program image and run it as a process
$(O)exec.o: ../my-kernel/kernel/exec.c $(O)
	gcc $(MY_CFLAGS) -c -o $@ $<

# Tiny flat filesystem (directory table)
$(O)fs.o: ../my-kernel/kernel/fs.c $(O)
	gcc $(MY_CFLAGS) -c -o $@ $<

# int 0x80 system-call entry
$(O)syscall.o: ../my-kernel/kernel/syscall.c $(O)
	gcc $(MY_CFLAGS) -c -o $@ $<

# Shell built-in commands, one per file in my-kernel/commands/.  Adding
# a command = add a .c + a rule + one entry in KERNEL_OBJS; the shell
# table/dispatch need no changes.
$(O)cmd_help.o:   ../my-kernel/commands/help.c $(O)
	gcc $(MY_CFLAGS) -c -o $@ $<
$(O)cmd_clear.o:  ../my-kernel/commands/clear.c $(O)
	gcc $(MY_CFLAGS) -c -o $@ $<
$(O)cmd_echo.o:   ../my-kernel/commands/echo.c $(O)
	gcc $(MY_CFLAGS) -c -o $@ $<
$(O)cmd_ps.o:     ../my-kernel/commands/ps.c $(O)
	gcc $(MY_CFLAGS) -c -o $@ $<
$(O)cmd_mem.o:    ../my-kernel/commands/mem.c $(O)
	gcc $(MY_CFLAGS) -c -o $@ $<
$(O)cmd_spawn.o:  ../my-kernel/commands/spawn.c $(O)
	gcc $(MY_CFLAGS) -c -o $@ $<
$(O)cmd_disk.o:   ../my-kernel/commands/disk.c $(O)
	gcc $(MY_CFLAGS) -c -o $@ $<
$(O)cmd_reboot.o: ../my-kernel/commands/reboot.c $(O)
	gcc $(MY_CFLAGS) -c -o $@ $<
$(O)cmd_exec.o:   ../my-kernel/commands/exec.c $(O)
	gcc $(MY_CFLAGS) -c -o $@ $<
$(O)cmd_ls.o:     ../my-kernel/commands/ls.c $(O)
	gcc $(MY_CFLAGS) -c -o $@ $<

KERNEL_OBJS = $(O)stage3.o $(O)kernel.o $(O)printf.o $(O)putchar.o \
	      $(O)idt.o $(O)idt_handlers.o $(O)pic.o $(O)memory.o \
	      $(O)paging.o $(O)ring3.o $(O)sched.o $(O)kbd.o $(O)readline.o \
	      $(O)shell.o $(O)pit.o $(O)ata.o $(O)exec.o $(O)syscall.o \
	      $(O)fs.o \
	      $(O)cmd_help.o $(O)cmd_clear.o $(O)cmd_echo.o $(O)cmd_ps.o \
	      $(O)cmd_mem.o $(O)cmd_spawn.o $(O)cmd_disk.o $(O)cmd_reboot.o \
	      $(O)cmd_exec.o $(O)cmd_ls.o

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
#  User programs — built into the disk image after the kernel
# -------------------------------------------------------------------
#
# Each program lives in my-kernel/programs/.  The build chain mirrors a
# real OS toolchain at toy scale:
#   prog.c -> objcopy -O binary -> mkimage wraps the exec header
#
# mkimage is a host tool (built with the host gcc, not -m32).  PROGS
# lists the images to bake into the disk; they land back-to-back
# starting at LBA 1+SECTORS (= 1 boot + SECTORS stage3.pad), matching
# EXEC_DEFAULT_LBA.

MKIMAGE  = ../my-kernel/programs/mkimage
PROG_DIR = ../my-kernel/programs
PROGS    = $(O)hello.img $(O)echo.img $(O)fork.img $(O)fault.img

$(MKIMAGE): ../my-kernel/programs/mkimage.c
	gcc -O2 -o $@ $<

# Compile + link a program as a 32-bit freestanding binary at 0x400000.
$(O)%.elf: $(PROG_DIR)/%.c $(PROG_DIR)/prog.ld $(O)
	gcc -m32 -ffreestanding -nostdlib -Wall -Wextra -fno-pic -fno-pie \
	    -c -o $(O)$*.o $<
	ld -m elf_i386 -T $(PROG_DIR)/prog.ld -o $@ $(O)$*.o

# Strip to a flat blob, then wrap with the 32-byte exec header sector.
# The third argument is the program's own filename: mkimage bakes it into
# the header so the filesystem's first-boot scan can name the program.
$(O)%.img: $(O)%.elf $(MKIMAGE)
	objcopy -O binary $< $(O)$*.bin
	$(MKIMAGE) $(O)$*.bin $@ $*

# -------------------------------------------------------------------
#  Combined disk image
# -------------------------------------------------------------------

# The FS directory table occupies FS_TABLE_SECTORS sectors (see fs.h)
# right after the kernel pad (LBA 1+KERNEL_SECTORS).  It is baked into the
# image as a zero block: fs_init() sees the blank (magic mismatch) on
# first boot and formats it by scanning the program region, then writes it
# back.  On later boots it trusts the on-disk table.  This zero block is
# what pushes the first program out to LBA EXEC_DEFAULT_LBA
# (= 1 + KERNEL_SECTORS + FS_TABLE_SECTORS), so the table sits between
# the kernel pad and the programs.  FS_TABLE_BYTES mirrors fs.h's size.
FS_TABLE_BYTES = 2048

$(O)fs_table.bin:
	@truncate -s $(FS_TABLE_BYTES) $@

# disk.img = boot sector + padded kernel + FS table blank + programs.
# The FS table blank sits between the kernel pad and the programs, so the
# first program lands at LBA EXEC_DEFAULT_LBA and fs_init's scan starts
# exactly there.
$(O)disk.img: $(O)boot.bin $(O)stage3.pad $(O)fs_table.bin $(PROGS)
	cat $^ > $@
	@echo "  disk.img: $$(stat -c%s $@) bytes ($$(( $$(stat -c%s $@) / 512 )) sectors)"

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

$(O):
	mkdir -p $@
