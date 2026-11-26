/* Derived from orbit-os (github.com/coopersimpson101-debug/orbit-os, MIT), kernel/include/kstring.h.
 * Changes for orbit-watch: none (verbatim) */
/* Portable freestanding string helpers (also compiled for host unit tests). */
#ifndef ORBIT_KSTRING_H
#define ORBIT_KSTRING_H

#include <stddef.h>
#include <stdint.h>

void  *k_memset(void *dst, int c, size_t n);
void  *k_memcpy(void *dst, const void *src, size_t n);
void  *k_memmove(void *dst, const void *src, size_t n);
int    k_memcmp(const void *a, const void *b, size_t n);
size_t k_strlen(const char *s);
size_t k_strnlen(const char *s, size_t max);
int    k_strcmp(const char *a, const char *b);
int    k_strncmp(const char *a, const char *b, size_t n);
size_t k_strlcpy(char *dst, const char *src, size_t size);
const char *k_strchr(const char *s, int c);

#endif
