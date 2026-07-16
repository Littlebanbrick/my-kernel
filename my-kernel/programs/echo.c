/* echo.c — a ring-3 program that reads a line and echoes it back.
 *
 * Exercises the line-mode SYS_READ syscall: it calls sys_read once,
 * which blocks until Enter (echoing each key and honouring Backspace
 * inside the kernel), then prints the committed line back.  SYS_GETCHAR
 * is the raw primitive beneath SYS_READ — this program shows canonical-
 * mode terminal input at user level.
 *
 * The line buffer lives on the user stack (mapped PAGE_USER at
 * USER_STACK_BASE), so the kernel can read into it through CR3. */

#include "syscall.h"

#define LINE_MAX 64

__attribute__((section(".text.startup")))
void _start(void)
{
	char buf[LINE_MAX];
	int n;

	sys_print("echo> ");
	n = sys_read(buf, LINE_MAX);

	sys_print("you typed: ");
	sys_print(buf);
	sys_print("\nbye (len ");
	/* print the length digit-by-digit — no %d in user space */
	if (n == 0) {
		char z[2];
		z[0] = '0';
		z[1] = '\0';
		sys_print(z);
	} else {
		/* reverse into a small buffer (max "nn" for 2 digits) */
		char tmp[8];
		int i = 0;
		while (n > 0) {
			tmp[i++] = '0' + (n % 10);
			n /= 10;
		}
		while (i > 0) {
			char one[2];
			one[0] = tmp[--i];
			one[1] = '\0';
			sys_print(one);
		}
	}
	sys_print(")\n");
	sys_exit(0);
}
