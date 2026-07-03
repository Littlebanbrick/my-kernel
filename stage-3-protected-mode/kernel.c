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

	/* Initialise physical memory bitmap */
	bitmap_init();

/*
	// Quick allocation / free test
	{
		void *p1 = alloc_page();
		void *p4 = alloc_pages(4);
		printf("page:  %x  pages(4): %x\n",
		       (u32)p1, (u32)p4);
		free_page(p1);
		free_pages(p4, 4);
		printf("freed, re-allocating...\n");
		p1 = alloc_page();
		printf("page:  %x\n", (u32)p1);
	}
*/

	/* Enable paging — identity-map first 4 MiB */
	paging_init();

	/* Paging demo: scatter → contiguous
	 *
	 * Allocate three physical pages (they may be scattered), then map
	 * them to a single contiguous block of virtual addresses so the
	 * program sees "one big buffer".  Verify by writing through the
	 * virtual address and reading back through the physical address. */
	{
		void *phys[3];
		void *virt;
		int i;

		for (i = 0; i < 3; i++)
			phys[i] = alloc_page();
		printf("phys pages: %x  %x  %x\n",
		       (u32)phys[0], (u32)phys[1], (u32)phys[2]);

		virt = valloc_pages(3);
		printf("virt block: %x - %x   (3 consecutive pages)\n",
		       (u32)virt, (u32)virt + 0x2FFF);

		/* Tell the page table: each virtual page -> one physical page */
		for (i = 0; i < 3; i++) {
			map_page((u32)virt + i * 0x1000,
				 (u32)phys[i],
				 PAGE_PRESENT | PAGE_WRITE);
		}

		/* Write a distinct pattern through the *virtual* address */
		*(u32 *)(virt + 0x0000) = 0xDEADBEEF;
		*(u32 *)(virt + 0x1000) = 0xCAFEBABE;
		*(u32 *)(virt + 0x2000) = 0x12345678;

		/* Read back through the *physical* address — it should
		 * contain exactly what we wrote via the virtual address! */
		printf("phys after virt write:\n");
		for (i = 0; i < 3; i++)
			printf("  [%d] phys %x = %x\n",
			       i, (u32)phys[i], *(u32 *)phys[i]);
		if (0xDEADBEEF == *(u32 *)phys[0] &&
			0xCAFEBABE == *(u32 *)phys[1] &&
			0x12345678 == *(u32 *)phys[2]) {
				printf("All the same!\n");
		}
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
