/* From orbit-os tests/test.h (MIT), unchanged. */
/* Tiny host-side test harness: CHECK() records failures, TEST_MAIN runs. */
#ifndef ORBIT_TEST_H
#define ORBIT_TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int t_failures, t_checks;

#define CHECK(cond) do {                                                   \
    t_checks++;                                                            \
    if (!(cond)) {                                                         \
        t_failures++;                                                      \
        fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                      \
} while (0)

#define CHECK_STR(a, b) do {                                               \
    const char *_a = (a), *_b = (b);                                       \
    t_checks++;                                                            \
    if (strcmp(_a, _b) != 0) {                                             \
        t_failures++;                                                      \
        fprintf(stderr, "%s:%d: expected \"%s\", got \"%s\"\n", __FILE__,  \
                __LINE__, _b, _a);                                         \
    }                                                                      \
} while (0)

#define RUN(fn) do { fn(); } while (0)

#define TEST_REPORT(name) do {                                             \
    printf("%s: %d checks, %d failures\n", name, t_checks, t_failures);    \
    return t_failures ? 1 : 0;                                             \
} while (0)

/* 4 KiB-aligned scratch memory for allocator tests. */
static __attribute__((unused)) void *aligned_pages(size_t npages, void **raw)
{
    char *p = malloc((npages + 1) * 4096);
    *raw = p;
    return (void *)(((unsigned long long)(size_t)p + 4095) & ~(unsigned long long)4095);
}

#endif
