/* Tickless-idle time accounting (portable, host-tested).
 *
 * The kernel normally counts periodic ticks (SysTick at OW_HZ). When the
 * idle thread finds nothing to do for a while it suppresses the tick,
 * programs the low-power counter (GRTC on nRF54L15, the S32K timer on the
 * MPS2 AN505 model) to wake it up just before the next kernel event, sleeps,
 * and on wake-up works out how many whole ticks really elapsed.
 *
 * Kernel time is slaved to the low-power counter. One fixed *anchor* (the
 * counter value at boot, tick 0) defines the whole tick grid:
 *
 *     lp(k) = anchor_lp + floor(k * lp_hz / tick_hz)
 *
 * so the fractional part of lp_hz / tick_hz (e.g. 32768 / 100 = 327.68)
 * never accumulates as drift: every boundary is computed exactly from the
 * anchor instead of by repeated addition, and the grid is never re-anchored
 * per sleep (which would lose up to one counter cycle each time). On wake-up
 * the *absolute* tick is recomputed from the counter, which also absorbs any
 * drift of the SysTick (CPU clock) during busy periods. The leftover part of
 * the tick is handed back (`lp_to_next_tick`) so the periodic tick can be
 * restarted phase-aligned with the grid.
 *
 * The low-power counter may be narrower than 64 bits (24-bit RTC, 32-bit
 * timer); raw readings are unwrapped with modular differences, so a single
 * sleep must stay below half the counter period (enforced via max_idle).
 */
#ifndef OW_TICKLESS_H
#define OW_TICKLESS_H

#include <stdint.h>

struct tickless_cfg {
    uint32_t lp_hz;           /* low-power counter frequency */
    unsigned lp_bits;         /* counter width, 1..64 */
    uint32_t tick_hz;         /* kernel tick rate */
    uint32_t min_idle_ticks;  /* below this, just WFI with the tick running */
    uint32_t max_idle_ticks;  /* hard cap (also clamped to half the wrap) */
    uint32_t wake_latency_lp; /* wake this many lp cycles early (clock start-up) */
};

struct tickless_stats {
    uint64_t sleeps;          /* tickless sleeps taken */
    uint64_t planned_ticks;   /* sum of planned lengths */
    uint64_t slept_ticks;     /* sum of ticks credited on wake */
    uint64_t early_wakes;     /* woke before the planned boundary (IRQ) */
    uint64_t late_wakes;      /* woke after the planned boundary */
    uint64_t rejected;        /* plan() said "not worth it" */
};

struct tickless {
    struct tickless_cfg cfg;
    uint64_t lp_mask;
    uint64_t lp_unwrapped;    /* last seen counter value, unwrapped */
    uint64_t lp_last_raw;
    uint64_t anchor_lp;       /* unwrapped lp value at tick 0 */
    uint64_t planned_tick;    /* boundary we asked to be woken at */
    uint64_t plan_from;       /* tick at which the sleep was planned */
    int sleeping;
    struct tickless_stats st;
};

struct tickless_plan {
    uint64_t wake_tick;       /* kernel tick we plan to wake at */
    uint64_t compare_raw;     /* raw counter value to program (masked) */
    uint32_t ticks;           /* planned sleep length in ticks */
};

void tickless_init(struct tickless *t, const struct tickless_cfg *cfg, uint64_t lp_raw_now);

/* Unwrap a raw counter reading (call at least once per half wrap period). */
uint64_t tickless_unwrap(struct tickless *t, uint64_t lp_raw);

/* Lp counter value (unwrapped) of tick boundary `tick`. */
uint64_t tickless_lp_of_tick(const struct tickless *t, uint64_t tick);

/* Absolute tick for an unwrapped counter value. */
uint64_t tickless_tick_of_lp(const struct tickless *t, uint64_t lp);

/* Decide whether to sleep tickless.
 *   now_tick         current kernel tick count
 *   lp_raw_now       raw low-power counter reading
 *   next_event_tick  first tick at which the kernel has work (UINT64_MAX: none)
 * Returns 1 and fills *plan when sleeping is worthwhile, else 0. */
int tickless_plan(struct tickless *t, uint64_t now_tick, uint64_t lp_raw_now,
                  uint64_t next_event_tick, struct tickless_plan *plan);

/* Account for the sleep after wake-up. Returns the absolute tick the
 * counter is in now (fewer than planned after an early interrupt, more
 * after a late wake); the caller sets its tick count to max(current, this).
 * Writes the lp cycles left until the next tick boundary to
 * *lp_to_next_tick. */
uint64_t tickless_wake(struct tickless *t, uint64_t lp_raw_now, uint32_t *lp_to_next_tick);

#endif
