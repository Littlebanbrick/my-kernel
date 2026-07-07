// readline.c — line-buffered input with echo and backspace editing
//
// Layered on top of getchar(): getchar() returns one raw character at a
// time (blocking until a key is available); readline() accumulates them
// into a NUL-terminated buffer and lets the user edit the line with
// Backspace, committing it on Enter.
//
// This is the application-layer companion to the kernel getchar().  It
// owns no kernel state — the only "screen" it touches is the shared VGA
// cursor via putchar(), same as printf().

#include "readline.h"
#include "kbd.h"          /* getchar() */
#include "printf.h"       /* putchar_one() */

/* The two control characters readline cares about.  getchar() hands
 * these to us straight from the scancode table (kbd.c). */
#define CHR_ENTER     '\n'
#define CHR_BACKSPACE '\b'

int readline(char *buf, int maxlen)
{
	int len = 0;

	for (;;) {
		char c = getchar();

		/* Enter — commit the line and return. */
		if (c == CHR_ENTER) {
			putchar_one('\n');          /* echo the newline */
			break;
		}

		/* Backspace — erase the last character if any. */
		if (c == CHR_BACKSPACE) {
			if (len > 0) {
				len--;
				/* Erase visually: step the cursor back, blank the
				 * cell, step back again so the next char overwrites
				 * this position instead of the one before it. */
				putchar_one('\b');
				putchar_one(' ');
				putchar_one('\b');
			}
			continue;
		}

		/* Any other character — store it (if room) and echo it. */
		if (len < maxlen - 1) {
			buf[len++] = c;
			putchar_one(c);
		}
		/* else: buffer full — silently drop the extra character,
		 * but still echo it so the user sees what they typed.  The
		 * line just won't all fit in the buffer. */
	}

	buf[len] = '\0';
	return len;
}
