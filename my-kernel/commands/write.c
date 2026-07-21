// write.c — `write <name>` command: read one line and save it as a file
//
// Usage:  write <name>
//
// Reads a single line of input (up to a sector) and stores it as a new
// file named <name> in the FS directory.  The line is saved verbatim
// WITHOUT its trailing newline, so `write foo` then `cat foo` echoes
// exactly what was typed on that one line.  This is the user-facing
// front end for fs_create(); the FS owns the on-disk format.
//
// Demo scope: one line per file.  Multi-line files would mean looping
// readline until EOF/blank-line, but we have no end-of-input convention
// in this toy shell, so we keep it to a single line (YAGNI).

#include "commands.h"
#include "fs.h"
#include "readline.h"      /* readline */
#include "printf.h"

/* The line buffer doubles as the file's data buffer: fs_create writes
 * exactly `len` bytes from it.  Kept under a sector so a created file
 * always fits in one (fs_create enforces size <= ATA_SECTOR_SIZE). */
#define WRITE_MAX 256

void cmd_write(int argc, char **argv)
{
	char line[WRITE_MAX];
	int len;

	if (argc < 2) {
		printf("usage: write <name>\n");
		return;
	}

	printf("> ");
	len = readline(line, sizeof(line));
	if (len <= 0) {
		printf("write: empty line, nothing saved\n");
		return;
	}

	fs_create(argv[1], line, (u32)len);
}
