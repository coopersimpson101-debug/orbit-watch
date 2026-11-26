/* Idle thread: tickless idle and deep sleep.
 *
 * With interrupts masked (PRIMASK; WFI still wakes on a pending IRQ):
 *   1. ask the scheduler for the next timed event and pm_select() for the
 *      deepest state that pays off before it
 *   2. PM_IDLE: plain WFI, SysTick keeps running
 *   3. PM_TICKLESS / PM_DEEP: stop SysTick, program the low-power counter
 *      compare via tickless_plan(), WFI (with SLEEPDEEP for PM_DEEP)
 *   4. on wake: tickless_wake() recomputes the absolute tick from the
 *      counter, sched_advance() runs the expired timers, and SysTick is
 *      restarted so its first period ends exactly on the next grid boundary
 *   5. unmask: the pending ISR (button, IMU FIFO, UART) runs now
 */
#include "board.h"
#include "cm33.h"
#include "kernel.h"
#include "sched.h"

struct idle_state idle;
static volatile int systick_short; /* first SysTick period after a sleep */
static uint32_t systick_period;

void SysTick_Handler(void)
{
    if (systick_short) {
        SYST_RVR = systick_period - 1; /* takes effect at the next reload */
        systick_short = 0;
    }
    sched_tick_isr();
}

static void systick_start(uint32_t first_cycles)
{
    if (first_cycles < 64)
        first_cycles = 64;
    if (first_cycles > systick_period)
        first_cycles = systick_period;
    SYST_CSR = 0;
    SYST_RVR = first_cycles - 1;
    SYST_CVR = 0;
    systick_short = first_cycles != systick_period;
    SYST_CSR = SYST_CSR_ENABLE | SYST_CSR_TICKINT | SYST_CSR_CLKSOURCE;
}

void kernel_lp_alarm_isr(void)
{
    /* Nothing to do: waking up is the point. Accounting happens in idle. */
}

void idle_init(void)
{
    struct tickless_cfg c;
    c.lp_hz = board.lp_hz;
    c.lp_bits = board.lp_bits;
    c.tick_hz = OW_HZ;
    c.min_idle_ticks = 2;
    c.max_idle_ticks = 60 * OW_HZ; /* re-check at least once a minute */
    c.wake_latency_lp = board.wake_latency_lp;
    idle.boot_lp = board_lp_read();
    tickless_init(&idle.tl, &c, idle.boot_lp);
    idle.locks = 0;
    idle.wake_sources = PM_WAKE_LPTIMER | PM_WAKE_BUTTON | PM_WAKE_IMU | PM_WAKE_PMIC |
                        PM_WAKE_UART | PM_WAKE_TICK;
    idle.deep_allowed = 1;
    systick_period = board.cpu_hz / OW_HZ;
    systick_start(systick_period);
}

void pm_lock(uint32_t bits)
{
    uint32_t f = irq_save();
    idle.locks |= bits;
    irq_restore(f);
}

void pm_unlock(uint32_t bits)
{
    uint32_t f = irq_save();
    idle.locks &= ~bits;
    irq_restore(f);
}

uint64_t lp_elapsed_since_boot(void)
{
    uint32_t f = irq_save();
    uint64_t v = tickless_unwrap(&idle.tl, board_lp_read()) - idle.boot_lp;
    irq_restore(f);
    return v;
}

void idle_thread(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t f = irq_save();
        uint64_t now = ticks_now(), next = sched_next_event();
        uint64_t idle_us = next == TQ_NEVER ? UINT64_MAX
                                            : (next > now ? (next - now) * (1000000u / OW_HZ) : 0);
        uint32_t locks = idle.locks | (idle.deep_allowed ? 0 : PM_LOCK_SPI);
        enum pm_state st = pm_select(idle_us, locks);
        uint64_t lp0 = tickless_unwrap(&idle.tl, board_lp_read()), lp1;
        struct tickless_plan plan;

        if (st != PM_IDLE) {
            SYST_CSR = 0; /* suppress the tick */
            if ((SCB_ICSR & (1u << 26)) || !tickless_plan(&idle.tl, now, lp0, next, &plan)) {
                /* a tick is already pending, or the sleep is too short */
                systick_start(systick_period);
                st = PM_IDLE;
            }
        }
        if (st == PM_IDLE) {
            uint32_t c0 = board_cycles();
            board_sleep(PM_IDLE, pm_wake_mask(PM_IDLE, idle.wake_sources));
            idle.sleep_cycles += (uint32_t)(board_cycles() - c0);
            lp1 = tickless_unwrap(&idle.tl, board_lp_read());
        } else {
            uint32_t left;
            uint64_t abs_tick;
            uint32_t c0;
            board_lp_alarm(plan.compare_raw);
            c0 = board_cycles();
            board_sleep(st, pm_wake_mask(st, idle.wake_sources));
            idle.sleep_cycles += (uint32_t)(board_cycles() - c0);
            board_lp_alarm_cancel();
            lp1 = board_lp_read();
            abs_tick = tickless_wake(&idle.tl, lp1, &left);
            lp1 = idle.tl.lp_unwrapped;
            systick_start((uint32_t)((uint64_t)left * board.cpu_hz / board.lp_hz));
            sched_advance(abs_tick);
        }
        idle.sleep_lp[st] += lp1 - lp0;
        pm_account(&idle.pm, st, (lp1 - lp0) * 1000000u / board.lp_hz);
        idle.wakeups++;
        irq_restore(f);
    }
}
