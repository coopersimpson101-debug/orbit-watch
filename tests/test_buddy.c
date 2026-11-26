/* From orbit-os tests/test_buddy.c (MIT), unchanged. */
#include <stdint.h>
#include "test.h"
#include "buddy.h"

#define NPAGES 1024 /* 4 MiB: exactly one order-10 block */

static struct buddy b;
static uint8_t meta[NPAGES];
static void *raw;
static uintptr_t base;

static void setup(void)
{
    if (!raw)
        base = (uintptr_t)aligned_pages(NPAGES, &raw);
    buddy_init(&b, base, NPAGES, meta);
}

static void test_add_and_coalesce(void)
{
    setup();
    CHECK(buddy_free_pages(&b) == 0);
    /* Add in two halves; they must merge into one order-10 block. */
    buddy_add_range(&b, base, NPAGES / 2);
    buddy_add_range(&b, base + (NPAGES / 2) * PAGE_SIZE, NPAGES / 2);
    CHECK(buddy_free_pages(&b) == NPAGES);
    CHECK(b.nfree[10] == 1);
    CHECK(buddy_check(&b) == 0);
}

static void test_split_and_merge(void)
{
    void *p0, *p1, *p2;
    setup();
    buddy_add_range(&b, base, NPAGES);
    p0 = buddy_alloc(&b, 0);
    CHECK(p0 == (void *)base);
    CHECK(buddy_free_pages(&b) == NPAGES - 1);
    /* splitting order 10 down to 0 leaves one free block at orders 0..9 */
    for (unsigned o = 0; o < 10; o++)
        CHECK(b.nfree[o] == 1);
    CHECK(buddy_check(&b) == 0);
    p1 = buddy_alloc(&b, 0);
    CHECK(p1 == (void *)(base + PAGE_SIZE));
    p2 = buddy_alloc(&b, 3);
    CHECK(((uintptr_t)p2 - base) % (8 * PAGE_SIZE) == 0); /* naturally aligned */
    CHECK(buddy_block_order(&b, p2) == 3);
    CHECK(buddy_free(&b, p0) == 0);
    CHECK(buddy_free(&b, p1) == 0);
    CHECK(buddy_free(&b, p2) == 0);
    CHECK(buddy_free_pages(&b) == NPAGES);
    CHECK(b.nfree[10] == 1);
    CHECK(buddy_check(&b) == 0);
}

static void test_errors(void)
{
    void *p;
    setup();
    buddy_add_range(&b, base, NPAGES);
    p = buddy_alloc(&b, 2);
    CHECK(buddy_free(&b, p) == 0);
    CHECK(buddy_free(&b, p) == -1);                        /* double free */
    CHECK(buddy_free(&b, (char *)p + 1) == -1);            /* misaligned */
    CHECK(buddy_free(&b, (void *)(base - PAGE_SIZE)) == -1);/* out of range */
    CHECK(buddy_free(&b, NULL) == -1);
    CHECK(buddy_alloc(&b, BUDDY_ORDERS) == NULL);
    CHECK(buddy_check(&b) == 0);
}

static void test_exhaustion_and_holes(void)
{
    void *pages[NPAGES];
    int n = 0;
    setup();
    /* Leave a reserved hole (e.g. the DTB) of 3 pages at index 100. */
    buddy_add_range(&b, base, 100);
    buddy_add_range(&b, base + 103 * PAGE_SIZE, NPAGES - 103);
    CHECK(buddy_free_pages(&b) == NPAGES - 3);
    CHECK(buddy_check(&b) == 0);
    while ((pages[n] = buddy_alloc(&b, 0)) != NULL)
        n++;
    CHECK(n == NPAGES - 3);
    for (int i = 0; i < n; i++) {
        uintptr_t idx = ((uintptr_t)pages[i] - base) / PAGE_SIZE;
        CHECK(idx < 100 || idx >= 103); /* never hands out the hole */
    }
    for (int i = n - 1; i >= 0; i -= 2)
        CHECK(buddy_free(&b, pages[i]) == 0);
    for (int i = n - 2; i >= 0; i -= 2)
        CHECK(buddy_free(&b, pages[i]) == 0);
    CHECK(buddy_free_pages(&b) == NPAGES - 3);
    CHECK(buddy_check(&b) == 0);
    CHECK(b.nfree[10] == 0); /* the hole prevents a full order-10 merge */
}

static void test_random_stress(void)
{
    struct { void *p; unsigned order; } live[256];
    int nlive = 0;
    uint32_t seed = 12345;
    setup();
    buddy_add_range(&b, base, NPAGES);
    for (int it = 0; it < 20000; it++) {
        seed = seed * 1103515245u + 12345u;
        if ((seed >> 16) % 3 != 0 && nlive < 256) {
            unsigned order = (seed >> 8) % 5;
            void *p = buddy_alloc(&b, order);
            if (p) {
                /* no overlap with any live block */
                for (int j = 0; j < nlive; j++) {
                    uintptr_t a0 = (uintptr_t)p, a1 = a0 + (PAGE_SIZE << order);
                    uintptr_t b0 = (uintptr_t)live[j].p, b1 = b0 + (PAGE_SIZE << live[j].order);
                    CHECK(a1 <= b0 || b1 <= a0);
                }
                live[nlive].p = p;
                live[nlive++].order = order;
            }
        } else if (nlive) {
            int k = (int)((seed >> 4) % (uint32_t)nlive);
            CHECK(buddy_free(&b, live[k].p) == 0);
            live[k] = live[--nlive];
        }
    }
    while (nlive)
        CHECK(buddy_free(&b, live[--nlive].p) == 0);
    CHECK(buddy_free_pages(&b) == NPAGES);
    CHECK(b.nfree[10] == 1);
    CHECK(buddy_check(&b) == 0);
}

static void test_order_for(void)
{
    CHECK(buddy_order_for(1) == 0);
    CHECK(buddy_order_for(4096) == 0);
    CHECK(buddy_order_for(4097) == 1);
    CHECK(buddy_order_for(16 * 1024) == 2);
    CHECK(buddy_order_for(16 * 1024 + 1) == 3);
}

int main(void)
{
    RUN(test_add_and_coalesce);
    RUN(test_split_and_merge);
    RUN(test_errors);
    RUN(test_exhaustion_and_holes);
    RUN(test_random_stress);
    RUN(test_order_for);
    free(raw);
    TEST_REPORT("test_buddy");
}
