// pic.c — 8259A PIC initialisation (32-bit protected mode)
//
// The 8259A Programmable Interrupt Controller is the legacy chip that
// multiplexes hardware IRQs (keyboard, timer, ...) into a single CPU
// interrupt signal.  On reset it maps IRQ 0-7 to IDT[0-7] and IRQ 8-15
// to IDT[8-15], which clashes with the CPU exception vectors.  We remap
// them to IDT[32-47] so exceptions and hardware interrupts don't overlap.
//
// Initialisation uses four Initialisation Command Words (ICWs):
//   ICW1 — begin initialisation (edge-triggered, cascade)
//   ICW2 — new vector base address
//   ICW3 — cascade wiring (master knows which IRQ the slave is on)
//   ICW4 — 80x86 mode, non-buffered, normal EOI

#include "utils.h"
#include "pic.h"

void pic_remap(void)
{
	/* ---- ICW1: begin initialisation ---- */
	outb(PIC1_CMD, 0x11);		/* ICW4-required, cascade mode, edge-triggered */
	outb(PIC2_CMD, 0x11);

	/* ---- ICW2: set vector offsets ---- */
	outb(PIC1_DATA, IRQ_BASE);		/* IRQ 0-7   → IDT[32-39]        */
	outb(PIC2_DATA, IRQ_BASE + 8);		/* IRQ 8-15  → IDT[40-47]        */

	/* ---- ICW3: tell master PIC that slave is on IRQ 2 ---- */
	outb(PIC1_DATA, 0x04);			/* bit 2 set = slave at IRQ 2    */
	outb(PIC2_DATA, 0x02);			/* slave cascade identity = 2    */

	/* ---- ICW4: 80x86 mode, non-buffered, normal EOI ---- */
	outb(PIC1_DATA, 0x01);
	outb(PIC2_DATA, 0x01);

	/* ---- Mask all IRQs ---- */
	/* We unmask individual IRQs as we set up their handlers. */
	outb(PIC1_DATA, 0xFF);
	outb(PIC2_DATA, 0xFF);
}

void pic_send_eoi(unsigned char irq)
{
	/* If this IRQ came through the slave PIC, send EOI there first */
	if (irq >= 8)
		outb(PIC2_CMD, PIC_EOI);

	/* Always send EOI to the master */
	outb(PIC1_CMD, PIC_EOI);
}

void pic_unmask_irq(unsigned char irq)
{
	unsigned char mask;

	/* Read the current mask, clear the bit for this IRQ, write back.
	 * A cleared bit means "allowed" in the 8259A IMR. */
	if (irq >= 8) {
		mask = inb(PIC2_DATA);
		mask &= ~(1 << (irq - 8));
		outb(PIC2_DATA, mask);
	} else {
		mask = inb(PIC1_DATA);
		mask &= ~(1 << irq);
		outb(PIC1_DATA, mask);
	}
}

void pic_mask_irq(unsigned char irq)
{
	unsigned char mask;

	if (irq >= 8) {
		mask = inb(PIC2_DATA);
		mask |= (1 << (irq - 8));
		outb(PIC2_DATA, mask);
	} else {
		mask = inb(PIC1_DATA);
		mask |= (1 << irq);
		outb(PIC1_DATA, mask);
	}
}
