/* Sorted kernel timer queue. See timerq.h. */
#include "timerq.h"

void tq_init(struct timerq *q)
{
    list_init(&q->head);
    q->count = 0;
}

void ktimer_setup(struct ktimer *t, ktimer_fn fn, void *arg)
{
    list_init(&t->node);
    t->expires = 0;
    t->period = 0;
    t->armed = 0;
    t->fn = fn;
    t->arg = arg;
}

int tq_del(struct timerq *q, struct ktimer *t)
{
    if (!t->armed)
        return -1;
    list_remove(&t->node);
    t->armed = 0;
    q->count--;
    return 0;
}

void tq_add(struct timerq *q, struct ktimer *t, uint64_t expires)
{
    struct list_node *it;
    if (t->armed)
        tq_del(q, t);
    t->expires = expires;
    /* walk from the back: new timers usually expire last */
    for (it = q->head.prev; it != &q->head; it = it->prev) {
        struct ktimer *o = container_of(it, struct ktimer, node);
        if (o->expires <= expires)
            break;
    }
    list_insert_between(&t->node, it, it->next);
    t->armed = 1;
    q->count++;
}

uint64_t tq_next(const struct timerq *q)
{
    if (list_empty(&q->head))
        return TQ_NEVER;
    return container_of(q->head.next, struct ktimer, node)->expires;
}

struct ktimer *tq_pop_expired(struct timerq *q, uint64_t now)
{
    struct ktimer *t;
    if (list_empty(&q->head))
        return 0;
    t = container_of(q->head.next, struct ktimer, node);
    if (t->expires > now)
        return 0;
    tq_del(q, t);
    if (t->period) {
        uint64_t next = t->expires + t->period;
        if (next <= now) /* missed periods: realign instead of firing a burst */
            next = now + t->period - (now - t->expires) % t->period;
        tq_add(q, t, next);
    }
    return t;
}
