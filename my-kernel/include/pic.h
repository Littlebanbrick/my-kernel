// pic.h — 8259A Programmable Interrupt Controller

#ifndef PIC_H
#define PIC_H

/* I/O ports */
#define PIC1_CMD        0x20            /* master PIC command port         */
#define PIC1_DATA       0x21            /* master PIC data port            */
#define PIC2_CMD        0xA0            /* slave PIC command port          */
#define PIC2_DATA       0xA1            /* slave PIC data port             */

/* EOI (End of Interrupt) command — non-specific */
#define PIC_EOI         0x20

/* IRQ base vectors after remapping */
#define IRQ0_VECTOR     32              /* IRQ 0  → IDT[32] (PIT timer)   */
#define IRQ1_VECTOR     33              /* IRQ 1  → IDT[33] (keyboard)    */
#define IRQ2_VECTOR     34              /* IRQ 2  → IDT[34] (cascade)     */
#define IRQ3_VECTOR     35              /* IRQ 3  → IDT[35] (COM2)        */
#define IRQ4_VECTOR     36              /* IRQ 4  → IDT[36] (COM1)        */
#define IRQ5_VECTOR     37              /* IRQ 5  → IDT[37] (LPT2)        */
#define IRQ6_VECTOR     38              /* IRQ 6  → IDT[38] (floppy)      */
#define IRQ7_VECTOR     39              /* IRQ 7  → IDT[39] (LPT1)        */
#define IRQ8_VECTOR     40              /* IRQ 8  → IDT[40] (RTC)         */
#define IRQ9_VECTOR     41              /* IRQ 9  → IDT[41] (ACPI)        */
#define IRQ10_VECTOR    42              /* IRQ 10 → IDT[42]                */
#define IRQ11_VECTOR    43              /* IRQ 11 → IDT[43]                */
#define IRQ12_VECTOR    44              /* IRQ 12 → IDT[44] (PS/2 mouse)  */
#define IRQ13_VECTOR    45              /* IRQ 13 → IDT[45] (FPU)         */
#define IRQ14_VECTOR    46              /* IRQ 14 → IDT[46] (primary IDE) */
#define IRQ15_VECTOR    47              /* IRQ 15 → IDT[47]                */

/* Helper range: IRQ vectors live in [IRQ_BASE, IRQ_BASE + 15] */
#define IRQ_BASE        32
#define IRQ_COUNT       16

/* Remap the PIC so IRQs 0-15 map to IDT[32-47] instead of IDT[0-15]. */
void pic_remap(void);

/* Send End-of-Interrupt signal for a given IRQ (0-15).
 * For IRQs >= 8 (slave PIC), sends EOI to both chips. */
void pic_send_eoi(unsigned char irq);

/* Unmask (enable) a single IRQ line so the PIC can forward it to the CPU.
 * All IRQs are masked after pic_remap(); call this to enable one. */
void pic_unmask_irq(unsigned char irq);

/* Mask (disable) a single IRQ line. */
void pic_mask_irq(unsigned char irq);

#endif