// ata.h — Minimal ATA/IDE PIO driver (LBA28 read + write)
//
// The PC's classic disk controller speaks the ATA protocol over a pair of
// I/O port groups ("primary" and "secondary" channel, two drives each).
// This driver talks to the PRIMARY channel, MASTER drive — which is where
// QEMU puts the `-drive ... file=disk.img` image by default.  We implement
// exactly what the kernel needs: read/write N consecutive sectors by LBA.
//
// PIO ("Programmed I/O") means the CPU itself copies every word out of the
// controller's data register with `insw` — there is no DMA.  Slow, but
// trivially correct and needs no setup beyond waiting on the status port.
//
//  LBA (Logical Block Addressing):
//   A disk is a linear array of 512-byte sectors, numbered from 0.  LBA
//   replaces the old CHS (cylinder/head/sector) geometry: "sector LBA 5"
//   is unambiguous, whereas CHS depends on the disk's fake geometry.  All
//   modern disks (and QEMU) accept LBA.  We use LBA28, which addresses up
//   to 128 GiB (2^28 sectors) — plenty for our image.
//
//  Port map (primary channel):
//   0x1F0  data        — 16-bit; read/write sector data here
//   0x1F1  features/err
//   0x1F2  sector count — how many sectors to transfer
//   0x1F3  LBA low      — bits 0-7 of the LBA
//   0x1F4  LBA mid      — bits 8-15
//   0x1F5  LBA high     — bits 16-23
//   0x1F6  drive/head   — LBA bit 24-27 + drive select in the top nibble
//   0x1F7  status/cmd   — read status, write command
//
/* This header keeps both the read and write paths: exec loads program
 * images with read, the filesystem stores its directory table back to
 * disk with write.  The two paths share the same setup (wait BSY, select
 * drive, feed count + LBA) and differ only in the command and the data
 * register direction. */

#ifndef ATA_H
#define ATA_H

#include "types.h"

/* Primary channel I/O ports */
#define ATA_DATA         0x1F0
#define ATA_ERROR        0x1F1
#define ATA_SECTOR_COUNT 0x1F2
#define ATA_LBA_LOW      0x1F3
#define ATA_LBA_MID      0x1F4
#define ATA_LBA_HIGH     0x1F5
#define ATA_DRIVE        0x1F6
#define ATA_STATUS       0x1F7
#define ATA_COMMAND      0x1F7

/* Status register bits (read from ATA_STATUS) */
#define ATA_SR_ERR   0x01   /* bit 0: error occurred, see ERROR reg     */
#define ATA_SR_DRQ   0x08   /* bit 3: data ready, PIO transfer pending  */
#define ATA_SR_DRDY  0x40   /* bit 6: drive ready to accept a command   */
#define ATA_SR_BSY   0x80   /* bit 7: controller busy, do NOT touch     */

/* Commands (written to ATA_COMMAND) */
#define ATA_CMD_READ_PIO  0x20   /* LBA28 read, 1 sector per IRQ (we poll) */
#define ATA_CMD_WRITE_PIO 0x30   /* LBA28 write, 1 sector per IRQ (we poll) */

/* Bytes per sector — the ATA logical block size, fixed since forever. */
#define ATA_SECTOR_SIZE 512

/* Read `count` consecutive sectors starting at LBA `lba` into `buf`.
 * buf must hold at least count * ATA_SECTOR_SIZE bytes.  Returns 0 on
 * success, negative on error.  This is a blocking, polled read. */
int ata_read_sectors(u32 lba, u32 count, void *buf);

/* Write `count` consecutive sectors starting at LBA `lba` from `buf`.
 * buf must hold at least count * ATA_SECTOR_SIZE bytes.  Returns 0 on
 * success, negative on error.  This is a blocking, polled write.
 *
 * A real driver issues a CACHE FLUSH (0xE7) + wait after writing so the
 * data reaches non-volatile media before the caller assumes it's durable.
 * QEMU's write-back cache flushes per sector on the next command, so for
 * our in-memory image we omit the explicit flush — adding it is the only
 * change needed before running on real hardware. */
int ata_write_sectors(u32 lba, u32 count, const void *buf);

#endif /* ATA_H */
