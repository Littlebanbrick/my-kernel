// sched.h — Tiny preemptive scheduler (32-bit, ring 0)

#ifndef SCHED_H
#define SCHED_H

#include "types.h"

/* Maximum number of processes (including the kernel). */
#define MAX_PROCS  8

/* Per-process stack size in bytes. */
#define PROC_STACK 4096

/* Priority constants.  Higher number = higher priority.
 *
 *   PRIO_IDLE   — only the idle task.  Runs when nothing else can.
 *   PRIO_USER   — ordinary user processes (default).
 *
 * Only two levels exist for now; a richer policy (nice values,
 * dynamic priority adjustment) can be layered on later. */
#define PRIO_IDLE  0
#define PRIO_USER  1

/* Process state.
 *
 *   READY     — runnable, waiting for its turn on the CPU
 *   SLEEPING  — blocked until g_ticks reaches pcb.wakeup_tick
 *   FINISHED   — terminated, slot may be reused
 *
 * pick_next() only selects READY.  SLEEPING is checked once per tick
 * inside irq0_enter(); those whose wakeup_tick has passed are moved
 * back to READY.  This is the simplest form of "block / wake": the
 * timer interrupt is both the preemptor and the wakeup source. */
enum proc_state {
	PROC_READY     = 0,
	PROC_SLEEPING  = 2,
	PROC_FINISHED  = 1,
};

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

	enum proc_state state;/* READY / SLEEPING / FINISHED           */

	/* Scheduling priority.  Higher = preferred.
	 *
	 * pick_next() chooses the READY process with the highest priority;
	 * ties are broken round-robin (the one that comes after the
	 * current pid wins).  idle_task is created with the lowest
	 * priority so it runs only when every other process is
	 * SLEEPING or FINISHED. */
	int  priority;

	u8  *stack;           /* allocated stack base (for freeing)    */
	u32 saved_sp;         /* ESP pointing at a saved cpu_state     */

	u32 wakeup_tick;      /* g_ticks value to wake up at (SLEEPING)*/
};

/* Global tick counter — incremented once per IRQ 0.  Read by
 * sched_wait_tick() to pace process output. */
extern volatile u32 g_ticks;

/* Initialise the scheduler.  No processes yet. */
void sched_init(void);

/* Create a process running `entry` with the default user priority.
 * Returns pid >= 0, or -1 on OOM.  The process starts in READY state;
 * it begins executing when the scheduler next picks it. */
int create_process(void (*entry)(void), const char *name);

/* Hand control to the first process.  Never returns. */
void sched_start(void) __attribute__((noreturn));

/* The idle task — the scheduler's permanent fallback.  It does
 * nothing but `hlt` in a loop, so the CPU sleeps until the next
 * interrupt (timer, keyboard, ...).  pick_next() always has at
 * least this one READY process to fall back to, so the system
 * never deadlocks on 'nothing to run'.  Created automatically by
 * sched_start(); user code does not create it. */
void idle_task(void) __attribute__((noreturn));

/* Yield the current time slice: spin until g_ticks advances (i.e.
 * until at least one more IRQ 0 has fired and the scheduler has
 * switched away and back).  Use this to pace output so a single
 * printf stays visible for one slice. */
void sched_wait_tick(void);

/* Terminate the current process.  Marks it FINISHED and triggers a
 * software IRQ 0 so the scheduler switches to the next runnable
 * process.  Never returns to the caller. */
void sched_exit(void) __attribute__((noreturn));

/* Sleep for `ticks` timer ticks.  Marks the current process SLEEPING,
 * records when it should wake up, and yields via a software IRQ 0.
 * The process resumes (returning from sleep()) once g_ticks has
 * advanced past its wakeup_tick.  sleep(0) is equivalent to a yield:
 * "let others run for at least one tick, then come back to me". */
void sleep(unsigned int ticks);

#endif
