// fs.c — tiny flat filesystem: directory table load/scan/lookup/list
//
// The directory table is the one piece of filesystem state that persists
// across reboots: a flat array of {name, start_lba, size} entries stored
// in FS_TABLE_SECTORS sectors right after the kernel pad.  This file
// owns its in-memory image and the policy for initialising it.
//
// Boot policy (the interesting part):
//
//   Read the table's first sector.  If its first word is FS_MAGIC, the
//   table is already formatted from a previous boot — trust it (it may
//   contain files the user created at runtime last session, which a scan
//   would NOT find because they have no exec header).  Load it as-is.
//
//   If the magic does not match (blank image, first boot, or corruption),
//   the table is empty: rebuild it by scanning the program region sector
//   by sector.  Each sector whose first 4 bytes are EXEC_MAGIC is a
//   program's header; read its name[] and length, compute the image's
//   start LBA and byte size, and add a directory entry.  Skip the image's
//   code sectors (length rounded up to sectors) before resuming the scan,
//   so we don't mistake a code sector for another header.  Then write the
//   freshly built table back to disk so the next boot trusts it.
//
// This hybrid gives us both:
//   - the 4 built-in programs appear by name with no host-side tool
//     (the scan reads each program's self-declared name), and
//   - runtime-created data files persist across reboots (they live in
//     the table, which is loaded as-is on subsequent boots).
//
// All file DATA access goes through the ATA driver — the table only
// locates data, it never caches file contents.

#include "fs.h"
#include "ata.h"        /* ata_read/write_sectors */
#include "memory.h"     /* alloc_page, free_page */
#include "exec.h"       /* EXEC_MAGIC, EXEC_DEFAULT_LBA, struct exec_hdr */
#include "utils.h"      /* kmemcpy */
#include "printf.h"

/* The in-memory copy of the directory table.  One page is plenty: the
 * table is FS_TABLE_BYTES (2048 for 4 sectors), well under 4 KiB.  We
 * keep it in a dedicated kernel buffer rather than reusing a scratch
 * page so a lookup pointer stays valid across other disk reads. */
static u8 table_buf[FS_TABLE_BYTES] __attribute__((aligned(4)));
static struct fs_table *const table = (struct fs_table *)table_buf;

/* ---- small string helpers (no libc) --------------------------------- */

static int name_eq(const char *a, const char *b)
{
	unsigned int i;

	for (i = 0; i < FS_NAME_MAX; i++) {
		if (a[i] != b[i])
			return 0;
		if (a[i] == '\0')
			return 1;
	}
	return 0;   /* ran out of room without a NUL — not equal */
}

static void name_copy(char *dst, const char *src)
{
	unsigned int i;

	for (i = 0; i < FS_NAME_MAX - 1 && src[i]; i++)
		dst[i] = src[i];
	dst[i] = '\0';
}

/* ---- scan: build a table from the program region -------------------- */

/* Scan LBA EXEC_DEFAULT_LBA onward for program headers and add each one
 * to the in-memory table.  Stops at the first sector that does not carry
 * the exec magic — which is either a read failure (we ran off the end of
 * the image) or a zero sector (the gap after the last program).  Either
 * way the program region is exhausted, so we stop quietly.  Each program
 * image is [header sector][code sectors...], where the header declares
 * length in bytes; we skip length rounded up to sectors before
 * continuing. */
static void scan_programs(void)
{
	u32 lba = EXEC_DEFAULT_LBA;
	u8 sec[ATA_SECTOR_SIZE];
	int idx = 0;

	for (;;) {
		struct exec_hdr *h;
		u32 nsec, img;

		if (ata_read_sectors(lba, 1, sec) < 0)
			break;          /* ran off the image end: stop quietly */
		h = (struct exec_hdr *)sec;
		if (h->magic != EXEC_MAGIC)
			break;          /* no more programs */

		if (idx < FS_MAX_ENTRIES) {
			struct fs_dirent *d = &table->entries[idx];

			d->used = 1;
			d->start_lba = lba;       /* points at the header sector */
			d->size = h->length + ATA_SECTOR_SIZE; /* header + code */
			d->flags = 0;
			name_copy(d->name, h->name);
			idx++;
		}

		/* Advance past this image: 1 header sector + ceil(length/512). */
		nsec = (h->length + ATA_SECTOR_SIZE - 1) / ATA_SECTOR_SIZE;
		img = 1 + nsec;
		lba += img;
	}

	/* The cursor stops at the first sector after the last program — the
	 * natural place to start appending runtime-created files.  It only
	 * ever moves forward (fs_create advances it, fs_unlink does not),
	 * so deleted files leave unreachable data sectors behind. */
	table->next_free_lba = lba;
	printf("fs: scanned %d programs, next free LBA %d\n", idx, lba);
}

/* Write the in-memory table back to the FS table region on disk. */
static int table_flush(void)
{
	return ata_write_sectors(FS_TABLE_LBA, FS_TABLE_SECTORS, table_buf);
}

/* -------------------------------------------------------------------- */
/*  fs_init                                                             */
/* -------------------------------------------------------------------- */

int fs_init(void)
{
	int rc;

	/* Read the table region.  If the magic matches, it's already
	 * formatted — use it as-is (it may carry runtime-created files
	 * from a previous session that a scan would not find). */
	rc = ata_read_sectors(FS_TABLE_LBA, FS_TABLE_SECTORS, table_buf);
	if (rc < 0) {
		printf("fs: cannot read table region (LBA %d)\n",
		       FS_TABLE_LBA);
		return -1;
	}

	if (table->magic == FS_MAGIC) {
		printf("fs: loaded existing table from disk\n");
		return 0;
	}

	/* No valid table: build a fresh one by scanning programs. */
	printf("fs: blank table, scanning program region...\n");
	kmemcpy(table_buf, "\x00\x00\x00\x00", 4);   /* clear magic first */
	{
		unsigned int i;
		for (i = 0; i < FS_MAX_ENTRIES; i++)
			table->entries[i].used = 0;
	}
	scan_programs();
	table->magic = FS_MAGIC;

	/* Persist so the next boot trusts the table instead of rescanning. */
	if (table_flush() < 0) {
		printf("fs: WARNING — could not write table back to disk\n");
		/* non-fatal: the in-memory table is still usable this session */
	}

	printf("fs: ready (%d-byte table at LBA %d, programs at LBA %d)\n",
	       FS_TABLE_BYTES, FS_TABLE_LBA, EXEC_DEFAULT_LBA);
	return 0;
}

/* -------------------------------------------------------------------- */
/*  fs_lookup / fs_list                                                 */
/* -------------------------------------------------------------------- */

const struct fs_dirent *fs_lookup(const char *name)
{
	unsigned int i;

	for (i = 0; i < FS_MAX_ENTRIES; i++) {
		if (table->entries[i].used && name_eq(table->entries[i].name,
						     name))
			return &table->entries[i];
	}
	return NULL;
}

void fs_list(void)
{
	unsigned int i;
	int count = 0;

	printf("NAME           SIZE   LBA\n");
	for (i = 0; i < FS_MAX_ENTRIES; i++) {
		const struct fs_dirent *d = &table->entries[i];

		if (!d->used)
			continue;
		printf("%-15s %5d %5d\n", d->name, d->size, d->start_lba);
		count++;
	}
	if (count == 0)
		printf("(empty)\n");
}

/* ---- create / unlink / read ----------------------------------------- */

/* Length of a NUL-terminated name, capped at FS_NAME_MAX.  Used to reject
 * empty and over-long names without running past the field. */
static unsigned int name_len(const char *s)
{
	unsigned int i;

	for (i = 0; i < FS_NAME_MAX; i++)
		if (s[i] == '\0')
			return i;
	return FS_NAME_MAX;   /* no NUL within the field — too long */
}

/* Find the first unused directory slot, or -1 if the table is full. */
static int find_free_slot(void)
{
	unsigned int i;

	for (i = 0; i < FS_MAX_ENTRIES; i++)
		if (!table->entries[i].used)
			return (int)i;
	return -1;
}

/* Create a file `name` from `size` bytes at `data`.  The data is appended
 * at the table's allocation cursor (next_free_lba), a directory entry is
 * recorded, the cursor advances, and the whole table is flushed — so a
 * created file survives a reboot.  Returns 0 on success, negative on error.
 *
 * Order matters for crash safety: we write the DATA first, then update +
 * flush the TABLE.  A crash between the two leaves either nothing (no
 * entry) or an entry pointing at already-written data — never an entry
 * pointing at unwritten garbage.  (We still have no journal, so a crash
 * mid-table-flush can corrupt the table itself; that's the accepted demo
 * boundary.) */
int fs_create(const char *name, const void *data, u32 size)
{
	unsigned int nlen;
	int slot;

	nlen = name_len(name);
	if (nlen == 0 || nlen >= FS_NAME_MAX) {
		printf("fs: bad name (must be 1..%d chars)\n", FS_NAME_MAX - 1);
		return -1;
	}
	if (size == 0) {
		printf("fs: refusing to create empty file\n");
		return -1;
	}
	/* Demo limit: a created file must fit in one sector.  The `write`
	 * command feeds a single input line (< 64 bytes), so this is ample;
	 * lifting it means switching the data write to a sector-count loop
	 * and rounding size up — left out as YAGNI. */
	if (size > ATA_SECTOR_SIZE) {
		printf("fs: file too large (%d > %d)\n", size, ATA_SECTOR_SIZE);
		return -1;
	}
	if (fs_lookup(name) != NULL) {
		printf("fs: '%s' already exists\n", name);
		return -1;
	}
	slot = find_free_slot();
	if (slot < 0) {
		printf("fs: directory table full\n");
		return -1;
	}

	/* 1. Write the file's data to the cursor sector.  We need a whole
	 *    sector buffer because ata_write_sectors writes 512 bytes; the
	 *    caller's `data` may be shorter, so we zero-pad into a local
	 *    sector. */
	{
		u8 sec[ATA_SECTOR_SIZE];
		u32 lba = table->next_free_lba;

		kmemcpy(sec, data, size);
		/* Zero the tail so we never persist stale bytes from a
		 * previous, longer file that occupied this sector. */
		{
			unsigned int i;
			for (i = size; i < ATA_SECTOR_SIZE; i++)
				sec[i] = 0;
		}
		if (ata_write_sectors(lba, 1, sec) < 0) {
			printf("fs: data write failed at LBA %d\n", lba);
			return -1;
		}

		/* 2. Record the directory entry + advance the cursor. */
		{
			struct fs_dirent *d = &table->entries[slot];

			d->used = 1;
			d->start_lba = lba;
			d->size = size;
			d->flags = 0;
			name_copy(d->name, name);
		}
		table->next_free_lba = lba + 1;
	}

	/* 3. Flush the table so the new entry outlives this session. */
	if (table_flush() < 0) {
		printf("fs: WARNING — table write-back failed; '%s' data is "
		       "on disk but the entry may not persist\n", name);
		return -1;
	}

	printf("fs: created '%s' (%d bytes)\n", name, size);
	return 0;
}

/* Remove the file `name`: clear its directory slot and flush the table.
 * The file's DATA sectors are NOT reclaimed — next_free_lba never moves
 * backward, so the sectors become unreachable orphans.  This is the
 * accepted demo simplification; a real FS marks them free in a bitmap. */
int fs_unlink(const char *name)
{
	const struct fs_dirent *d = fs_lookup(name);

	if (!d) {
		printf("fs: no such file '%s'\n", name);
		return -1;
	}

	/* fs_lookup returned a const pointer into the table; clear through
	 * the matching non-const entry.  Only `used` needs clearing — the
	 * other fields are ignored once used == 0, and will be overwritten
	 * when the slot is reused. */
	table->entries[d - table->entries].used = 0;

	if (table_flush() < 0) {
		printf("fs: WARNING — table write-back failed; '%s' may "
		       "reappear on reboot\n", name);
		return -1;
	}

	printf("fs: removed '%s' (data sectors NOT reclaimed)\n", name);
	return 0;
}

/* Read a file's data into `buf`.  `buf` must hold at least d->size bytes
 * (the caller allocates it).  Reads ceil(size/512) sectors — always at
 * least one — straight from the ATA driver.  Returns 0 on success,
 * negative on read error. */
int fs_read(const struct fs_dirent *d, void *buf)
{
	u32 nsec;

	if (!d || !d->used)
		return -1;

	nsec = (d->size + ATA_SECTOR_SIZE - 1) / ATA_SECTOR_SIZE;
	if (nsec == 0)
		nsec = 1;
	return ata_read_sectors(d->start_lba, nsec, buf);
}

u32 fs_table_bytes(void)
{
	return FS_TABLE_BYTES;
}
