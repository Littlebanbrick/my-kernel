/* stage3_dbg.S — Minimal 32-bit test: write "OK" to VGA, halt.
 *
 * Use this to verify the boot sector loads and jumps correctly.
 * No page tables, no long mode — just direct VGA memory access.
 *
 * Empty page-table sections are included so the existing linker.ld
 * can link this as a drop-in replacement for stage3.S.
 */

.section .pml4, "aw", @progbits
.align 4096
.space 4096

.section .pdp, "aw", @progbits
.align 4096
.space 4096

.section .pd, "aw", @progbits
.align 4096
.space 4096

.section .text
.code32
.globl stage3_32_entry
stage3_32_entry:
	/* VGA text-mode framebuffer is at 0xB8000 */
	movl	$0xB8000, %edi

	/* Write "O" (0x4F) with attribute 0x07 */
	movw	$0x074F, (%edi)

	/* Write "K" at the next character cell */
	movw	$0x074B, 2(%edi)

	/* Halt forever */
1:	cli
	hlt
	jmp	1b
