#ifndef PUTCHAR_H
#define PUTCHAR_H

#include "types.h"
#include "cursor_coordinates.h"
#include "vga.h"

void putchar(u16* vga, cursor_coordinates* coord, unsigned char c);
void scroll_screen(u16* vga);

#endif