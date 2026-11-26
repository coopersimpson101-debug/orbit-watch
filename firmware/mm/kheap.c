/* Derived from orbit-os (github.com/coopersimpson101-debug/orbit-os, MIT), kernel/mm/kheap.c.
 * Changes for orbit-watch: page size is overridable (1 KiB on the MCU) */
#include "kheap.h"

#ifndef KHEAP_PAGE_SIZE
#define KHEAP_PAGE_SIZE 4096UL
#endif
#define KPAGE_SIZE ((uintptr_t)KHEAP_PAGE_SIZE)
#define SLAB_MAGIC  0x534c4142u /* 'SLAB' */
#define LARGE_MAGIC 0x4c524745u /* 'LRGE' */
#define HDR_SIZE 64             /* keeps objects 16-byte (and up to 64) aligned */

struct page_hdr {
    uint32_t magic;
    uint16_t cls;    /* slab: size class index */
    uint16_t order;  /* large: buddy order */
    uint32_t inuse;  /* slab: live objects in this page */
    uint32_t pad;
};

static inline struct page_hdr *hdr_of(const void *p)
{
    return (struct page_hdr *)((uintptr_t)p & ~(uintptr_t)(KPAGE_SIZE - 1));
}

static inline size_t class_size(unsigned c)
{
    return (size_t)1 << (c + KHEAP_MIN_SHIFT);
}

void kheap_init(struct kheap *h, const struct kheap_page_source *src)
{
    unsigned i;
    h->src = *src;
    for (i = 0; i < KHEAP_NCLASSES; i++)
        h->free[i] = 0;
    h->st.bytes_in_use = h->st.slab_pages = h->st.large_pages = 0;
    h->st.allocs = h->st.frees = h->st.failed = 0;
}

static int refill(struct kheap *h, unsigned c)
{
    struct page_hdr *pg = h->src.alloc_pages(h->src.ctx, 0);
    size_t sz = class_size(c), off;
    if (!pg)
        return -1;
    pg->magic = SLAB_MAGIC;
    pg->cls = (uint16_t)c;
    pg->order = 0;
    pg->inuse = 0;
    pg->pad = 0;
    /* Push objects in reverse so allocation walks the page forward. */
    for (off = KPAGE_SIZE - sz; off >= HDR_SIZE; off -= sz) {
        struct kheap_free_obj *o = (struct kheap_free_obj *)((char *)pg + off);
        o->next = h->free[c];
        h->free[c] = o;
        if (off < sz)
            break;
    }
    h->st.slab_pages++;
    return 0;
}

void *kheap_alloc(struct kheap *h, size_t size)
{
    unsigned c;
    if (size == 0)
        return 0;
    for (c = 0; c < KHEAP_NCLASSES; c++)
        if (size <= class_size(c))
            break;
    if (c < KHEAP_NCLASSES) {
        struct kheap_free_obj *o;
        if (!h->free[c] && refill(h, c)) {
            h->st.failed++;
            return 0;
        }
        o = h->free[c];
        h->free[c] = o->next;
        hdr_of(o)->inuse++;
        h->st.bytes_in_use += class_size(c);
        h->st.allocs++;
        return o;
    }
    {
        size_t total = size + HDR_SIZE;
        unsigned order = 0;
        struct page_hdr *pg;
        if (total < size) { /* overflow */
            h->st.failed++;
            return 0;
        }
        while ((KPAGE_SIZE << order) < total) {
            order++;
            if (order > 20) {
                h->st.failed++;
                return 0;
            }
        }
        pg = h->src.alloc_pages(h->src.ctx, order);
        if (!pg) {
            h->st.failed++;
            return 0;
        }
        pg->magic = LARGE_MAGIC;
        pg->cls = 0xffff;
        pg->order = (uint16_t)order;
        pg->inuse = 1;
        pg->pad = 0;
        h->st.large_pages += (size_t)1 << order;
        h->st.bytes_in_use += (KPAGE_SIZE << order) - HDR_SIZE;
        h->st.allocs++;
        return (char *)pg + HDR_SIZE;
    }
}

int kheap_free(struct kheap *h, void *ptr)
{
    struct page_hdr *pg;
    if (!ptr)
        return 0;
    pg = hdr_of(ptr);
    if (pg->magic == SLAB_MAGIC && pg->cls < KHEAP_NCLASSES) {
        size_t off = (uintptr_t)ptr - (uintptr_t)pg;
        size_t sz = class_size(pg->cls);
        struct kheap_free_obj *o = ptr;
        /* Objects are laid out from the end of the page downward. */
        if (off < HDR_SIZE || (KPAGE_SIZE - off) % sz != 0 || pg->inuse == 0)
            return -1;
        o->next = h->free[pg->cls];
        h->free[pg->cls] = o;
        pg->inuse--;
        h->st.bytes_in_use -= sz;
        h->st.frees++;
        return 0;
    }
    if (pg->magic == LARGE_MAGIC && (uintptr_t)ptr - (uintptr_t)pg == HDR_SIZE &&
        pg->inuse == 1) {
        unsigned order = pg->order;
        pg->magic = 0;
        pg->inuse = 0;
        h->st.large_pages -= (size_t)1 << order;
        h->st.bytes_in_use -= (KPAGE_SIZE << order) - HDR_SIZE;
        h->st.frees++;
        h->src.free_pages(h->src.ctx, pg);
        return 0;
    }
    return -1;
}

size_t kheap_usable_size(const struct kheap *h, const void *ptr)
{
    const struct page_hdr *pg = hdr_of(ptr);
    (void)h;
    if (pg->magic == SLAB_MAGIC && pg->cls < KHEAP_NCLASSES)
        return class_size(pg->cls);
    if (pg->magic == LARGE_MAGIC)
        return (KPAGE_SIZE << pg->order) - HDR_SIZE;
    return 0;
}
