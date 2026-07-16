/* syscall.h — user-side system-call stubs (ring 3).
 *
 * Tiny inlined wrappers around `int $0x80`.  The kernel's
 * syscall_enter() (my-kernel/kernel/syscall.c) dispatches on eax; ebx
 * is the first argument.  These numbers MUST match the kernel's SYS_* in
 * my-kernel/include/syscall.h — the two sides compile as independent
 * units (no shared header at runtime), so a mismatch is a silent
 * wrong-syscall bug, not a link error.
 *
 * Freestanding: no libc, so the ABI is hand-rolled in inline asm. */

#ifndef PROG_SYSCALL_H
#define PROG_SYSCALL_H

#define SYS_EXIT    0
#define SYS_PRINT   1
#define SYS_GETCHAR 2

/* Print a NUL-terminated string, then return to the caller.  The kernel
 * reads the string through the current process's mappings, which stay
 * live across the interrupt (CR3 is unchanged). */
static inline void sys_print(const char *s)
{
	__asm__ volatile (
		"int $0x80\n"
		: : "a"(SYS_PRINT), "b"(s)
	);
}

/* Block until a key is available, then return it as an unsigned char
 * (0..255).  The kernel's getchar sleeps the process until the next
 * keystroke; from the caller's view it just takes a while.  The char
 * comes back in eax. */
static inline int sys_getchar(void)
{
	int ret;

	__asm__ volatile (
		"int $0x80\n"
		: "=a"(ret)
		: "a"(SYS_GETCHAR)
		: "memory"
	);
	return ret;
}

/* Terminate this process with exit code `code`.  Never returns: the
 * kernel's SYS_EXIT handler calls sched_exit(), which does not come back. */
static inline __attribute__((noreturn)) void sys_exit(int code)
{
	__asm__ volatile (
		"int $0x80\n"
		: : "a"(SYS_EXIT), "b"(code)
	);
	__builtin_unreachable();
}

#endif /* PROG_SYSCALL_H */
