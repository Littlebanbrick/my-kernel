// spawn.c — `spawn` command: demonstrate the process lifecycle end-to-end
//
// The shell (the parent) creates a child process, then blocks in
// wait() until the child exits and is reaped.  While they run
// concurrently you can see both in `ps` (run it from a second spawn
// if you like — the table holds up to MAX_PROCS).  This is the
// create -> run -> exit -> wait -> reap path that fork/exec will build
// on, minus the address-space copy (we create, not fork).
//
// We deliberately pace the child with sleep() so it survives long
// enough to be observable: a process that just printf()s and exits
// can finish inside a single timer slice, which makes the concurrency
// invisible.

#include "commands.h"
#include "sched.h"
#include "printf.h"

static void spawn_child(void)
{
	int i;

	printf("  [child] hello from a spawned process\n");
	for (i = 0; i < 3; i++) {
		printf("  [child] working... %d\n", i);
		sleep(2);
	}
	printf("  [child] done, exiting\n");
	sched_exit(7);
}

void cmd_spawn(int argc, char **argv)
{
	int pid;
	int waited_pid;
	int code;

	(void)argc; (void)argv;

	pid = create_process(spawn_child, "child");
	if (pid < 0) {
		printf("spawn: failed (process table full?)\n");
		return;
	}

	printf("spawn: created child pid %d, waiting for it...\n", pid);
	waited_pid = wait(NULL, &code);
	if (waited_pid < 0) {
		/* Shouldn't happen: we just made a child.  But if the
		 * child raced ahead and we somehow lost it, say so
		 * instead of hanging. */
		printf("spawn: wait() returned %d (no child?)\n", waited_pid);
		return;
	}
	printf("spawn: child %d reaped, exit code %d\n", waited_pid, code);
}
