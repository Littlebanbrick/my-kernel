// sched.c — Tiny preemptive round-robin scheduler (32-bit, ring 0)
//
// Cooperation with idt_handlers.S:
//
//   IRQ 0 entry (irq0_handler, in idt_handlers.S):
//
//     1. CPU pushes EIP, CS, EFLAGS onto the *current* process stack.
//     2. irq0_handler does `pusha`  → saves 8 regs, ESP now points at
//        the bottom of a `struct cpu_state`.
//     3. calls irq0_enter(saved_sp = ESP)  (this file)
//
//   irq0_enter() stores `saved_sp` into pcb[current].saved_sp, picks
//   the next process, sends EOI to the PIC, and RETURNS the next
//   process's saved_sp.
//
//   IRQ 0 exit (back in irq0_handler):
//
//     4. `mov %eax, %esp`  → switch to the returned stack
//     5. `popa`  → restores the next process's registers
//     6. `iret`  → pops EIP, CS, EFLAGS → resumes the next process
//
//   So a "context switch" is just: save ESP, get another ESP, popa, iret.
//
//   The very first switch is special: no process has ever been
//   interrupted, so none has a saved IRQ frame.  We solve this by
//   *preparing* each new process's stack to look exactly like a
//   freshly-interrupted process:  pusha-slots (with sane initial
//   values) + EIP/CS/EFLAGS.  `create_process` lays this out;
//   `do_first_switch` (idt_handlers.S) loads process 0's saved_sp
//   and `popa; iret`s into it.
//
//   After that, every subsequent switch goes through `irq0_handler`
//   uniformly — no special "first time" branch needed.
//
//   Process termination:  sched_exit() marks the current process
//   FINISHED and triggers a software IRQ 0 (`int $0x20`).  Because
//   pick_next() skips FINISHED processes, the scheduler never resumes
//   it — the process effectively ends.  When no runnable process
//   remains, sched_finish() halts the machine.
//
//   The same IRQ 0 path is thus used for two triggers:
//     - hardware timer tick (preemptive, from the PIT)
//     - software `int $0x20` from sched_exit (cooperative yield)
//   This mirrors Linux, where schedule() can be entered from a timer
//   interrupt or from a syscall/voluntary yield.
//
// All processes run in ring 0 (CPL=0) on a single shared address space.
// No TSS, no privilege switching — pure register save/restore.

#include "types.h"
#include "printf.h"
#include "sched.h"
#include "memory.h"
#include "paging.h"
#include "pic.h"

/* ------------------------------------------------------------------ */
/*  Process table                                                      */
/* ------------------------------------------------------------------ */

static struct pcb procs[MAX_PROCS];
int current_pid;     /* pid of the currently running process         */
static int procs_count;     /* total processes ever created           */

/* Global tick counter — incremented on every IRQ 0.  Process code
 * reads this to pace itself (see sched_wait_tick). */
volatile u32 g_ticks;

/* Defined in idt_handlers.S — loads `saved_sp` into ESP and resumes
 * the process via `popa; iret`.  Used only for the very first switch
 * (from the kernel to process 0). */
extern void do_first_switch(u32 saved_sp);

/* Forward decl — called when no runnable process remains. */
static void sched_finish(void) __attribute__((noreturn));

/* ------------------------------------------------------------------ */
/*  sched_init                                                         */
/* ------------------------------------------------------------------ */

void sched_init(void)
{
	int i;

	for (i = 0; i < MAX_PROCS; i++) {
		procs[i].used = 0;
		procs[i].pid  = i;
		procs[i].stack = NULL;
		procs[i].saved_sp = 0;
		procs[i].state = PROC_READY;
		procs[i].wakeup_tick = 0;
		procs[i].priority = PRIO_USER;
		procs[i].parent = -1;
		procs[i].waiting_for = -1;
		procs[i].exit_code = 0;
		procs[i].page_dir = NULL;
		procs[i].page_dir_phys = 0;
		procs[i].priv_pt = NULL;
		procs[i].priv_phys = 0;
	}
	current_pid = -1;     /* nothing running yet */
	procs_count = 0;
	g_ticks = 0;
}

/* ------------------------------------------------------------------ */
/*  create_process — lay out a fake interrupt frame on a fresh stack   */
/* ------------------------------------------------------------------ */

int create_process(void (*entry)(void), const char *name)
{
	int pid;
	struct pcb *p;
	struct cpu_state *frame;
	u8 *stack;
	int i;

	/* 1. Find a free slot */
	for (pid = 0; pid < MAX_PROCS; pid++)
		if (!procs[pid].used)
			break;
	if (pid == MAX_PROCS) {
		printf("sched: table full\n");
		return -1;
	}

	/* 2. Allocate a 4 KiB stack */
	stack = (u8 *)alloc_page();
	if (!stack) {
		printf("sched: OOM for stack\n");
		return -1;
	}

	p = &procs[pid];
	p->used  = 1;
	p->pid   = pid;
	p->stack = stack;
	p->state = PROC_READY;
	p->priority = PRIO_USER;     /* user processes outrank idle */
	p->parent = current_pid;     /* the caller is the parent */
	p->waiting_for = -1;
	p->exit_code = 0;

	for (i = 0; i < 7 && name && name[i]; i++)
		p->name[i] = name[i];
	p->name[i] = '\0';

	/* 2b. Give this process its own page directory.
	 *
	 *     clone_kernel_page_dir() copies the kernel's present PDEs, so
	 *     the kernel mappings (image, buddy, VGA) are shared.  The
	 *     process's own mappings — the private page below — get added
	 *     on top, only in this PD, so other processes can't see them.
	 *
	 *     The stack stays in the identity-mapped first 4 MiB (shared
	 *     with the kernel), so the IRQ handler can switch CR3 safely
	 *     while running on it.  Full stack isolation needs ring 3 +
	 *     a kernel high-half — future work. */
	p->page_dir = clone_kernel_page_dir();
	if (!p->page_dir) {
		printf("sched: OOM for page dir\n");
		free_page(stack);
		p->used = 0;
		return -1;
	}
	p->page_dir_phys = (u32)p->page_dir;   /* identity-mapped */

	/* 2c. Allocate a private physical page + a page-table page to map it.
	 *
	 *     The PT covers USER_PRIVATE_BASE's 4 MiB region; only this
	 *     process's PD has a PDE pointing at it.  Each process gets a
	 *     different priv_phys, so a write to USER_PRIVATE_BASE in
	 *     process A lands in a different physical page than the same
	 *     virtual address in process B — that is the isolation demo. */
	p->priv_phys = (u32)alloc_page();
	p->priv_pt   = (u32 *)alloc_page();
	if (!p->priv_phys || !p->priv_pt) {
		printf("sched: OOM for private page\n");
		if (p->priv_phys) free_page((void *)p->priv_phys);
		if (p->priv_pt)   free_page(p->priv_pt);
		free_page(p->page_dir);
		free_page(stack);
		p->used = 0;
		return -1;
	}

	/* Zero the PT, then pre-install it into PD[512] before calling
	 * map_page_in(), so map_page_in reuses this PT instead of
	 * allocating its own.  Without the pre-install, map_page_in sees
	 * PDE 512 absent and silently allocates a SECOND page-table page
	 * for it; reap_proc only frees priv_pt (the PCB-tracked one), so
	 * the map_page_in-allocated PT leaks -- one page per process.
	 * Pre-installing makes priv_pt the single, PCB-tracked owner of
	 * PDE 512's page table, so map_page_in just fills the PTE and
	 * invalidates the TLB (no allocation).  PD[512] index =
	 * USER_PRIVATE_BASE >> 22 (= 512, see paging.h). */
	for (i = 0; i < PT_ENTRIES; i++)
		p->priv_pt[i] = 0;
	p->page_dir[USER_PRIVATE_BASE >> 22] =
		PAGE_ENTRY((u32)p->priv_pt, PAGE_PRESENT | PAGE_WRITE);
	map_page_in(p->page_dir, USER_PRIVATE_BASE, p->priv_phys,
		    PAGE_PRESENT | PAGE_WRITE);

	/* 3. Build the fake interrupt frame at the very top of the stack.
	 *
	 *    `do_first_switch` will load `saved_sp` into ESP and execute
	 *    `popa; iret`, so the saved_sp must point at a `struct
	 *    cpu_state` filled in as if the CPU had just been interrupted:
	 *
	 *      - 8 GP registers  (popa restores these)
	 *      - EIP = entry     (iret pops this → starts the process)
	 *      - CS   = 0x08     (kernel code segment, ring 0)
	 *      - EFLAGS = 0x202  (IF=1 so IRQs stay on; bit 1 reserved=1)
	 *
	 *    `popa` discards the ESP slot, so cpu_state.esp's value
	 *    doesn't matter at resume — we set it to (u32)frame for
	 *    cleanliness.  Once the process is running, its ESP is just
	 *    "wherever it pushed to last". */
	frame = (struct cpu_state *)(stack + PROC_STACK);
	frame--;                          /* one struct below the top */

	frame->edi = 0;
	frame->esi = 0;
	frame->ebp = 0;
	frame->esp = (u32)frame;
	frame->ebx = 0;
	frame->edx = 0;
	frame->ecx = 0;
	frame->eax = 0;

	frame->eip    = (u32)entry;
	frame->cs     = 0x08;
	frame->eflags = 0x202;

	p->saved_sp = (u32)frame;

	procs_count++;
	printf("sched: created pid %d (%s) eip=%x sp=%x\n",
	       pid, p->name, (u32)entry, p->saved_sp);

	return pid;
}

/* ------------------------------------------------------------------ */
/*  pick_next — select the highest-priority READY process              */
/*                                                                     */
/*  Walks the process table once starting just after `current_pid`,    */
/*  keeping track of the best (highest-priority, then earliest-seen)  */
/*  candidate.  Ties on priority are broken round-robin: because the  */
/*  scan starts at current_pid + 1, when several processes share the  */
/*  top priority the one that comes after the current pid is picked   */
/*  first — so they take turns.                                       */
/*                                                                     */
/*  idle_task (PRIO_IDLE) is always the lowest priority, so it only   */
/*  wins when every other process is SLEEPING or FINISHED — exactly   */
/*  what we want from a fallback.                                      */
/* ------------------------------------------------------------------ */

static int pick_next(void)
{
	int best_pid = -1;
	int best_prio = -1;
	int pid;
	int n;

	if (procs_count == 0)
		return -1;

	/* Start scanning just after the current process so equal-priority
	 * processes round-robin.  If current_pid is -1 (no current
	 * process yet, e.g. the very first pick), start at 0. */
	pid = (current_pid < 0) ? 0 : current_pid + 1;

	for (n = 0; n < MAX_PROCS; n++) {
		if (pid >= MAX_PROCS)
			pid = 0;

		if (procs[pid].used && procs[pid].state == PROC_READY) {
			/* Strictly higher priority wins outright.  Equal
			 * priority does NOT displace the earlier pick —
			 * that's what gives us round-robin among peers. */
			if (procs[pid].priority > best_prio) {
				best_prio = procs[pid].priority;
				best_pid  = pid;
			}
		}
		pid++;
	}

	return best_pid;
}

/* ------------------------------------------------------------------ */
/*  wake_sleepers — move SLEEPING processes whose time has come to READY */
/*                                                                     */
/*  Called once per tick from irq0_enter().  Walks the process table    */
/*  linearly — O(MAX_PROCS), trivial at our scale.  The signed-diff    */
/*  comparison `(s32)(g_ticks - p->wakeup_tick) >= 0` correctly       */
/*  handles u32 wraparound: if g_ticks has just wrapped to 0 while    */
/*  wakeup_tick is near UINT32_MAX, the subtraction yields a small     */
/*  positive number (mod 2^32 arithmetic), so a process that should   */
/*  wake up does.  A naive `g_ticks >= wakeup_tick' would fail here.   */
/*                                                                     */
/*  Only looks at PROC_SLEEPING (timer waits).  PROC_BLOCKED (event    */
/*  waits) has no timeout and is ignored here — only wake(pid) can     */
/*  rescue a BLOCKED process. */
/* ------------------------------------------------------------------ */

static void wake_sleepers(void)
{
	int i;

	for (i = 0; i < MAX_PROCS; i++) {
		struct pcb *p = &procs[i];

		if (p->used && p->state == PROC_SLEEPING &&
		    (s32)(g_ticks - p->wakeup_tick) >= 0)
			p->state = PROC_READY;
	}
}

/* ------------------------------------------------------------------ */
/*  reap_finished — free the stack + slot of dead processes            */
/*                                                                     */
/*  Called once per tick from irq0_enter().  A FINISHED process can    */
/*  never run again, so its stack page and PCB slot are pure waste.    */
/*  We free the stack page (back to the buddy allocator) and clear     */
/*  the slot so it can be reused by a future create_process.           */
/*                                                                     */
/*  Why the reaper lives here, not in sched_exit():  a process cannot   */
/*  free its own stack — it's running on it.  Someone else must do it  */
/*  after the process is definitely never coming back.  The timer       */
/*  interrupt is that someone: by the time irq0_enter runs, the dying   */
/*  process has already yielded (its `int $0x20` switched ESP away),   */
/*  so its stack is no longer in use and is safe to free.              */
/*                                                                     */
/*  Never reaps the current process: sched_exit() sets the state then   */
/*  triggers the IRQ that runs us, but `current_pid` is still set to    */
/*  the dying process during this call.  We must NOT free its stack     */
/*  yet — irq0_handler still needs to return through it.  It'll be      */
/*  reaped on a later tick, after current_pid has moved on.            */
/*                                                                     */
/*  ZOMBIE handling: a ZOMBIE is an exited process whose parent has not */
/*  waited on it yet.  We must NOT free it here unconditionally — the  */
/*  parent needs its exit_code.  reap_finished() only reaps a ZOMBIE   */
/*  whose parent is gone (FINISHED/unused), because then nothing can    */
/*  ever wait() on it, so keeping it would only leak.  A ZOMBIE with   */
/*  a living parent is reaped explicitly by wait().                    */
/* ------------------------------------------------------------------ */

/* Free one process's resources and mark its slot free.  Used by both
 * reap_finished() (timer-driven, for FINISHED and orphaned ZOMBIEs)
 * and wait() (parent-driven, for a waited ZOMBIE).  Caller guarantees
 * `p` is no longer running and its state is a terminal one. */
static void reap_proc(struct pcb *p)
{
	if (p->stack)
		free_page(p->stack);
	/* Free the per-process address-space pages.  The shared kernel
	 * page tables are NOT freed — they belong to the kernel and are
	 * still in use by every other PD. */
	if (p->priv_phys)
		free_page((void *)p->priv_phys);
	if (p->priv_pt)
		free_page(p->priv_pt);
	if (p->page_dir)
		free_page(p->page_dir);
	p->used = 0;
	p->state = PROC_READY;     /* clean slate */
	p->stack = NULL;
	p->saved_sp = 0;
	p->wakeup_tick = 0;
	p->priority = PRIO_USER;
	p->parent = -1;
	p->waiting_for = -1;
	p->exit_code = 0;
	p->page_dir = NULL;
	p->page_dir_phys = 0;
	p->priv_pt = NULL;
	p->priv_phys = 0;
	/* procs_count deliberately not decremented:
	 * it tracks total processes ever created, not
	 * live processes, so pick_next() and sched_start()
	 * don't need to special-case slots opening up. */
}

/* True if `pid` is not a live process (so it cannot wait() on a
 * child).  A parent with no parent of its own, or one that is itself
 * dead, counts as gone: nothing can observe its children. */
static int parent_is_gone(int pid)
{
	if (pid < 0 || pid >= MAX_PROCS)
		return 1;          /* no parent at all → gone */
	return !procs[pid].used || procs[pid].state == PROC_FINISHED;
}

static void reap_finished(void)
{
	int i;

	for (i = 0; i < MAX_PROCS; i++) {
		struct pcb *p = &procs[i];

		if (i == current_pid)
			continue;          /* never reap the running process */

		if (p->used && p->state == PROC_FINISHED) {
			reap_proc(p);
		} else if (p->used && p->state == PROC_ZOMBIE &&
			   parent_is_gone(p->parent)) {
			/* Orphaned zombie: no parent can ever wait(),
			 * so reap it now to avoid leaking the slot. */
			reap_proc(p);
		}
	}
}

/* ------------------------------------------------------------------ */
/*  irq0_enter — called from irq0_handler after `pusha`                 */
/*                                                                     */
/*  Receives `saved_sp` = ESP after pusha (points at the saved         */
/*  cpu_state on the *current* process's stack).  Returns the ESP to   */
/*  switch to (the next process's saved cpu_state).                    */
/* ------------------------------------------------------------------ */

u32 irq0_enter(u32 saved_sp)
{
	int next_pid;

	/* Count this tick — sched_wait_tick() spins waiting for this. */
	g_ticks++;

	/* Save the interrupted process's stack pointer.  If the process
	 * just called sched_exit() or sleep() it's already in a non-RUNNING
	 * state and we won't pick it next, but storing the saved_sp is
	 * harmless (it's used only when the process is READY again). */
	if (current_pid >= 0)
		procs[current_pid].saved_sp = saved_sp;

	/* Wake up any SLEEPING processes whose wakeup_tick has passed. */
	wake_sleepers();

	/* Reap FINISHED processes (free their stack + slot).  Done before
	 * pick_next() so freed slots are visible, though pick_next skips
	 * FINISHED anyway so the order isn't load-bearing. */
	reap_finished();

	/* Pick the next runnable process (skips FINISHED and SLEEPING). */
	next_pid = pick_next();

	/* Acknowledge the IRQ so the PIC will deliver the next one. */
	pic_send_eoi(0);

	/* No runnable process left — every process has exited.  Halt. */
	if (next_pid < 0)
		sched_finish();

	/* Only the current process is runnable — resume it. */
	if (next_pid == current_pid)
		return saved_sp;

	/* Switch to the next process.  Load its page directory into CR3
	 * before returning its saved_sp — once irq0_handler does
	 * `mov %eax, %esp` and `popa`, we're running in the next process's
	 * address space.  This is safe because both PDs identity-map the
	 * first 4 MiB (where the stack and the IRQ-handler code live), so
	 * the few instructions between this CR3 switch and the ESP switch
	 * touch only identity-mapped memory. */
	current_pid = next_pid;
	__asm__ volatile("mov %0, %%cr3" : : "r"(procs[current_pid].page_dir_phys));
	return procs[current_pid].saved_sp;
}

/* ------------------------------------------------------------------ */
/*  idle_task — the scheduler's permanent fallback                     */
/*                                                                     */
/*  pick_next() must always have at least one READY process to return, */
/*  otherwise the scheduler would deadlock.  idle_task is that        */
/*  process: it never exits, never blocks, just `hlt`s in a loop.      */
/*  `hlt` parks the CPU until the next interrupt; that interrupt       */
/*  (timer, keyboard, ...) wakes the CPU, runs its handler, and on      */
/*  `iret` we come back here to `hlt` again.  This is the hardware     */
/*  implementation of an event loop.                                  */
/*                                                                     */
/*  Created automatically by sched_start() as pid 0, before any user   */
/*  process.  Other processes can come and go; idle is forever.        */
/* ------------------------------------------------------------------ */

void idle_task(void)
{
	for (;;)
		asm volatile("hlt");
}

/* ------------------------------------------------------------------ */
/*  sched_wait_tick — yield until the next IRQ 0 tick                   */
/*                                                                     */
/*  Records the current g_ticks, then spins until it changes.          */
/*  Because g_ticks only advances inside irq0_enter (which switches    */
/*  away from us), this means: "preempt me, and don't resume me until  */
/*  at least one tick has passed."  Used to pace output so a single    */
/*  printf stays visible for one slice instead of vanishing instantly.  */
/* ------------------------------------------------------------------ */

void sched_wait_tick(void)
{
	u32 t = g_ticks;

	while (g_ticks == t)
		asm volatile("pause");   /* spin-wait hint to the CPU */
}

/* ------------------------------------------------------------------ */
/*  sleep — block the current process for `ticks` timer ticks          */
/*                                                                     */
/*  Records the wake-up time, marks the current process SLEEPING, and  */
/*  triggers a software IRQ 0 (`int $0x20`) — the same mechanism       */
/*  sched_exit() uses to yield.  irq0_enter() then picks the next      */
/*  READY process (the SLEEPING one is skipped).  Each subsequent tick  */
/*  wake_sleepers() checks whether the process should wake up; once it */
/*  does, the process is READY again and will be picked in turn.       */
/*                                                                     */
/*  sleep(0) means "yield for at least one tick": we set wakeup_tick   */
/*  to the current g_ticks, which has *already passed* by the time      */
/*  wake_sleepers runs on the next tick, so the process becomes READY   */
/*  immediately on the next tick.                                       */
/*                                                                     */
/*  Returns normally when the process is resumed (after the sleep has  */
/*  elapsed).  The caller continues from after the sleep() call.        */
/* ------------------------------------------------------------------ */

void sleep(unsigned int ticks)
{
	if (current_pid < 0)
		return;   /* called outside a process context — nothing to do */

	/* Record when this process should wake up.  g_ticks is volatile
	 * (it changes in irq0_enter), so read it once. */
	procs[current_pid].wakeup_tick = g_ticks + ticks;
	procs[current_pid].state = PROC_SLEEPING;

	/* Trigger IRQ 0 ourselves: this pushes the return-to-caller frame
	 * onto our stack, then jumps to irq0_handler, which saves our
	 * registers, calls irq0_enter (which sees we're SLEEPING and
	 * picks someone else), and resumes the next process.  When we're
	 * eventually woken and picked again, irq0_handler's `popa; iret`
	 * pops our saved registers and returns us to just after this `int`
	 * instruction — sleep() then returns normally to its caller. */
	asm volatile("int $0x20");
}

/* ------------------------------------------------------------------ */
/*  sched_block — block the current process until woken               */
/*                                                                     */
/*  The event-wait counterpart to sleep().  Instead of waking at a     */
/*  fixed tick, the process sleeps indefinitely — its wakeup_tick is   */
/*  set to the maximum u32 value so wake_sleepers()'s timeout check    */
/*  `(s32)(g_ticks - wakeup_tick) >= 0` never fires (g_ticks would     */
/*  have to reach 0xFFFFFFFF, effectively never).  The ONLY way out    */
/*  is for someone to call wake(pid) on us.                            */
/*                                                                     */
/*  Same yield mechanism as sleep(): mark SLEEPING, then `int $0x20`   */
/*  to hand the CPU to the scheduler.  When wake() sets us READY and   */
/*  the scheduler picks us again, we resume just after the `int`.      */
/*                                                                     */
/*  CRITICAL CONTRACT: the caller MUST have IF=0 (cli) around          */
/*  "check condition + sched_block()", otherwise the classic lost-     */
/*  wakeup race bites: the event fires between the check and the       */
/*  block, finds no waiter registered, and we sleep forever.  The      */
/*  caller re-enables interrupts (sti) only after the whole check-     */
/*  and-block is done or aborted.  This is the wait_event() pattern.   */
/* ------------------------------------------------------------------ */

void sched_block(void)
{
	if (current_pid < 0)
		return;   /* called outside a process context */

	/* PROC_BLOCKED is the event-wait state: wake_sleepers() ignores
	 * it (no timeout), so only wake(pid) can move us back to READY.
	 * Using a distinct state (rather than SLEEPING with a sentinel
	 * wakeup_tick) avoids a subtle bug: the sentinel 0xFFFFFFFF was
	 * being treated as "already expired" by the signed wraparound
	 * test, so the blocked process was re-armed as READY every tick
	 * — and because sched_block() runs with IF=0, the keyboard IRQ
	 * could not preempt it, and the system never made progress. */
	procs[current_pid].state = PROC_BLOCKED;

	/* Hand off to the scheduler.  `int` pushes EFLAGS (with IF as it
	 * currently is — 0, because the caller cleared it) before
	 * entering the interrupt gate (which itself clears IF).  When
	 * iret eventually resumes us, it pops that saved EFLAGS, so IF
	 * is restored to 0 here — the caller must sti on return. */
	asm volatile("int $0x20");
}

/* ------------------------------------------------------------------ */
/*  wake — move a blocked process back to READY                         */
/*                                                                     */
/*  Called from an ISR (or any context) after the event a process was  */
/*  waiting for has occurred.  Accepts both PROC_SLEEPING (timer)     */
/*  and PROC_BLOCKED (event) so the same primitive serves both wake    */
/*  paths.                                                            */
/*                                                                     */
/*  Safe to call with IF=0 (typical: from inside an ISR that has IF=0  */
/*  anyway) — it only touches PCB state, no re-entrancy issues.        */
/* ------------------------------------------------------------------ */

void wake(int pid)
{
	if (pid < 0 || pid >= MAX_PROCS)
		return;
	if (!procs[pid].used)
		return;
	if (procs[pid].state != PROC_BLOCKED && procs[pid].state != PROC_SLEEPING)
		return;   /* not waiting — nothing to wake */

	procs[pid].wakeup_tick = 0;
	procs[pid].state = PROC_READY;
}

/* ------------------------------------------------------------------ */
/*  sched_dump_ps — print the process table (for the `ps` command)     */
/*                                                                     */
/*  A read-only snapshot: one line per used slot showing pid, name,    */
/*  state and priority.  Lives in sched.c because procs[] is static     */
/*  here — exposing the table itself would leak the PCB layout into     */
/*  the shell.  The stringified state names are the only thing the      */
/*  caller needs to know about.                                        */
/*                                                                     */
/*  This runs in process context (called from the shell), so the table */
/*  can be mutated by a timer IRQ mid-scan.  We don't care: at our      */
/*  scale the worst case is a slightly stale line, never corruption     */
/*  (each field read is atomic-ish, and a name copy isn't needed).      */
/* ------------------------------------------------------------------ */

static const char *state_name(enum proc_state s)
{
	switch (s) {
	case PROC_READY:    return "READY";
	case PROC_SLEEPING: return "SLEEP";
	case PROC_BLOCKED:  return "BLOCK";
	case PROC_ZOMBIE:   return "ZOMBIE";
	case PROC_FINISHED: return "DONE";
	default:            return "?";
	}
}

u32 *sched_current_pd(void)
{
	if (current_pid < 0)
		return NULL;
	return procs[current_pid].page_dir;
}

void sched_dump_ps(void)
{
	int i;

	printf("PID  NAME     STATE   PRIO\n");
	for (i = 0; i < MAX_PROCS; i++) {
		if (!procs[i].used)
			continue;
		/* current_pid is marked with a star so you can see who's
		 * running the command. */
		printf("%d%-3s %-8s %-7s %d\n",
		       procs[i].pid,
		       (i == current_pid) ? "*" : " ",
		       procs[i].name,
		       state_name(procs[i].state),
		       procs[i].priority);
	}
}

/* ------------------------------------------------------------------ */
/*  sched_exit — terminate the current process                         */
/*                                                                     */
/*  Records `code` as the exit status, then either:                    */
/*    - becomes a ZOMBIE if a living parent might wait() on it (the    */
/*      parent needs to observe the death and read exit_code), or      */
/*    - becomes FINISHED if there is no living parent (nothing can       */
/*      wait() on it, so reap_finished() will free it on the next      */
/*      tick — no point lingering).                                    */
/*                                                                     */
/*  If the parent is currently blocked in wait(), wake it so it can    */
/*  reap this child immediately.  Then trigger a software IRQ 0 to      */
/*  switch away (same path as a timer tick).  Never returns.           */
/* ------------------------------------------------------------------ */

void sched_exit(int code)
{
	int me;
	struct pcb *p;

	if (current_pid < 0) {
		asm volatile("int $0x20");
		while (1)
			asm volatile("hlt");
	}

	me = current_pid;
	p = &procs[me];
	p->exit_code = code;

	if (p->parent >= 0 && procs[p->parent].used &&
	    procs[p->parent].state != PROC_FINISHED) {
		/* A living parent exists → stay as a ZOMBIE until it
		 * waits.  If it is already waiting, wake it now so it
		 * reaps us right away. */
		p->state = PROC_ZOMBIE;
		if (procs[p->parent].state == PROC_BLOCKED)
			wake(p->parent);
	} else {
		/* No living parent → nothing can wait() on us, so go
		 * straight to FINISHED for the next-tick reaper. */
		p->state = PROC_FINISHED;
	}

	/* Software-trigger IRQ 0 — same path as a hardware timer tick,
	 * just initiated by us instead of the PIT.  IRQ0_VECTOR = 0x20. */
	asm volatile("int $0x20");

	/* Should be unreachable: we're ZOMBIE/FINISHED, the scheduler
	 * won't pick us again.  Halt just in case. */
	while (1)
		asm volatile("hlt");
}

/* ------------------------------------------------------------------ */
/*  wait — block until a child exits, then reap it                    */
/*                                                                     */
/*  Mirrors Unix wait(): a parent blocks until one of its children     */
/*  becomes a ZOMBIE, then frees that child's slot and reports its     */
/*  pid + exit code.  If a child has already exited (ZOMBIE) when      */
/*  wait() is called, it returns immediately without blocking.         */
/*                                                                     */
/*  Returns the reaped child's pid, or -1 if the caller has no         */
/*  children at all (waiting would deadlock forever, so we refuse).    */
/* ------------------------------------------------------------------ */

static int has_living_child(int parent)
{
	int i;

	for (i = 0; i < MAX_PROCS; i++) {
		if (!procs[i].used)
			continue;
		if (procs[i].parent == parent)
			return 1;
	}
	return 0;
}

int wait(int *out_pid, int *out_code)
{
	int me;
	int child;

	if (current_pid < 0)
		return -1;

	me = current_pid;

	for (;;) {
		/* First, look for an already-exited (ZOMBIE) child to
		 * reap — no need to block if one is already waiting. */
		for (child = 0; child < MAX_PROCS; child++) {
			if (!procs[child].used)
				continue;
			if (procs[child].parent != me)
				continue;
			if (procs[child].state != PROC_ZOMBIE)
				continue;

			/* Found a zombie child: report it, then reap. */
			if (out_pid)
				*out_pid = child;
			if (out_code)
				*out_code = procs[child].exit_code;
			reap_proc(&procs[child]);
			return child;
		}

		/* No zombie yet.  If we have no children at all, waiting
		 * would deadlock — refuse rather than sleep forever. */
		if (!has_living_child(me))
			return -1;

		/* We have live children but none has exited yet.
		 * Block until one does (sched_exit() will wake us). */
		asm volatile("cli");
		procs[me].state = PROC_BLOCKED;
		asm volatile("int $0x20");
		asm volatile("sti");
	}
}

/* ------------------------------------------------------------------ */
/*  sched_finish — should be unreachable                              */
/*                                                                     */
/*  With idle_task always READY, pick_next() can never return -1.     */
/*  If we get here, something corrupted the process table or idle      */
/*  itself exited — both are bugs.  Halt and report.                    */
/* ------------------------------------------------------------------ */

static void sched_finish(void)
{
	printf("sched: PANIC — no runnable process (idle exited?)\n");
	asm volatile("cli");
	for (;;)
		asm volatile("hlt");
}

/* ------------------------------------------------------------------ */
/*  sched_start — switch into the first process                        */
/* ------------------------------------------------------------------ */

void sched_start(void)
{
	int first;

	/* Create the idle task — the scheduler's permanent fallback.
	 * pick_next() will always have at least this to run, so the
	 * system never deadlocks waiting for a READY process.  Created
	 * here (rather than in sched_init) because create_process needs
	 * the buddy allocator, which is set up before sched_start but
	 * not before sched_init.
	 *
	 * Override the default PRIO_USER with PRIO_IDLE so that
	 * pick_next() only chooses idle when no user process can run. */
	int idle_pid = create_process(idle_task, "idle");
	if (idle_pid >= 0)
		procs[idle_pid].priority = PRIO_IDLE;

	if (procs_count == 0) {
		printf("sched: no processes to start\n");
		while (1)
			asm volatile("hlt");
	}

	first = pick_next();
	current_pid = first;

	printf("sched: starting, first pid = %d\n", first);

	/* Load the first process's page directory before entering it.
	 * do_first_switch bypasses irq0_enter (it's a direct mov esp;
	 * popa; iret), so the per-tick CR3 switch doesn't run for this
	 * first handoff.  We must switch CR3 here, or the first process
	 * runs with the kernel's PD (no USER_PRIVATE_BASE mapping) and
	 * page-faults on its first access to its private page. */
	__asm__ volatile("mov %0, %%cr3" : : "r"(procs[first].page_dir_phys));

	/* Load the first process's saved_sp and `popa; iret` into it.
	 * This never returns: control transfers to process `first`. */
	do_first_switch(procs[first].saved_sp);

	/* not reached */
	while (1)
		asm volatile("hlt");
}
