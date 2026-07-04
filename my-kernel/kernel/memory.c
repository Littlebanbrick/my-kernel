// memory.c — Physical memory buddy allocator (32-bit)
//
// Tracks free / allocated physical pages via the classic buddy algorithm.
//
//   free_lists[o]   — singly-linked list of free blocks of order o
//   page_order[p]   — state of page p  (per-page byte, 32 KB total)
//
//   page_order[] encoding (one byte per page):
//     0xFF            = ORDER_RESERVED  (not available for allocation)
//     0x80 | o        = part of a FREE block of order o (0 <= o <= 15)
//     o               = part of an ALLOCATED block of order o
//
//   A free block's first 4 bytes serve as the 'next' pointer for the
//   free list — no separate memory is needed for bookkeeping.
//
//   For merging (coalescing) we only ever read page_order[base_page]
//   where base_page = (block address) / PAGE_SIZE.  Non-base pages of a
//   block carry stale values that are never consulted.

#include "memory.h"
#include "printf.h"

/* ----------------------------------------------------------------- */
/*  Constants & page-order encoding                                   */
/* ----------------------------------------------------------------- */

#define ORDER_RESERVED  0xFFu
#define FREE_FLAG       0x80u
#define ORDER_MASK      0x0Fu

/* Helpers for the per-page encoding */
static inline u8  blk_order(u8 v)     { return v & ORDER_MASK; }
static inline int is_free_blk(u8 v)   { return v & FREE_FLAG;  }
static inline u8  make_free(u8 o)     { return FREE_FLAG | (o & ORDER_MASK); }

/* ----------------------------------------------------------------- */
/*  Free-list node  (lives inside each free page's first 4 bytes)    */
/* ----------------------------------------------------------------- */

struct free_block {
    struct free_block *next;
};

/* ----------------------------------------------------------------- */
/*  Core data                                                        */
/* ----------------------------------------------------------------- */

/* One entry per order [0 .. MAX_ORDER].  free_lists[o] heads a
 * singly-linked list of free blocks whose size is 2^o pages. */
static struct free_block *free_lists[MAX_ORDER + 1];

/* One byte per physical page — records the block's order + free/alloc. */
static u8 page_order[TOTAL_PAGES];

/* ----------------------------------------------------------------- */
/*  Helpers                                                          */
/* ----------------------------------------------------------------- */

/* Smallest order such that 2^order >= count */
static inline int order_for_pages(int count)
{
    int order = 0;
    int size  = 1;          /* 1 << order */

    while (size < count && order < MAX_ORDER) {
        size <<= 1;
        order++;
    }
    return order;
}

/* ----------------------------------------------------------------- */
/*  Mark a physical address range as reserved                         */
/*  (inclusive of start, exclusive of end).                           */
/* ----------------------------------------------------------------- */

static void mark_reserved(u32 start, u32 end)
{
    int first = start / PAGE_SIZE;
    int last  = (end + PAGE_SIZE - 1) / PAGE_SIZE;
    int i;

    for (i = first; i < last; i++)
        page_order[i] = ORDER_RESERVED;
}

/* ----------------------------------------------------------------- */
/*  Initialisation — scan free memory and form power-of-2 blocks     */
/* ----------------------------------------------------------------- */

void bitmap_init(void)
{
    extern u32 _end;
    int i;

    /* 1. Everything starts as "unscanned free" (value 0).  This is a
     *    transient state that will be consumed by the scan below. */
    for (i = 0; i < TOTAL_PAGES; i++)
        page_order[i] = 0;

    /* 2. Mark known-reserved regions (same as the old bitmap). */
    mark_reserved(0x000000, 0x007000);      /* IVT + BDA + stack buffer */
    mark_reserved(0x007000, 0x008000);      /* stack + boot sector      */
    mark_reserved(0x008000, (u32)&_end);    /* kernel image             */
    mark_reserved(0x0A0000, 0x0C0000);      /* VGA video + text + ROMs  */
    mark_reserved(0x0C0000, 0x100000);      /* option ROMs / BIOS       */

    /* 3. Clear free lists. */
    for (i = 0; i <= MAX_ORDER; i++)
        free_lists[i] = NULL;

    /* 4. Scan all pages.  Consecutive free runs are broken into
     *    power-of-2 blocks, each aligned to its own size. */
    {
        int page = 0;

        while (page < TOTAL_PAGES) {
            /* Skip reserved pages */
            if (page_order[page] == ORDER_RESERVED) {
                page++;
                continue;
            }

            /* Find the end of this free run */
            int start = page;
            int end   = start + 1;

            while (end < TOTAL_PAGES && page_order[end] != ORDER_RESERVED)
                end++;

            /* Break the run [start, end) into 2^o blocks. */
            int remaining = end - start;
            int p         = start;

            while (remaining > 0) {
                /* Largest order that fits in 'remaining' pages */
                int order = 0;
                int sz    = 1;

                while (sz * 2 <= remaining && order < MAX_ORDER) {
                    sz <<= 1;
                    order++;
                }

                /* Constrain by alignment: p must be a multiple of sz */
                while ((p & (sz - 1)) != 0) {
                    sz >>= 1;
                    order--;
                }

                /* Add block to the free list */
                page_order[p] = make_free(order);
                {
                    struct free_block *blk;
                    blk = (struct free_block *)(p * PAGE_SIZE);
                    blk->next    = free_lists[order];
                    free_lists[order] = blk;
                }

                p += sz;
                remaining -= sz;
            }

            page = end;
        }
    }

    /* Print a quick summary */
    printf("buddy: %d order levels, %d pages total\n",
           MAX_ORDER + 1, TOTAL_PAGES);
    printf("kernel  _end = %x\n", (u32)&_end);
}

/* ----------------------------------------------------------------- */
/*  Allocation                                                        */
/* ----------------------------------------------------------------- */

void *alloc_pages(int count)
{
    int order, i, page, o;
    u32 addr;

    if (count <= 0)
        return NULL;

    order = order_for_pages(count);

    /* 1. Find the smallest free list with a block */
    for (i = order; i <= MAX_ORDER; i++) {
        if (free_lists[i] != NULL)
            break;
    }
    if (i > MAX_ORDER) {       /* OOM */
        printf("OOM: alloc_pages(%d)  (no block at order %d or above)\n",
               count, order);
        return NULL;
    }

    /* 2. Take the first block from free_lists[i] */
    {
        struct free_block *blk = free_lists[i];
        free_lists[i] = blk->next;
        addr = (u32)blk;
    }
    page = addr / PAGE_SIZE;

    /* 3. Split the block down to the target order.
     *
     *    At each level we split block A into two buddies:
     *      - A (stays as the block we keep working on)
     *      - B = A + 2^(i-1) pages  (goes to free_lists[i-1])        */
    for (o = i; o > order; o--) {
        int sz_pages   = 1 << (o - 1);          /* size of each half   */
        u32 buddy_addr = addr + sz_pages * PAGE_SIZE;
        int buddy_page = buddy_addr / PAGE_SIZE;

        /* The buddy half goes to the lower free list */
        {
            struct free_block *buddy;
            buddy = (struct free_block *)buddy_addr;
            buddy->next       = free_lists[o - 1];
            free_lists[o - 1] = buddy;
        }
        page_order[buddy_page] = make_free(o - 1);
    }

    /* 4. Mark the allocated block and return */
    for (o = 0; o < (1 << order); o++)
        page_order[page + o] = (u8)order;

    return (void *)addr;
}

void *alloc_page(void)
{
    return alloc_pages(1);
}

/* ----------------------------------------------------------------- */
/*  Free                                                              */
/* ----------------------------------------------------------------- */

/* Remove a block at 'buddy_addr' from free_lists[o].  Returns 1 on
 * success, 0 if the block was not found (shouldn't happen).         */
static int remove_from_list(int o, u32 buddy_addr)
{
    struct free_block **pp = &free_lists[o];

    while (*pp) {
        if ((u32)(*pp) == buddy_addr) {
            *pp = (*pp)->next;
            return 1;
        }
        pp = &(*pp)->next;
    }
    return 0;       /* not found — should never happen */
}

void free_pages(void *addr, int count)
{
    int page, order, o;

    (void)count;        /* buddy knows the block size from page_order[] */

    if (!addr)
        return;

    page  = (u32)addr / PAGE_SIZE;
    order = page_order[page];       /* the block's order (0..MAX_ORDER) */

    /* Try to coalesce upwards with the buddy at each level. */
    for (o = order; o < MAX_ORDER; o++) {
        int buddy_page;

        /* Compute the buddy's base page index */
        buddy_page = page ^ (1 << o);

        /* If the buddy is not a FREE block of the same order, stop. */
        if (page_order[buddy_page] != make_free(o))
            break;

        /* Remove the buddy from its free list. */
        remove_from_list(o, buddy_page * PAGE_SIZE);

        /* Merge: the new block starts at the lower address */
        if (buddy_page < page)
            page = buddy_page;
    }

    /* 'page' is now the base of the merged block, 'order' is final. */
    {
        struct free_block *blk;
        blk = (struct free_block *)(page * PAGE_SIZE);
        blk->next         = free_lists[order];
        free_lists[order] = blk;
    }
    page_order[page] = make_free(order);
}

void free_page(void *addr)
{
    free_pages(addr, 1);
}
