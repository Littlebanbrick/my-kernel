// memory.h — Physical memory management (bitmap)

#ifndef MEMORY_H
#define MEMORY_H

#include "types.h"

#define PAGE_SIZE       4096                    /* bytes per page        */
#define TOTAL_MEMORY    (128 * 1024 * 1024)     /* 128 MB (QEMU default) */
#define TOTAL_PAGES     (TOTAL_MEMORY / PAGE_SIZE)  /* 32768            */
#define BITMAP_SIZE     (TOTAL_PAGES / 8)       /* 4096 bytes = 4 KB    */

/* Initialise the physical-memory bitmap.
 * Call once during startup, after the screen and IDT are ready. */
void bitmap_init(void);

/* Allocate one 4-KiB page.  Returns its physical address, or NULL. */
void *alloc_page(void);

/* Allocate 'count' consecutive 4-KiB pages.  Returns NULL on failure. */
void *alloc_pages(int count);

/* Free one page previously returned by alloc_page. */
void free_page(void *addr);

/* Free 'count' pages previously returned by alloc_pages. */
void free_pages(void *addr, int count);

#endif
