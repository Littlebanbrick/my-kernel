/* kernel.c — Test kernel_main with printf + IDT (32-bit) */

#include "types.h"
#include "vga.h"
#include "printf.h"
#include "idt.h"

/* Write a byte to an x86 I/O port */
static inline void outb(unsigned short port, unsigned char val)
{
	__asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
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

	/* Load the IDT so exceptions don't triple-fault */
	idt_init();
	printf("IDT loaded.  Triggering #UD...\n");

	/* Deliberately trigger exception 6 (Invalid Opcode) */
	__asm__ volatile("ud2");

	/* Should never reach here */
	printf("Survived!  Halted.\n");
	while (1)
		asm volatile("hlt");
}
