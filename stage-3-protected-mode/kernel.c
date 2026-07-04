/* kernel.c — Test kernel_main with printf + IDT + PIC (32-bit) */

#include "types.h"
#include "vga.h"
#include "printf.h"
#include "idt.h"
#include "pic.h"
#include "utils.h"
#include "memory.h"
#include "paging.h"

/* Tick counter — incremented by PIT IRQ 0 handler in idt.c */
volatile u32 g_ticks;

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
/*
	printf("char: %c  str: %s\n", 'X', "works");
	printf("dec: %d  hex: %x\n", 42, 0xDEAD);
	printf("percent: %%\n");
*/

	/* Load IDT and remap PIC */
	idt_init();
	printf("IDT loaded.\n");

	pic_remap();
	printf("PIC remapped.\n");

	/* Initialise physical memory buddy allocator */
	bitmap_init();

	/* Enable paging — identity-map first 4 MiB */
	paging_init();

	/* Combined buddy + paging test:
	 *
	 *   1. Buddy allocates 8 scattered physical pages.
	 *   2. Paging maps them to a contiguous virtual block.
	 *   3. Write through virtual addresses, verify through physical.
	 *   4. Free all 8 — buddy coalesces them into larger blocks.
	 *   5. alloc_pages(6) should succeed (proves coalescing worked). */
	{
		enum { N = 8 };
		void *phys[N];
		void *virt;
		u32 patterns[N];
		void *big;
		int i, ok = 1;

		for (i = 0; i < N; i++)
			phys[i] = alloc_page();
		printf("phys:");
		for (i = 0; i < N; i++)
			printf(" %x", (u32)phys[i]);
		printf("\n");

		/* Reserve N consecutive virtual pages and map them */
		virt = valloc_pages(N);
		printf("virt: %x - %x\n", (u32)virt,
		       (u32)virt + (N - 1) * 0x1000);

		for (i = 0; i < N; i++)
			map_page((u32)virt + i * 0x1000, (u32)phys[i],
				 PAGE_PRESENT | PAGE_WRITE);

		/* Write distinct patterns via virtual address */
		for (i = 0; i < N; i++)
			patterns[i] = 0xBEEF0000 | i;
		for (i = 0; i < N; i++)
			*(u32 *)(virt + i * 0x1000) = patterns[i];

		/* Read back via physical address — verify every one */
		printf("phys readback:\n");
		for (i = 0; i < N; i++) {
			u32 val = *(u32 *)phys[i];
			printf("  [%d] %x = %x\n",
			       i, (u32)phys[i], val);
			if (val != patterns[i]) {
				printf("       (expected %x)\n", patterns[i]);
				ok = 0;
			}
		}

		/* Free all, then buddy should merge them back */
		for (i = 0; i < N; i++)
			free_page(phys[i]);

		big = alloc_pages(6);
		if (big) {
			*(u32 *)big = 0xCAFE;
			free_pages(big, 6);
		} else {
			printf("alloc_pages(6) after free: FAIL\n");
			ok = 0;
		}

		printf("buddy+paging: %s\n", ok ? "OK" : "FAIL");
	}

/*
	// Unmask PIT timer (IRQ 0) and keyboard (IRQ 1)
	outb(PIC1_DATA, inb(PIC1_DATA) & ~((1 << 0) | (1 << 1)));
	printf("PIT + keyboard unmasked.\n");
*/

/*
	// Write static labels at rows 8-9
	for (i = 0; "Ticks: "[i]; i++)
		vga[8 * MAX_WIDTH + i] = 0x0700 | "Ticks: "[i];
	for (i = 0; "Key:   "[i]; i++)
		vga[9 * MAX_WIDTH + i] = 0x0700 | "Key:   "[i];
*/

	/* Enable interrupts — PIT will now fire at ~18 Hz */
	__asm__ volatile("sti");

	while (1) {
		/* Sleep until the next interrupt */
		__asm__ volatile("hlt");
/*
		// Update the tick display
		vga_write_dec_at(8, 7, g_ticks);
*/
	}
}
