// idt.c — IDT initialisation and default exception handler
//
// The assembly trampolines in idt_handlers.S each hard-code a vector
// number, jump through handler_common (which saves all registers),
// and then call handle_exception().  This file sets up the IDT table
// and provides that C function.

#include "types.h"
#include "vga.h"
#include "putchar.h"
#include "idt.h"

// ----------------------------------------------------------------
//  IDT entry helpers
// ----------------------------------------------------------------

// Pack a 64-bit handler address into the three offset fields.
static void idt_set_entry(struct idt_entry *entry,
			  void *handler, u8 flags)
{
	u64 addr = (u64)handler;

	entry->offset_low  = addr & 0xFFFF;
	entry->selector	   = 0x08;	// kernel code segment
	entry->ist	   = 0;		// use current stack
	entry->flags	   = flags;
	entry->offset_mid  = (addr >> 16) & 0xFFFF;
	entry->offset_high = (addr >> 32) & 0xFFFFFFFF;
	entry->reserved	   = 0;
}

// ----------------------------------------------------------------
//  Handler-address table exported from idt_handlers.S
// ----------------------------------------------------------------

extern void *handler_addrs[256];

// ----------------------------------------------------------------
//  Common C handler
// ----------------------------------------------------------------

/*
 * exception_names — human-readable names for CPU exception vectors.
 * Vectors outside this list (32-255, or gaps) show "Reserved".
 */
static const char *exception_names[32] = {
	[0]  = "Divide Error",
	[1]  = "Debug",
	[2]  = "Non-Maskable Interrupt",
	[3]  = "Breakpoint",
	[4]  = "Overflow",
	[5]  = "Bound Range Exceeded",
	[6]  = "Invalid Opcode",
	[7]  = "Device Not Available",
	[8]  = "Double Fault",
	[9]  = "Coprocessor Segment Overrun",
	[10] = "Invalid TSS",
	[11] = "Segment Not Present",
	[12] = "Stack-Segment Fault",
	[13] = "General Protection Fault",
	[14] = "Page Fault",
	[15] = "Reserved",
	[16] = "x87 Floating-Point Exception",
	[17] = "Alignment Check",
	[18] = "Machine Check",
	[19] = "SIMD Floating-Point Exception",
	[20] = "Virtualisation Exception",
	[21] = "Control Protection Exception",
};

static int has_error_code(u64 vec)
{
	return vec == 8  || vec == 10 || vec == 11 ||
	       vec == 12 || vec == 13 || vec == 14 || vec == 17;
}

void handle_exception(u64 vec, u64 error_code,
		      struct interrupt_frame *frame)
{
	u16 *vga = (u16 *)0xB8000;
	int line = 0;

	// ---- Helper: print a string at a fixed VGA row ----
#define SAY(row, str) do {					\
	cursor_coordinates c = { .x = 0, .y = row };		\
	const char *p_ = (str);					\
	while (*p_)						\
		putchar(vga, &c, *p_++);			\
} while (0)

	SAY(line++, "!!! CPU EXCEPTION");
	SAY(line++, "");

	// Vector number and name
	{
		char buf[48];
		int i = 0;
		const char *name;

		if (vec < 32 && exception_names[vec])
			name = exception_names[vec];
		else
			name = "Reserved / Unknown";

		// "Vector: 0xXX  (Name)"
		buf[i++] = 'V';
		buf[i++] = 'e';
		buf[i++] = 'c';
		buf[i++] = 't';
		buf[i++] = 'o';
		buf[i++] = 'r';
		buf[i++] = ':';
		buf[i++] = ' ';
		// "0x"
		buf[i++] = '0';
		buf[i++] = 'x';
		// vec in hex (up to two nibbles for 0-255)
		u8 v = (u8)vec;
		u8 hi = v >> 4;
		u8 lo = v & 0xF;
		buf[i++] = hi < 10 ? '0' + hi : 'a' + hi - 10;
		buf[i++] = lo < 10 ? '0' + lo : 'a' + lo - 10;
		buf[i++] = ' ';
		buf[i++] = '(';
		// name
		while (*name && i < 44)
			buf[i++] = *name++;
		buf[i++] = ')';
		buf[i] = 0;

		SAY(line++, buf);
	}

	// RIP (where the fault happened)
	{
		char buf[32];
		int i = 0;

		buf[i++] = 'R';
		buf[i++] = 'I';
		buf[i++] = 'P';
		buf[i++] = ':';
		buf[i++] = ' ';
		buf[i++] = '0';
		buf[i++] = 'x';

		u64 rip = frame->rip;
		int started = 0;
		int j;
		for (j = 15; j >= 0; j--) {
			int nibble = (rip >> (j * 4)) & 0xF;
			if (nibble || started || j == 0) {
				started = 1;
				buf[i++] = nibble < 10
					? '0' + nibble
					: 'a' + nibble - 10;
			}
		}
		buf[i] = 0;
		SAY(line++, buf);
	}

	// Error code (if meaningful)
	if (has_error_code(vec)) {
		char buf[32];
		int i = 0;

		buf[i++] = 'E';
		buf[i++] = 'r';
		buf[i++] = 'r';
		buf[i++] = ' ';
		buf[i++] = ':';
		buf[i++] = ' ';
		buf[i++] = '0';
		buf[i++] = 'x';

		u64 ec = error_code;
		int started = 0;
		int j;
		for (j = 15; j >= 0; j--) {
			int nibble = (ec >> (j * 4)) & 0xF;
			if (nibble || started || j == 0) {
				started = 1;
				buf[i++] = nibble < 10
					? '0' + nibble
					: 'a' + nibble - 10;
			}
		}
		buf[i] = 0;
		SAY(line++, buf);
	}

	SAY(line++, "");
	SAY(line++, "System halted.");

	(void)frame;		// we use it above; silence -Wextra

#undef SAY

	// Halt forever
	while (1)
		asm volatile("hlt");
}

// ----------------------------------------------------------------
//  IDT initialisation
// ----------------------------------------------------------------

static struct idt_entry idt[256] __attribute__((aligned(16)));

void idt_init(void)
{
	struct idt_ptr idtp;
	int i;

	// ---- Fill all 256 entries ----
	for (i = 0; i < 256; i++)
		idt_set_entry(&idt[i], handler_addrs[i], IDT_KERN_INT);

	// ---- Load the IDT ----
	idtp.limit = sizeof(idt) - 1;
	idtp.base  = (u64)&idt;

	asm volatile("lidt %0" : : "m"(idtp));
}
