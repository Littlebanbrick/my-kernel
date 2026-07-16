// syscall.c — int 0x80 system-call dispatch (32-bit)
//
// The ABI for this toy kernel:
//
//   eax         — syscall number (SYS_* below)
//   ebx         — first argument
//   (future) ecx, edx ... — further arguments as needed
//
// Two syscalls exist so far:
//
//   SYS_PRINT  (ebx = const char *str)
//       Print a NUL-terminated string, then RETURN to the caller.
//
//   SYS_GETCHAR ()
//       Block until a key is available, return it in eax (0..255).
//       The first syscall that waits for an external event — proof the
//       ring-3 path can block, not just run-to-completion.
//
//   SYS_EXIT   (ebx = int exit_code)
//       Terminate the calling process.  Never returns: the handler
//       calls sched_exit(), which does not come back.
//
// Why the kernel can dereference the user pointer directly: an interrupt
// changes CS/SS/ESP (via TSS.esp0) but does NOT change CR3, so the
// current process's user-space mappings — code, stack, all PAGE_USER —
// are still live.  And the U/S bit restricts ring 3, not ring 0: a
// supervisor may read a user page.  A real kernel validates and
// fault-handles this with copy_from_user; this toy one trusts the
// pointer (and faults hard if the user lies).

#include "syscall.h"
#include "sched.h"		/* sched_exit */
#include "printf.h"		/* putchar_one */
#include "putchar.h"
#include "kbd.h"		/* getchar — blocking keyboard read */

u32 syscall_enter(struct cpu_state *regs)
{
	switch (regs->eax) {
	case SYS_PRINT: {
		const char *s = (const char *)regs->ebx;

		/* Emit through the shared console cursor — the same path
		 * printf and readline use.  This is the kernel doing I/O on
		 * the user's behalf: the user process cannot touch VGA. */
		while (*s)
			putchar_one((unsigned char)*s++);
		break;
	}
	case SYS_GETCHAR:
		/* Block until a key is available, then return it.  The
		 * kernel's getchar() already handles the lost-wakeup race
		 * (cli/sti around check-and-block) and registers itself as
		 * the keyboard waiter; kbd_isr() wakes it on the next
		 * keystroke.  Blocking inside a syscall works because the
		 * scheduler treats this trampoline's saved stack like
		 * irq0's: when getchar() does `int $0x20` to yield, the
		 * process is switched out and later resumed right here, the
		 * iret frame intact.  The char goes back to ring 3 in EAX. */
		regs->eax = (u32)getchar();
		break;
	case SYS_EXIT:
		/* sched_exit() never returns: it marks the process
		 * ZOMBIE/FINISHED and triggers a software IRQ 0. */
		sched_exit((int)regs->ebx);
		break;		/* unreachable */
	default:
		/* Unknown syscall — ignore and resume the caller.  A real
		 * kernel would deliver SIGSYS or return -ENOSYS; here we
		 * just let the program keep running. */
		break;
	}

	/* Resume the caller (unless SYS_EXIT ended it).  The trampoline
	 * switches to this ESP, popa, iret — same frame, back in ring 3. */
	return (u32)regs;
}
