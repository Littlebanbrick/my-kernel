// syscall.h — user-to-kernel system-call entry (int 0x80)

#ifndef SYSCALL_H
#define SYSCALL_H

#include "types.h"
#include "sched.h"	/* struct cpu_state */

/* Syscall numbers — passed in eax by the user program and dispatched on
 * by syscall_enter().  These MUST stay in sync with the user-side stubs
 * in my-kernel/programs/syscall.h: the kernel and user programs compile
 * as independent units, so a mismatch is a silent wrong-syscall bug,
 * not a link error.  ebx carries the first (and so far only) argument. */
#define SYS_EXIT  0
#define SYS_PRINT 1

/* The syscall trampoline (idt_handlers.S) saves the ring-3 register
 * state into a struct cpu_state on the kernel stack, then calls
 * syscall_enter() with a pointer to it.  The handler may rewrite the
 * saved registers (to deliver a return value) and may exit the
 * process; it returns the ESP to resume (usually the same pointer it
 * was given). */
u32 syscall_enter(struct cpu_state *regs);

#endif
