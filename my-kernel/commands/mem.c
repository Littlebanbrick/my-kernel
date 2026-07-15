// mem.c — `mem` command: print the buddy allocator's free lists
//
// Same delegation pattern as ps: the allocator owns its internals, the
// shell just asks for a snapshot.

#include "commands.h"
#include "memory.h"

void cmd_mem(int argc, char **argv)
{
	(void)argc; (void)argv;
	mem_dump();
}
