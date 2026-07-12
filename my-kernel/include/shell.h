// shell.h — minimal command interpreter

#ifndef SHELL_H
#define SHELL_H

/* The shell process: prompts, reads a line, parses it into a command
 * name plus arguments, and dispatches to the matching built-in via a
 * command table.  Runs forever (until the kernel halts). */
void shell(void);

#endif
