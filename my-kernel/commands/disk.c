// disk.c — `disk` command: read/dump a sector, or write-test a sector
//
// Usage:
//   disk <lba>        Read sector `lba` and dump it as hex + ASCII.
//   disk <lba> w      Write a test pattern to sector `lba`, then read it
//                     back and dump it — proves the ATA write path works
//                     (the dump shows the pattern we just wrote).
//
// Reads use the ATA driver into a freshly allocated page, then print it as
// 16-byte rows: hex bytes on the left, printable ASCII on the right (dots
// for non-printable).  This is the classical `hexdump -C` layout, trimmed
// to fit 80 columns.
//
// The default (no arg) reads LBA 0 — the boot sector — which makes a good
// smoke test: its last two bytes are the 0xAA55 boot signature, so a
// correct read shows "55 aa" at offset 510.
//
// WARNING: `disk <lba> w` overwrites the sector in the live image.  Pick a
// sector you don't mind losing (e.g. one in the kernel's zero-pad region,
// or beyond the programs) — writing the boot sector or a program image
// will brick the next boot or the next `exec`.

#include "commands.h"
#include "memory.h"      /* alloc_page, free_page */
#include "ata.h"         /* ata_read/write_sectors, ATA_SECTOR_SIZE */
#include "printf.h"      /* printf, putchar_one */

void cmd_disk(int argc, char **argv)
{
	u32 lba = 0;
	u8 *buf;
	int row, col;
	int do_write = 0;

	if (argc >= 2) {
		if (!parse_uint(argv[1], &lba)) {
			printf("disk: '%s' is not a number\n", argv[1]);
			return;
		}
	}
	if (argc >= 3) {
		/* `disk <lba> w` — write a test pattern then read it back. */
		if (argv[2][0] == 'w' && argv[2][1] == '\0') {
			do_write = 1;
		} else {
			printf("disk: unknown option '%s' (use 'w')\n", argv[2]);
			return;
		}
	}

	buf = (u8 *)alloc_page();
	if (!buf) {
		printf("disk: out of memory\n");
		return;
	}

	if (do_write) {
		int i;

		/* Fill the sector with a recognizable pattern: byte value =
		 * its offset mod 256, so the hexdump reads 00 01 02 ... ff
		 * twice across the 512 bytes. */
		for (i = 0; i < ATA_SECTOR_SIZE; i++)
			buf[i] = (u8)(i & 0xFF);

		if (ata_write_sectors(lba, 1, buf) < 0) {
			printf("disk: write failed at LBA %d\n", lba);
			free_page(buf);
			return;
		}
		printf("disk: wrote test pattern to LBA %d, reading back...\n",
		       lba);
	}

	if (ata_read_sectors(lba, 1, buf) < 0) {
		printf("disk: read failed at LBA %d\n", lba);
		free_page(buf);
		return;
	}

	printf("sector %d (0x%x):\n", lba, lba);
	/* Dump 32 rows of 16 bytes = the whole 512-byte sector. */
	for (row = 0; row < ATA_SECTOR_SIZE / 16; row++) {
		u8 *r = buf + row * 16;

		printf("%03x: ", row * 16);
		for (col = 0; col < 16; col++)
			printf("%02x ", r[col]);
		/* ASCII gutter: print printable chars, '.' otherwise. */
		printf(" ");
		for (col = 0; col < 16; col++) {
			u8 c = r[col];
			putchar_one(c >= 32 && c < 127 ? c : '.');
		}
		printf("\n");
	}

	free_page(buf);
}
