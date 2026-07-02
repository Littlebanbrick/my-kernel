/* stage3.S — 32-bit protected mode entry
 *
 * Called from boot.S after it enters protected mode and jumps to 0xB000.
 * Sets up a stack and calls kernel_main() in C.
 *
 * No page tables, no long mode — just flat 32-bit protected mode.
 */

.section .text
.code32
.globl stage3_32_entry
stage3_32_entry:
	/* Set up stack (grows down from 0x7000, below our loaded code) */
	movl	$0x7000, %esp

	/* Clear direction flag (required by C calling convention) */
	cld

	/* Call the C entry point */
	call	kernel_main

	/* Halt if C code returns */
	cli
	hlt
	jmp	.
