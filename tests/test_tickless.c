/* Tickless-idle accounting: drift-free over long random runs, counter
 * wrap-around, early/late wakes, wake latency, plan rejection. */
#include <stdint.h>
#include "test.h"
#include "tickless.h"

static uint32_t rng = 12345;
static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

/* Exact global tick boundary k for a clock that started at lp = 0. */
static uint64_t boundary(uint64_t k, uint32_t lp_hz, uint32_t tick_hz)
{
    return k * lp_hz / tick_hz;
}

/* Simulate hours of alternating run / tickless-sleep and check the kernel
 * tick count never drifts from the true low-power time. */
static void soak(uint32_t lp_hz, unsigned bits, uint32_t tick_hz, uint32_t latency, int iters)
{
    struct tickless t;
    struct tickless_cfg c = { lp_hz, bits, tick_hz, 2, 0, latency };
    uint64_t mask = bits >= 64 ? ~0ull : ((1ull << bits) - 1);
    uint64_t T = 0;      /* true lp time (unwrapped) */
    uint64_t tick = 0;   /* kernel tick count */
    int i, bad_phase = 0, bad_count = 0, wraps = 0;
    uint64_t last_raw = 0;
    tickless_init(&t, &c, 0);
    for (i = 0; i < iters; i++) {
        struct tickless_plan p;
        uint32_t run = rnd() % 7, frac, left;
        (void)frac;
        uint64_t next_ev, wake_T;
        /* run a few ticks with the periodic tick */
        tick += run;
        T = boundary(tick, lp_hz, tick_hz);
        /* somewhere inside the current tick */
        frac = (uint32_t)(rnd() % (boundary(tick + 1, lp_hz, tick_hz) - T));
        T += frac;
        next_ev = tick + 1 + rnd() % (tick_hz * 3);
        if (!tickless_plan(&t, tick, T & mask, next_ev, &p)) {
            tick++; /* not worth sleeping: the periodic tick fires as usual */
            T = boundary(tick, lp_hz, tick_hz);
            continue;
        }
        CHECK(p.compare_raw <= mask);
        /* wake either at the compare (normal) or early (button IRQ) */
        {
            uint64_t cmp_T = boundary(p.wake_tick, lp_hz, tick_hz) - latency;
            if (rnd() % 4 == 0 && cmp_T > T + 1)
                wake_T = T + 1 + rnd() % (cmp_T - T - 1);
            else
                wake_T = cmp_T + rnd() % 3; /* a few cycles of IRQ latency */
        }
        if ((wake_T & mask) < (last_raw & mask))
            wraps++;
        last_raw = wake_T;
        T = wake_T;
        {
            uint64_t k = tickless_wake(&t, T & mask, &left);
            CHECK(k >= tick); /* time never runs backwards */
            tick = k;
        }
        /* invariant: boundary(tick) <= T < boundary(tick+1) */
        if (!(boundary(tick, lp_hz, tick_hz) <= T && T < boundary(tick + 1, lp_hz, tick_hz)))
            bad_count++;
        /* the restarted tick must land exactly on the next global boundary */
        if (T + left != boundary(tick + 1, lp_hz, tick_hz))
            bad_phase++;
        T += left;
        tick++;
    }
    CHECK(bad_count == 0);
    CHECK(bad_phase == 0);
    CHECK(t.st.sleeps > (uint64_t)iters / 2);
    CHECK(t.st.early_wakes > 0);
    if (bits < 32)
        CHECK(wraps > 0); /* the 24-bit case really exercised wrap-around */
    printf("  lp %u Hz/%u bit, latency %u: %llu sleeps, %llu ticks slept, %d wraps, "
           "%llu early\n", lp_hz, bits, latency, (unsigned long long)t.st.sleeps,
           (unsigned long long)t.st.slept_ticks, wraps, (unsigned long long)t.st.early_wakes);
}

static void test_plan_rules(void)
{
    struct tickless t;
    struct tickless_cfg c = { 32768, 24, 100, 3, 500, 0 };
    struct tickless_plan p;
    uint32_t left;
    tickless_init(&t, &c, 1000);
    /* too short */
    CHECK(tickless_plan(&t, 10, 1000 + 3277, 12, &p) == 0);
    /* event in the past */
    CHECK(tickless_plan(&t, 10, 1000 + 3277, 10, &p) == 0);
    /* no event: clamped to max_idle */
    CHECK(tickless_plan(&t, 10, 1000 + 3277, UINT64_MAX, &p) == 1);
    CHECK(p.ticks == 500);
    CHECK(p.wake_tick == 510);
    /* tick 510 = 5.1 s after the anchor = 167116.8 -> 167116 lp cycles */
    CHECK(p.compare_raw == ((1000 + 167116) & 0xffffff));
    /* an on-time wake lands in tick 510 */
    CHECK(tickless_wake(&t, (1000 + 167116) & 0xffffff, &left) == 510);
    CHECK(left == 328); /* tick 511 starts at floor(511 * 327.68) = 167444 */
    CHECK(t.st.slept_ticks == 500);
    /* late wake (e.g. a debugger halted the core): more ticks credited */
    CHECK(tickless_plan(&t, 600, 1000 + 196608, 610, &p) == 1);
    CHECK(tickless_wake(&t, 1000 + 196608 + 32768 * 2, &left) == 800);
    CHECK(t.st.late_wakes == 1);
}

static void test_wake_latency(void)
{
    struct tickless t;
    struct tickless_cfg c = { 1000000, 52, 1000, 2, 0, 300 }; /* GRTC-like, 1 kHz tick */
    struct tickless_plan p;
    uint32_t left;
    tickless_init(&t, &c, 0);
    CHECK(tickless_plan(&t, 0, 0, 100, &p) == 1);
    CHECK(p.compare_raw == 100000 - 300);
    /* waking at the compare is still in tick 99; next boundary is 300 us away */
    CHECK(tickless_wake(&t, 100000 - 300, &left) == 99);
    CHECK(left == 300);
    CHECK(t.st.early_wakes == 0);
    /* max idle clamps to half the counter wrap (2^51 us) */
    CHECK(t.cfg.max_idle_ticks == 0xffffffffu);
}

static void test_narrow_counter_clamp(void)
{
    struct tickless t;
    struct tickless_cfg c = { 32768, 24, 100, 2, 0, 0 };
    tickless_init(&t, &c, 0);
    /* half of the 24-bit wrap, whole seconds only */
    CHECK(t.cfg.max_idle_ticks == 25500); /* floor((2^23-1)/32768) = 255 s */
}

int main(void)
{
    RUN(test_plan_rules);
    RUN(test_wake_latency);
    RUN(test_narrow_counter_clamp);
    soak(32768, 24, 100, 0, 200000);      /* nRF52-style 24-bit RTC */
    soak(32768, 24, 128, 5, 100000);      /* non-integer cycles per tick */
    soak(1000000, 52, 1000, 30, 100000);  /* nRF54L GRTC SYSCOUNTER */
    soak(25000000, 32, 100, 2500, 50000); /* AN505 CMSDK timer at 25 MHz */
    TEST_REPORT("test_tickless");
}
