// disk.c — `disk` command: read and dump one disk sector as hex + ASCII
//
// Usage:  disk <lba>
// Reads sector `lba` (512 bytes) via the ATA driver into a freshly
// allocated page, then prints it as 16-byte rows: hex bytes on the
// left, printable ASCII on the right (dots for non-printable).  This
// is the classical `hexdump -C` layout, trimmed to fit 80 columns.
//
// The default (no arg) reads LBA 0 — the boot sector — which makes a
// good smoke test: its last two bytes are the 0xAA55 boot signature,
// so a correct read shows "55 aa" at offset 510.

#include "commands.h"
#include "memory.h"      /* alloc_page, free_page */
#include "ata.h"         /* ata_read_sectors, ATA_SECTOR_SIZE */
#include "printf.h"      /* printf, putchar_one */

void cmd_disk(int argc, char **argv)
{
	u32 lba = 0;
	u8 *buf;
	int row, col;

	if (argc >= 2) {
		if (!parse_uint(argv[1], &lba)) {
			printf("disk: '%s' is not a number\n", argv[1]);
			return;
		}
	}

	buf = (u8 *)alloc_page();
	if (!buf) {
		printf("disk: out of memory\n");
		return;
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
