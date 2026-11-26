/* From orbit-os tests/test_printf.c (MIT), unchanged. */
/* k_snprintf vs. the host C library on a matrix of formats. */
#include <stdint.h>
#include "test.h"
#include "kprintf.h"

#define SAME(fmt, ...) do {                                                \
    char a[128], b[128];                                                   \
    int ra = k_snprintf(a, sizeof(a), fmt, __VA_ARGS__);                   \
    int rb = snprintf(b, sizeof(b), fmt, __VA_ARGS__);                     \
    CHECK_STR(a, b);                                                       \
    CHECK(ra == rb);                                                       \
} while (0)

static void test_matches_libc(void)
{
    SAME("%d", 0);
    SAME("%d", -42);
    SAME("%i|%u", 2147483647, 4294967295u);
    SAME("%x %X", 0xdeadbeefu, 0xcafeu);
    SAME("[%5d] [%-5d] [%05d]", 42, 42, 42);
    SAME("[%05d]", -42);
    SAME("[%8x] [%08x] [%-8x]", 0xabcu, 0xabcu, 0xabcu);
    SAME("%s and %s", "alpha", "beta");
    SAME("[%10s] [%-10s]", "pad", "pad");
    SAME("%c%c%c", 'o', 'k', '!');
    SAME("100%% %s", "done");
    SAME("%lld %llu %llx", (long long)INT64_MIN, (unsigned long long)UINT64_MAX,
         (unsigned long long)0x0123456789abcdefULL);
    SAME("%zu", (size_t)123456789);
}

static void test_64bit_l_modifier(void)
{
    /* The kernel is LP64, so %ld/%lx always consume 64 bits in k_snprintf. */
    /* Call through a pointer: on LLP64 hosts (Windows) the compiler's
     * format checker would rightly object to %lx with a 64-bit argument. */
    int (*fmt)(char *, size_t, const char *, ...) = k_snprintf;
    char buf[64];
    fmt(buf, sizeof(buf), "%lx %ld", (uint64_t)0xffff000000000000ULL, (int64_t)-5);
    CHECK_STR(buf, "ffff000000000000 -5");
}

static void test_pointer_and_null(void)
{
    char buf[64];
    k_snprintf(buf, sizeof(buf), "%p", (void *)(uintptr_t)0x40080000);
    CHECK_STR(buf, "0x40080000");
    k_snprintf(buf, sizeof(buf), "%s", (const char *)0);
    CHECK_STR(buf, "(null)");
}

static void test_truncation(void)
{
    char buf[8];
    int n = k_snprintf(buf, sizeof(buf), "%s", "0123456789");
    CHECK(n == 10);
    CHECK_STR(buf, "0123456");
    n = k_snprintf(buf, 1, "abc");
    CHECK(n == 3);
    CHECK(buf[0] == 0);
    n = k_snprintf(0, 0, "%d", 12345);
    CHECK(n == 5);
}

static void test_unknown_conversion(void)
{
    char buf[16];
    k_snprintf(buf, sizeof(buf), "%q%");
    CHECK_STR(buf, "%q");
}

int main(void)
{
    RUN(test_matches_libc);
    RUN(test_64bit_l_modifier);
    RUN(test_pointer_and_null);
    RUN(test_truncation);
    RUN(test_unknown_conversion);
    TEST_REPORT("test_printf");
}
