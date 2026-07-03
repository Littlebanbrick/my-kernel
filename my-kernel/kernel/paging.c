// paging.c — 32-bit x86 paging initialisation
//
// Sets up a 2-level page table hierarchy:
//
//   Page Directory (PD)   — 1 page,  1024 PDEs × 4 bytes each
//   Page Tables    (PTs)  — N pages, 1024 PTEs × 4 bytes each
//
// Currently identity-maps the first 4 MiB (one page table is enough).
// All page directory / page table pages are allocated from the bitmap
// allocator so they are counted as used physical memory.

#include "paging.h"
#include "memory.h"
#include "printf.h"

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                    */
/* ------------------------------------------------------------------ */

/* Extract the page-directory index from a 32-bit virtual address */
static inline u32 pd_idx(u32 virt)
{
	return virt >> 22;			/* top 10 bits */
}

/* Extract the page-table index from a 32-bit virtual address */
static inline u32 pt_idx(u32 virt)
{
	return (virt >> 12) & 0x3FF;		/* middle 10 bits */
}

/* Zero out an entire 4096-byte page-table / directory page.
 * `page` is a physical address (identity-mapped before paging is on). */
static void zero_page(u32 *page)
{
	int i;
	for (i = 0; i < PT_ENTRIES; i++)
		page[i] = 0;
}

/* ------------------------------------------------------------------ */
/*  Identity-map the range [virt, virt + size) in `page_dir`.
 *
 *  The virtual address is mapped to the same physical address
 *  (identity mapping).  Page-table pages are allocated on demand.      */
/* ------------------------------------------------------------------ */

static void identity_map(u32 virt, u32 size, u32 *page_dir)
{
	u32 end = virt + size;

	for (; virt < end; virt += 0x1000) {
		u32 pdx = pd_idx(virt);
		u32 ptx = pt_idx(virt);

		/* Fetch or allocate the page table for this directory slot */
		u32 *pt;
		if (!(page_dir[pdx] & PAGE_PRESENT)) {
			pt = (u32 *)alloc_page();
			if (!pt) {
				printf("OOM: identity_map can't allocate PT\n");
				return;
			}
			zero_page(pt);
			page_dir[pdx] = PAGE_ENTRY((u32)pt, PAGE_PRESENT | PAGE_WRITE);
		} else {
			pt = (u32 *)(page_dir[pdx] & 0xFFFFF000);
		}

		/* Map this 4-KiB page — virtual == physical */
		pt[ptx] = PAGE_ENTRY(virt, PAGE_PRESENT | PAGE_WRITE);
	}
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

void paging_init(void)
{
	u32 *page_dir;
	u32 cr0;

	printf("paging: setting up page tables...\n");

	/* 1. Allocate the root page directory (one 4-KiB page) */
	page_dir = (u32 *)alloc_page();
	if (!page_dir) {
		printf("OOM: page directory\n");
		return;
	}
	zero_page(page_dir);
	printf("paging: PD at phys 0x%x\n", (u32)page_dir);

	/* 2. Identity-map the first 4 MiB.
	 *
	 *    This covers: IVT, BDA, stack, boot sector, kernel image,
	 *    VGA framebuffer, option ROMs, and 3 MiB of extended memory.
	 *    One page table of 1024 entries × 4 KiB = exactly 4 MiB. */
	identity_map(0x00000000, 0x400000, page_dir);

	/*
	 *    Why 4 MiB is enough:
	 *      0x000000 - 0x0FFFFF   1 MiB  low memory (kernel, stack, VGA, ROMs)
	 *      0x100000 - 0x3FFFFF   3 MiB  extended (bitmap pages, page tables)
	 *      ───────────────────
	 *      0x400000  =  4 MiB  (end of identity-mapped region)
	 */

	/* 3. Load CR3 = physical address of the page directory */
	__asm__ volatile("mov %0, %%cr3" : : "r"((u32)page_dir));

	/* 4. Enable paging: set CR0.PG (bit 31) */
	__asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
	cr0 |= (1 << 31);
	__asm__ volatile("mov %0, %%cr0" : : "r"(cr0));

	/* Re-read CR0 to print a self-check */
	__asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
	printf("paging: CR0=0x%x (PG=%d), identity-mapped 0-4 MiB\n",
	       cr0, (cr0 >> 31) & 1);
}
