// help.c — `help` command: list every known command
//
// Walks the command table (builtins[]) and prints each entry's name.
// This needs no per-command knowledge: newly added commands appear
// automatically because the table is the single source of truth.

#include "commands.h"
#include "printf.h"

void cmd_help(int argc, char **argv)
{
	unsigned int i;
	(void)argc; (void)argv;

	printf("commands:\n");
	for (i = 0; i < num_builtins; i++)
		printf("  %s\n", builtins[i].name);
}
