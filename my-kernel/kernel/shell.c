// shell.c — minimal command interpreter built on readline
//
// The shell loop is simple: prompt, read a line, dispatch.  The
// interesting part is how a line becomes a function call.  Two common
// approaches:
//
//   1. A chain of strcmp() against every command name.  Linear, ugly,
//      and you must remember to extend the chain for each new command.
//
//   2. A command table: an array of {name, handler} pairs.  To run a
//      command, walk the table, strcmp the name, call the handler.
//      Adding a command is one line in the table — no dispatch code
//      changes.  This is how real shells and Linux init binaries work.
//
// We use (2).  It scales: the dispatch logic never grows as commands
// are added; only the table does.
//
// The handlers themselves live one-per-file in my-kernel/commands/
// (see commands.h).  This file owns only the table, the tokenizer, and
// the dispatch/loop glue — the parts that are stable, not the parts
// that grow with every new command.

#include "shell.h"
#include "kbd.h"          /* getchar (indirectly, via readline) */
#include "readline.h"
#include "printf.h"       /* printf */
#include "commands.h"     /* builtins table + handler decls + parse_uint */

#define LINE_MAX 64

/* The number of typed command-line tokens we parse out of a line
 * (command + arguments).  Our commands take few args, so this is
 * plenty. */
#define ARGV_MAX 8

/* The command table.  To add a command: write its handler in
 * my-kernel/commands/<name>.c, declare it in commands.h, then add one
 * row here and the object to the Makefile.  Nothing else in this file
 * changes.  Order doesn't matter for correctness; help() prints them
 * in this order. */
const struct builtin builtins[] = {
	{ "help",   cmd_help   },
	{ "clear",  cmd_clear  },
	{ "echo",   cmd_echo   },
	{ "ps",     cmd_ps     },
	{ "mem",    cmd_mem    },
	{ "spawn",  cmd_spawn  },
	{ "disk",   cmd_disk   },
	{ "ls",     cmd_ls     },
	{ "write",  cmd_write  },
	{ "cat",    cmd_cat    },
	{ "rm",     cmd_rm     },
	{ "exec",   cmd_exec   },
	{ "reboot", cmd_reboot },
};

/* A real variable (not a sizeof macro) so other translation units —
 * help.c, which walks the table — can read the length without taking
 * sizeof an extern array (which C can't do across files). */
const unsigned int num_builtins = sizeof(builtins) / sizeof(builtins[0]);

/* ---- string helpers ---------------------------------------------------
 *
 * We have no libc, so the usual string functions don't exist.  These
 * are the two the dispatcher needs.  parse_uint() is shared across
 * command handlers, so it lives with the commands (declared in
 * commands.h) — but we define it here, next to tokenize(), since both
 * are the line-parsing layer. */
static int str_equal(const char *a, const char *b)
{
	while (*a && *b) {
		if (*a != *b)
			return 0;
		a++;
		b++;
	}
	/* equal only if both ended at the same time */
	return *a == *b;
}

/* Split `line` in-place into NUL-terminated tokens.  Spaces are the
 * only separator.  Writes up to argv_max-1 token pointers into argv,
 * returns the count.  argv[count] is set to NULL (like execve). */
static int tokenize(char *line, char **argv, int argv_max)
{
	int argc = 0;

	/* skip leading spaces */
	while (*line == ' ')
		line++;

	while (*line && argc < argv_max - 1) {
		argv[argc++] = line;
		/* advance to end of token */
		while (*line && *line != ' ')
			line++;
		if (*line == ' ') {
			*line = '\0';
			line++;
			/* collapse runs of spaces */
			while (*line == ' ')
				line++;
		}
	}
	argv[argc] = NULL;
	return argc;
}

/* Parse a non-negative decimal integer from `s`.  Returns 0 and writes
 * 0 to *ok if the string is empty or non-numeric, 1 on success.  We
 * have no strtol, so this is hand-rolled; shared by commands that take
 * a numeric argument (e.g. `disk <lba>`). */
int parse_uint(const char *s, u32 *out)
{
	u32 v = 0;

	if (!s || !*s)
		return 0;
	while (*s) {
		if (*s < '0' || *s > '9')
			return 0;
		v = v * 10 + (u32)(*s - '0');
		s++;
	}
	*out = v;
	return 1;
}

/* ---- dispatch + main loop ------------------------------------------- */

/* Try to run one command line.  argv[0] is the command name.  If it
 * doesn't match any table entry, complain (don't crash). */
static void dispatch(int argc, char **argv)
{
	unsigned int i;

	if (argc == 0)
		return;			/* empty line, nothing to do */

	for (i = 0; i < num_builtins; i++) {
		if (str_equal(argv[0], builtins[i].name)) {
			builtins[i].fn(argc, argv);
			return;
		}
	}

	/* No match.  argv[0] is NUL-terminated because tokenize() always
	 * NUL-terminates every token, so %s is safe here. */
	printf("%s: command not found\n", argv[0]);
}

void shell(void)
{
	char line[LINE_MAX];
	char *argv[ARGV_MAX];

	for (;;) {
		printf("> ");
		readline(line, sizeof(line));
		dispatch(tokenize(line, argv, ARGV_MAX), argv);
	}
}
