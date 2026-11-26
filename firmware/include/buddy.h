/* Derived from orbit-os (github.com/coopersimpson101-debug/orbit-os, MIT), kernel/include/buddy.h.
 * Changes for orbit-watch: page size and order count are now overridable at compile time */
/* Binary buddy page-frame allocator.
 *
 * Manages a contiguous span of `npages` 4 KiB pages starting at `base`.
 * One metadata byte per page (supplied by the caller) records whether the
 * page heads a free block or an allocated block and that block's order.
 * Free-list links live inside the free pages themselves, so the span must
 * be writable memory (identity mapped in the kernel, a host buffer in tests).
 *
 * After buddy_init() every page is considered reserved; call
 * buddy_add_range() for each usable sub-range. That makes punching holes
 * for the DTB, firmware reservations, etc. trivial. */
#ifndef ORBIT_BUDDY_H
#define ORBIT_BUDDY_H

#include <stddef.h>
#include <stdint.h>
#include "list.h"

#ifndef BUDDY_PAGE_SHIFT
#define BUDDY_PAGE_SHIFT 12 /* orbit-os default; orbit-watch firmware builds with 10 */
#endif
#ifndef BUDDY_ORDERS
#define BUDDY_ORDERS 11 /* orders 0..10: 4 KiB .. 4 MiB at the default page size */
#endif
#define PAGE_SHIFT BUDDY_PAGE_SHIFT
#define PAGE_SIZE  (1UL << PAGE_SHIFT)

struct buddy {
    uintptr_t base;
    size_t npages;
    uint8_t *meta;
    struct list_node free[BUDDY_ORDERS];
    size_t nfree[BUDDY_ORDERS];
    size_t free_pages;
    size_t managed_pages; /* pages ever added via buddy_add_range */
};

void   buddy_init(struct buddy *b, uintptr_t base, size_t npages, uint8_t *meta);
void   buddy_add_range(struct buddy *b, uintptr_t addr, size_t npages);
void  *buddy_alloc(struct buddy *b, unsigned order);
/* Returns 0 on success, -1 on invalid pointer / double free. */
int    buddy_free(struct buddy *b, void *ptr);
/* Order of an allocated block, or -1. */
int    buddy_block_order(const struct buddy *b, const void *ptr);
size_t buddy_free_pages(const struct buddy *b);
/* Size-to-order helper: smallest order whose block holds `bytes`. */
unsigned buddy_order_for(size_t bytes);
/* Consistency check used by tests: returns 0 if free lists and metadata
 * agree and the free page count matches. */
int    buddy_check(const struct buddy *b);

#endif
