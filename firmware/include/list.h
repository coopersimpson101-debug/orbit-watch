/* Derived from orbit-os (github.com/coopersimpson101-debug/orbit-os, MIT), kernel/include/list.h.
 * Changes for orbit-watch: none (verbatim) */
/* Intrusive circular doubly-linked list. */
#ifndef ORBIT_LIST_H
#define ORBIT_LIST_H

#include <stddef.h>

struct list_node {
    struct list_node *next, *prev;
};

#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

static inline void list_init(struct list_node *h)
{
    h->next = h->prev = h;
}

static inline int list_empty(const struct list_node *h)
{
    return h->next == h;
}

static inline void list_insert_between(struct list_node *n,
                                       struct list_node *prev,
                                       struct list_node *next)
{
    n->prev = prev;
    n->next = next;
    prev->next = n;
    next->prev = n;
}

static inline void list_push_back(struct list_node *h, struct list_node *n)
{
    list_insert_between(n, h->prev, h);
}

static inline void list_push_front(struct list_node *h, struct list_node *n)
{
    list_insert_between(n, h, h->next);
}

static inline void list_remove(struct list_node *n)
{
    n->prev->next = n->next;
    n->next->prev = n->prev;
    n->next = n->prev = n;
}

static inline struct list_node *list_pop_front(struct list_node *h)
{
    struct list_node *n;
    if (list_empty(h))
        return NULL;
    n = h->next;
    list_remove(n);
    return n;
}

static inline size_t list_length(const struct list_node *h)
{
    size_t n = 0;
    const struct list_node *it;
    for (it = h->next; it != h; it = it->next)
        n++;
    return n;
}

#endif
