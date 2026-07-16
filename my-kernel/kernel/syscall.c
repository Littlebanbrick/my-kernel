// syscall.c — int 0x80 system-call dispatch (32-bit)
//
// The ABI for this toy kernel:
//
//   eax         — syscall number / request
//   (future) ebx, ecx, edx ... — per-syscall arguments
//
// For now there is exactly one syscall, used by the exec'd ring-3 demo:
//
//   eax = 'H' (any printable ASCII char)   — print that char, then exit.
//
// Bundling "print one char" and "exit" into a single call keeps the
// ring-3 demo to two instructions (mov eax,'H'; int 0x80) while still
// exercising the full ring-3 -> ring-0 path.  Splitting write/exit
// into distinct syscalls is the obvious next step once more programs
// exist to use them.

#include "syscall.h"
#include "sched.h"		/* sched_exit */
#include "printf.h"		/* putchar_one */
#include "putchar.h"

u32 syscall_enter(struct cpu_state *regs)
{
	unsigned char c = (unsigned char)regs->eax;

	/* Emit the character through the shared console cursor, the same
	 * path printf and readline use.  This is the kernel doing I/O on
	 * the user's behalf: the user process cannot touch VGA directly
	 * (its pages are supervisor-only), so output must go through here. */
	putchar_one(c);

	/* For now every syscall also ends the process.  sched_exit() never
	 * returns: it marks the process ZOMBIE/FINISHED and triggers a
	 * software IRQ 0, so the value we return here is never used. */
	sched_exit(0);

	/* unreachable; appease -Wreturn-type */
	return (u32)regs;
}
