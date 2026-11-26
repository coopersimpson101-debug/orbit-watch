/* The orbit-os buddy allocator + kheap in the MCU configuration used by the
 * firmware: 1 KiB pages, 8 orders (1..128 KiB), 6 slab classes (16..512 B). */
#include <stdint.h>
#include "test.h"
#include "buddy.h"
#include "kheap.h"

static struct buddy b;
static void *src_alloc(void *ctx, unsigned order) { return buddy_alloc(ctx, order); }
static void src_free(void *ctx, void *p) { buddy_free(ctx, p); }

int main(void)
{
    static uint8_t meta[128];
    void *raw;
    uintptr_t base;
    struct kheap h;
    struct kheap_page_source src = { src_alloc, src_free, &b };
    void *stack, *p[64];
    int i;
    char *big = malloc(129 * 1024);
    raw = big;
    base = ((uintptr_t)big + 1023) & ~(uintptr_t)1023;
    CHECK(PAGE_SIZE == 1024 && BUDDY_ORDERS == 8);
    buddy_init(&b, base, 128, meta);
    buddy_add_range(&b, base, 128);
    CHECK(buddy_free_pages(&b) == 128);
    CHECK(b.nfree[7] == 1); /* one 128 KiB block */
    CHECK(buddy_check(&b) == 0);
    /* a 2 KiB thread stack */
    stack = buddy_alloc(&b, buddy_order_for(2048));
    CHECK(stack && buddy_block_order(&b, stack) == 1);
    kheap_init(&h, &src);
    for (i = 0; i < 64; i++) {
        p[i] = kheap_alloc(&h, (size_t)(16 << (i % 6)));
        CHECK(p[i] != NULL);
        memset(p[i], 0xa5, (size_t)(16 << (i % 6)));
    }
    CHECK(kheap_usable_size(&h, p[5]) == 512);
    CHECK(kheap_alloc(&h, 600) != NULL);  /* > 512: whole pages */
    for (i = 0; i < 64; i++)
        CHECK(kheap_free(&h, p[i]) == 0);
    CHECK(buddy_free(&b, stack) == 0);
    CHECK(buddy_check(&b) == 0);
    free(raw);
    TEST_REPORT("test_mm_mcu");
}
