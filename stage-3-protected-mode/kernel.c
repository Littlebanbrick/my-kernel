/* kernel.c — Test kernel_main using printf from my-kernel */

#include "types.h"
#include "vga.h"
#include "printf.h"

/* Write a byte to an x86 I/O port */
static inline void outb(unsigned short port, unsigned char val)
{
	__asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

void kernel_main(void)
{
	volatile u16 *const vga = (u16 *)0xB8000;
	int i;

	/* Disable the hardware cursor (it survives screen clears) */
	outb(0x3D4, 0x0A);	/* cursor start register */
	outb(0x3D5, 0x20);	/* bit 5 = 1 → disable */

	/* Clear screen */
	for (i = 0; i < MAX_WIDTH * MAX_HEIGHT; i++)
		vga[i] = 0x0700 | ' ';

	printf("Hello world!\n");
	printf("char: %c  str: %s\n", 'X', "works");
	printf("dec: %d  hex: %x\n", 42, 0xDEAD);
	printf("percent: %%\n");

	/* Scroll test — print more lines than the screen fits */
	for (i = 0; i < 2; i++)
		printf("Line %d\n", i);

	while (1)
		;
}
