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

/* Set up 2-level page tables and enable paging.
 * Identity-maps the first 4 MiB so the kernel can keep running. */
void paging_init(void);

#endif
