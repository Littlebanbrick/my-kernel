// exec.h — load a program image from disk and run it as a process
//
// A "program" is a bare binary on disk, wrapped with a tiny header so
// exec can be self-describing: it knows where to load the code and
// where to jump, without a filesystem.  The format is a deliberately
// minimal single-segment image — a degenerate ELF (one PT_LOAD, one
// size, no .bss).  See agentic/22 for the block-device framing.

#ifndef EXEC_H
#define EXEC_H

#include "types.h"

/* 16-byte program-image header.  This occupies the first 16 bytes of
 * the program's first sector on disk; the rest of that sector is zero
 * padding, and the actual code starts at the NEXT sector. */
#define EXEC_MAGIC 0x00584E4Cu   /* "LNX\0" little-endian (4C 4E 58 00) */

/* On-disk layout of a program image's first (header) sector:
 *
 *   offset 0   : magic         (EXEC_MAGIC)
 *   offset 4   : load_addr     (virtual address to load the code)
 *   offset 8   : entry         (virtual address to start at)
 *   offset 12  : length        (code size, excludes this header sector)
 *   offset 16  : name[16]      (NUL-terminated program name, for the FS)
 *
 * `name` is how a program declares its own filename: the FS scans the
 * program region on first boot and reads each header's name into its
 * directory table, so `exec hello` works without a host-side tool
 * pre-filling the table.  The struct's natural layout is exactly 32
 * bytes (all fields aligned), matching what mkimage writes. */
struct exec_hdr {
	u32 magic;
	u32 load_addr;
	u32 entry;
	u32 length;
	char name[16];
};

/* Default LBA of the first program on disk.
 *
 * disk.img layout:
 *   LBA 0                       boot sector
 *   LBA 1 .. KERNEL_SECTORS     stage3.pad (the kernel)
 *   LBA 1+KERNEL_SECTORS .. +FS_TABLE_SECTORS-1   FS directory table
 *   LBA 1+KERNEL_SECTORS+FS_TABLE_SECTORS  ...    first program image
 *
 * boot.S loads only LBA 1..KERNEL_SECTORS (the kernel), so the FS table
 * and program region are never touched by the boot path — they're read at
 * runtime by fs_init / exec.  KERNEL_SECTORS comes from the Makefile
 * (-DKERNEL_SECTORS=$(SECTORS)); FS_TABLE_SECTORS comes from fs.h.  No
 * manual copy of either number lives in this header. */
#ifndef KERNEL_SECTORS
#define KERNEL_SECTORS 128      /* fallback if built without the Makefile */
#endif
#define EXEC_DEFAULT_LBA (1u + KERNEL_SECTORS + FS_TABLE_SECTORS)

/* Virtual address of the one-page user stack every exec'd program
 * shares.  Grows down from USER_STACK_TOP.  Lives in the same 4-MiB
 * region as load_addr (PDX 1, 0x400000-0x7fffff), so it reuses the
 * code's page table — no second PT allocation. */
#define USER_STACK_BASE 0x500000u
#define USER_STACK_TOP  (USER_STACK_BASE + 0x1000u)

/* Old debug hook kept for `exec <lba>`: run the program image whose header
 * sector is at LBA `lba`.  `exec <name>` resolves the name to an LBA via
 * the FS directory table (fs_lookup) and calls this.  Returns 0 on
 * success, negative on error. */
int do_exec(u32 lba);

#endif /* EXEC_H */
