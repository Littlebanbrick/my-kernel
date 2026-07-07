// kbd.h — PS/2 keyboard driver (32-bit)

#ifndef KBD_H
#define KBD_H

#include "types.h"

/* IRQ 1 top half — read a scancode from the PS/2 data port, translate
 * it to ASCII, and push the character into the kernel ring buffer.
 * Called from handle_exception() on vector 33; runs with IF=0. */
void kbd_isr(void);

/* Pull one character from the buffer.  Returns the character as an
 * unsigned char (0..255), or -1 if the buffer is empty (non-blocking). */
int kbd_getchar_nonblocking(void);

/* Number of characters currently held in the buffer (debug aid). */
int kbd_buffer_count(void);

/* Block until a key is available, then return it as an unsigned char
 * (0..255).  This is the blocking consumer of the keyboard buffer.
 *
 * If the buffer is empty, the caller registers itself as the keyboard
 * waiter and sleeps until kbd_isr() pushes a byte and calls wake().
 * The check-and-block is wrapped in cli/sti so a keystroke cannot
 * arrive in the gap between "see empty buffer" and "go to sleep" —
 * that gap is the classic lost-wakeup race (the wait_event pattern). */
int getchar(void);

/* Wake the keyboard waiter, if any.  Called by kbd_isr() after it has
 * pushed a byte, so a getchar() that is sleeping gets resumed. */
void kbd_wake_waiter(void);

#endif
