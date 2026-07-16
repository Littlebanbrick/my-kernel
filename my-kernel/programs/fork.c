/* fork.c — a ring-3 program that exercises SYS_FORK.
 *
 * Proves three things at once:
 *
 *   1. fork() returns twice from the same call site: once in the
 *      parent (eax = child pid) and once in the child (eax = 0).
 *   2. The child is a true copy: both resume at the same C statement
 *      right after the sys_fork() call, with identical registers
 *      except eax.
 *   3. The two address spaces are independent: a variable written
 *      before the fork (x = 100) is visible to both, but a write
 *      after the fork by either side diverges — the child sets x to
 *      11, the parent to 22, and each prints its own value.  If the
 *      address spaces were shared (a bug), one would clobber the other
 *      and the printed values would not be the expected 11 / 22.
 *
 * The `x` variable is a stack local, so it lives in the user stack
 * page — a PAGE_USER mapping that clone_address_space deep-copies. */

#include "syscall.h"

/* Print an unsigned int digit-by-digit — no %d in user space.  Same
 * hand-rolled technique echo.c uses for the line length. */
static void print_uint(unsigned int n)
{
	char tmp[8];
	int i = 0;

	if (n == 0) {
		char z[2];
		z[0] = '0';
		z[1] = '\0';
		sys_print(z);
		return;
	}
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

__attribute__((section(".text.startup")))
void _start(void)
{
	int x = 100;          /* visible to both: set before the fork */
	int pid;

	pid = sys_fork();     /* returns twice — eax differs each time */

	if (pid == 0) {
		x = 11;       /* child's private write */
		sys_print("child  x=");
	} else {
		x = 22;       /* parent's private write */
		sys_print("parent x=");
	}
	print_uint((unsigned int)x);
	sys_print(" (pid=");
	print_uint((unsigned int)pid);
	sys_print(")\n");

	sys_exit(0);
}
