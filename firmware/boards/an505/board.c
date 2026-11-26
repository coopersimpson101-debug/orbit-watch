/* Board support for QEMU `-M mps2-an505` (Arm MPS2+ AN505, Cortex-M33).
 *
 * Mapping of the watch onto the emulated machine (all Secure aliases):
 *   console        CMSDK UART0 @ 0x50200000, RX IRQ 32 (33 on QEMU <= 9.0)
 *   lp counter     CMSDK S32K timer @ 0x5002f000, free-running at 32 kHz
 *                  (QEMU's S32KCLK is 32000 Hz), stands in for the GRTC
 *   lp alarm       CMSDK dual timer @ 0x50002000 (IRQ 5), timer 1 in one-shot
 *                  mode, programmed from the lp delta (the timer:lp clock ratio
 *                  is measured at boot). A re-armed CMSDK APB timer (TIMER1)
 *                  fires late or not at all on QEMU 8.2, the one-shot does not.
 *   SysTick        20 MHz system clock
 *   cycle counter  CMSDK TIMER0 free-running at 20 MHz (QEMU has no DWT CYCCNT);
 *                  with -icount shift=0 one count = 50 guest instructions
 *   display        "virtual panel": every SPI frame is decoded bit by bit by
 *                  mip_panel_feed(), the datasheet-level model of the Sharp
 *                  panel's pixel memory, and can be dumped as a PBM image
 *   sensors        synthetic accelerometer and PPG signals (app/synth.c),
 *                  following a fixed one-hour activity script
 *   files / exit   Arm semihosting (QEMU -semihosting-config enable=on)
 * None of this is hardware; it exists so the kernel, power management and
 * display pipeline can run unmodified in CI.
 */
#include "board.h"
#include "cm33.h"
#include "kernel.h"
#include "kprintf.h"
#include "kstring.h"
#include "mip.h"
#include "sched.h"
#include "synth.h"

#define UART0      0x50200000u
#define TIMER0     0x50000000u
#define DUALTIMER  0x50002000u /* CMSDK dual timer, timer 1 */
#define IRQ_DUALTIMER 5
#define DT_LOAD 0x00
#define DT_CTRL 0x08
#define DT_INTCLR 0x0c
#define DT_EN (1u << 7)
#define DT_IE (1u << 5)
#define DT_32BIT (1u << 1)
#define DT_ONESHOT (1u << 0)
#define S32KTIMER  0x5002f000u
#define IRQ_S32K   2
/* UART0 RX is IRQ 32 in QEMU >= 9.1, but QEMU 9.0 and older wired the CMSDK
 * UART's TX and RX interrupt lines the other way round (RX on 33). Enable
 * both and drain RX from either. TX interrupts stay disabled in CTRL. */
#define IRQ_UART0_RX 32
#define IRQ_UART0_RX_OLDQEMU 33

#define T_CTRL   0x00
#define T_VALUE  0x04
#define T_RELOAD 0x08
#define T_INT    0x0c
#define U_DATA   0x00
#define U_STATE  0x04
#define U_CTRL   0x08
#define U_INT    0x0c
#define U_BAUD   0x10

const struct board_info board = {
    .name = "qemu-mps2-an505",
    .cpu_hz = 20000000,
    .lp_hz = 32000,
    .lp_bits = 32,
    .wake_latency_lp = 0,
    .ram_bytes = 256 * 1024,
};

/* ----------------------------------------------------------- semihosting */

static int semihost(int op, void *arg)
{
    register int r0 __asm__("r0") = op;
    register void *r1 __asm__("r1") = arg;
    __asm__ volatile("bkpt 0xab" : "+r"(r0) : "r"(r1) : "memory");
    return r0;
}

void board_write_file(const char *name, const char *data, size_t n)
{
    uint32_t open_args[3] = { (uint32_t)(uintptr_t)name, 5 /* "wb" */, (uint32_t)k_strlen(name) };
    int fd = semihost(0x01, open_args);
    uint32_t wr[3];
    if (fd < 0) {
        kprintf("semihosting: cannot open %s\n", name);
        return;
    }
    wr[0] = (uint32_t)fd;
    wr[1] = (uint32_t)(uintptr_t)data;
    wr[2] = (uint32_t)n;
    semihost(0x05, wr);
    semihost(0x02, wr);
}

void board_exit(int code)
{
    uint32_t args[2] = { 0x20026 /* ADP_Stopped_ApplicationExit */, (uint32_t)code };
    semihost(0x20, args); /* SYS_EXIT_EXTENDED */
    for (;;)
        __asm__ volatile("wfi");
}

void board_system_off(void)
{
    board_exit(0);
}

/* ---------------------------------------------------------- cycle counter */

uint32_t board_cycles_hz = 20000000; /* replaced by the measured rate in board_init */
static uint64_t emu_overhead;
/* CMSDK timer counts per S32K cycle, Q8: measured at boot (and printed)
 * rather than assumed, so the alarm stays right if a QEMU version or machine
 * model clocks the timers differently. */
static uint32_t timer_per_lp_q8 = 625u << 8;

uint32_t board_cycles(void)
{
    return 0xffffffffu - REG32(TIMER0 + T_VALUE);
}

uint64_t board_emu_overhead_cycles(void)
{
    return emu_overhead;
}

/* --------------------------------------------------------------- console */

void board_putc(char c)
{
    while (REG32(UART0 + U_STATE) & 1u)
        ;
    REG32(UART0 + U_DATA) = (uint8_t)c;
}

void board_early_init(void)
{
    REG32(TIMER0 + T_CTRL) = 0;
    REG32(TIMER0 + T_RELOAD) = 0xffffffffu;
    REG32(TIMER0 + T_CTRL) = 1u;
    REG32(UART0 + U_BAUD) = 16;
    REG32(UART0 + U_CTRL) = 1u | 2u | 8u; /* TX, RX, RX interrupt */
}

/* ----------------------------------------------------- lp counter / alarm */

uint64_t board_lp_read(void)
{
    return 0xffffffffu - REG32(S32KTIMER + T_VALUE);
}

void board_lp_alarm(uint64_t compare_raw)
{
    uint32_t now = (uint32_t)board_lp_read();
    uint32_t delta = (uint32_t)compare_raw - now;
    uint64_t cycles;
    if (delta == 0 || delta > 0x80000000u)
        delta = 1; /* already due */
    cycles = ((uint64_t)delta * timer_per_lp_q8) >> 8;
    if (cycles > 0xffffffffu)
        cycles = 0xffffffffu;
    REG32(DUALTIMER + DT_CTRL) = 0;
    REG32(DUALTIMER + DT_INTCLR) = 1;
    REG32(DUALTIMER + DT_LOAD) = (uint32_t)cycles;
    REG32(DUALTIMER + DT_CTRL) = DT_EN | DT_IE | DT_32BIT | DT_ONESHOT;
}

void board_lp_alarm_cancel(void)
{
    REG32(DUALTIMER + DT_CTRL) = 0;
    REG32(DUALTIMER + DT_INTCLR) = 1;
    NVIC_ICPR(0) = 1u << IRQ_DUALTIMER;
}

void board_sleep(enum pm_state st, uint32_t wake_mask)
{
    (void)wake_mask; /* QEMU wakes on any enabled IRQ */
    if (st >= PM_DEEP)
        SCB_SCR |= SCB_SCR_SLEEPDEEP;
    cpu_wfi();
    SCB_SCR &= ~SCB_SCR_SLEEPDEEP;
}

void board_irq(unsigned irq)
{
    switch (irq) {
    case IRQ_DUALTIMER:
        REG32(DUALTIMER + DT_CTRL) = 0;
        REG32(DUALTIMER + DT_INTCLR) = 1;
        kernel_lp_alarm_isr();
        break;
    case IRQ_UART0_RX:
    case IRQ_UART0_RX_OLDQEMU:
        /* clear first, then drain: a byte landing after the last read
         * re-raises the interrupt instead of being stranded in RXFULL */
        REG32(UART0 + U_INT) = 2u;
        while (REG32(UART0 + U_STATE) & 2u)
            console_rx_isr((char)REG32(UART0 + U_DATA));
        break;
    case IRQ_S32K:
        REG32(S32KTIMER + T_INT) = 1;
        break;
    default:
        kprintf("unexpected irq %u\n", irq);
        nvic_disable(irq);
        break;
    }
}

/* ------------------------------------------------------------- display */

static struct mip_panel vpanel;
static uint64_t spi_bytes, spi_frames;

void board_display_xfer(const uint8_t *buf, size_t n)
{
    uint32_t c0 = board_cycles();
    spi_bytes += n;
    spi_frames++;
    if (mip_panel_feed(&vpanel, buf, n) < 0)
        kprintf("virtual panel: protocol error in a %u-byte frame\n", (unsigned)n);
    emu_overhead += (uint32_t)(board_cycles() - c0);
}

void board_snapshot(const char *tag)
{
    static char pbm[32 + MIP_H * MIP_STRIDE];
    static uint8_t fb[MIP_H][MIP_STRIDE];
    char name[48];
    int hdr = k_snprintf(pbm, sizeof(pbm), "P4\n%d %d\n", MIP_W, MIP_H);
    mip_panel_to_fb(&vpanel, fb);
    k_memcpy(pbm + hdr, fb, sizeof(fb));
    k_snprintf(name, sizeof(name), "%s.pbm", tag);
    board_write_file(name, pbm, (size_t)hdr + sizeof(fb));
    kprintf("snapshot %s: panel frames %u, rows written %u, protocol errors %u, spi bytes %u\n",
            name, (unsigned)vpanel.frames, (unsigned)vpanel.lines_written,
            (unsigned)vpanel.errors, (unsigned)spi_bytes);
}

/* ------------------------------------------------------ synthetic sensors */

static struct synth_accel acc;
static struct synth_ppg ppg;
static uint64_t acc_t_us, ppg_t_us;
static int ppg_on;

/* One-hour activity script (repeats). Times in seconds. */
static const struct { uint16_t until; uint8_t act; uint16_t cad_mhz, amp, bpm; } script[] = {
    { 300, ACT_REST, 0, 0, 62 },
    { 1200, ACT_WALK, 1800, 250, 88 },
    { 1500, ACT_RUN, 2700, 550, 135 },
    { 2100, ACT_TYPING, 0, 0, 74 },
    { 2400, ACT_GESTURE, 0, 0, 78 },
    { 3300, ACT_WALK, 2000, 280, 92 },
    { 3600, ACT_REST, 0, 0, 66 },
};

static int script_at(uint64_t t_us)
{
    unsigned s = (unsigned)(t_us / 1000000u % 3600u), i;
    for (i = 0; i < sizeof(script) / sizeof(script[0]) - 1; i++)
        if (s < script[i].until)
            return (int)i;
    return (int)(sizeof(script) / sizeof(script[0]) - 1);
}

int board_accel_read(int16_t (*xyz)[3], int max)
{
    uint64_t now = uptime_us();
    uint32_t c0 = board_cycles();
    int n = 0;
    while (n < max && acc_t_us + 1000000u / 25u <= now) {
        int k = script_at(acc_t_us);
        if (acc.act != (enum synth_activity)script[k].act)
            synth_accel_set(&acc, (enum synth_activity)script[k].act, script[k].cad_mhz,
                            script[k].amp);
        synth_accel_sample(&acc, xyz[n++]);
        acc_t_us += 1000000u / 25u;
    }
    emu_overhead += (uint32_t)(board_cycles() - c0);
    return n;
}

void board_ppg_enable(int on)
{
    ppg_on = on;
    ppg_t_us = uptime_us();
}

int board_ppg_read(int32_t *out, int max)
{
    uint64_t now = uptime_us();
    uint32_t c0 = board_cycles();
    int n = 0;
    if (!ppg_on)
        return 0;
    while (n < max && ppg_t_us + 1000000u / 25u <= now) {
        int k = script_at(ppg_t_us);
        synth_ppg_set(&ppg, script[k].bpm * 10u, 600, 20, 0);
        out[n++] = synth_ppg_sample(&ppg);
        ppg_t_us += 1000000u / 25u;
    }
    emu_overhead += (uint32_t)(board_cycles() - c0);
    return n;
}

unsigned board_battery_pct(int *charging)
{
    *charging = 0;
    return 87; /* no fuel gauge in the emulator: fixed value */
}

uint64_t board_true_steps(void)
{
    return acc.true_steps;
}

void board_init(void)
{
    /* free-running low-power counter */
    REG32(S32KTIMER + T_CTRL) = 0;
    REG32(S32KTIMER + T_RELOAD) = 0xffffffffu;
    REG32(S32KTIMER + T_CTRL) = 1u;
    {
        /* calibrate the CMSDK timer clock against the low-power counter */
        uint32_t l0 = (uint32_t)board_lp_read(), l1, c0, c1;
        while ((l1 = (uint32_t)board_lp_read()) == l0)
            ;
        c0 = board_cycles();
        while ((uint32_t)board_lp_read() - l1 < 320)
            ;
        c1 = board_cycles();
        timer_per_lp_q8 = (uint32_t)(((uint64_t)(c1 - c0) << 8) / 320);
        board_cycles_hz = (uint32_t)(((uint64_t)timer_per_lp_q8 * board.lp_hz) >> 8);
        kprintf("an505: CMSDK timer clock %u Hz (measured against S32K)\n",
                (unsigned)board_cycles_hz);
    }
    nvic_enable(IRQ_DUALTIMER, PRIO_TIMER);
    nvic_enable(IRQ_UART0_RX, PRIO_TIMER);
    nvic_enable(IRQ_UART0_RX_OLDQEMU, PRIO_TIMER);
    mip_panel_init(&vpanel);
    synth_accel_init(&acc, 25, 0x5eed1234u);
    synth_ppg_init(&ppg, 25, 0xbeef5678u);
    acc_t_us = 0;
}
