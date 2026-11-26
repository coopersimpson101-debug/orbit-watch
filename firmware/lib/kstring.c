/* Derived from orbit-os (github.com/coopersimpson101-debug/orbit-os, MIT), kernel/lib/kstring.c.
 * Changes for orbit-watch: none (verbatim) */
/* Byte-at-a-time string routines. Deliberately simple: the kernel builds
 * with -mstrict-align and runs part of early boot with the MMU off, where
 * unaligned accesses to (implicitly Device) memory would fault. */
#include "kstring.h"

void *k_memset(void *dst, int c, size_t n)
{
    unsigned char *d = dst;
    while (n--)
        *d++ = (unsigned char)c;
    return dst;
}

void *k_memcpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--)
        *d++ = *s++;
    return dst;
}

void *k_memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    if (d == s || n == 0)
        return dst;
    if (d < s) {
        while (n--)
            *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--)
            *--d = *--s;
    }
    return dst;
}

int k_memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = a, *y = b;
    for (; n; n--, x++, y++)
        if (*x != *y)
            return *x < *y ? -1 : 1;
    return 0;
}

size_t k_strlen(const char *s)
{
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}

size_t k_strnlen(const char *s, size_t max)
{
    size_t n = 0;
    while (n < max && s[n])
        n++;
    return n;
}

int k_strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int k_strncmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        if (*a != *b)
            return (unsigned char)*a - (unsigned char)*b;
        if (!*a)
            return 0;
    }
    return 0;
}

size_t k_strlcpy(char *dst, const char *src, size_t size)
{
    size_t len = k_strlen(src);
    if (size) {
        size_t n = len < size - 1 ? len : size - 1;
        k_memcpy(dst, src, n);
        dst[n] = 0;
    }
    return len;
}

const char *k_strchr(const char *s, int c)
{
    for (;; s++) {
        if (*s == (char)c)
            return s;
        if (!*s)
            return 0;
    }
}
