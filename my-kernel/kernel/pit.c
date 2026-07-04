// pit.c — 8253/8254 Programmable Interval Timer driver (32-bit)
//
// Channel 0 of the PIT is permanently wired to the master PIC's IRQ 0
// line.  We programme it in mode 2 (rate generator) with a 16-bit
// reload value; the resulting IRQ 0 frequency is
//
//     f = 1193182 / reload
//
// On every terminal count the PIT pulls IRQ 0 high for one cycle,
// the PIC latches it, and (if unmasked) the CPU takes vector
// IRQ0_VECTOR (= 32 after pic_remap()).
//
// We don't need channel 1 (RAM refresh) or channel 2 (speaker).

#include "utils.h"
#include "pit.h"

void pit_init(unsigned int hz)
{
	unsigned int reload;

	if (hz == 0)
		hz = 1;

	/* Round to nearest reload value; clamp to the 16-bit range. */
	reload = (PIT_BASE_HZ + (hz / 2)) / hz;
	if (reload < 1)
		reload = 1;
	if (reload > 0xFFFF)
		reload = 0xFFFF;

	/*
	 * Command byte for channel 0:
	 *   bits 7-6 = 00    select channel 0
	 *   bits 5-4 = 11    access mode: lobyte/hibyte (must write low
	 *                    then high)
	 *   bits 3-1 = 010   operating mode 2 (rate generator)
	 *   bit 0    = 0     binary (not BCD)
	 *   => 0x34
	 */
	outb(PIT_CMD, 0x34);

	/* Write reload value low byte, then high byte */
	outb(PIT_DATA0, (unsigned char)(reload & 0xFF));
	outb(PIT_DATA0, (unsigned char)((reload >> 8) & 0xFF));
}
