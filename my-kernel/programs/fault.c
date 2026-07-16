/* fault.c — a ring-3 program that deliberately triggers a page fault.
 *
 * Writes to a virtual address whose page-directory entry does not exist
 * (0x70000000, PDE 448 — far above the identity-mapped 4 MiB and the
 * USER_PRIVATE region).  There is no PDE → no PT → no PTE, so the CPU
 * raises a page fault with:
 *
 *     P = 0  (the page was not present)
 *     W = 1  (the faulting access was a write)
 *     U = 1  (it came from ring 3)
 *
 * The dedicated page_fault() handler in idt.c reads CR2 (the faulting
 * address) and decodes the error code, so instead of a bare "Page
 * Fault / EIP" you see the offending address and the access type.
 *
 * This program is a debug aid, not a feature: a real kernel would map
 * the missing page (demand paging) or deliver a signal.  We halt so
 * the fault is impossible to miss during development.  Remove this
 * program (or the write) once the handler is proven; it is not a
 * permanent resident of the disk image. */

#include "syscall.h"

__attribute__((section(".text.startup")))
void _start(void)
{
	volatile int *bad = (volatile int *)0x70000000u;

	sys_print("about to write an unmapped address...\n");
	*bad = 0xdeadbeef;          /* triggers the page fault */

	/* unreachable: the fault halts the machine before we get here */
	sys_print("no fault?! (bug)\n");
	sys_exit(0);
}
