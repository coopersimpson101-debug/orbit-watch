/* Power-state policy (portable, host-tested).
 *
 * The idle thread asks pm_select() which sleep state to enter given how
 * long the system expects to be idle and which subsystems currently forbid
 * deep states ("locks", e.g. the console while a shell session is active or
 * the SPI bus while a display transfer is in flight).
 *
 * States, shallow to deep (names follow the nRF54L15 datasheet):
 *   PM_IDLE      WFI with the periodic tick running        (System ON, CPU sleeping)
 *   PM_TICKLESS  WFI, tick suppressed, lp-timer wake-up     (System ON idle, HF clock on demand)
 *   PM_DEEP      SLEEPDEEP + tick suppressed; only GRTC, button and IMU
 *                interrupts can wake; UART RX and HF peripherals are off
 *   PM_OFF       System OFF: RAM not retained, wake = reset via button/GRTC.
 *                Never chosen automatically; entered by an explicit request.
 * A state is eligible only if expected idle time >= its minimum residency
 * plus its exit latency (the break-even point where the saved energy pays
 * for the entry/exit cost) and no lock forbids it.
 */
#ifndef OW_PM_H
#define OW_PM_H

#include <stdint.h>

enum pm_state { PM_IDLE = 0, PM_TICKLESS = 1, PM_DEEP = 2, PM_OFF = 3, PM_NSTATES = 4 };

/* Wake sources (bit mask). */
#define PM_WAKE_LPTIMER  (1u << 0) /* GRTC compare */
#define PM_WAKE_BUTTON   (1u << 1) /* GPIO sense / GPIOTE */
#define PM_WAKE_IMU      (1u << 2) /* BMI270 INT1 (FIFO watermark / motion) */
#define PM_WAKE_PPG      (1u << 3) /* MAX30101 INT (FIFO almost full) */
#define PM_WAKE_UART     (1u << 4) /* console RX */
#define PM_WAKE_PMIC     (1u << 5) /* nPM1300 GPIO: charger / VBUS events */
#define PM_WAKE_TICK     (1u << 6) /* periodic SysTick */

/* Locks: a set bit forbids states deeper than the listed one. */
#define PM_LOCK_CONSOLE  (1u << 0) /* forbids PM_DEEP (UART needs HFCLK) */
#define PM_LOCK_SPI      (1u << 1) /* forbids PM_DEEP during a display DMA */
#define PM_LOCK_PPG      (1u << 2) /* heart-rate session: forbids PM_DEEP */
#define PM_LOCK_NOSLEEP  (1u << 3) /* forbids everything below PM_IDLE (debug) */

struct pm_state_desc {
    const char *name;
    uint32_t min_residency_us;
    uint32_t exit_latency_us;
    uint32_t wake_sources;
};

struct pm_stats {
    uint64_t entries[PM_NSTATES];
    uint64_t residency_us[PM_NSTATES];
    uint64_t active_us;
};

extern const struct pm_state_desc pm_states[PM_NSTATES];

/* Pick the deepest eligible state. idle_us == UINT64_MAX means "no timed
 * event pending" (only external interrupts will wake us). */
enum pm_state pm_select(uint64_t idle_us, uint32_t locks);

/* Wake sources armed for a state (subset of its descriptor, minus sources
 * the caller has disabled, e.g. the PPG interrupt when HR is off). */
uint32_t pm_wake_mask(enum pm_state st, uint32_t enabled_sources);

void pm_account(struct pm_stats *s, enum pm_state st, uint64_t slept_us);

#endif
