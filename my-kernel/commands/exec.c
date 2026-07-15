// exec.c — `exec` command: load and run a disk program
//
// Usage:  exec [lba]
// Loads the program image at LBA `lba` (default 81 — the first slot
// after boot+stage3) and runs it as a process.  See exec.h / exec.c.

#include "commands.h"
#include "exec.h"
#include "printf.h"

void cmd_exec(int argc, char **argv)
{
	u32 lba = EXEC_DEFAULT_LBA;

	if (argc >= 2) {
		if (!parse_uint(argv[1], &lba)) {
			printf("exec: '%s' is not a number\n", argv[1]);
			return;
		}
	}

	do_exec(lba);
}
