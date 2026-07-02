# Makefile — Build stage 3 (long mode: page tables, disk load, C kernel)
#
# Build process:
#   1. boot.S   → boot.bin   (512-byte boot sector at 0x7C00)
#   2. stage3.S + kernel.c   → stage3.bin (page tables + kernel at 0x8000)
#   3. Concatenate into disk.img and pad to 60 sectors (30 KB)
#
# Targets:
#   make          — Build disk.img
#   make run      — Build and run in QEMU
#   make debug    — Build and run with debug server (-s -S)
#   make clean    — Remove build artifacts

O ?= build/

SECTORS   = 60
PAD_SIZE  = $(shell echo $$(($(SECTORS) * 512)))

.PHONY: all run debug clean vbox vbox-setup

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
#  Stage 3 payload — page tables + long-mode entry + C kernel
# -------------------------------------------------------------------

$(O)stage3.o: stage3.S $(O)
	as --64 -o $@ $<

$(O)kernel.o: kernel.c $(O)
	gcc -m64 -ffreestanding -mno-red-zone -c -o $@ $<

$(O)stage3.elf: linker.ld $(O)stage3.o $(O)kernel.o
	ld -m elf_x86_64 -T linker.ld -N -no-pie -o $@ \
		$(O)stage3.o $(O)kernel.o

$(O)stage3.bin: $(O)stage3.elf
	objcopy -O binary $< $@

$(O)stage3.pad: $(O)stage3.bin
	@cp $< $@
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
	qemu-system-x86_64 -enable-kvm -cpu host -drive format=raw,file=$(O)disk.img

debug: all
	qemu-system-x86_64 -enable-kvm -cpu host -s -S -drive format=raw,file=$(O)disk.img

# -------------------------------------------------------------------
#  VirtualBox targets
# -------------------------------------------------------------------

VBOX_VM_NAME ?= my-kernel
VBOX_VDI     ?= $(O)kernel.vdi

# Convert the current disk image and start the VM.
# Run 'make vbox-setup' first (one-time VM creation).
vbox: all
	rm -f $(VBOX_VDI)
	VBoxManage convertfromraw $(O)disk.img $(VBOX_VDI) --format VDI
	@echo ""
	@echo "=== Starting VirtualBox VM '$(VBOX_VM_NAME)' ==="
	VBoxManage startvm "$(VBOX_VM_NAME)"

# One-time VM creation: creates the VM, builds disk, attaches, and boots.
vbox-setup: all
	@echo "Creating VirtualBox VM '$(VBOX_VM_NAME)'..."
	VBoxManage createvm --name "$(VBOX_VM_NAME)" --ostype "Linux_64" --register 2>&1 | grep -v 'already exists' || true
	VBoxManage storagectl "$(VBOX_VM_NAME)" --name "IDE" --add ide 2>&1 | grep -v 'already exists' || true
	rm -f $(VBOX_VDI)
	VBoxManage convertfromraw $(O)disk.img $(VBOX_VDI) --format VDI
	VBoxManage storageattach "$(VBOX_VM_NAME)" --storagectl IDE \
		--port 0 --device 0 --type hdd --medium "$(CURDIR)/$(VBOX_VDI)"
	@echo ""
	@echo "=== Starting VirtualBox VM '$(VBOX_VM_NAME)' ==="
	VBoxManage startvm "$(VBOX_VM_NAME)"

# -------------------------------------------------------------------
#  Clean
# -------------------------------------------------------------------

clean:
	rm -rf build/
