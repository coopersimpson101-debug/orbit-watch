/* orbit-watch kernel entry. */
#include "board.h"
#include "buddy.h"
#include "cm33.h"
#include "kernel.h"
#include "kheap.h"
#include "sched.h"

struct buddy kernel_pages;
struct kheap kernel_heap;

extern char __heap_start[], __heap_end[];
static uint8_t page_meta[512];

static void *pages_alloc(void *ctx, unsigned order) { return buddy_alloc(ctx, order); }
static void pages_free(void *ctx, void *p) { buddy_free(ctx, p); }

void *kmalloc(size_t n)
{
    uint32_t f = irq_save();
    void *p = kheap_alloc(&kernel_heap, n);
    irq_restore(f);
    return p;
}

void kfree(void *p)
{
    uint32_t f = irq_save();
    kheap_free(&kernel_heap, p);
    irq_restore(f);
}

static void mm_init(void)
{
    uintptr_t start = ((uintptr_t)__heap_start + PAGE_SIZE - 1) & ~(uintptr_t)(PAGE_SIZE - 1);
    uintptr_t end = (uintptr_t)__heap_end & ~(uintptr_t)(PAGE_SIZE - 1);
    size_t npages = (end - start) / PAGE_SIZE;
    struct kheap_page_source src = { pages_alloc, pages_free, &kernel_pages };
    if (npages > sizeof(page_meta))
        npages = sizeof(page_meta);
    buddy_init(&kernel_pages, start, npages, page_meta);
    buddy_add_range(&kernel_pages, start, npages);
    kheap_init(&kernel_heap, &src);
    kprintf("mm: %u KiB in %u x %u B pages at %p, buddy orders 0..%u\n",
            (unsigned)(npages * PAGE_SIZE / 1024), (unsigned)npages, (unsigned)PAGE_SIZE,
            (void *)start, BUDDY_ORDERS - 1);
}

int main(void)
{
    board_early_init();
    kprintf("\norbit-watch %s on %s (Cortex-M33)\n", OW_VERSION, board.name);
    kprintf("clocks: cpu %u Hz, lp counter %u Hz / %u bit, tick %u Hz\n", board.cpu_hz,
            board.lp_hz, board.lp_bits, OW_HZ);
    mm_init();
    sched_init();
    board_init();
    idle_init();
    thread_create("idle", idle_thread, 0, PRIO_IDLE, 1024);
    console_set_reader(thread_create("shell", shell_thread, 0, 3, 3072));
    watch_start();
    kprintf("sched: %d threads, starting\n", sched_thread_count());
    sched_start();
}
