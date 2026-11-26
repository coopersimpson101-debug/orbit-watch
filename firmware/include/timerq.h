/* Kernel timer queue: timers sorted by absolute expiry tick. The head gives
 * the tickless idle code its "next event". Pure data structure (callers
 * mask interrupts), unit-tested on the host. */
#ifndef OW_TIMERQ_H
#define OW_TIMERQ_H

#include <stdint.h>
#include "list.h"

#define TQ_NEVER UINT64_MAX

struct ktimer;
typedef void (*ktimer_fn)(struct ktimer *t, void *arg);

struct ktimer {
    struct list_node node;
    uint64_t expires;   /* absolute tick */
    uint32_t period;    /* 0 = one-shot, else re-armed every `period` ticks */
    int armed;
    ktimer_fn fn;
    void *arg;
};

struct timerq {
    struct list_node head;
    unsigned count;
};

void tq_init(struct timerq *q);
void ktimer_setup(struct ktimer *t, ktimer_fn fn, void *arg);
/* (Re-)arm at absolute tick `expires`; equal expiries keep FIFO order. */
void tq_add(struct timerq *q, struct ktimer *t, uint64_t expires);
int  tq_del(struct timerq *q, struct ktimer *t);    /* 0 if it was armed */
uint64_t tq_next(const struct timerq *q);            /* TQ_NEVER when empty */
/* Remove and return one timer with expires <= now (NULL if none). Periodic
 * timers are re-armed at expires + period (catching up without bursts if
 * several periods were missed). */
struct ktimer *tq_pop_expired(struct timerq *q, uint64_t now);

#endif
