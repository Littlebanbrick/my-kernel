/* kernel.c — Test kernel_main with printf + IDT + PIC (32-bit) */

#include "types.h"
#include "vga.h"
#include "printf.h"
#include "idt.h"
#include "pic.h"
#include "utils.h"

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
	printf("char: %c  str: %s\n", 'X', "works");
	printf("dec: %d  hex: %x\n", 42, 0xDEAD);
	printf("percent: %%\n");

	/* Load IDT and remap PIC */
	idt_init();
	printf("IDT loaded.\n");

	pic_remap();
	printf("PIC remapped.\n");

	/* Unmask PIT timer (IRQ 0) and keyboard (IRQ 1) */
	outb(PIC1_DATA, inb(PIC1_DATA) & ~((1 << 0) | (1 << 1)));
	printf("PIT + keyboard unmasked.\n");

	/* Write static labels at rows 8-9 */
	for (i = 0; "Ticks: "[i]; i++)
		vga[8 * MAX_WIDTH + i] = 0x0700 | "Ticks: "[i];
	for (i = 0; "Key:   "[i]; i++)
		vga[9 * MAX_WIDTH + i] = 0x0700 | "Key:   "[i];

	/* Enable interrupts — PIT will now fire at ~18 Hz */
	__asm__ volatile("sti");

	while (1) {
		/* Sleep until the next interrupt */
		__asm__ volatile("hlt");
		/* Update the tick display */
		vga_write_dec_at(8, 7, g_ticks);
	}
}
