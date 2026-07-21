// rm.c — `rm <name>` command: remove a file from the directory
//
// Usage:  rm <name>
//
// Removes <name> from the FS directory table by clearing its slot, then
// writes the table back so the removal survives a reboot.  The file's
// DATA sectors are NOT reclaimed: the allocation cursor never moves
// backward, so the bytes stay on disk as unreachable orphans.  This is
// the accepted demo simplification (see fs_unlink for the rationale and
// how a real FS avoids the leak with a free-space bitmap).
//
// There is no confirmation prompt: the cost of a wrong `rm` in a toy
// kernel is low (rebuild the image), and we have no trash/undo anyway.

#include "commands.h"
#include "fs.h"
#include "printf.h"

void cmd_rm(int argc, char **argv)
{
	if (argc < 2) {
		printf("usage: rm <name>\n");
		return;
	}

	fs_unlink(argv[1]);
}
