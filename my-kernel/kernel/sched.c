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
// All processes run in ring 0 (CPL=0) on a single shared address space.
// No TSS, no privilege switching — pure register save/restore.

#include "types.h"
#include "printf.h"
#include "sched.h"
#include "memory.h"
#include "pic.h"

/* ------------------------------------------------------------------ */
/*  Process table                                                      */
/* ------------------------------------------------------------------ */

static struct pcb procs[MAX_PROCS];
static int current_pid;     /* pid of the currently running process   */
static int procs_count;     /* total processes ever created           */

/* Defined in idt_handlers.S — loads `saved_sp` into ESP and resumes
 * the process via `popa; iret`.  Used only for the very first switch
 * (from the kernel to process 0). */
extern void do_first_switch(u32 saved_sp);

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
	}
	current_pid = -1;     /* nothing running yet */
	procs_count = 0;
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

	for (i = 0; i < 7 && name && name[i]; i++)
		p->name[i] = name[i];
	p->name[i] = '\0';

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
/*  pick_next — round-robin selection                                  */
/* ------------------------------------------------------------------ */

static int pick_next(void)
{
	int pid;
	int n;

	if (procs_count == 0)
		return -1;

	pid = (current_pid < 0) ? 0 : current_pid + 1;
	for (n = 0; n < MAX_PROCS; n++) {
		if (pid >= MAX_PROCS)
			pid = 0;
		if (procs[pid].used)
			return pid;
		pid++;
	}
	return -1;
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

	/* Save the interrupted process's stack pointer */
	if (current_pid >= 0)
		procs[current_pid].saved_sp = saved_sp;

	/* Pick the next process */
	next_pid = pick_next();

	/* Acknowledge the IRQ so the PIC will deliver the next one */
	pic_send_eoi(0);

	/* If there's no other process to switch to, resume the current
	 * one by returning the same saved_sp we were handed. */
	if (next_pid < 0 || next_pid == current_pid)
		return saved_sp;

	current_pid = next_pid;
	return procs[current_pid].saved_sp;
}

/* ------------------------------------------------------------------ */
/*  sched_start — switch into the first process                        */
/* ------------------------------------------------------------------ */

void sched_start(void)
{
	int first;

	if (procs_count == 0) {
		printf("sched: no processes to start\n");
		while (1)
			asm volatile("hlt");
	}

	first = pick_next();
	current_pid = first;

	printf("sched: starting, first pid = %d\n", first);

	/* Load the first process's saved_sp and `popa; iret` into it.
	 * This never returns: control transfers to process `first`. */
	do_first_switch(procs[first].saved_sp);

	/* not reached */
	while (1)
		asm volatile("hlt");
}
