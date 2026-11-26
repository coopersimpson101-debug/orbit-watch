/* Board interface: everything the portable kernel and the watch app need
 * from the hardware. Implemented by boards/an505 (QEMU) and boards/nrf54l15. */
#ifndef OW_BOARD_H
#define OW_BOARD_H

#include <stddef.h>
#include <stdint.h>
#include "pm.h"

struct board_info {
    const char *name;
    uint32_t cpu_hz;          /* SysTick clock */
    uint32_t lp_hz;           /* low-power counter */
    unsigned lp_bits;
    uint32_t wake_latency_lp; /* PM_DEEP exit latency in lp cycles */
    uint32_t ram_bytes;
};

extern const struct board_info board;

void board_early_init(void);           /* clocks + console, before the banner */
void board_init(void);                 /* timers, interrupts, peripherals */
void board_putc(char c);
void board_irq(unsigned irq);          /* external interrupt dispatcher */

/* Fine CPU-time counter (32-bit, wraps): DWT CYCCNT on hardware, a CMSDK
 * timer at the system clock on the emulator. Used for per-thread CPU time. */
uint32_t board_cycles(void);
extern const uint32_t board_cycles_hz;
/* Cycles the emulator spent on work that has no hardware counterpart
 * (synthesising sensor data, decoding SPI frames in the virtual panel). */
uint64_t board_emu_overhead_cycles(void);

/* Low-power counter and its compare/alarm. */
uint64_t board_lp_read(void);
void board_lp_alarm(uint64_t compare_raw);
void board_lp_alarm_cancel(void);

/* Enter a sleep state; returns after the next interrupt is pending. */
void board_sleep(enum pm_state st, uint32_t wake_mask);
void board_system_off(void) __attribute__((noreturn));

/* Display: one chip-select-framed SPI transfer to the MIP panel. */
void board_display_xfer(const uint8_t *buf, size_t n);
/* Sensor FIFOs (returns samples copied). */
int  board_accel_read(int16_t (*xyz)[3], int max);
int  board_ppg_read(int32_t *out, int max);
void board_ppg_enable(int on);
unsigned board_battery_pct(int *charging);

/* Emulator-only hooks (no-ops on hardware). */
void board_snapshot(const char *tag);  /* dump the panel image */
void board_write_file(const char *name, const char *data, size_t n);
void board_exit(int code) __attribute__((noreturn));
uint64_t board_true_steps(void);       /* synthetic ground truth (0 on hardware) */

/* Kernel hooks called from board interrupt handlers. */
void kernel_lp_alarm_isr(void);
void console_rx_isr(char c);
void watch_button_isr(unsigned buttons);

#endif
