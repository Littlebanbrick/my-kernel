// printf.c — Minimal variadic printf for VGA text-mode output
//
// printf parses a format string left-to-right.  Literal characters and
// '\n' are emitted directly; a '%' starts a format specifier, parsed as
//
//     % [flags] [width] conversion
//        ↓        ↓        ↓
//        - 0      digits    d i u x c s %
//
// Parsing and rendering are deliberately split:
//   - parse_spec() reads flags + width + conversion into a struct,
//     advancing the format pointer past them.
//   - the conversion then renders its argument into a small buffer and
//     hands the buffer (plus its length) to emit_padded(), which applies
//     the width/alignment uniformly.
//
// Every conversion shares the same padding path, so adding a new one
// means writing its buffer fill only — never re-implementing padding.
// Precision (".N") is intentionally not supported; the parser skips
// nothing for it, so a "%5.3s" would be read as width=5, conv='.', then
// fall through to the unknown-specifier branch.  Add it when needed.

#include <stdarg.h>
#include "types.h"
#include "vga.h"
#include "putchar.h"

/* The single shared text cursor for the whole screen.  printf() and
 * putchar_one() both write through it, so console output (including
 * readline's echo) stays in one place.  This is the very thing that
 * multi-process output would contend on — fine while only the shell
 * prints; the contention is a follow-up analysis. */
static cursor_coordinates console_cursor = {0, 0};

/* console_clear — wipe the whole screen and home the cursor.
 *
 * Used by the `clear` shell command.  Lives here because printf.c owns
 * the shared console_cursor; only it can reset it safely. */
void console_clear(void)
{
	u16 *const vga = (u16 *)0xB8000;
	int i;

	for (i = 0; i < MAX_WIDTH * MAX_HEIGHT; i++)
		vga[i] = 0x0700 | ' ';

	console_cursor.x = 0;
	console_cursor.y = 0;
}

/* putchar_one — emit one character to the shared console cursor.
 *
 * Wraps putchar() so callers (readline, etc.) don't have to know about
 * the VGA base or the cursor.  Handles the control characters readline
 * produces:
 *   '\b' — move the cursor back one cell without erasing (the caller
 *          erases by writing a space, then another '\b')
 *   else — write the glyph and advance (putchar handles '\n') */
void putchar_one(unsigned char c)
{
	u16 *const vga = (u16 *)0xB8000;

	if (c == '\b') {
		if (console_cursor.x > 0)
			console_cursor.x--;
		return;
	}
	putchar(vga, &console_cursor, c);
}

/* ------------------------------------------------------------------ */
/*  Parsed format specifier                                            */
/* ------------------------------------------------------------------ */

/* A specifier is parsed into this struct, then rendered.  Keeping the
 * two steps separate means every conversion shares one padding path. */
struct fmt_spec {
	int  left_align;   /* '-' flag: pad on the right  */
	int  zero_pad;     /* '0' flag: pad with '0'      */
	int  width;        /* minimum field width         */
	char conv;         /* conversion letter           */
};

/* Parse one '%' specifier starting just past the '%'.
 * Returns the spec and advances *pp to the byte after the conversion
 * letter (or after whatever byte stopped parsing). */
static struct fmt_spec parse_spec(const char **pp)
{
	const char *p = *pp;
	struct fmt_spec s = {0, 0, 0, 0};

	/* flags: '-' and '0' may appear in any order, once each */
	for (;;) {
		if (*p == '-') {
			s.left_align = 1;
			p++;
		} else if (*p == '0') {
			s.zero_pad = 1;
			p++;
		} else {
			break;
		}
	}

	/* width: decimal digits */
	while (*p >= '0' && *p <= '9') {
		s.width = s.width * 10 + (*p - '0');
		p++;
	}

	s.conv = *p;
	if (*p)
		p++;
	*pp = p;
	return s;
}

/* ------------------------------------------------------------------ */
/*  Rendering helpers                                                  */
/* ------------------------------------------------------------------ */

/* Emit `len` copies of `pad` to the console. */
static void emit_pad(u16 *vga, char pad, int len)
{
	while (len-- > 0)
		putchar(vga, &console_cursor, pad);
}

/* Render already-prepared content with the spec's width/alignment.
 *
 *   content  — the bytes to show (not NUL-terminated; length is `len`)
 *   len      — how many bytes of content to emit
 *
 * Padding fills width-len cells (none if len >= width):
 *   left_align:  content first, then pad on the right
 *   else:        pad on the left, then content
 *   pad char:    '0' if zero_pad AND not left_align, else ' '
 *
 * zero_pad + left_align is contradictory in C; left_align wins (space
 * pad on the right), matching glibc. */
static void emit_padded(u16 *vga, const struct fmt_spec *s,
			const char *content, int len)
{
	int pad = s->width - len;
	char padch;

	if (pad <= 0) {
		while (len-- > 0)
			putchar(vga, &console_cursor, (unsigned char)*content++);
		return;
	}

	padch = (s->zero_pad && !s->left_align) ? '0' : ' ';

	if (s->left_align) {
		while (len-- > 0)
			putchar(vga, &console_cursor, (unsigned char)*content++);
		emit_pad(vga, padch, pad);
	} else {
		emit_pad(vga, padch, pad);
		while (len-- > 0)
			putchar(vga, &console_cursor, (unsigned char)*content++);
	}
}

/* Format a signed integer into `buf` (NUL-terminated), return its
 * length (excluding the NUL).  Used for %d/%i. */
static int fmt_dec(char *buf, int val)
{
	char tmp[12];	/* enough for -2147483648 */
	int i = 0, n = 0;
	unsigned int u;
	int negative = 0;

	if (val < 0) {
		negative = 1;
		u = (unsigned int)(-val);   /* safe even for INT_MIN */
	} else {
		u = (unsigned int)val;
	}

	do {
		tmp[i++] = '0' + (u % 10);
		u /= 10;
	} while (u > 0);

	if (negative)
		buf[n++] = '-';

	while (i > 0)
		buf[n++] = tmp[--i];

	buf[n] = '\0';
	return n;
}

/* Format an unsigned integer as hex (lowercase, no "0x" prefix) into
 * `buf`, return its length.  Used for %x. */
static int fmt_hex(char *buf, unsigned int val)
{
	const char hex_chars[] = "0123456789abcdef";
	char tmp[8];
	int i = 0, n = 0;

	do {
		tmp[i++] = hex_chars[val & 0xF];
		val >>= 4;
	} while (val > 0);

	while (i > 0)
		buf[n++] = tmp[--i];

	buf[n] = '\0';
	return n;
}

/* ------------------------------------------------------------------ */
/*  printf                                                             */
/* ------------------------------------------------------------------ */

void printf(const char* fmt, ...)
{
	u16* const vga = (u16*)0xB8000;
	va_list ap;

	va_start(ap, fmt);

	for (const char* p = fmt; *p; p++) {
		/* Handle newline: move cursor to next line */
		if (*p == '\n') {
			console_cursor.x = 0;
			console_cursor.y++;
			if (console_cursor.y >= MAX_HEIGHT) {
				scroll_screen(vga);
				console_cursor.y = MAX_HEIGHT - 1;
			}
			continue;
		}

		/* Ordinary character — print directly */
		if (*p != '%') {
			putchar(vga, &console_cursor, (unsigned char)*p);
			continue;
		}

		/* Format specifier — parse, then dispatch on conversion */
		p++;
		struct fmt_spec s = parse_spec(&p);
		/* parse_spec advanced p to the byte after the conversion
		 * letter; the for-loop's p++ would skip one too many, so
		 * step back one here. */
		p--;

		switch (s.conv) {
		case 'c':
			/* char is promoted to int in varargs */
		{
			char ch = (char)va_arg(ap, int);
			emit_padded(vga, &s, &ch, 1);
			break;
		}
		case 's': {
			const char *str = va_arg(ap, const char*);
			int len = 0;
			if (!str)
				str = "(null)";
			while (str[len])
				len++;
			emit_padded(vga, &s, str, len);
			break;
		}
		case 'd':
		case 'i': {
			char buf[12];
			int len = fmt_dec(buf, va_arg(ap, int));
			emit_padded(vga, &s, buf, len);
			break;
		}
		case 'u': {
			char buf[12];
			int len = fmt_dec(buf, va_arg(ap, int));
			emit_padded(vga, &s, buf, len);
			break;
		}
		case 'x': {
			char buf[8];
			int len = fmt_hex(buf, va_arg(ap, unsigned int));
			emit_padded(vga, &s, buf, len);
			break;
		}
		case '%':
			/* width on a literal '%' is silly but harmless */
			putchar(vga, &console_cursor, '%');
			break;
		default:
			/* Unknown specifier — print the '%' and the letter
			 * literally so a typo is visible, not silent. */
			putchar(vga, &console_cursor, '%');
			if (s.conv)
				putchar(vga, &console_cursor,
					(unsigned char)s.conv);
			break;
		}
	}

	va_end(ap);
}
