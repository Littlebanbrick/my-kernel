/* kernel.c — Test kernel_main with printf + IDT + PIC (32-bit) */

#include "types.h"
#include "vga.h"
#include "printf.h"
#include "idt.h"
#include "pic.h"
#include "utils.h"
#include "memory.h"
#include "paging.h"
#include "sched.h"
#include "pit.h"
#include "paging.h"
#include "ring3.h"
#include "kbd.h"
#include "readline.h"
#include "shell.h"
#include "fs.h"

/* Write a decimal number directly to VGA memory at a fixed position.
 * Safe to call from any context — no shared state, no printf. */
static void vga_write_dec_at(int row, int col, u32 val)
{
	u16 *const vga = (u16 *)0xB8000;
	char buf[10];
	int i = 0;
	int pos = row * MAX_WIDTH + col;
	int j;

	do {
		buf[i++] = '0' + (val % 10);
		val /= 10;
	} while (val > 0);

	/* Pad to 6 characters (enough for our test run) */
	for (j = 0; j < 6 - i; j++)
		vga[pos++] = 0x0700 | ' ';

	/* Write digits in correct order */
	for (j = i - 1; j >= 0; j--)
		vga[pos++] = 0x0700 | buf[j];
}

/* ==================================================================== */
/*  Ring 3 experiment — TEMPORARILY DISABLED while we build the          */
/*  scheduler.  The ring-3 infrastructure (ring3.c / ring3.h) is left     */
/*  untouched; only the experiment that wires it up is commented out.   */
/* ==================================================================== */

#if 0

/* ---- Ring 3 experiment (shared page for user code + kernel stack) ---- */
static u8 ring3_page[4096] __attribute__((aligned(4096)));

static void run_ring3_experiment(void)
{
	u32 *pt;
	u32  pdx, ptx;
	int  i;

	/* 1. Set up GDT with user segments + TSS */
	ring3_init_gdt_tss((u32)ring3_page + 0x1000);

	/* 2. Find or allocate a page table for the user virtual range */
	pdx = 0x400000 >> 22;			/* PDX = 1 */

	if (!(kernel_page_dir[pdx] & PAGE_PRESENT)) {
		pt = (u32 *)alloc_page();
		if (!pt) {
			printf("ring3: OOM for PT\n");
			return;
		}
		for (i = 0; i < 1024; i++)
			pt[i] = 0;
		kernel_page_dir[pdx] = PAGE_ENTRY((u32)pt,
			PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
	} else {
		pt = (u32 *)(kernel_page_dir[pdx] & 0xFFFFF000);
		kernel_page_dir[pdx] |= PAGE_USER;
	}

	/* 3. Map the same physical page at two virtual addresses:
	 *      0x400000 — user code (lower part of ring3_page)
	 *      0x500000 — user stack (upper part, grows down from 0x501000) */
	ptx = (0x400000 >> 12) & 0x3FF;
	pt[ptx] = PAGE_ENTRY((u32)ring3_page, PAGE_PRESENT | PAGE_WRITE | PAGE_USER);

	ptx = (0x500000 >> 12) & 0x3FF;
	pt[ptx] = PAGE_ENTRY((u32)ring3_page, PAGE_PRESENT | PAGE_WRITE | PAGE_USER);

	asm volatile("invlpg (%0)" : : "r"(0x400000) : "memory");
	asm volatile("invlpg (%0)" : : "r"(0x500000) : "memory");

	printf("ring3: jumping to user code at 0x400000...\n");

	/* 4. Write user program and enter ring 3
	 *
	 *    mov eax, 'Y'    → syscall (proves we are in ring 3)
	 *    int $0x80
	 *    cli             → privileged instruction → #GP (proves protection) */
	ring3_page[0] = 0xB8;			/* mov eax, imm32 */
	ring3_page[1] = 'Y';
	ring3_page[2] = 0;
	ring3_page[3] = 0;
	ring3_page[4] = 0;
	ring3_page[5] = 0xCD;			/* int  */
	ring3_page[6] = 0x80;			/* 0x80 */
	ring3_page[7] = 0xFA;			/* cli — #GP in ring 3 */

	ring3_jump(0x400000, 0x501000);
}

#endif /* ring 3 experiment disabled */

/* ==================================================================== */
/*  Scheduler experiment — two processes that take turns                 */
/* ==================================================================== */

/* Process A / B — the address-space isolation demo.
 *
 * Temporarily disabled: they printf() concurrently with the shell, and
 * all three share a single VGA cursor with no arbitration, so their
 * output collides and the screen scrambles.  Re-enable once we solve
 * the multi-process cursor-contention problem (a follow-up analysis).
 * The bodies are kept (not deleted) for that future use. */
#if 0
static void process_a(void)
{
	volatile char *priv = (volatile char *)USER_PRIVATE_BASE;
	int i;

	*priv = 'A';
	for (i = 0; i < 8; i++) {
		printf("A sees: %c\n", *priv);
		sleep(3);
	}
	sched_exit(0);
}

static void process_b(void)
{
	volatile char *priv = (volatile char *)USER_PRIVATE_BASE;
	int i;

	*priv = 'B';
	for (i = 0; i < 8; i++) {
		printf("B sees: %c\n", *priv);
		sleep(5);
	}
	sched_exit(0);
}
#endif

/* ==================================================================== */

void kernel_main(void)
{
	volatile u16 *const vga = (u16 *)0xB8000;
	int i;

	/* Disable the hardware cursor */
	outb(0x3D4, 0x0A);
	outb(0x3D5, 0x20);

	/* Clear screen */
	for (i = 0; i < MAX_WIDTH * MAX_HEIGHT; i++)
		vga[i] = 0x0700 | ' ';

	printf("Hello from 32-bit kernel!\n");

	/* Load IDT and remap PIC */
	idt_init();
	pic_remap();
	
	/* Initialise physical memory buddy allocator */
	bitmap_init();

	/* Enable paging — identity-map first 4 MiB */
	paging_init();

	/* Load the GDT/TSS for ring 3.  ring3_init_gdt_tss() rebuilds the
	 * GDT (adding user code/data segments and a TSS descriptor), loads
	 * it, reloads the segment registers onto the new kernel descriptors,
	 * and `ltr`s the TSS.  The esp0 argument is a placeholder top of
	 * kernel stack; the scheduler refreshes tss.esp0 on every context
	 * switch (ring3_set_esp0), so the value passed here only matters
	 * until the first switch.
	 *
	 * Done here, before sched_init(): once IRQs are unmasked below, any
	 * interrupt could (in principle) privilege-switch, so the TSS must
	 * already be valid.  Doing it after paging_init is required: the
	 * GDT/TSS live in identity-mapped low memory and we are already
	 * paging, so the addresses we feed to lgdt/ltr are virtual but
	 * identity-mapped, which is fine. */
	ring3_init_gdt_tss(0x7000);

	/* ---- Scheduler setup ---- */
	sched_init();
	create_process(shell, "shell");

	/* Initialise the filesystem directory table.  Done after paging
	 * (the table buffer is kernel memory) but before the shell, so
	 * `ls` / `exec <name>` work from the first prompt.  On first boot
	 * the table is blank: fs_init scans the program region, names each
	 * built-in program from its header, and writes the table back. */
	fs_init();

	/* Programme the PIT: 10 Hz → one tick every 100 ms.  Slow
	 * enough to read each line of output as it appears. */
	pit_init(10);

	/* Unmask IRQ 0 (PIT) and IRQ 1 (keyboard) so both can reach
	 * the CPU.  pic_remap() left every IRQ masked; we enable the
	 * ones we have handlers for. */
	pic_unmask_irq(0);
	pic_unmask_irq(1);

	/* Start the scheduler — never returns */
	sched_start();

	/* not reached */
	while (1)
		__asm__ volatile("hlt");
}
