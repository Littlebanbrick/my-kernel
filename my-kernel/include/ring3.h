// ring3.h — Ring 3 infrastructure (32-bit protected mode)

#ifndef RING3_H
#define RING3_H

#include "types.h"

/* Build a new GDT with kernel + user segments, initialise the TSS
 * (kstack_top = physical top of the kernel stack used when ring-3
 *  code triggers an interrupt), load GDT/TSS, reload segments.
 * Call once before entering ring 3. */
void ring3_init_gdt_tss(u32 kstack_top);

/* Update the TSS's kernel-stack pointer (esp0).  The scheduler calls
 * this on every context switch so a privilege-switching interrupt
 * (ring 3 -> ring 0) lands on the CURRENT process's kernel stack, not
 * a stale one.  Pass the top (highest address) of the process's kernel
 * stack — the CPU uses esp0 as the starting ESP and grows down.
 *
 * Dormant while every process runs in ring 0: the CPU only reads esp0
 * on a privilege change (CPL != DPL of the interrupt gate), which never
 * happens at CPL=0.  So this is plumbing for ring 3 — but it must be in
 * place before the first ring-3 entry. */
void ring3_set_esp0(u32 esp0);

/* Jump to ring 3 via iret.  Never returns.
 *   eip        — user code entry point (must be mapped with PAGE_USER)
 *   stack_top  — user stack pointer (must be mapped with PAGE_USER)  */
void ring3_jump(u32 eip, u32 stack_top);

#endif
