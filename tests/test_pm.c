/* Power-state policy: break-even selection, locks, wake masks, accounting. */
#include <stdint.h>
#include "test.h"
#include "pm.h"

static void test_select(void)
{
    CHECK(pm_select(0, 0) == PM_IDLE);
    CHECK(pm_select(19999, 0) == PM_IDLE);
    CHECK(pm_select(20050, 0) == PM_TICKLESS);
    CHECK(pm_select(200999, 0) == PM_TICKLESS);
    CHECK(pm_select(201000, 0) == PM_DEEP);
    CHECK(pm_select(UINT64_MAX, 0) == PM_DEEP);  /* never PM_OFF automatically */
    CHECK(pm_select(UINT64_MAX, PM_LOCK_CONSOLE) == PM_TICKLESS);
    CHECK(pm_select(UINT64_MAX, PM_LOCK_SPI) == PM_TICKLESS);
    CHECK(pm_select(UINT64_MAX, PM_LOCK_PPG) == PM_TICKLESS);
    CHECK(pm_select(UINT64_MAX, PM_LOCK_NOSLEEP) == PM_IDLE);
}

static void test_wake_masks(void)
{
    uint32_t all = 0xffffffffu;
    /* deep sleep cannot be woken by the UART or the periodic tick */
    CHECK(!(pm_wake_mask(PM_DEEP, all) & PM_WAKE_UART));
    CHECK(!(pm_wake_mask(PM_DEEP, all) & PM_WAKE_TICK));
    CHECK(pm_wake_mask(PM_DEEP, all) & PM_WAKE_BUTTON);
    CHECK(pm_wake_mask(PM_DEEP, all) & PM_WAKE_IMU);
    CHECK(pm_wake_mask(PM_DEEP, all) & PM_WAKE_LPTIMER);
    CHECK(pm_wake_mask(PM_DEEP, PM_WAKE_BUTTON) == PM_WAKE_BUTTON);
    CHECK(pm_wake_mask(PM_OFF, all) == PM_WAKE_BUTTON);
    CHECK(pm_wake_mask(PM_TICKLESS, all) & PM_WAKE_UART);
    CHECK(!(pm_wake_mask(PM_TICKLESS, all) & PM_WAKE_TICK));
    CHECK(pm_wake_mask(PM_IDLE, all) & PM_WAKE_TICK);
    CHECK(pm_wake_mask((enum pm_state)9, all) == 0);
}

static void test_account(void)
{
    struct pm_stats s = {0};
    pm_account(&s, PM_DEEP, 1000);
    pm_account(&s, PM_DEEP, 500);
    pm_account(&s, PM_IDLE, 7);
    pm_account(&s, (enum pm_state)9, 7);
    CHECK(s.entries[PM_DEEP] == 2 && s.residency_us[PM_DEEP] == 1500);
    CHECK(s.entries[PM_IDLE] == 1 && s.residency_us[PM_IDLE] == 7);
    /* deeper states have strictly larger break-even points */
    CHECK(pm_states[PM_TICKLESS].min_residency_us < pm_states[PM_DEEP].min_residency_us);
}

int main(void)
{
    RUN(test_select);
    RUN(test_wake_masks);
    RUN(test_account);
    TEST_REPORT("test_pm");
}
