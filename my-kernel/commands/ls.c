// ls.c — `ls` command: list every file in the filesystem directory
//
// Usage:  ls
// Walks the in-memory FS directory table and prints one line per used
// entry: name, size, and the LBA where the file's image starts.  This is
// the user-facing view of fs_list() — the table itself is private to
// fs.c, so the command is a thin wrapper that calls in.

#include "commands.h"
#include "fs.h"

void cmd_ls(int argc, char **argv)
{
	(void)argc; (void)argv;

	fs_list();
}
