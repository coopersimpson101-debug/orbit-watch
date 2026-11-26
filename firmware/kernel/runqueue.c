/* Derived from orbit-os (github.com/coopersimpson101-debug/orbit-os, MIT), kernel/sched/runqueue.c.
 * Changes for orbit-watch: none (verbatim) */
#include "runqueue.h"

void rq_init(struct runqueue *rq)
{
    int i;
    for (i = 0; i < RQ_NPRIO; i++)
        list_init(&rq->level[i]);
    rq->bitmap = 0;
    rq->count = 0;
}

int rq_push(struct runqueue *rq, struct rq_entry *e, int prio)
{
    if (prio < 0 || prio >= RQ_NPRIO || e->queued)
        return -1;
    e->prio = prio;
    e->queued = 1;
    list_push_back(&rq->level[prio], &e->node);
    rq->bitmap |= 1u << prio;
    rq->count++;
    return 0;
}

int rq_highest_prio(const struct runqueue *rq)
{
    if (!rq->bitmap)
        return -1;
    return __builtin_ctz(rq->bitmap);
}

struct rq_entry *rq_peek(const struct runqueue *rq)
{
    int p = rq_highest_prio(rq);
    if (p < 0)
        return 0;
    return container_of(rq->level[p].next, struct rq_entry, node);
}

int rq_remove(struct runqueue *rq, struct rq_entry *e)
{
    if (!e->queued)
        return -1;
    list_remove(&e->node);
    e->queued = 0;
    if (list_empty(&rq->level[e->prio]))
        rq->bitmap &= ~(1u << e->prio);
    rq->count--;
    return 0;
}

struct rq_entry *rq_pop(struct runqueue *rq)
{
    struct rq_entry *e = rq_peek(rq);
    if (e)
        rq_remove(rq, e);
    return e;
}
