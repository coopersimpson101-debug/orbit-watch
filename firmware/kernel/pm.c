/* Power-state selection policy. See pm.h. */
#include "pm.h"

/* Residency / latency figures are design targets for the nRF54L15 build,
 * not measurements: PM_TICKLESS pays one GRTC compare + interrupt (tens of
 * microseconds); PM_DEEP additionally stops HFXO, which the 32 MHz crystal
 * needs ~hundreds of microseconds to restart. Re-tune on hardware. */
const struct pm_state_desc pm_states[PM_NSTATES] = {
    [PM_IDLE] = { "idle", 0, 0,
                  PM_WAKE_TICK | PM_WAKE_LPTIMER | PM_WAKE_BUTTON | PM_WAKE_IMU |
                  PM_WAKE_PPG | PM_WAKE_UART | PM_WAKE_PMIC },
    [PM_TICKLESS] = { "tickless", 20000, 50,
                      PM_WAKE_LPTIMER | PM_WAKE_BUTTON | PM_WAKE_IMU | PM_WAKE_PPG |
                      PM_WAKE_UART | PM_WAKE_PMIC },
    [PM_DEEP] = { "deep", 200000, 1000,
                  PM_WAKE_LPTIMER | PM_WAKE_BUTTON | PM_WAKE_IMU | PM_WAKE_PMIC },
    [PM_OFF] = { "off", 0xffffffffu, 0xffffffffu, PM_WAKE_BUTTON },
};

enum pm_state pm_select(uint64_t idle_us, uint32_t locks)
{
    int s;
    if (locks & PM_LOCK_NOSLEEP)
        return PM_IDLE;
    for (s = PM_DEEP; s > PM_IDLE; s--) {
        const struct pm_state_desc *d = &pm_states[s];
        uint64_t need = (uint64_t)d->min_residency_us + d->exit_latency_us;
        if (s == PM_DEEP && (locks & (PM_LOCK_CONSOLE | PM_LOCK_SPI | PM_LOCK_PPG)))
            continue;
        if (idle_us >= need)
            return (enum pm_state)s;
    }
    return PM_IDLE;
}

uint32_t pm_wake_mask(enum pm_state st, uint32_t enabled_sources)
{
    if ((unsigned)st >= PM_NSTATES)
        return 0;
    return pm_states[st].wake_sources & enabled_sources;
}

void pm_account(struct pm_stats *s, enum pm_state st, uint64_t slept_us)
{
    if ((unsigned)st >= PM_NSTATES)
        return;
    s->entries[st]++;
    s->residency_us[st] += slept_us;
}
