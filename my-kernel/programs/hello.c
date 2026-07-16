/* hello.c — a disk-loaded program that runs in ring 3.
 *
 * Freestanding: no libc, no kernel symbols.  It cannot touch hardware
 * directly — the VGA buffer and all kernel memory are mapped
 * supervisor-only, so a direct write would fault (#GP).  The only way
 * out is the int $0x80 system call.
 *
 * This version exercises two distinct syscalls: SYS_PRINT (which returns
 * to the caller) and SYS_EXIT (which ends the process).  That split is
 * the point — it proves a syscall can resume user code, not only trap
 * and die.
 *
 * The string literal lives in .rodata, which prog.ld concatenates right
 * after .text into the same loaded page at 0x400000 (PAGE_USER), so the
 * kernel can read it.  No .bss — the image format is a single load
 * segment, so all data must be initialised. */

#include "syscall.h"

/* _start is placed in its own section (.text.startup) so prog.ld can
 * pin it FIRST in the loaded image — at the load address 0x400000.
 * Without this, a program whose source lists helper functions before
 * _start would see those helpers emitted first, pushing _start past
 * 0x400000 and breaking the entry==load_addr assumption mkimage bakes
 * into the header. */
__attribute__((section(".text.startup")))
void _start(void)
{
	sys_print("Hello, ring 3!\n");
	sys_exit(0);
}
