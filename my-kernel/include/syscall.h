// syscall.h — user-to-kernel system-call entry (int 0x80)

#ifndef SYSCALL_H
#define SYSCALL_H

#include "types.h"
#include "sched.h"	/* struct cpu_state */

/* The syscall trampoline (idt_handlers.S) saves the ring-3 register
 * state into a struct cpu_state on the kernel stack, then calls
 * syscall_enter() with a pointer to it.  The handler may rewrite the
 * saved registers (to deliver a return value) and may exit the
 * process; it returns the ESP to resume (usually the same pointer it
 * was given). */
u32 syscall_enter(struct cpu_state *regs);

#endif
