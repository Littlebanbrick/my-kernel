// reboot.c — `reboot` command: reset the machine via the 8042
//
// Port 0x64 is the 8042 keyboard controller's command/status port.
// Writing 0xFE tells the 8042 to pulse its reset line, which forces
// the CPU into a hardware reset — the same path a real PC uses on
// power-on reset.  QEMU honours this and restarts the machine, so we
// land back at 0x7C00 with a clean boot.
//
// This is a "bare-metal reboot": there is no filesystem to sync and
// no init to shut down (unlike Linux's reboot(2) syscall, which is the
// tail end of a graceful shutdown protocol).  We just reset.

#include "commands.h"
#include "utils.h"        /* inb/outb (8042 reset) */

void cmd_reboot(int argc, char **argv)
{
	(void)argc; (void)argv;

	/* Use iodelay-safe writes: wait for the input buffer to clear
	 * before sending each byte.  On real hardware dropping the wait
	 * can lose the command; QEMU is lenient but we keep the
	 * discipline for correctness. */
	while ((inb(0x64) & 0x02) != 0)   /* input buffer full? */
		;
	outb(0x64, 0xFE);                /* pulse reset line */

	/* If the reset didn't fire (broken 8042), halt.  In QEMU this
	 * line is never reached. */
	for (;;)
		asm volatile("hlt");
}
