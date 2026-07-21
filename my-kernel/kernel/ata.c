// ata.c — Minimal ATA/IDE PIO driver: LBA28 polled read + write
//
// The read path, in prose:
//
//   1. Wait for BSY=0.  While the controller is busy it ignores us, so we
//      spin on the status port until bit 7 clears.
//
//   2. Select the drive + send the top LBA nibble.  Port 0x1F6's top
//      nibble carries LBA bits 24-27 AND the master/slave bit (0xE0 for
//      master, with the LBA bit set so the controller knows it's LBA, not
//      CHS).  We OR in the low 4 bits of (lba >> 24).
//
//   3. Send the sector count, then the low three bytes of the LBA, to
//      ports 0x1F2..0x1F5.
//
//   4. Issue the READ command (0x20) to port 0x1F7.
//
//   5. For each sector: wait for DRQ (bit 3), then `insw` 256 words
//      (=512 bytes) out of the data port (0x1F0).  The controller raises
//      DRQ once per sector, so a multi-sector read is just a loop.
//
//   The write path is the mirror image: same setup, but issue the WRITE
//   command (0x30) and `outsw` words INTO the data port instead of
//   `insw`-ing them out.  The controller still raises DRQ once per sector
//   to signal it's ready to swallow the next 512 bytes.
//
// We poll instead of using IRQs: simpler, and reads are short.  The cost
// is that we burn CPU spinning; for a teaching kernel that's fine.
//
// All disk I/O here is identity-mapped (the buffer lives in the first
// 4 MiB), so we can touch it from ring 0 without paging worries.

#include "ata.h"
#include "utils.h"   /* inb/outb */
#include "printf.h"

/* Spin until the controller is not busy, or until we give up.
 * Returns 0 on success, -1 on timeout.  Real hardware can take a while
 * to spin up; QEMU is instant, but we keep the wait for correctness. */
static int ata_wait_not_busy(void)
{
	int timeout;

	for (timeout = 0; timeout < 100000; timeout++) {
		if ((inb(ATA_STATUS) & ATA_SR_BSY) == 0)
			return 0;
	}
	return -1;
}

/* Wait until the controller can hand us data: DRQ set, and no error.
 * Returns 0 on ready, -1 on error or timeout. */
static int ata_wait_drq(void)
{
	int timeout;
	u8 st;

	for (timeout = 0; timeout < 100000; timeout++) {
		st = inb(ATA_STATUS);
		if (st & ATA_SR_ERR)
			return -1;          /* error bit set — abort */
		if (st & ATA_SR_DRQ)
			return 0;           /* data ready */
	}
	return -1;
}

/* Read one 512-byte sector via 256 16-bit PIO transfers.  The CPU reads
 * the data port with `insw`, which copies ECX words from port DX to ES:EDI.
 * We drive it from C with a tight loop of inw() for clarity. */
static void ata_read_sector(void *buf)
{
	u16 *dst = (u16 *)buf;
	int i;

	for (i = 0; i < ATA_SECTOR_SIZE / 2; i++)
		dst[i] = inw(ATA_DATA);
}

/* Write one 512-byte sector via 256 16-bit PIO transfers.  The mirror of
 * ata_read_sector: the CPU writes words to the data port with `outsw`. */
static void ata_write_sector(const void *buf)
{
	const u16 *src = (const u16 *)buf;
	int i;

	for (i = 0; i < ATA_SECTOR_SIZE / 2; i++)
		outw(ATA_DATA, src[i]);
}

/* Read `count` sectors starting at LBA `lba` into `buf`.
 *
 * LBA28: only the low 28 bits of the LBA are used, so this caps out at
 * 128 GiB — far beyond our image.  `count` must be non-zero; the
 * controller accepts up to 256 sectors per command (0 means 256), but we
 * keep reads modest. */
int ata_read_sectors(u32 lba, u32 count, void *buf)
{
	u32 i;
	u8 *p = (u8 *)buf;

	if (count == 0)
		return 0;

	/* 1. Don't talk to a busy controller. */
	if (ata_wait_not_busy() < 0) {
		printf("ata: controller busy (timeout)\n");
		return -1;
	}

	/* 2-3. Select master + LBA mode, then send count + LBA bytes.
	 *
	 *    0xE0 = 1110 0000b: the top nibble's high bits are the "LBA mode"
	 *    and "master drive" selectors.  We OR in bits 24-27 of the LBA
	 *    (for LBA28, that's all that's left after the three byte ports). */
	outb(ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
	outb(ATA_SECTOR_COUNT, (u8)count);
	outb(ATA_LBA_LOW,  (u8)(lba >> 0));
	outb(ATA_LBA_MID,  (u8)(lba >> 8));
	outb(ATA_LBA_HIGH, (u8)(lba >> 16));

	/* 4. Issue READ SECTORS (PIO, LBA28). */
	outb(ATA_COMMAND, ATA_CMD_READ_PIO);

	/* 5. For each sector, wait for data, then drain 256 words. */
	for (i = 0; i < count; i++) {
		if (ata_wait_drq() < 0) {
			printf("ata: read error at LBA %d\n", lba + i);
			return -1;
		}
		ata_read_sector(p);
		p += ATA_SECTOR_SIZE;
	}

	return 0;
}

/* Write `count` sectors starting at LBA `lba` from `buf`.  Mirror of
 * ata_read_sectors: same LBA28 setup, but WRITE command + outsw.  After
 * feeding every sector the controller needs a final not-busy wait so a
 * caller can immediately issue the next command (e.g. a verify read)
 * without racing the write-back. */
int ata_write_sectors(u32 lba, u32 count, const void *buf)
{
	u32 i;
	const u8 *p = (const u8 *)buf;

	if (count == 0)
		return 0;

	/* 1. Don't talk to a busy controller. */
	if (ata_wait_not_busy() < 0) {
		printf("ata: controller busy (timeout)\n");
		return -1;
	}

	/* 2-3. Select master + LBA mode, then send count + LBA bytes. */
	outb(ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
	outb(ATA_SECTOR_COUNT, (u8)count);
	outb(ATA_LBA_LOW,  (u8)(lba >> 0));
	outb(ATA_LBA_MID,  (u8)(lba >> 8));
	outb(ATA_LBA_HIGH, (u8)(lba >> 16));

	/* 4. Issue WRITE SECTORS (PIO, LBA28). */
	outb(ATA_COMMAND, ATA_CMD_WRITE_PIO);

	/* 5. For each sector, wait for DRQ, then feed 256 words. */
	for (i = 0; i < count; i++) {
		if (ata_wait_drq() < 0) {
			printf("ata: write error at LBA %d\n", lba + i);
			return -1;
		}
		ata_write_sector(p);
		p += ATA_SECTOR_SIZE;
	}

	/* 6. Let the write-back complete before the caller issues the next
	 *    command.  Polling BSY here is the cheap substitute for a
	 *    CACHE FLUSH command — sufficient for QEMU's write-back cache. */
	if (ata_wait_not_busy() < 0) {
		printf("ata: write-back busy (timeout) at LBA %d\n", lba);
		return -1;
	}

	return 0;
}
