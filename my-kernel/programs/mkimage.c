/* mkimage.c — wrap a raw program binary with the exec header.
 *
 * Output layout (sector-aligned, 512-byte units):
 *   sector 0:  [16-byte header][zero-pad to 512]   (exec reads this)
 *   sector 1+: the raw code bytes, zero-padded to a sector boundary
 *
 * The header's `length` field is the CODE size only (excludes the
 * header sector), so exec knows how many code sectors to read.
 *
 * Usage: mkimage <in.bin> <out.img>
 *
 * This is the host-side tool that turns a linked binary into a
 * loadable program image — a miniature analogue of what `objcopy`
 * plus an ELF program header do for real executables. */

#include <stdio.h>
#include <stdlib.h>

#define SECTOR    512
#define LOAD_ADDR 0x400000u
#define ENTRY_ADDR 0x400000u
#define MAGIC     0x00584E4Cu   /* "LNX\0" little-endian */

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

	if (argc != 3) {
		fprintf(stderr, "usage: %s <in.bin> <out.img>\n", argv[0]);
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

	/* Header sector: 16-byte header + zero pad to 512. */
	put32(out, MAGIC);
	put32(out, LOAD_ADDR);
	put32(out, ENTRY_ADDR);
	put32(out, (unsigned long)len);
	for (hdr_pad = 16; hdr_pad < SECTOR; hdr_pad++)
		fputc(0, out);

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
	fprintf(stderr, "mkimage: %ld code bytes -> %s (%ld sectors)\n",
		len, argv[2],
		(SECTOR + len + code_pad) / SECTOR);
	return 0;
}
