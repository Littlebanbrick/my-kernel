// commands.h — built-in shell commands, one per file under commands/
//
// The shell dispatches a typed line to a handler via a command table
// (the table itself lives in shell.c).  Each handler lives in its own
// file in my-kernel/commands/ and shares the shape (argc, argv) — the
// same as a C main() — so dispatch is uniform regardless of how many
// args a command reads.
//
// To add a command:
//   1. write my-kernel/commands/<name>.c with a cmd_<name>(argc, argv);
//   2. declare it here;
//   3. add one row to builtins[] in shell.c;
//   4. add the object to the Makefile's KERNEL_OBJS.
// Nothing in shell.c's dispatch logic ever changes.
//
// This header is the shared contract between shell.c (which owns the
// table and dispatch) and the individual command files (which own the
// handlers).  help.c also walks the table to list commands, so the
// table and its length are exposed here as externs.

#ifndef COMMANDS_H
#define COMMANDS_H

#include "types.h"

/* Every built-in handler has this signature: argc/argv exactly like a
 * C main(), so the table can call them all the same way. */
typedef void (*cmd_fn)(int argc, char **argv);

/* One row in the command table: a name and the function that runs it. */
struct builtin {
	const char *name;
	cmd_fn fn;
};

/* The table itself and its length.  Defined in shell.c; walked by
 * dispatch() and listed by cmd_help().  num_builtins is a real
 * variable rather than a sizeof macro because other translation units
 * (help.c) cannot take sizeof an extern array. */
extern const struct builtin builtins[];
extern const unsigned int num_builtins;

/* Parse a non-negative decimal integer from `s`.  Returns 1 on success
 * (and writes the value to *out), 0 if `s` is NULL, empty, or non-
 * numeric.  Hand-rolled because we have no strtol; used by commands
 * that take a numeric argument (e.g. `disk <lba>`). */
int parse_uint(const char *s, u32 *out);

/* ---- handlers (one per file in my-kernel/commands/) ---------------- */
void cmd_help(int argc, char **argv);
void cmd_clear(int argc, char **argv);
void cmd_echo(int argc, char **argv);
void cmd_ps(int argc, char **argv);
void cmd_mem(int argc, char **argv);
void cmd_spawn(int argc, char **argv);
void cmd_disk(int argc, char **argv);
void cmd_reboot(int argc, char **argv);
void cmd_exec(int argc, char **argv);
void cmd_ls(int argc, char **argv);

#endif /* COMMANDS_H */
