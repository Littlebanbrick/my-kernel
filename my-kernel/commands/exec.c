// exec.c — `exec` command: load and run a disk program
//
// Usage:
//   exec <name>    Look up `name` in the FS directory table and run it.
//   exec <lba>     Run the program image whose header sector is at LBA
//                  `lba` (debug fallback, bypassing the directory table).
//
// A name is matched against fs_lookup(); a pure-decimal argument falls
// through to the legacy LBA path.  See exec.h / exec.c and fs.h / fs.c.

#include "commands.h"
#include "exec.h"
#include "fs.h"
#include "printf.h"

void cmd_exec(int argc, char **argv)
{
	u32 lba;

	if (argc < 2) {
		/* No argument: run the first program in the table, if any.
		 * (The legacy default was EXEC_DEFAULT_LBA by number.) */
		lba = EXEC_DEFAULT_LBA;
		do_exec(lba);
		return;
	}

	/* Try the FS directory first: any argument that isn't a pure
	 * decimal number is treated as a filename.  If it is a number,
	 * fall through to the legacy LBA path. */
	if (!parse_uint(argv[1], &lba)) {
		const struct fs_dirent *d = fs_lookup(argv[1]);

		if (!d) {
			printf("exec: no such file '%s'\n", argv[1]);
			return;
		}
		do_exec(d->start_lba);
		return;
	}

	do_exec(lba);
}
