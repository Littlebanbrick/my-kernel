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
 *   SLEEPING  — blocked until g_ticks reaches pcb.wakeup_tick (TIMER wait)
 *   BLOCKED   — blocked until woken by wake(pid) (EVENT wait, no timeout)
 *   FINISHED   — terminated, slot may be reused
 *   ZOMBIE     — exited but not yet reaped by its parent (see below)
 *
 * pick_next() only selects READY.  wake_sleepers() runs every tick and
 * moves SLEEPING processes whose wakeup_tick has passed back to READY —
 * it deliberately does NOT touch BLOCKED, because BLOCKED has no
 * timeout: only an explicit wake() can rescue it.  Keeping the two
 * wake mechanisms on separate states is what prevents a lost-wakeup
 * from a broken "infinite timeout" sentinel (an earlier attempt used
 * wakeup_tick = 0xFFFFFFFF, which the signed-wraparound test treats
 * as already-expired — the process busy-spun with interrupts off and
 * starved the keyboard IRQ).  This mirrors Linux: timer wake and
 * event wake are distinct paths.
 *
 * ZOMBIE is the "reaped-on-wait" half of process death.  A process
 * that calls sched_exit() cannot free its own stack (it is running on
 * it), and its parent may want to observe its exit.  So it becomes a
 * ZOMBIE — invisible to the scheduler, visible to wait() — and the
 * reaper only frees it once the parent has waited (or once its parent
 * is gone, so nothing can ever wait on it).  This is exactly Linux's
 * zombie: the task is gone, but the exit status lingers until reaped. */
enum proc_state {
	PROC_READY     = 0,
	PROC_FINISHED  = 1,
	PROC_SLEEPING  = 2,
	PROC_BLOCKED   = 3,
	PROC_ZOMBIE    = 4,
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

	/* Parent / child relationships — for wait() and zombie reaping.
	 *
	 *   parent       — pid of the process that created this one, or -1
	 *                  for processes with no parent (idle, shell).
	 *   waiting_for  — if non-negative, this process is blocked in
	 *                  wait() until that child becomes a ZOMBIE (or is
	 *                  already one).  Set by wait(), consumed by
	 *                  sched_exit() of the named child.
	 *   exit_code    — value passed to sched_exit(); handed to the
	 *                  parent's wait() before the slot is reused. */
	int parent;
	int waiting_for;
	int exit_code;

	u32 *page_dir;
	u32  page_dir_phys;
	u32 *priv_pt;
	u32  priv_phys;

	/* Does this process own (and must reap) the user-space pages
	 * reachable through page_dir?  Set by do_fork for forked children
	 * and by create_process for exec'd programs.  The reaper frees
	 * those PTs + physical pages when true; the private isolation page
	 * (priv_phys/priv_pt) is always owned and always freed. */
	int  owns_user_pages;
};

/* Global tick counter — incremented once per IRQ 0.  Read by
 * sched_wait_tick() to pace process output. */
extern volatile u32 g_ticks;

/* PID of the process currently running, or -1 before sched_start(). */
extern int current_pid;

/* Initialise the scheduler.  No processes yet. */
void sched_init(void);

/* Create a process running `entry` with the default user priority.
 * The new process's parent is the caller (current_pid), or -1 if called
 * before the scheduler starts.  Returns pid >= 0, or -1 on OOM.
 * The process starts in READY state; it begins executing when the
 * scheduler next picks it. */
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

/* Terminate the current process.  Records `code` as the exit status,
 * marks the process ZOMBIE (kept around until its parent waits), and
 * wakes the parent if it is currently blocked in wait().  Never
 * returns to the caller.  If the process has no living parent, its
 * slot is freed directly (reaped as FINISHED) — nothing can wait on
 * it, so lingering would only leak. */
void sched_exit(int code) __attribute__((noreturn));

/* Sleep for `ticks` timer ticks.  Marks the current process SLEEPING,
 * records when it should wake up, and yields via a software IRQ 0.
 * The process resumes (returning from sleep()) once g_ticks has
 * advanced past its wakeup_tick.  sleep(0) is equivalent to a yield:
 * "let others run for at least one tick, then come back to me". */
void sleep(unsigned int ticks);

/* Block the current process until woken by wake(pid).  Unlike sleep(),
 * there is no timeout — the process stays SLEEPING until something
 * calls wake() on it.  Used by event-waiters like getchar().
 *
 * The caller MUST surround "check condition + sched_block()" with
 * cli/sti: otherwise an interrupt could make the condition true and
 * find no waiter registered, causing a lost wakeup.  This is the
 * classic wait_event pattern. */
void sched_block(void);

/* Wake a blocked process: move it from SLEEPING back to READY so the
 * scheduler will pick it up.  Typically called from an ISR after the
 * awaited event (e.g. a keypress) has occurred.  No-op if `pid` is
 * invalid or not currently SLEEPING. */
void wake(int pid);

/* Wait for any child of the current process to exit, then reap it.
 *
 * Blocks (PROC_BLOCKED) until a child becomes a ZOMBIE — unless one is
 * already a ZOMBIE, in which case it returns immediately.  Writes the
 * dead child's pid into *out_pid (if non-NULL) and its exit code into
 * *out_code (if non-NULL), then frees the child's slot.
 *
 * Returns the reaped child's pid, or -1 if the current process has no
 * children at all (so waiting would deadlock).  This is the
 * reap-on-exit half of the Unix wait(): the child's death is observed
 * here, and its resources freed only when a parent is ready to see. */
int wait(int *out_pid, int *out_code);

/* Return the current process's page directory as an identity-mapped
 * virtual pointer, or NULL if no process is running yet (before
 * sched_start).  Used by exec to install the program's code mapping
 * into the process that will run it, so the bytes can be written in
 * from the calling context. */
u32 *sched_current_pd(void);

/* fork() the current process.  `parent_regs` points at the parent's
 * saved ring-3 syscall frame (8 pusha regs + 5-word iret frame with
 * ESP/SS — 52 bytes total: see do_fork in sched.c).  Creates a child
 * that is a deep copy of the parent's address space, with its saved
 * registers set to a copy of the parent's — except eax, which is 0 in
 * the child (and the child pid in the parent).  Returns the child pid
 * (>= 0) on success, or -1 on failure (no slot, or OOM).  The child
 * starts READY and runs when the scheduler picks it. */
int do_fork(struct cpu_state *parent_regs);

/* Print one line per used PCB slot — pid, name, state, priority.
 * Intended for the `ps` shell command: a read-only snapshot of the
 * process table.  Walks procs[] here (rather than exposing the table)
 * so the PCB layout stays private to sched.c. */
void sched_dump_ps(void);

#endif
