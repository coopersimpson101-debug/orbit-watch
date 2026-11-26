/* Tickless-idle time accounting. See tickless.h for the model. */
#include "tickless.h"

static uint64_t mask_of(unsigned bits)
{
    return bits >= 64 ? ~(uint64_t)0 : (((uint64_t)1 << bits) - 1);
}

void tickless_init(struct tickless *t, const struct tickless_cfg *cfg, uint64_t lp_raw_now)
{
    uint64_t half_wrap_ticks;
    t->cfg = *cfg;
    t->lp_mask = mask_of(cfg->lp_bits);
    t->lp_last_raw = lp_raw_now & t->lp_mask;
    t->lp_unwrapped = t->lp_last_raw;
    t->anchor_lp = t->lp_unwrapped;
    t->planned_tick = 0;
    t->plan_from = 0;
    t->sleeping = 0;
    t->st.sleeps = t->st.planned_ticks = t->st.slept_ticks = 0;
    t->st.early_wakes = t->st.late_wakes = t->st.rejected = 0;
    /* A sleep must not span half a wrap, or unwrapping becomes ambiguous. */
    half_wrap_ticks = (t->lp_mask >> 1) / cfg->lp_hz * cfg->tick_hz;
    if (half_wrap_ticks == 0)
        half_wrap_ticks = 1;
    if (t->cfg.max_idle_ticks == 0 || t->cfg.max_idle_ticks > half_wrap_ticks)
        t->cfg.max_idle_ticks = half_wrap_ticks > 0xffffffffu ? 0xffffffffu
                                                             : (uint32_t)half_wrap_ticks;
}

uint64_t tickless_unwrap(struct tickless *t, uint64_t lp_raw)
{
    uint64_t raw = lp_raw & t->lp_mask;
    uint64_t delta = (raw - t->lp_last_raw) & t->lp_mask;
    t->lp_last_raw = raw;
    t->lp_unwrapped += delta;
    return t->lp_unwrapped;
}

uint64_t tickless_lp_of_tick(const struct tickless *t, uint64_t tick)
{
    return t->anchor_lp + tick * t->cfg.lp_hz / t->cfg.tick_hz;
}

uint64_t tickless_tick_of_lp(const struct tickless *t, uint64_t lp)
{
    uint64_t e = lp - t->anchor_lp;
    /* largest k with floor(k * L / T) <= e */
    return ((e + 1) * t->cfg.tick_hz - 1) / t->cfg.lp_hz;
}

int tickless_plan(struct tickless *t, uint64_t now_tick, uint64_t lp_raw_now,
                  uint64_t next_event_tick, struct tickless_plan *plan)
{
    uint64_t now_lp = tickless_unwrap(t, lp_raw_now);
    uint64_t n, target;

    if (next_event_tick <= now_tick) {
        t->st.rejected++;
        return 0;
    }
    n = next_event_tick - now_tick;
    if (n > t->cfg.max_idle_ticks)
        n = t->cfg.max_idle_ticks;
    if (n < t->cfg.min_idle_ticks || n == 0) {
        t->st.rejected++;
        return 0;
    }
    target = tickless_lp_of_tick(t, now_tick + n);
    if (target > t->cfg.wake_latency_lp)
        target -= t->cfg.wake_latency_lp;
    if (target <= now_lp + 1) { /* too close to be worth programming */
        t->st.rejected++;
        return 0;
    }
    t->planned_tick = now_tick + n;
    t->plan_from = now_tick;
    t->sleeping = 1;
    t->st.sleeps++;
    t->st.planned_ticks += n;
    plan->wake_tick = now_tick + n;
    plan->compare_raw = target & t->lp_mask;
    plan->ticks = (uint32_t)n;
    return 1;
}

uint64_t tickless_wake(struct tickless *t, uint64_t lp_raw_now, uint32_t *lp_to_next_tick)
{
    uint64_t now_lp = tickless_unwrap(t, lp_raw_now);
    uint64_t k = tickless_tick_of_lp(t, now_lp);
    uint64_t next = tickless_lp_of_tick(t, k + 1);

    if (lp_to_next_tick)
        *lp_to_next_tick = (uint32_t)(next - now_lp);
    if (t->sleeping) {
        /* Waking `wake_latency` early lands in tick planned-1 by design. */
        if (k + 1 < t->planned_tick)
            t->st.early_wakes++;
        else if (k > t->planned_tick)
            t->st.late_wakes++;
        t->st.slept_ticks += k > t->plan_from ? k - t->plan_from : 0;
        t->sleeping = 0;
    }
    return k;
}
