// printf.c — Minimal variadic printf for VGA text-mode output

#include <stdarg.h>
#include "types.h"
#include "vga.h"
#include "putchar.h"

/* Divide val by 10 and produce digits from least- to most-significant */
static void print_dec(u16* vga, cursor_coordinates* coord, int val)
{
	char buf[12];	/* enough for -2147483648 */
	int i = 0;
	int negative = 0;

	if (val < 0) {
		negative = 1;
		val = -val;
	}

	do {
		buf[i++] = '0' + (val % 10);
		val /= 10;
	} while (val > 0);

	if (negative)
		buf[i++] = '-';

	while (i > 0)
		putchar(vga, coord, buf[--i]);
}

/* Print hex with lowercase a-f, prefixed by "0x" */
static void print_hex(u16* vga, cursor_coordinates* coord, unsigned int val)
{
	const char hex_chars[] = "0123456789abcdef";
	char buf[8];
	int i = 0;

	do {
		buf[i++] = hex_chars[val & 0xF];
		val >>= 4;
	} while (val > 0);

	putchar(vga, coord, '0');
	putchar(vga, coord, 'x');

	while (i > 0)
		putchar(vga, coord, buf[--i]);
}

void printf(const char* fmt, ...)
{
	u16* const vga = (u16*)0xB8000;
	static cursor_coordinates cursor = {0, 0};
	va_list ap;

	va_start(ap, fmt);

	for (const char* p = fmt; *p; p++) {
		/* Handle newline: move cursor to next line */
		if (*p == '\n') {
			cursor.x = 0;
			cursor.y++;
			if (cursor.y >= MAX_HEIGHT) {
				scroll_screen(vga);
				cursor.y = MAX_HEIGHT - 1;
			}
			continue;
		}

		/* Ordinary character — print directly */
		if (*p != '%') {
			putchar(vga, &cursor, (unsigned char)*p);
			continue;
		}

		/* Format specifier */
		p++;
		switch (*p) {
		case 'c':
			/* char is promoted to int in varargs */
			putchar(vga, &cursor,
				(unsigned char)va_arg(ap, int));
			break;
		case 's': {
			const char* s = va_arg(ap, const char*);
			while (*s)
				putchar(vga, &cursor,
					(unsigned char)*s++);
			break;
		}
		case 'd':
		case 'i':
			print_dec(vga, &cursor, va_arg(ap, int));
			break;
		case 'x':
			print_hex(vga, &cursor,
				  va_arg(ap, unsigned int));
			break;
		case '%':
			putchar(vga, &cursor, '%');
			break;
		default:
			/* Unknown specifier — print it literally */
			putchar(vga, &cursor, '%');
			putchar(vga, &cursor, (unsigned char)*p);
			break;
		}
	}

	va_end(ap);
}
