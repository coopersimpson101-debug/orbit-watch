/* Derived from orbit-os (github.com/coopersimpson101-debug/orbit-os, MIT), kernel/include/kprintf.h.
 * Changes for orbit-watch: none (verbatim) */
/* Minimal printf engine. Supports: %d %i %u %x %X %p %s %c %%,
 * length modifiers l, ll, z, flags '-' and '0', and a field width.
 * Output is always NUL-terminated when size > 0; the return value is the
 * length the full output would have had (C99 snprintf semantics). */
#ifndef ORBIT_KPRINTF_H
#define ORBIT_KPRINTF_H

#include <stdarg.h>
#include <stddef.h>

int k_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int k_snprintf(char *buf, size_t size, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

#endif
