/* Derived from orbit-os (github.com/coopersimpson101-debug/orbit-os, MIT), kernel/include/runqueue.h.
 * Changes for orbit-watch: none (verbatim) */
/* Priority run-queue: one FIFO per priority level plus a bitmap of
 * non-empty levels. Priority 0 is the most urgent. Pure data structure,
 * no locking (callers mask interrupts), unit-tested on the host. */
#ifndef ORBIT_RUNQUEUE_H
#define ORBIT_RUNQUEUE_H

#include <stdint.h>
#include "list.h"

#define RQ_NPRIO 8

struct rq_entry {
    struct list_node node;
    int prio;
    int queued;
};

struct runqueue {
    struct list_node level[RQ_NPRIO];
    uint32_t bitmap;
    unsigned count;
};

void rq_init(struct runqueue *rq);
/* Returns 0, or -1 if prio is out of range or entry already queued. */
int  rq_push(struct runqueue *rq, struct rq_entry *e, int prio);
/* Highest-priority entry (FIFO within a level), or NULL when empty. */
struct rq_entry *rq_pop(struct runqueue *rq);
struct rq_entry *rq_peek(const struct runqueue *rq);
int  rq_remove(struct runqueue *rq, struct rq_entry *e);
int  rq_highest_prio(const struct runqueue *rq); /* -1 when empty */

#endif
