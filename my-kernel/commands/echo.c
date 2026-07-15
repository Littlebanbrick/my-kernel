// echo.c — `echo` command: print arguments separated by single spaces
//
// If no args, just the newline (like POSIX echo).

#include "commands.h"
#include "printf.h"

void cmd_echo(int argc, char **argv)
{
	int i;
	for (i = 1; i < argc; i++) {
		printf("%s", argv[i]);
		if (i < argc - 1)
			printf(" ");
	}
	printf("\n");
}
