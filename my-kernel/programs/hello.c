/* hello.c — a trivial program loaded from disk by exec.
 *
 * Freestanding: no libc, no kernel symbols.  It can only touch hardware
 * directly (here: the VGA text buffer at 0xB8000) and then RETURN —
 * control falls back to exec's trampoline (exec_run), which calls
 * sched_exit().  That `ret`-back-to-kernel is how a syscall-less
 * program terminates.
 *
 * No .bss: the image format is a single load segment (load size ==
 * memory size), so all data must be initialised — in .rodata/.data,
 * never `int x;` (that would be .bss, which we don't carry).  The
 * string below is `static const`, so it lands in .rodata and is baked
 * into the image. */

#define VGA ((volatile unsigned short *)0xB8000)

static const char msg[] = "hello from a disk-loaded program!";

void _start(void)
{
	int i;

	/* Write the message at row 15, yellow-on-black (attr 0x0E). */
	for (i = 0; msg[i]; i++)
		VGA[15 * 80 + i] =
			(unsigned short)((0x0E << 8) | (unsigned char)msg[i]);

	/* Falling off the end emits a `ret`, returning to exec_run. */
}
