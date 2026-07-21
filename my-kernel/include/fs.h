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
//   byte 4   ..     : FS_MAX_ENTRIES directory entries, back to back
//
// Each directory entry (struct fs_dirent) is 32 bytes, so one 512-byte
// sector holds 16 entries; FS_TABLE_SECTORS sectors hold
// FS_TABLE_SECTORS * 16 entries.  Entry 0's magic word is the FS's own
// magic, NOT a file — files start at entry 0 of the entries[] region.
// (We keep the table head and the entries contiguous in one buffer for
// simplicity; the layout is defined by fs_super and fs_table below.)
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
 * bytes; 512 / 32 = 16 entries per sector. */
#define FS_ENTRIES_PER_SECTOR (ATA_SECTOR_SIZE / 32)
#define FS_MAX_ENTRIES        (FS_ENTRIES_PER_SECTOR * FS_TABLE_SECTORS)

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

/* The whole table in memory: `magic` is the on-disk magic word (written
 * back so a reboot recognises a formatted table), followed by the entry
 * array.  We read/write the whole region as FS_TABLE_SECTORS sectors. */
#define FS_TABLE_BYTES (FS_TABLE_SECTORS * ATA_SECTOR_SIZE)

struct fs_table {
	u32 magic;                     /* FS_MAGIC when formatted            */
	u8  _pad[12];                  /* align entries[] to a 32-byte grid */
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

/* ---- create / unlink (added in a later step) ------------------------ */

/* Create a new file `name` holding `size` bytes starting at `start_lba`.
 * Returns 0 on success, negative on error (table full, name too long,
 * duplicate name).  The table is written back to disk on success. */
int fs_create(const char *name, u32 start_lba, u32 size);

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
