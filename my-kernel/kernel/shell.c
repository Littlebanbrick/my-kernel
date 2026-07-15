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

#include "shell.h"
#include "kbd.h"          /* getchar (indirectly, via readline) */
#include "readline.h"
#include "printf.h"       /* printf, putchar_one, console_clear */
#include "sched.h"        /* sched_dump_ps (ps), wait/create_process (spawn) */
#include "memory.h"       /* mem_dump (mem), alloc_page (disk) */
#include "ata.h"          /* ata_read_sectors (disk) */
#include "utils.h"        /* inb/outb (reboot: 8042 reset) */

#define LINE_MAX 64

/* The number of typed command-line tokens we parse out of a line
 * (command + arguments).  Our commands take few args, so this is
 * plenty. */
#define ARGV_MAX 8

/* forward decls of built-ins so the table can reference them */
static void cmd_help(int argc, char **argv);
static void cmd_clear(int argc, char **argv);
static void cmd_echo(int argc, char **argv);
static void cmd_ps(int argc, char **argv);
static void cmd_mem(int argc, char **argv);
static void cmd_spawn(int argc, char **argv);
static void cmd_disk(int argc, char **argv);
static void cmd_reboot(int argc, char **argv);

/* A single command-table entry: a name and the function that runs it.
 * Every handler takes (argc, argv) — the same shape as a C main() —
 * so dispatch is uniform regardless of how many args a command reads. */
struct builtin {
	const char *name;
	void (*fn)(int argc, char **argv);
};

/* The table.  To add a command: write its handler, then add one row
 * here.  Nothing else in this file needs to change.  Order doesn't
 * matter for correctness; help() prints them in this order. */
static const struct builtin builtins[] = {
	{ "help",   cmd_help   },
	{ "clear",  cmd_clear  },
	{ "echo",   cmd_echo   },
	{ "ps",     cmd_ps     },
	{ "mem",    cmd_mem    },
	{ "spawn",  cmd_spawn   },
	{ "disk",   cmd_disk   },
	{ "reboot", cmd_reboot },
};

#define NUM_BUILTINS (sizeof(builtins) / sizeof(builtins[0]))

/* ---- string helpers ---------------------------------------------------
 *
 * We have no libc, so the usual string functions don't exist.  These
 * are the three we need.  They're small enough to define locally; if a
 * second module ever needs them, lift them into a shared string.h. */

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
 * have no strtol, so this is hand-rolled; used by the `disk` command to
 * take an LBA argument. */
static int parse_uint(const char *s, u32 *out)
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

/* ---- built-in commands ----------------------------------------------- */

/* help — list the known commands by walking the table.  Notice this
 * needs no per-command knowledge: it just prints every entry, so newly
 * added commands appear automatically. */
static void cmd_help(int argc, char **argv)
{
	unsigned int i;
	(void)argc; (void)argv;

	printf("commands:\n");
	for (i = 0; i < NUM_BUILTINS; i++)
		printf("  %s\n", builtins[i].name);
}

/* clear — wipe the screen and home the cursor.  Implemented in the
 * console layer (printf.c) because it owns the shared cursor. */
static void cmd_clear(int argc, char **argv)
{
	(void)argc; (void)argv;
	console_clear();
}

/* echo — print the arguments separated by single spaces, then a
 * newline.  If no args, just the newline (like POSIX echo). */
static void cmd_echo(int argc, char **argv)
{
	int i;
	for (i = 1; i < argc; i++) {
		printf("%s", argv[i]);
		if (i < argc - 1)
			printf(" ");
	}
	printf("\n");
}

/* ps — print the process table.  Delegates to the scheduler, which
 * owns the PCB array; the shell just triggers a dump. */
static void cmd_ps(int argc, char **argv)
{
	(void)argc; (void)argv;
	sched_dump_ps();
}

/* mem — print the buddy allocator's free lists and the total free
 * page count.  Same delegation pattern as ps: the allocator owns its
 * internals, the shell just asks for a snapshot. */
static void cmd_mem(int argc, char **argv)
{
	(void)argc; (void)argv;
	mem_dump();
}

/* spawn — demonstrate the process lifecycle end-to-end.
 *
 * The shell (the parent) creates a child process, then blocks in
 * wait() until the child exits and is reaped.  While they run
 * concurrently you can see both in `ps` (run it from a second spawn
 * if you like — the table holds up to MAX_PROCS).  This is the
 * create → run → exit → wait → reap path that fork/exec will build
 * on, minus the address-space copy (we create, not fork).
 *
 * We deliberately pace the child with sleep() so it survives long
 * enough to be observable: a process that just printf()s and exits
 * can finish inside a single timer slice, which makes the concurrency
 * invisible. */
static void spawn_child(void)
{
	int i;

	printf("  [child] hello from a spawned process\n");
	for (i = 0; i < 3; i++) {
		printf("  [child] working... %d\n", i);
		sleep(2);
	}
	printf("  [child] done, exiting\n");
	sched_exit(7);
}

static void cmd_spawn(int argc, char **argv)
{
	int pid;
	int waited_pid;
	int code;

	(void)argc; (void)argv;

	pid = create_process(spawn_child, "child");
	if (pid < 0) {
		printf("spawn: failed (process table full?)\n");
		return;
	}

	printf("spawn: created child pid %d, waiting for it...\n", pid);
	waited_pid = wait(NULL, &code);
	if (waited_pid < 0) {
		/* Shouldn't happen: we just made a child.  But if the
		 * child raced ahead and we somehow lost it, say so
		 * instead of hanging. */
		printf("spawn: wait() returned %d (no child?)\n", waited_pid);
		return;
	}
	printf("spawn: child %d reaped, exit code %d\n", waited_pid, code);
}

/* disk — read and dump one disk sector as hex + ASCII.
 *
 * Usage:  disk <lba>
 * Reads sector `lba` (512 bytes) via the ATA driver into a freshly
 * allocated page, then prints it as 16-byte rows: hex bytes on the left,
 * printable ASCII on the right (dots for non-printable).  This is the
 * classical `hexdump -C` layout, trimmed to fit 80 columns.
 *
 * The default (no arg) reads LBA 0 — the boot sector — which makes a
 * good smoke test: its last two bytes are the 0xAA55 boot signature,
 * so a correct read shows "55 aa" at offset 510. */
static void cmd_disk(int argc, char **argv)
{
	u32 lba = 0;
	u8 *buf;
	int row, col;

	if (argc >= 2) {
		if (!parse_uint(argv[1], &lba)) {
			printf("disk: '%s' is not a number\n", argv[1]);
			return;
		}
	}

	buf = (u8 *)alloc_page();
	if (!buf) {
		printf("disk: out of memory\n");
		return;
	}

	if (ata_read_sectors(lba, 1, buf) < 0) {
		printf("disk: read failed at LBA %d\n", lba);
		free_page(buf);
		return;
	}

	printf("sector %d (0x%x):\n", lba, lba);
	/* Dump 32 rows of 16 bytes = the whole 512-byte sector. */
	for (row = 0; row < ATA_SECTOR_SIZE / 16; row++) {
		u8 *r = buf + row * 16;

		printf("%03x: ", row * 16);
		for (col = 0; col < 16; col++)
			printf("%02x ", r[col]);
		/* ASCII gutter: print printable chars, '.' otherwise. */
		printf(" ");
		for (col = 0; col < 16; col++) {
			u8 c = r[col];
			putchar_one(c >= 32 && c < 127 ? c : '.');
		}
		printf("\n");
	}

	free_page(buf);
}

/* reboot — reset the machine via the 8042 keyboard controller.
 *
 * Port 0x64 is the 8042's command/status port.  Writing 0xFE tells
 * the 8042 to pulse its reset line, which forces the CPU into a
 * hardware reset — the same path a real PC uses on power-on reset.
 * QEMU honours this and restarts the machine, so we land back at
 * 0x7C00 with a clean boot.
 *
 * This is a "bare-metal reboot": there is no filesystem to sync and
 * no init to shut down (unlike Linux's reboot(2) syscall, which is
 * the tail end of a graceful shutdown protocol).  We just reset. */
static void cmd_reboot(int argc, char **argv)
{
	(void)argc; (void)argv;

	/* Use iodelay-safe writes: wait for the input buffer to clear
	 * before sending each byte.  On real hardware dropping the
	 * wait can lose the command; QEMU is lenient but we keep the
	 * discipline for correctness. */
	while ((inb(0x64) & 0x02) != 0)   /* input buffer full? */
		;
	outb(0x64, 0xFE);                /* pulse reset line */

	/* If the reset didn't fire (broken 8042), halt.  In QEMU this
	 * line is never reached. */
	for (;;)
		asm volatile("hlt");
}

/* ---- dispatch + main loop ------------------------------------------- */

/* Try to run one command line.  argv[0] is the command name.  If it
 * doesn't match any table entry, complain (don't crash). */
static void dispatch(int argc, char **argv)
{
	unsigned int i;

	if (argc == 0)
		return;			/* empty line, nothing to do */

	for (i = 0; i < NUM_BUILTINS; i++) {
		if (str_equal(argv[0], builtins[i].name)) {
			builtins[i].fn(argc, argv);
			return;
		}
	}

	/* No match.  argv[0] may be unterminated-safe because tokenize
	 * NUL-terminated every token, so %s is fine here. */
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
