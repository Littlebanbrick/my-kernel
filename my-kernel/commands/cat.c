// cat.c — `cat <name>` command: print a file's contents
//
// Usage:  cat <name>
//
// Looks up <name> in the FS directory, reads its data sectors into a
// freshly allocated page, and writes the bytes to the console.  This is
// the user-facing front end for fs_read(); the FS owns the read path.
//
// We print exactly dirent->size bytes — fs_create zero-pads a file's
// sector to 512, but the directory records the true byte length, so cat
// does not echo the trailing padding.  (A created file's size excludes
// the readline newline, so cat prints the line back without one.)

#include "commands.h"
#include "fs.h"
#include "memory.h"        /* alloc_page, free_page */
#include "printf.h"         /* printf, putchar_one */

void cmd_cat(int argc, char **argv)
{
	const struct fs_dirent *d;
	u8 *buf;
	unsigned int i;

	if (argc < 2) {
		printf("usage: cat <name>\n");
		return;
	}

	d = fs_lookup(argv[1]);
	if (!d) {
		printf("cat: no such file '%s'\n", argv[1]);
		return;
	}

	/* A page is big enough for any one-sector file (the only kind we can
	 * create at runtime); built-in programs can be larger, but cat is
	 * meant for text data files, not program images. */
	buf = (u8 *)alloc_page();
	if (!buf) {
		printf("cat: out of memory\n");
		return;
	}

	if (fs_read(d, buf) < 0) {
		printf("cat: read failed for '%s'\n", argv[1]);
		free_page(buf);
		return;
	}

	for (i = 0; i < d->size; i++)
		putchar_one(buf[i]);
	/* Files created by `write` have no trailing newline; add one so the
	 * shell prompt lands on its own line.  Program images (raw code)
	 * would print garbage here, but cat isn't meant for those. */
	putchar_one('\n');

	free_page(buf);
}
