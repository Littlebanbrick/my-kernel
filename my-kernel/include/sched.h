// sched.h — Tiny preemptive scheduler (32-bit, ring 0)

#ifndef SCHED_H
#define SCHED_H

#include "types.h"

/* Maximum number of processes (including the kernel). */
#define MAX_PROCS  8

/* Per-process stack size in bytes. */
#define PROC_STACK 4096

/* Saved register set — exactly the values pushed/popped by the
 * `pusha` / `popa` instructions, in the order they appear on the
 * stack (lowest address first).
 *
 *   pusha pushes  EAX ECX EDX EBX ESP EBP ESI EDI
 *   in that order, so the LAST pushed (EDI) is at the lowest
 *   address (where ESP ends up):
 *
 *      ESP+0   : EDI    ← lowest, pushed last
 *      ESP+4   : ESI
 *      ESP+8   : EBP
 *      ESP+12  : ESP    (pusha's saved pre-push ESP; popa discards)
 *      ESP+16  : EBX
 *      ESP+20  : EDX
 *      ESP+24  : ECX
 *      ESP+28  : EAX    ← highest, pushed first
 *
 * Right above that (pushed by the CPU on interrupt) is the iret
 * frame:  EIP, CS, EFLAGS.
 *
 * `popa` reads these in the same low→high order and discards the
 * ESP slot; `iret` then reads EIP/CS/EFLAGS. */
struct cpu_state {
	u32 edi;
	u32 esi;
	u32 ebp;
	u32 esp;
	u32 ebx;
	u32 edx;
	u32 ecx;
	u32 eax;

	u32 eip;
	u32 cs;
	u32 eflags;
} __attribute__((packed));

struct pcb {
	int  used;            /* slot in use?                          */
	int  pid;             /* process id (0 = kernel)               */
	char name[8];         /* short tag for debug                   */

	u8  *stack;           /* allocated stack base (for freeing)    */
	u32 saved_sp;         /* ESP pointing at a saved cpu_state    */
};

/* Initialise the scheduler.  No processes yet. */
void sched_init(void);

/* Create a process running `entry`.  Returns pid >= 0, or -1 on OOM.
 * The process starts in READY state; it begins executing when the
 * scheduler next picks it. */
int create_process(void (*entry)(void), const char *name);

/* Hand control to the first process.  Never returns. */
void sched_start(void) __attribute__((noreturn));

#endif
