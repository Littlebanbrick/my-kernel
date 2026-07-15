// ata.h — Minimal ATA/IDE PIO driver (LBA28 read only)
//
// The PC's classic disk controller speaks the ATA protocol over a pair of
// I/O port groups ("primary" and "secondary" channel, two drives each).
// This driver talks to the PRIMARY channel, MASTER drive — which is where
// QEMU puts the `-drive ... file=disk.img` image by default.  We only
// implement what exec will need: read N consecutive sectors by LBA.
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
// See OSDev wiki "ATA PIO Mode" for the full protocol.  This header keeps
// only the read path.

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

/* Bytes per sector — the ATA logical block size, fixed since forever. */
#define ATA_SECTOR_SIZE 512

/* Read `count` consecutive sectors starting at LBA `lba` into `buf`.
 * buf must hold at least count * ATA_SECTOR_SIZE bytes.  Returns 0 on
 * success, negative on error.  This is a blocking, polled read. */
int ata_read_sectors(u32 lba, u32 count, void *buf);

#endif /* ATA_H */
