// putchar.c — VGA text-mode output with cursor tracking and scrolling

#include "types.h"
#include "cursor_coordinates.h"
#include "vga.h"

void scroll_screen(u16* vga)
{
	u16* dst = vga;
	u16* src = vga + MAX_WIDTH;
	int i;

	/* Shift rows 1..24 up to 0..23 */
	for (i = 0; i < (MAX_HEIGHT - 1) * MAX_WIDTH; i++)
		*dst++ = *src++;

	/* Clear last row */
	for (i = 0; i < MAX_WIDTH; i++)
		*dst++ = 0x0700 | ' ';
}

void putchar(u16* vga, cursor_coordinates* coord, unsigned char c)
{
	u16 index = coord->y * MAX_WIDTH + coord->x;

	vga[index] = ((u16)0x0700) | c;

	coord->x++;
	if (coord->x >= MAX_WIDTH) {
		coord->x = 0;
		coord->y++;
		if (coord->y >= MAX_HEIGHT) {
			scroll_screen(vga);
			coord->y = MAX_HEIGHT - 1;
		}
	}
}
