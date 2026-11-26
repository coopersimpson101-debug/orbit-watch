/* Derived from orbit-os (github.com/coopersimpson101-debug/orbit-os, MIT), kernel/include/kheap.h.
 * Changes for orbit-watch: size-class count is overridable */
/* Kernel heap: power-of-two size-class slabs (16 B .. 1 KiB) carved out of
 * single pages, plus direct multi-page allocations for anything larger.
 * Pages come from a pluggable page source (the buddy allocator in the
 * kernel, a host buffer in unit tests). Every page handed out by the heap
 * starts with a small header, so kheap_free() only needs the pointer. */
#ifndef ORBIT_KHEAP_H
#define ORBIT_KHEAP_H

#include <stddef.h>
#include <stdint.h>

#ifndef KHEAP_NCLASSES
#define KHEAP_NCLASSES 7 /* 16,32,64,128,256,512,1024 (6 with 1 KiB pages) */
#endif
#define KHEAP_MIN_SHIFT 4

struct kheap_page_source {
    void *(*alloc_pages)(void *ctx, unsigned order);
    void (*free_pages)(void *ctx, void *pages);
    void *ctx;
};

struct kheap_free_obj {
    struct kheap_free_obj *next;
};

struct kheap_stats {
    size_t bytes_in_use;  /* requested-size rounded to class/pages */
    size_t slab_pages;
    size_t large_pages;
    size_t allocs;
    size_t frees;
    size_t failed;
};

struct kheap {
    struct kheap_page_source src;
    struct kheap_free_obj *free[KHEAP_NCLASSES];
    struct kheap_stats st;
};

void   kheap_init(struct kheap *h, const struct kheap_page_source *src);
void  *kheap_alloc(struct kheap *h, size_t size);
/* Returns 0 on success, -1 if ptr was not produced by this heap. */
int    kheap_free(struct kheap *h, void *ptr);
/* Usable size of an allocation (>= requested), or 0 if invalid. */
size_t kheap_usable_size(const struct kheap *h, const void *ptr);

#endif
