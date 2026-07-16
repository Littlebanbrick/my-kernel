/* echo.c — a ring-3 program that reads keys and echoes them.
 *
 * Exercises the blocking SYS_GETCHAR syscall: it loops reading one
 * character at a time (each call blocks the process until a key is
 * pressed) and echoes it back via SYS_PRINT, until Enter is pressed,
 * then SYS_EXIT.  This is the first ring-3 program that waits for an
 * external event — proof the syscall path can block, not just run to
 * completion.
 *
 * No libc: the single-char echo uses a 2-byte buffer on the user stack
 * (mapped PAGE_USER at USER_STACK_BASE), so sys_print can read it. */

#include "syscall.h"

__attribute__((section(".text.startup")))
void _start(void)
{
	char c;
	char buf[2];

	buf[1] = '\0';

	sys_print("echo> ");
	for (;;) {
		c = (char)sys_getchar();
		if (c == '\n' || c == '\r')
			break;
		buf[0] = c;
		sys_print(buf);
	}
	sys_print("\nbye\n");
	sys_exit(0);
}
