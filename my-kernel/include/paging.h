// paging.h — 32-bit x86 paging (2-level page tables)
//
// Standard 32-bit page translation (CR0.PG = 1, CR4.PAE = 0):
//
//   Linear address (32 bits)
//   ┌───────┬──────────┬──────────────┐
//   │  PDX  │   PTX    │   OFFSET     │
//   │ 10bit │  10bit   │   12bit      │
//   └───┬───┴────┬─────┴──────┬───────┘
//       │        │            │
//    ┌──┘        │            └──┐
//    v           v               v
//  Page       Page Table     4-KiB byte
//  Directory  (1024 × 4B)    offset within
//  (1024×4B)                  the page
//    │           │
//    │  ┌────────┘
//    v  v
//   CR3 ─────→ Physical address of Page Directory

#ifndef PAGING_H
#define PAGING_H

#include "types.h"

/* Page-table / page-directory entry flags (bits 0-7) */
#define PAGE_PRESENT    (1 << 0)        /* P:    page is in memory        */
#define PAGE_WRITE      (1 << 1)        /* R/W:  0 = read-only, 1 = r/w  */
#define PAGE_USER       (1 << 2)        /* U/S:  0 = supervisor, 1 = all */
#define PAGE_PWT        (1 << 3)        /* write-through cache            */
#define PAGE_PCD        (1 << 4)        /* cache disable                  */
#define PAGE_ACCESSED   (1 << 5)        /* A:    set by CPU on access     */
#define PAGE_DIRTY      (1 << 6)        /* D:    set by CPU on write      */
#define PAGE_4MB        (1 << 7)        /* PS:   PDE → 4-MiB page         */

/* Number of entries in a page directory / page table */
#define PT_ENTRIES      1024
#define PD_ENTRIES      1024

/* Build a PDE/PTE that maps the page at `phys` (page-aligned) */
#define PAGE_ENTRY(phys, flags)  (((u32)(phys) & 0xFFFFF000) | (flags))

/* Fixed virtual address each process maps its own private page at.
 * Used by the address-space-isolation demo: every process writes a
 * character here, and the value read back must be its own — proving
 * the page is mapped to a different physical page per process.
 * PDX = 0x80000000 >> 22 = 512, well above the identity-mapped 4 MiB. */
#define USER_PRIVATE_BASE 0x80000000u

/* Set up 2-level page tables and enable paging.
 * Identity-maps the first 4 MiB so the kernel can keep running. */
void paging_init(void);

/* Map one virtual page (4 KiB) to a physical page in the kernel's
 * page directory.  Allocates a page-table page from the bitmap if
 * needed.  flags: lower 12 bits of the PTE. */
void map_page(u32 vaddr, u32 paddr, u32 flags);

/* Map one virtual page into an arbitrary page directory `pd` (given
 * as an identity-mapped virtual pointer).  Used to install a
 * process-private mapping in that process's own PD. */
void map_page_in(u32 *pd, u32 vaddr, u32 paddr, u32 flags);

/* Remove one virtual-page mapping.  This clears only the PTE; it does
 * not free the mapped physical page or the page-table page.  Callers
 * own the physical-page lifetime explicitly. */
void unmap_page(u32 vaddr);
void unmap_page_in(u32 *pd, u32 vaddr);

/* Allocate a fresh page directory and clone the kernel's present
 * PDEs into it (sharing the kernel page tables).  Returns the PD
 * as an identity-mapped virtual pointer (= its physical address).
 * Caller fills in per-process mappings on top. */
u32 *clone_kernel_page_dir(void);

/* Deep-clone a process's user address space into a fresh page
 * directory.  Kernel mappings (non-USER PDEs) are shared by copying
 * the PDE — they point at the same kernel page tables.  USER PDEs are
 * deep-copied: a new page-table page is allocated, and each present
 * USER PTE gets a fresh physical page whose contents are memcpy'd from
 * the source.  The result is a process whose user-visible pages map to
 * independent physical memory, so parent and child diverge on writes.
 *
 * `src` is an identity-mapped virtual pointer to the source page
 * directory.  Returns the clone (identity-mapped pointer), or NULL on
 * allocation failure (the partial clone is freed before returning). */
u32 *clone_address_space(u32 *src);

/* Free every user-owned page table and physical page reachable from
 * `page_dir`, then the directory itself is freed by the caller.
 * Walks the PD: for each present USER PDE, frees every present PTE's
 * physical page, then the page-table page.  Non-USER (kernel) PDEs are
 * shared and skipped — only the process's own USER mappings are freed.
 * Called by the scheduler's reaper when a process that owns its user
 * pages (forked child / exec'd program) exits. */
void free_user_address_space(u32 *page_dir);

/* Allocate 'count' consecutive virtual pages (bump allocator).
 * Returns the virtual address of the block.  Does NOT create mappings.
 * Virtual heap starts at 0x400000 (right after identity-mapped 4 MiB). */
void *valloc_pages(int count);

/* Flush the entire TLB by reloading CR3. */
void tlb_flush_all(void);

/* Page directory — exported so ring-3 setup can add PAGE_USER. */
extern u32 *kernel_page_dir;

#endif
