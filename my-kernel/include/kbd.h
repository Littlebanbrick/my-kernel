// kbd.h — PS/2 keyboard driver (32-bit)

#ifndef KBD_H
#define KBD_H

#include "types.h"

/* IRQ 1 top half — read a scancode from the PS/2 data port, translate
 * it to ASCII, and push the character into the kernel ring buffer.
 * Called from handle_exception() on vector 33; runs with IF=0. */
void kbd_isr(void);

/* Pull one character from the buffer.  Returns the character as an
 * unsigned char (0..255), or -1 if the buffer is empty (non-blocking).
 * The blocking getchar() to be added next will wrap this: if it
 * returns -1, sleep until the keyboard ISR wakes us. */
int kbd_getchar_nonblocking(void);

/* Number of characters currently held in the buffer (debug aid). */
int kbd_buffer_count(void);

#endif
