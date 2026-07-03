// memory.c — Physical memory bitmap allocator (32-bit)
//
// Tracks used / free physical pages via a simple bit array.
// Each bit represents one 4-KiB page:  0 = free, 1 = used.
// The bitmap itself lives in .bss and is marked as used after init.

#include "memory.h"
#include "printf.h"

/* ----------------------------------------------------------------- */
/*  Bitmap helpers                                                    */
/* ----------------------------------------------------------------- */

static u8 bitmap[BITMAP_SIZE];

static inline void bitmap_set(int page)
{
	bitmap[page / 8] |= (u8)(1 << (page % 8));
}

static inline void bitmap_clear(int page)
{
	bitmap[page / 8] &= (u8)~(1 << (page % 8));
}

static inline int bitmap_test(int page)
{
	return (bitmap[page / 8] >> (page % 8)) & 1;
}

/* Mark a physical address range as used (inclusive of start, exclusive of
 * end).  Both start and end are byte addresses, not page indices. */
static void mark_used(u32 start, u32 end)
{
	int first = start / PAGE_SIZE;
	int last  = (end + PAGE_SIZE - 1) / PAGE_SIZE;  /* round up */
	int i;

	for (i = first; i < last; i++)
		bitmap_set(i);
}

/* Find 'count' consecutive free pages by linear scan.
 * Returns the page index, or -1 if out of memory. */
static int find_free(int count)
{
	int i, j;

	for (i = 0; i <= TOTAL_PAGES - count; i++) {
		if (bitmap_test(i))
			continue;

		/* Check that the next (count - 1) pages are also free */
		for (j = 1; j < count; j++) {
			if (bitmap_test(i + j))
				break;
		}
		if (j == count)
			return i;		/* found a run */

		i += j;				/* skip past the used page  */
	}
	return -1;				/* OOM */
}

/* ----------------------------------------------------------------- */
/*  Public API                                                        */
/* ----------------------------------------------------------------- */

void bitmap_init(void)
{
	extern u32 _end;		/* defined in linker.ld */
	int i;

	/* Everything starts free */
	for (i = 0; i < BITMAP_SIZE; i++)
		bitmap[i] = 0;

	/*
	 * Mark reserved / already-used regions.
	 *
	 * Standard PC memory layout (simplified):
	 *   0x000000 - 0x000FFF    IVT + BDA (real mode)
	 *   0x007000 - 0x007FFF    stack (ESP = 0x7000, grows down)
	 *   0x007C00 - 0x007FFF    boot sector
	 *   0x008000 - _end        kernel .pad + .text + .data + .bss
	 *   0x0A0000 - 0x0B7FFF    VGA graphics / video RAM
	 *   0x0B8000 - 0x0BFFFF    VGA text framebuffer
	 *   0x0C0000 - 0x0FFFFF    Video BIOS / expansion ROMs
	 *   ----------------------------------------------------
	 *   0x100000 - 0x07FFFFFF  free extended memory (~127 MB)
	 */
	mark_used(0x000000, 0x001000);		/* IVT + BDA                */
	mark_used(0x007000, 0x008000);		/* stack + boot sector      */
	mark_used(0x008000, (u32)&_end);	/* kernel image             */
	mark_used(0x0A0000, 0x0C0000);		/* VGA video + text + ROMs  */
	mark_used(0x0C0000, 0x100000);		/* option ROMs / BIOS       */

	printf("bitmap: %d bytes for %d pages (%d MB)\n",
	       BITMAP_SIZE, TOTAL_PAGES,
	       TOTAL_MEMORY / (1024 * 1024));
	printf("kernel  _end = %x\n", (u32)&_end);

	/* The first free page starts right after our highest
	 * reserved region, i.e. at 0x100000 (1 MiB). */
}

void *alloc_pages(int count)
{
	int page;

	if (count <= 0)
		return NULL;

	page = find_free(count);
	if (page < 0) {
		printf("OOM: alloc_pages(%d) failed!\n", count);
		return NULL;
	}

	for (int i = 0; i < count; i++)
		bitmap_set(page + i);

	return (void *)(page * PAGE_SIZE);
}

void *alloc_page(void)
{
	return alloc_pages(1);
}

void free_pages(void *addr, int count)
{
	int page;
	int i;

	if (!addr || count <= 0)
		return;

	page = (u32)addr / PAGE_SIZE;
	for (i = 0; i < count; i++)
		bitmap_clear(page + i);
}

void free_page(void *addr)
{
	free_pages(addr, 1);
}
