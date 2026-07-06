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

/* The kernel's page directory — set by paging_init(), used by map_page().
 * Stored permanently so callers can map pages after paging is enabled.
 * Exported so ring-3 code can add PAGE_USER to entries. */
u32 *kernel_page_dir;

/* Virtual heap bump allocator — starts right after identity-mapped 4 MiB.
 * Each call to valloc_pages() returns the current value and advances it. */
static u32 virt_heap = 0x400000;

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
	kernel_page_dir = page_dir;
	zero_page(page_dir);
	printf("paging: PD at phys %x\n", (u32)page_dir);

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
	printf("paging: CR0=%x (PG=%d), identity-mapped 0-4 MiB\n",
	       cr0, (cr0 >> 31) & 1);
}

/* ------------------------------------------------------------------ */
/*  map_page_in — map one virtual page into an arbitrary page directory */
/*                                                                     */
/*  Same as map_page but takes the PD as an argument, so a caller can  */
/*  install a mapping in a process's own PD (not the kernel's).        */
/* ------------------------------------------------------------------ */

void map_page_in(u32 *page_dir, u32 vaddr, u32 paddr, u32 flags)
{
	u32 pdx = pd_idx(vaddr);
	u32 ptx = pt_idx(vaddr);
	u32 *pt;

	/* Fetch or allocate the page table for this directory slot */
	if (!(page_dir[pdx] & PAGE_PRESENT)) {
		pt = (u32 *)alloc_page();
		if (!pt) {
			printf("OOM: map_page_in can't allocate PT\n");
			return;
		}
		zero_page(pt);
		page_dir[pdx] = PAGE_ENTRY((u32)pt, PAGE_PRESENT | PAGE_WRITE);
	} else {
		pt = (u32 *)(page_dir[pdx] & 0xFFFFF000);
	}

	/* Fill the PTE: virtual page -> physical page */
	pt[ptx] = PAGE_ENTRY(paddr, flags);

	/* Invalidate the TLB cache for this virtual address so the CPU
	 * picks up the new mapping on the next access. */
	__asm__ volatile("invlpg (%0)" : : "r"(vaddr) : "memory");
}

/* ------------------------------------------------------------------ */
/*  map_page — map one virtual page into the kernel's page directory   */
/* ------------------------------------------------------------------ */

void map_page(u32 vaddr, u32 paddr, u32 flags)
{
	map_page_in(kernel_page_dir, vaddr, paddr, flags);
}

/* ------------------------------------------------------------------ */
/*  clone_kernel_page_dir — make a new PD sharing the kernel mappings   */
/*                                                                     */
/*  Allocates a fresh page directory and copies every present PDE      */
/*  from kernel_page_dir.  The cloned PDEs point at the *same* kernel  */
/*  page tables, so the kernel mappings (image, .data/.bss, buddy      */
/*  metadata, VGA) are shared by all processes — exactly what we want  */
/*  for the kernel to remain accessible after a CR3 switch.            */
/*                                                                     */
/*  Per-process mappings (the private page at USER_PRIVATE_BASE) are  */
/*  installed later by the caller via map_page_in().                   */
/*                                                                     */
/*  Returns the PD as an identity-mapped virtual pointer, which (for  */
/*  32-bit identity-mapped memory) equals its physical address — so    */
/*  callers can store it directly as page_dir_phys.                    */
/* ------------------------------------------------------------------ */

u32 *clone_kernel_page_dir(void)
{
	u32 *pd;
	int i;

	pd = (u32 *)alloc_page();
	if (!pd) {
		printf("OOM: clone_kernel_page_dir\n");
		return NULL;
	}
	zero_page(pd);

	/* Copy present PDEs.  This shares the kernel page tables (the
	 * PDEs point at the same PT physical pages); it does NOT copy
	 * the PTs themselves, so kernel mappings stay common. */
	for (i = 0; i < PD_ENTRIES; i++) {
		if (kernel_page_dir[i] & PAGE_PRESENT)
			pd[i] = kernel_page_dir[i];
	}

	return pd;
}

/* ------------------------------------------------------------------ */
/*  valloc_pages — allocate virtual address space (bump allocator)     */
/* ------------------------------------------------------------------ */

void *valloc_pages(int count)
{
	u32 addr = virt_heap;

	if (count <= 0)
		return NULL;

	virt_heap += (u32)count * 0x1000;
	return (void *)addr;
}

/* ------------------------------------------------------------------ */
/*  tlb_flush_all — flush the entire TLB by reloading CR3              */
/* ------------------------------------------------------------------ */

void tlb_flush_all(void)
{
	__asm__ volatile("mov %0, %%cr3" : : "r"((u32)kernel_page_dir)
			 : "memory");
}
