/* mkimage.c — wrap a raw program binary with the exec header.
 *
 * Output layout (sector-aligned, 512-byte units):
 *   sector 0:  [32-byte header][zero-pad to 512]   (exec/fs read this)
 *   sector 1+: the raw code bytes, zero-padded to a sector boundary
 *
 * The header's `length` field is the CODE size only (excludes the
 * header sector), so exec knows how many code sectors to read.  The
 * `name` field is the program's own filename (taken from argv[3]); the
 * filesystem reads it during its first-boot scan so `exec <name>` works
 * without a host-side tool pre-filling the directory table.
 *
 * Usage: mkimage <in.bin> <out.img> <name>
 *
 * This is the host-side tool that turns a linked binary into a
 * loadable program image — a miniature analogue of what `objcopy`
 * plus an ELF program header do for real executables. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SECTOR    512
#define LOAD_ADDR 0x400000u
#define ENTRY_ADDR 0x400000u
#define MAGIC     0x00584E4Cu   /* "LNX\0" little-endian */
#define NAME_MAX  16

static void put32(FILE *f, unsigned long v)
{
	fputc((int)(v & 0xFF), f);
	fputc((int)((v >> 8) & 0xFF), f);
	fputc((int)((v >> 16) & 0xFF), f);
	fputc((int)((v >> 24) & 0xFF), f);
}

int main(int argc, char **argv)
{
	FILE *in, *out;
	long len, i, hdr_pad, code_pad, c;
	const char *name;

	if (argc != 4) {
		fprintf(stderr, "usage: %s <in.bin> <out.img> <name>\n",
			argv[0]);
		return 1;
	}
	name = argv[3];
	if (strlen(name) == 0 || strlen(name) >= NAME_MAX) {
		fprintf(stderr, "%s: name must be 1..%d chars\n",
			argv[0], NAME_MAX - 1);
		return 1;
	}
	in = fopen(argv[1], "rb");
	if (!in) { perror("open input"); return 1; }
	fseek(in, 0, SEEK_END);
	len = ftell(in);
	fseek(in, 0, SEEK_SET);
	if (len <= 0) { fprintf(stderr, "empty input\n"); return 1; }

	out = fopen(argv[2], "wb");
	if (!out) { perror("open output"); return 1; }

	/* Header sector: a fixed 32-byte header, then zero-pad to 512.
	 *
	 * The 32 bytes mirror struct exec_hdr exactly: 4 u32 fields
	 * (magic, load, entry, length) followed by a 16-byte name field.
	 * Writing the name into a fixed 16-byte slot (NUL-padded) keeps the
	 * header a whole number of bytes regardless of name length, so the
	 * sector pad arithmetic below is exact.  A variable-length name
	 * would make the header 496 bytes for short names, mis-aligning
	 * every following image to a non-sector boundary — which exec
	 * tolerated (it reads whole sectors) but the FS first-boot scan
	 * cannot (it walks the program region one image at a time). */
	put32(out, MAGIC);
	put32(out, LOAD_ADDR);
	put32(out, ENTRY_ADDR);
	put32(out, (unsigned long)len);
	fwrite(name, 1, strlen(name), out);     /* name, no NUL yet        */
	for (hdr_pad = (long)strlen(name); hdr_pad < NAME_MAX; hdr_pad++)
		fputc(0, out);                  /* NUL + zero-pad name field */
	for (hdr_pad = 32; hdr_pad < SECTOR; hdr_pad++)
		fputc(0, out);                  /* pad rest of header sector */

	/* Code bytes, then zero-pad to a sector boundary so the whole
	 * image is a whole number of sectors (disk reads are sector-
	 * aligned; this keeps reads from running past the image end). */
	for (i = 0; i < len; i++) {
		c = fgetc(in);
		if (c == EOF) break;
		fputc((int)c, out);
	}
	code_pad = (SECTOR - (len % SECTOR)) % SECTOR;
	for (i = 0; i < code_pad; i++)
		fputc(0, out);

	fclose(in);
	fclose(out);
	fprintf(stderr, "mkimage: %ld code bytes -> %s (%ld sectors, name \"%s\")\n",
		len, argv[2],
		(SECTOR + len + code_pad) / SECTOR, name);
	return 0;
}
