// pit.h — 8253/8254 Programmable Interval Timer (PIT)

#ifndef PIT_H
#define PIT_H

/* I/O ports */
#define PIT_DATA0        0x40    /* channel 0 data port             */
#define PIT_DATA1        0x41    /* channel 1 data port             */
#define PIT_DATA2        0x42    /* channel 2 data port             */
#define PIT_CMD          0x43    /* mode/command register           */

/* PIT input clock frequency — fixed by hardware.  The IRQ0 pulse
 * frequency = 1193182 / divisor.  A divisor of 11932 ≈ 100 Hz. */
#define PIT_BASE_HZ      1193182u

/* Programme channel 0 to fire `hz` times per second.
 *
 * Channel 0 is wired to IRQ 0; on every terminal count it raises
 * IRQ 0, which the 8259A forwards to the CPU as vector IRQ0_VECTOR.
 *
 * We use mode 2 (rate generator): periodic square wave.  A reload
 * value of N gives a pulse every N input cycles. */
void pit_init(unsigned int hz);

#endif
