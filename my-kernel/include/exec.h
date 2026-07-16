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

/* Default LBA of the first program on disk.
 * disk.img = boot (1 sector) + stage3.pad (82 sectors), so the program
 * image starts at LBA 83.  boot.S loads only LBA 1..82 (the kernel),
 * so this region is never touched by the boot path — exec reads it at
 * runtime.  Kept in sync with SECTORS in the Makefile. */
#define EXEC_DEFAULT_LBA 83u

struct exec_hdr {
	u32 magic;       /* EXEC_MAGIC — sanity check "is this a program" */
	u32 load_addr;   /* virtual address to load the code at            */
	u32 entry;       /* virtual address to start execution at          */
	u32 length;      /* code size in bytes (excludes the header sector) */
};

/* Virtual address of the one-page user stack every exec'd program
 * shares.  Grows down from USER_STACK_TOP.  Lives in the same 4-MiB
 * region as load_addr (PDX 1, 0x400000-0x7fffff), so it reuses the
 * code's page table — no second PT allocation. */
#define USER_STACK_BASE 0x500000u
#define USER_STACK_TOP  (USER_STACK_BASE + 0x1000u)

/* Load and run the program whose image starts at LBA `lba`.  Maps the
 * image into memory, creates a process running it, and waits for that
 * process to exit.  Returns 0 on success, negative on error. */
int do_exec(u32 lba);

#endif /* EXEC_H */
