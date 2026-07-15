// ps.c — `ps` command: print the process table
//
// Delegates to the scheduler, which owns the PCB array; the shell
// just triggers a dump.

#include "commands.h"
#include "sched.h"

void cmd_ps(int argc, char **argv)
{
	(void)argc; (void)argv;
	sched_dump_ps();
}
