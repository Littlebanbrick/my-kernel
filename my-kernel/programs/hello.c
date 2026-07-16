/* hello.c — a disk-loaded program that runs in ring 3.
 *
 * Freestanding: no libc, no kernel symbols.  It cannot touch hardware
 * directly — the VGA buffer and all kernel memory are mapped
 * supervisor-only, so a direct write would fault (#GP).  The only way
 * out is the int 0x80 system call, which (in this toy kernel) prints
 * one character and then exits the process.
 *
 * So this program does the minimum that proves ring 3 works:
 *
 *   mov eax, 'H'   ; the character to print (= the syscall request)
 *   int 0x80       ; trap to the kernel — ring 3 -> ring 0
 *
 * `int 0x80` never returns: the kernel prints 'H' and exits us.  There
 * is no `ret` because there is nowhere sane to return to in ring 3.
 *
 * Built flat at 0x400000 (prog.ld).  No .bss — the image format is a
 * single load segment, so all data must be initialised. */

void _start(void)
{
	__asm__ volatile (
		"mov $'H', %%eax\n"   /* eax = 'H' (72) — print+exit request */
		"int $0x80\n"         /* syscall: kernel prints 'H', exits us */
		:
		:
		: "eax"
	);

	/* unreachable: int 0x80 exits the process */
	for (;;)
		__asm__ volatile ("hlt");
}
