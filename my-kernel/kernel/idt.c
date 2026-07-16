// idt.c — IDT initialisation and default exception handler (32-bit)

#include "types.h"
#include "vga.h"
#include "putchar.h"
#include "printf.h"
#include "idt.h"
#include "pic.h"
#include "utils.h"
#include "kbd.h"
#include "syscall.h"	/* syscall_handler — dedicated int 0x80 entry */

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

/* Dedicated IRQ 0 handler (defined in idt_handlers.S) — drives the
 * scheduler directly instead of going through handle_exception(). */
extern void irq0_handler(void);

/* Dedicated int 0x80 syscall entry (idt_handlers.S) — saves ring-3
 * state (5-word privilege-change frame) and calls syscall_enter().
 * Kept separate from the generic trampoline so it can switch stacks
 * on return. */
extern void syscall_handler(void);

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

/* page_fault — dedicated handler for vector 14.
 *
 * A page fault means the CPU could not translate a virtual address
 * (or a translation existed but lacked the permission for the access
 * that wanted it).  Two pieces of information tell us exactly what
 * happened:
 *
 *   - CR2 holds the faulting LINEAR address (the one that could not be
 *     translated / was illegal to access).  The CPU loads it before
 *     entering the handler; we read it here with `mov %cr2`.
 *
 *   - The error code (pushed by the CPU) packs the cause:
 *       bit 0 (P)   0 = non-present page, 1 = protection violation
 *       bit 1 (W/R) 0 = read access, 1 = write access
 *       bit 2 (U/S) 0 = supervisor (ring 0), 1 = user (ring 3)
 *       bit 3 (RSVD) 1 = a reserved bit in a page-table entry was set
 *       bit 4 (I/D)  1 = instruction fetch (vs. data access)
 *
 * We decode these and print a diagnostic, then halt.  A real kernel
 * would either map in the missing page (demand paging), copy-on-write
 * the faulted page, or deliver SIGSEGV to the offending process.
 * Halting keeps the toy kernel honest: a fault is a bug, not a feature
 * we recover from yet — the value here is *seeing* the fault instead
 * of a bare "Page Fault" line that hides the offending address.
 *
 * `cr2` is read from inline asm rather than a C variable because the
 * CPU sets it on the fault and we must read it before anything else
 * could clobber it (a nested fault would; we are careful not to). */
static void __attribute__((noreturn)) page_fault(u32 error_code,
						struct interrupt_frame *frame)
{
	u32 cr2;

	__asm__ volatile("mov %%cr2, %0" : "=r"(cr2));

	printf("!!! PAGE FAULT\n");
	printf("fault addr (CR2): %08x\n", cr2);
	printf("EIP:              %08x\n", frame->eip);

	/* Decode the access type from the low bits of the error code. */
	printf("cause: %s, %s, %s",
	       (error_code & 0x1) ? "protection-violation"
				  : "not-present",
	       (error_code & 0x2) ? "write" : "read",
	       (error_code & 0x4) ? "user" : "supervisor");
	if (error_code & 0x8)
		printf(", reserved-bit-set");
	if (error_code & 0x10)
		printf(", instruction-fetch");
	printf("\n");

	printf("error code:       %x\n", error_code);
	printf("System halted.\n");

	asm volatile("cli");
	for (;;)
		asm volatile("hlt");
}

void handle_exception(u32 vec, u32 error_code,
		      struct interrupt_frame *frame)
{
	/* Hardware IRQ (other than IRQ 0, which has its own dedicated
	 * irq0_handler) — acknowledge and return. */
	if (vec >= IRQ_BASE && vec < IRQ_BASE + 16) {
		if (vec == IRQ1_VECTOR)
			kbd_isr();
		pic_send_eoi(vec - IRQ_BASE);
		return;
	}

	/* Page fault gets a dedicated diagnostic before the generic
	 * path would bury the relevant detail (CR2, the decoded cause). */
	if (vec == 14) {
		page_fault(error_code, frame);
		return;          /* unreachable — page_fault does not return */
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
		printf("Err:  %x\n", error_code);

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

	/* Allow ring 3 code to invoke int 0x80 (system call).  Dedicated
	 * syscall_handler saves the 5-word privilege-change frame and
	 * dispatches to syscall_enter(). */
	idt_set_entry(&idt[0x80], syscall_handler, IDT_USER_INT);

	/* Dedicated IRQ 0 handler for the scheduler — bypasses the
	 * generic trampoline so it can switch stacks on return. */
	idt_set_entry(&idt[IRQ0_VECTOR], irq0_handler, IDT_KERN_INT);

	idtp.limit = sizeof(idt) - 1;
	idtp.base  = (u32)&idt;

	asm volatile("lidt %0" : : "m"(idtp));
}
