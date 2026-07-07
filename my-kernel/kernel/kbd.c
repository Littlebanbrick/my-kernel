// kbd.c — PS/2 keyboard driver with a ring buffer (32-bit)
//
// Split out of idt.c: idt.c is the generic interrupt dispatcher, the
// keyboard is a device and deserves its own translation unit.
//
// Two halves:
//
//   Producer — kbd_isr(), invoked from handle_exception() on IRQ 1.
//              Reads a scancode from port 0x60, translates it to ASCII
//              via the scancode tables, and pushes the byte into the
//              ring buffer.  Runs with IF=0 (interrupt context).
//
//   Consumer — kbd_getchar_nonblocking(), called by whoever wants a
//              keystroke.  Pops one byte from the buffer, or returns -1.
//              The blocking getchar() will wrap this: on -1, mark the
//              caller as the keyboard waiter, sleep, and on wake pop the
//              character the ISR just pushed.
//
// The ring buffer decouples producer and consumer: a fast burst of keys
// (typist) won't lose characters to a slow consumer, and a consumer that
// isn't ready yet won't block the ISR.  It also makes the upcoming
// event-wake getchar trivial — wake the waiter, *then* let it pop.

#include "kbd.h"
#include "types.h"
#include "utils.h"
#include "pic.h"
#include "vga.h"

/* PS/2 keyboard data port — scancode byte is read here on every IRQ 1. */
#define KBD_DATA_PORT   0x60

/* ------------------------------------------------------------------ */
/*  Ring buffer                                                        */
/* ------------------------------------------------------------------ */

/* A circular queue of bytes.  head = next slot to read (consumer side),
 * tail = next slot to write (producer side), count = bytes currently
 * stored (0 == empty, KBD_BUF_SIZE == full).
 *
 * Power-of-two size so `& (KBD_BUF_SIZE - 1)` wraps the indices without
 * a branch.  If the buffer is full on push, the new byte is dropped —
 * the typist is way slower than 128 bytes can be drained, and a stale
 * drop beats a corrupt overrun. */
#define KBD_BUF_SIZE    128     /* must be a power of two */

static u8   kbd_buf[KBD_BUF_SIZE];
static u32  kbd_head;          /* consumer reads here */
static u32  kbd_tail;          /* producer writes here */
static u32  kbd_count;         /* bytes currently in the buffer */

/* Push one byte into the buffer.  Drops silently if full. */
static void kbd_push(u8 c)
{
	if (kbd_count == KBD_BUF_SIZE)
		return;                     /* full — drop the new byte */
	kbd_buf[kbd_tail] = c;
	kbd_tail = (kbd_tail + 1) & (KBD_BUF_SIZE - 1);
	kbd_count++;
}

/* Pop one byte from the buffer.  Returns the byte, or -1 if empty. */
int kbd_getchar_nonblocking(void)
{
	u8 c;

	if (kbd_count == 0)
		return -1;
	c = kbd_buf[kbd_head];
	kbd_head = (kbd_head + 1) & (KBD_BUF_SIZE - 1);
	kbd_count--;
	return c;
}

int kbd_buffer_count(void)
{
	return (int)kbd_count;
}

/* ------------------------------------------------------------------ */
/*  Scancode -> ASCII translation (US layout, Set 1)                   */
/*                                                                    */
/*  Two tables indexed by make code (0x00-0x7F):                      */
/*    s2a       — unmodified  (lowercase, unshifted symbols)          */
/*    s2a_shift — shifted     (uppercase, shifted symbols)             */
/*  Break codes (>= 0x80) are masked down with & 0x7F; only make      */
/*  codes produce a character.  Shift is tracked across events.       */
/* ------------------------------------------------------------------ */

static const char s2a[128] = {
	[0x01] = 0,             /* Esc                     */
	[0x02] = '1', [0x03] = '2', [0x04] = '3',
	[0x05] = '4', [0x06] = '5', [0x07] = '6',
	[0x08] = '7', [0x09] = '8', [0x0A] = '9',
	[0x0B] = '0',
	[0x0C] = '-', [0x0D] = '=',
	[0x0E] = 0,             /* Backspace               */
	[0x0F] = 0,             /* Tab                     */
	[0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r',
	[0x14] = 't', [0x15] = 'y', [0x16] = 'u', [0x17] = 'i',
	[0x18] = 'o', [0x19] = 'p',
	[0x1A] = '[', [0x1B] = ']',
	[0x1C] = '\n',          /* Enter                   */
	[0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f',
	[0x22] = 'g', [0x23] = 'h', [0x24] = 'j', [0x25] = 'k',
	[0x26] = 'l',
	[0x27] = ';', [0x28] = '\'', [0x29] = '`',
	[0x2A] = 0,             /* Left Shift              */
	[0x2B] = '\\',
	[0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v',
	[0x30] = 'b', [0x31] = 'n', [0x32] = 'm',
	[0x33] = ',', [0x34] = '.', [0x35] = '/',
	[0x36] = 0,             /* Right Shift             */
	[0x39] = ' ',           /* Space                   */
};

static const char s2a_shift[128] = {
	[0x01] = 0,             /* Esc                     */
	[0x02] = '!', [0x03] = '@', [0x04] = '#',
	[0x05] = '$', [0x06] = '%', [0x07] = '^',
	[0x08] = '&', [0x09] = '*', [0x0A] = '(',
	[0x0B] = ')',
	[0x0C] = '_', [0x0D] = '+',
	[0x0E] = 0,             /* Backspace               */
	[0x0F] = 0,             /* Tab                     */
	[0x10] = 'Q', [0x11] = 'W', [0x12] = 'E', [0x13] = 'R',
	[0x14] = 'T', [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I',
	[0x18] = 'O', [0x19] = 'P',
	[0x1A] = '{', [0x1B] = '}',
	[0x1C] = '\n',          /* Enter                   */
	[0x1E] = 'A', [0x1F] = 'S', [0x20] = 'D', [0x21] = 'F',
	[0x22] = 'G', [0x23] = 'H', [0x24] = 'J', [0x25] = 'K',
	[0x26] = 'L',
	[0x27] = ':', [0x28] = '"', [0x29] = '~',
	[0x2A] = 0,             /* Left Shift              */
	[0x2B] = '|',
	[0x2C] = 'Z', [0x2D] = 'X', [0x2E] = 'C', [0x2F] = 'V',
	[0x30] = 'B', [0x31] = 'N', [0x32] = 'M',
	[0x33] = '<', [0x34] = '>', [0x35] = '?',
	[0x36] = 0,             /* Right Shift             */
	[0x39] = ' ',           /* Space                   */
};

/* ------------------------------------------------------------------ */
/*  kbd_isr — IRQ 1 top half                                           */
/*                                                                    */
/*  Reads the scancode, tracks Shift, translates to ASCII, and pushes */
/*  the byte into the ring buffer.  The old behaviour (writing the    */
/*  raw scancode to a fixed VGA cell) is gone — the buffer is now the */
/*  single sink for keystrokes, and a future getchar will drain it.   */
/*                                                                    */
/*  Runs in interrupt context with IF=0, so the shared `shift` state  */
/*  and the ring buffer are safe from concurrent access.              */
/* ------------------------------------------------------------------ */

void kbd_isr(void)
{
	u8 scancode = inb(KBD_DATA_PORT);
	u8 idx = scancode & 0x7F;
	static int shift = 0;

	/* Track Shift state — make (0x2A/0x36) sets, break clears. */
	if (idx == 0x2A || idx == 0x36) {
		shift = !(scancode & 0x80);
		return;
	}

	/* Break codes (>= 0x80) don't produce a character. */
	if (scancode & 0x80)
		return;

	/* Make code → ASCII via the right table. */
	const char *table = shift ? s2a_shift : s2a;
	char ch = table[idx];
	if (ch)
		kbd_push((u8)ch);
}
