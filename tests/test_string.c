/* From orbit-os tests/test_string.c (MIT), unchanged. */
#include "test.h"
#include "kstring.h"

static void test_mem(void)
{
    char a[32], b[32];
    k_memset(a, 'x', sizeof(a));
    CHECK(a[0] == 'x' && a[31] == 'x');
    k_memcpy(b, "hello world", 12);
    CHECK_STR(b, "hello world");
    CHECK(k_memcmp("abc", "abd", 3) < 0);
    CHECK(k_memcmp("abd", "abc", 3) > 0);
    CHECK(k_memcmp("abc", "abc", 3) == 0);
    /* overlapping moves both ways */
    k_memcpy(a, "0123456789", 11);
    k_memmove(a + 2, a, 8);
    CHECK(memcmp(a, "0101234567", 10) == 0);
    k_memcpy(a, "0123456789", 11);
    k_memmove(a, a + 3, 7);
    CHECK(memcmp(a, "3456789", 7) == 0);
}

static void test_str(void)
{
    char buf[8];
    CHECK(k_strlen("") == 0);
    CHECK(k_strlen("orbit") == 5);
    CHECK(k_strnlen("orbit", 3) == 3);
    CHECK(k_strcmp("a", "b") < 0 && k_strcmp("b", "a") > 0 && k_strcmp("x", "x") == 0);
    CHECK(k_strncmp("help", "hello", 3) == 0);
    CHECK(k_strncmp("help", "hello", 4) != 0);
    CHECK(k_strlcpy(buf, "truncate-me", sizeof(buf)) == 11);
    CHECK_STR(buf, "truncat");
    CHECK(k_strchr("a:b", ':') != 0 && *k_strchr("a:b", ':') == ':');
    CHECK(k_strchr("abc", 'z') == 0);
}

int main(void)
{
    RUN(test_mem);
    RUN(test_str);
    TEST_REPORT("test_string");
}
