// fs.h — a tiny flat filesystem (directory table on disk)
//
// What "filesystem" means here, in one sentence: a persistent directory
// table that maps a human name to (LBA, size), so we can stop addressing
// programs by sector number and start saying `exec hello`.
//
// Disk layout of the FS region:
//
//   LBA FS_TABLE_LBA .. FS_TABLE_LBA + FS_TABLE_SECTORS - 1
//
//   byte 0          : FS_MAGIC (a sector whose first word != this is
//                     treated as uninitialised and reformatted on boot)
//   byte 4          : next_free_lba — the monotonic allocation cursor
//                     (where the next runtime-created file lands; never
//                     moves back, so rm'd sectors stay orphaned)
//   byte 8  .. 31   : padding (head is one 32-byte entry-slot wide)
//   byte 32 ..      : FS_MAX_ENTRIES directory entries, back to back
//
// Each directory entry (struct fs_dirent) is 32 bytes, so one 512-byte
// sector holds 16 entry-slots.  The first sector gives up one slot to
// the table head, so a 4-sector table holds 4*16 - 1 = 63 files.  The
// head is NOT a file — files start at entry 0 of the entries[] region.
//
// The table lives in memory after fs_init() and is written back to disk
// whenever it changes (create/unlink).  Reads of file DATA go straight to
// the ATA driver — the table only locates data, it does not cache it.
//
// This is deliberately minimal:
//   - flat (one directory, no subdirectories)
//   - contiguous files (each file's sectors are one contiguous run; we
//     record only start LBA + size, no block list, no fragmentation
//     handling — a real FS chains or indexes its blocks)
//   - no journaling (a crash mid-write-back can corrupt the table)
//
// All of those are intentional demo boundaries, not oversights.

#ifndef FS_H
#define FS_H

#include "types.h"
#include "ata.h"        /* ATA_SECTOR_SIZE */
#include "exec.h"       /* KERNEL_SECTORS */

/* ---- on-disk sizes -------------------------------------------------- */

/* The directory table occupies this many sectors right after the kernel
 * pad.  4 sectors * 16 entries/sector = 64 entries — comfortable headroom
 * over the 4 built-in programs.  Change here and EXEC_DEFAULT_LBA follows
 * (it derives from this via the layout in exec.h). */
#define FS_TABLE_SECTORS 4

/* LBA where the FS table begins: right after boot (1) + kernel pad. */
#define FS_TABLE_LBA      (1u + KERNEL_SECTORS)

/* Number of directory entries that fit in the table.  Each entry is 32
 * bytes; 512 / 32 = 16 entries per sector.  The table's first 16 bytes
 * are the head (magic + cursor + pad), so the first sector holds 15
 * entries (16 - 1 head slot) and each further sector holds 16. */
#define FS_ENTRIES_PER_SECTOR (ATA_SECTOR_SIZE / 32)
#define FS_MAX_ENTRIES        (FS_ENTRIES_PER_SECTOR * FS_TABLE_SECTORS - 1)

/* A name fits in this many bytes including the NUL terminator, so the
 * usable length is FS_NAME_MAX - 1 characters.  Matches the name[16]
 * field baked into each program's exec header. */
#define FS_NAME_MAX 16

/* The first word of the table's first sector.  fs_init() reads it to tell
 * "already formatted" (magic matches → trust the on-disk table) from
 * "blank/corrupt" (magic mismatch → rebuild the table from scratch by
 * scanning the program region).  This is the same self-describing trick
 * exec uses with EXEC_MAGIC. */
#define FS_MAGIC 0x46535331u   /* "FSS1" little-endian (31 53 53 46) */

/* ---- on-disk structures --------------------------------------------- */

/* One directory entry: a file's name + where its data lives on disk.
 * 32 bytes, matching the exec header's 32-byte footprint so both pack
 * cleanly into sectors.  Padded to 32 with `reserved`. */
struct fs_dirent {
	u32  used;          /* 0 = free slot, 1 = in use                    */
	u32  start_lba;     /* first data sector of the file                */
	u32  size;          /* file size in bytes                           */
	u32  flags;        /* reserved for future use (type, perms)       */
	char name[FS_NAME_MAX]; /* NUL-terminated, ≤ FS_NAME_MAX-1 chars   */
};
/* static_assert: 4*4 + 16 == 32 */

/* The whole table in memory: a 32-byte head (magic + allocation cursor,
 * one entry-slot's worth so the entries pack cleanly) followed by the
 * entry array.  We read/write the whole region as FS_TABLE_SECTORS
 * sectors.  The cursor `next_free_lba` is a monotonic counter:
 * fs_create appends each new file at the cursor and advances it,
 * fs_unlink only clears the entry (the data sectors are NOT reclaimed).
 * This is a deliberate demo simplification — a real FS uses a free-space
 * bitmap or a free list so deleted space gets reused. */
#define FS_TABLE_BYTES (FS_TABLE_SECTORS * ATA_SECTOR_SIZE)

struct fs_table {
	u32 magic;                     /* FS_MAGIC when formatted            */
	u32 next_free_lba;             /* cursor: where the next file lands  */
	u8  _pad[24];                  /* head = one entry-slot (32 bytes)   */
	struct fs_dirent entries[FS_MAX_ENTRIES];
};

/* ---- API ------------------------------------------------------------ */

/* Read the directory table from disk into memory (or, if the on-disk
 * table is uninitialised, build it by scanning the program region for
 * exec headers and write it back).  Called once from kernel_main before
 * the shell starts.  Returns 0 on success, negative on error. */
int fs_init(void);

/* Look up a file by name.  On success returns a pointer to the in-memory
 * dirent (valid until the table changes); on miss returns NULL. */
const struct fs_dirent *fs_lookup(const char *name);

/* Print every used directory entry (the `ls` command). */
void fs_list(void);

/* ---- create / unlink / read ----------------------------------------- */

/* Create a new file `name` from `size` bytes at `data`.  The file is
 * appended at the table's allocation cursor (next_free_lba): the data is
 * written to disk, a directory entry is recorded, the cursor is advanced,
 * and the table is flushed.  Returns 0 on success, negative on error
 * (table full, name empty/too long, duplicate name, size too large).
 *
 * Demo limit: a created file fits in one sector (size <= ATA_SECTOR_SIZE);
 * multi-sector runtime files are not supported (YAGNI).  The built-in
 * programs, added by the first-boot scan, can be any size. */
int fs_create(const char *name, const void *data, u32 size);

/* Remove the file `name`: free its slot and write the table back.  The
 * file's data sectors are NOT reclaimed (no free-space bitmap yet) —
 * only the directory entry is cleared.  Returns 0 on success, negative
 * if the name was not found. */
int fs_unlink(const char *name);

/* Read a file into `buf`.  The buffer must hold at least dirent->size
 * bytes.  Returns 0 on success, negative on read error. */
int fs_read(const struct fs_dirent *d, void *buf);

/* Total bytes the FS table occupies on disk (for `ls` / debugging). */
u32 fs_table_bytes(void);

#endif /* FS_H */
