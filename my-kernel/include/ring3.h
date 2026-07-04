// ring3.h — Ring 3 infrastructure (32-bit protected mode)

#ifndef RING3_H
#define RING3_H

#include "types.h"

/* Build a new GDT with kernel + user segments, initialise the TSS
 * (kstack_top = physical top of the kernel stack used when ring-3
 *  code triggers an interrupt), load GDT/TSS, reload segments.
 * Call once before entering ring 3. */
void ring3_init_gdt_tss(u32 kstack_top);

/* Jump to ring 3 via iret.  Never returns.
 *   eip        — user code entry point (must be mapped with PAGE_USER)
 *   stack_top  — user stack pointer (must be mapped with PAGE_USER)  */
void ring3_jump(u32 eip, u32 stack_top);

#endif
