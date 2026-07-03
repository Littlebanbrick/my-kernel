// idt.c — IDT initialisation and default exception handler (32-bit)

#include "types.h"
#include "vga.h"
#include "putchar.h"
#include "printf.h"
#include "idt.h"
#include "pic.h"
#include "utils.h"

/* Tick counter — incremented each time PIT IRQ 0 fires */
extern volatile u32 g_ticks;

/* PS/2 keyboard data port */
#define KBD_DATA_PORT   0x60

/* Display a scancode byte at a fixed VGA position (row 9, col 0).
 * Also translate to ASCII and write the character on a new line.
 * Safe to call from interrupt context — no shared state. */
static void kbd_display_scancode(u8 scancode)
{
	static const char hex[] = "0123456789abcdef";
	u16 *const vga = (u16 *)0xB8000;
	int base = 9 * MAX_WIDTH;

	vga[base]     = 0x0700 | 'K';
	vga[base + 1] = 0x0700 | 'e';
	vga[base + 2] = 0x0700 | 'y';
	vga[base + 3] = 0x0700 | ':';
	vga[base + 4] = 0x0700 | ' ';
	vga[base + 5] = 0x0700 | '0';
	vga[base + 6] = 0x0700 | 'x';
	vga[base + 7] = 0x0700 | hex[(scancode >> 4) & 0xF];
	vga[base + 8] = 0x0700 | hex[scancode & 0xF];
	vga[base + 9] = 0x0700 | ' ';

	/*
	 * ----  Scancode → ASCII translation (US keyboard layout)  ----
	 *
	 * Index  = make code (0x00-0x7F).
	 * Value  = corresponding ASCII character, or '\0' for non-ASCII.
	 * Break  codes (≥ 0x80) are converted via   idx = scancode & 0x7F.
	 */
	static const char s2a[128] = {
		[0x01] = 0,             /* Esc                     */
		[0x02] = '1', [0x03] = '2', [0x04] = '3',
		[0x05] = '4', [0x06] = '5', [0x07] = '6',
		[0x08] = '7', [0x09] = '8', [0x0A] = '9',
		[0x0B] = '0',
		[0x0C] = '-', [0x0D] = '=',
		[0x0E] = 0,             /* Backspace — non-printable   */
		[0x0F] = 0,             /* Tab — non-printable        */
		[0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r',
		[0x14] = 't', [0x15] = 'y', [0x16] = 'u', [0x17] = 'i',
		[0x18] = 'o', [0x19] = 'p',
		[0x1A] = '[', [0x1B] = ']',
		[0x1C] = 0,             /* Enter — non-printable      */
		[0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f',
		[0x22] = 'g', [0x23] = 'h', [0x24] = 'j', [0x25] = 'k',
		[0x26] = 'l',
		[0x27] = ';', [0x28] = '\'', [0x29] = '`',
		[0x2A] = 0,             /* Left Shift                 */
		[0x2B] = '\\',
		[0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v',
		[0x30] = 'b', [0x31] = 'n', [0x32] = 'm',
		[0x33] = ',', [0x34] = '.', [0x35] = '/',
		[0x36] = 0,             /* Right Shift                */
		[0x39] = ' ',           /* Space                      */
	};

	u8 idx = scancode & 0x7F;
	char ch = (idx < 128) ? s2a[idx] : 0;

	/* Output line — starts at row 11, wraps to 11 after row 24 */
	static int out_line = 11;
	int pos = out_line * MAX_WIDTH;

	if (ch) {
		vga[pos]     = 0x0700 | ch;
		vga[pos + 1] = 0x0700 | ' ';
	} else {
		/* Non-ASCII key or unlisted scancode */
		vga[pos]     = 0x0700 | 'o';
		vga[pos + 1] = 0x0700 | 't';
		vga[pos + 2] = 0x0700 | 'h';
		vga[pos + 3] = 0x0700 | 'e';
		vga[pos + 4] = 0x0700 | 'r';
		vga[pos + 5] = 0x0700 | 's';
	}

	out_line++;
	if (out_line > 24)
		out_line = 11;
}

// ----------------------------------------------------------------
//  IDT entry helpers
// ----------------------------------------------------------------

// Pack a 32-bit handler address into the two 16-bit offset fields.
static void idt_set_entry(struct idt_entry *entry,
			  void *handler, u8 flags)
{
	u32 addr = (u32)handler;

	entry->offset_low  = addr & 0xFFFF;
	entry->selector    = 0x08;	// kernel code segment
	entry->zero        = 0;
	entry->flags       = flags;
	entry->offset_high = (addr >> 16) & 0xFFFF;
}

// ----------------------------------------------------------------
//  Common C handler
// ----------------------------------------------------------------

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

static int has_error_code(u32 vec)
{
	return vec == 8  || vec == 10 || vec == 11 ||
	       vec == 12 || vec == 13 || vec == 14 || vec == 17;
}

void handle_exception(u32 vec, u32 error_code,
		      struct interrupt_frame *frame)
{
	/* Hardware IRQ — acknowledge and return */
	if (vec >= IRQ_BASE && vec < IRQ_BASE + 16) {
		if (vec == IRQ0_VECTOR)
			g_ticks++;
		else if (vec == IRQ1_VECTOR)
			kbd_display_scancode(inb(KBD_DATA_PORT));
		pic_send_eoi(vec - IRQ_BASE);
		return;
	}

	const char *name;

	if (vec < 32 && exception_names[vec])
		name = exception_names[vec];
	else
		name = "Reserved / Unknown";

	printf("!!! CPU EXCEPTION\n");
	printf("Vector: %02x (%s)\n", vec, name);
	printf("EIP: %08x\n", frame->eip);

	if (has_error_code(vec))
		printf("Err:  0x%x\n", error_code);

	printf("System halted.\n");

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

	for (i = 0; i < 256; i++)
		idt_set_entry(&idt[i], handler_addrs[i], IDT_KERN_INT);

	idtp.limit = sizeof(idt) - 1;
	idtp.base  = (u32)&idt;

	asm volatile("lidt %0" : : "m"(idtp));
}
