/* From orbit-os tests/test_kheap.c (MIT), unchanged. */
/* kmalloc/kfree on top of the real buddy allocator. */
#include <stdint.h>
#include "test.h"
#include "buddy.h"
#include "kheap.h"

#define NPAGES 512

static struct buddy b;
static uint8_t meta[NPAGES];
static struct kheap h;

static void *src_alloc(void *ctx, unsigned order) { return buddy_alloc(ctx, order); }
static void src_free(void *ctx, void *p) { buddy_free(ctx, p); }

static void test_small_classes(void)
{
    void *p[7];
    size_t sizes[7] = { 1, 16, 17, 100, 256, 700, 1024 };
    size_t expect[7] = { 16, 16, 32, 128, 256, 1024, 1024 };
    for (int i = 0; i < 7; i++) {
        p[i] = kheap_alloc(&h, sizes[i]);
        CHECK(p[i] != NULL);
        CHECK(((uintptr_t)p[i] & 15) == 0);
        CHECK(kheap_usable_size(&h, p[i]) == expect[i]);
        memset(p[i], 0xa5, sizes[i]);
    }
    for (int i = 0; i < 7; i++)
        CHECK(kheap_free(&h, p[i]) == 0);
    CHECK(h.st.bytes_in_use == 0);
}

static void test_reuse_and_distinct(void)
{
    void *a = kheap_alloc(&h, 64), *b2 = kheap_alloc(&h, 64);
    CHECK(a && b2 && a != b2);
    CHECK(kheap_free(&h, a) == 0);
    CHECK(kheap_alloc(&h, 64) == a); /* LIFO free list reuses the slot */
    kheap_free(&h, a);
    kheap_free(&h, b2);
}

static void test_large(void)
{
    size_t before = buddy_free_pages(&b);
    char *p = kheap_alloc(&h, 20000); /* > 1 KiB: direct pages, order 3 */
    CHECK(p != NULL);
    CHECK(kheap_usable_size(&h, p) >= 20000);
    memset(p, 0x5a, 20000);
    CHECK(buddy_free_pages(&b) == before - 8);
    CHECK(kheap_free(&h, p) == 0);
    CHECK(buddy_free_pages(&b) == before);
    CHECK(kheap_free(&h, p) == -1); /* double free of a large block */
}

static void test_invalid_free(void)
{
    char *p = kheap_alloc(&h, 32);
    CHECK(kheap_free(&h, p + 8) == -1);  /* not an object boundary */
    CHECK(kheap_free(&h, NULL) == 0);
    CHECK(kheap_free(&h, p) == 0);
    CHECK(kheap_alloc(&h, 0) == NULL);
}

static void test_many_no_overlap(void)
{
    enum { N = 600 };
    unsigned char *p[N];
    size_t sz[N];
    for (int i = 0; i < N; i++) {
        sz[i] = (size_t)(i * 37 % 1500) + 1;
        p[i] = kheap_alloc(&h, sz[i]);
        CHECK(p[i] != NULL);
        memset(p[i], (unsigned char)i, sz[i]);
    }
    for (int i = 0; i < N; i++) {
        int ok = 1;
        for (size_t k = 0; k < sz[i]; k++)
            if (p[i][k] != (unsigned char)i)
                ok = 0;
        CHECK(ok); /* nobody overwrote this allocation */
    }
    for (int i = 0; i < N; i += 2)
        CHECK(kheap_free(&h, p[i]) == 0);
    for (int i = 1; i < N; i += 2)
        CHECK(kheap_free(&h, p[i]) == 0);
    CHECK(h.st.bytes_in_use == 0);
    CHECK(h.st.allocs >= N && h.st.frees >= N);
}

static void test_oom(void)
{
    /* Exhaust the page source with large allocations, then recover. */
    void *blocks[64];
    int n = 0;
    while (n < 64 && (blocks[n] = kheap_alloc(&h, 200000)) != NULL)
        n++;
    CHECK(n > 0 && n < 64);
    CHECK(h.st.failed > 0);
    while (n)
        kheap_free(&h, blocks[--n]);
}

int main(void)
{
    void *raw;
    uintptr_t base = (uintptr_t)aligned_pages(NPAGES, &raw);
    struct kheap_page_source src = { src_alloc, src_free, &b };
    buddy_init(&b, base, NPAGES, meta);
    buddy_add_range(&b, base, NPAGES);
    kheap_init(&h, &src);

    RUN(test_small_classes);
    RUN(test_reuse_and_distinct);
    RUN(test_large);
    RUN(test_invalid_free);
    RUN(test_many_no_overlap);
    RUN(test_oom);
    CHECK(buddy_check(&b) == 0);
    free(raw);
    TEST_REPORT("test_kheap");
}
