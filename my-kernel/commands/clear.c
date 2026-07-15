// clear.c — `clear` command: wipe the screen and home the cursor
//
// Implemented in the console layer (printf.c) because it owns the
// shared cursor.

#include "commands.h"
#include "printf.h"

void cmd_clear(int argc, char **argv)
{
	(void)argc; (void)argv;
	console_clear();
}
