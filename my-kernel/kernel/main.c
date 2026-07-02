// main.c — Kernel entry point (called from stage-3 boot code)

#include "types.h"
#include "vga.h"
#include "printf.h"
#include "idt.h"

void kernel_main(void)
{
	volatile u16 *const vga = (u16 *)0xB8000;
	int i;

	/* Clear screen */
	for (i = 0; i < MAX_WIDTH * MAX_HEIGHT; i++)
		vga[i] = 0x0700 | ' ';

	/* Initialise IDT so exceptions go to our handler */
	idt_init();

	printf("Kernel started. IDT loaded.\n");
	printf("Triggering invalid opcode (#UD) ...\n");

	/*
	 * Deliberately execute an undefined instruction.
	 * The CPU raises exception 6 → our handler prints the
	 * error and halts.
	 */
	asm volatile("ud2");

	/* Not reached */
	while (1)
		asm volatile("hlt");
}
