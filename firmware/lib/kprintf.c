/* Derived from orbit-os (github.com/coopersimpson101-debug/orbit-os, MIT), kernel/lib/kprintf.c.
 * Changes for orbit-watch: none (verbatim) */
#include <stdint.h>
#include "kprintf.h"

struct out {
    char *buf;
    size_t size;
    size_t pos;
};

static void emit(struct out *o, char c)
{
    if (o->pos + 1 < o->size)
        o->buf[o->pos] = c;
    o->pos++;
}

static void emit_padded(struct out *o, const char *s, size_t len, int width,
                        int left, char pad)
{
    int padn = width > (int)len ? width - (int)len : 0;
    /* Zero padding goes after a leading sign. */
    if (!left && pad == '0' && len && (s[0] == '-')) {
        emit(o, '-');
        s++;
        len--;
    }
    if (!left)
        while (padn-- > 0)
            emit(o, pad);
    while (len--)
        emit(o, *s++);
    if (left)
        while (padn-- > 0)
            emit(o, ' ');
}

static size_t utoa_base(uint64_t v, unsigned base, int upper, char *tmp)
{
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char rev[24];
    size_t n = 0, i;
    do {
        rev[n++] = digits[v % base];
        v /= base;
    } while (v);
    for (i = 0; i < n; i++)
        tmp[i] = rev[n - 1 - i];
    return n;
}

int k_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    struct out o = { buf, size, 0 };
    char tmp[32];

    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            emit(&o, *fmt);
            continue;
        }
        fmt++;
        int left = 0, width = 0, lng = 0;
        char pad = ' ';
        for (;; fmt++) {
            if (*fmt == '-')
                left = 1;
            else if (*fmt == '0')
                pad = '0';
            else
                break;
        }
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + (*fmt++ - '0');
        for (;; fmt++) {
            if (*fmt == 'l')
                lng++;
            else if (*fmt == 'z')
                lng = 2;
            else
                break;
        }
        if (left)
            pad = ' ';

        switch (*fmt) {
        case 'd':
        case 'i': {
            int64_t v = lng ? va_arg(ap, int64_t) : va_arg(ap, int);
            size_t n = 0;
            uint64_t mag = v < 0 ? (uint64_t)0 - (uint64_t)v : (uint64_t)v;
            if (v < 0)
                tmp[n++] = '-';
            n += utoa_base(mag, 10, 0, tmp + n);
            emit_padded(&o, tmp, n, width, left, pad);
            break;
        }
        case 'u':
        case 'x':
        case 'X': {
            uint64_t v = lng ? va_arg(ap, uint64_t) : va_arg(ap, unsigned int);
            unsigned base = *fmt == 'u' ? 10 : 16;
            size_t n = utoa_base(v, base, *fmt == 'X', tmp);
            emit_padded(&o, tmp, n, width, left, pad);
            break;
        }
        case 'p': {
            uint64_t v = (uint64_t)(uintptr_t)va_arg(ap, void *);
            tmp[0] = '0';
            tmp[1] = 'x';
            size_t n = 2 + utoa_base(v, 16, 0, tmp + 2);
            emit_padded(&o, tmp, n, width, left, ' ');
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            size_t n = 0;
            if (!s)
                s = "(null)";
            while (s[n])
                n++;
            emit_padded(&o, s, n, width, left, ' ');
            break;
        }
        case 'c':
            tmp[0] = (char)va_arg(ap, int);
            emit_padded(&o, tmp, 1, width, left, ' ');
            break;
        case '%':
            emit(&o, '%');
            break;
        case 0:
            fmt--; /* trailing '%': stop at the terminator */
            break;
        default:
            emit(&o, '%');
            emit(&o, *fmt);
            break;
        }
    }
    if (size)
        buf[o.pos < size ? o.pos : size - 1] = 0;
    return (int)o.pos;
}

int k_snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = k_vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}
