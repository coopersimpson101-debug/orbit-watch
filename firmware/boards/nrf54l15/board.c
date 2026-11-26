/* Board support for the orbit-watch rev A board (nRF54L15-QFAA).
 *
 * STATUS: compiles and links in CI; has NOT run on silicon. Register
 * addresses come from Nordic's MDK (see nrf54l15_regs.h); sensor register
 * values from the BMI270 (BST-BMI270-DS000-08) and MAX30101 (19-7742)
 * datasheets. Expect bring-up fixes; docs/hardware-validation.md lists the
 * steps.
 *
 * Pin map (must match hardware/schematic/orbit_watch.py):
 *   display  SPIM00: SCK P2.01, MOSI P2.02; SCS P2.05 (GPIO, active high);
 *            DISP P2.00; EXTCOMIN P0.03 = GRTC PWM output (hardware VCOM
 *            toggling, no CPU wake-ups); EXTMODE tied high on the board
 *   I2C      TWIM21: SCL P1.11, SDA P1.12 (BMI270 0x68, MAX30101 0x57,
 *            nPM1300 0x6B)
 *   IRQs     BMI270 INT1 P1.13, MAX30101 INT P1.14, nPM1300 GPIO0 P1.10
 *            (all via GPIOTE20 PORT event, sense-low/high)
 *   buttons  P1.04 / P1.05 / P1.06 to GND, pull-ups
 *   motor    P1.07 -> AO3400A gate
 *   console  SEGGER-RTT-compatible block in RAM (lib/rtt.c)
 *   lp clock GRTC SYSCOUNTER (1 MHz, 52 bit) from the 32.768 kHz LFXO
 */
#include "board.h"
#include "cm33.h"
#include "kernel.h"
#include "kstring.h"
#include "mip.h"
#include "nrf54l15_regs.h"
#include "rtt.h"
#include "sched.h"

#define R(base, off) REG32((base) + (off))

const struct board_info board = {
    .name = "orbit-watch-revA (nRF54L15)",
    .cpu_hz = 128000000,
    .lp_hz = 1000000,
    .lp_bits = 52,
    .wake_latency_lp = 1000, /* HFXO restart budget, refine on hardware */
    .ram_bytes = 256 * 1024,
};

#define SYSCOUNTER_IDX 0 /* verify: index owned by the application core */
#define GRTC_CC_KERNEL 0
#define GRTC_IRQ_GROUP 2

#define PIN_BTN0 4
#define PIN_MOTOR 7
#define PIN_PMIC_INT 10
#define PIN_SCL 11
#define PIN_SDA 12
#define PIN_IMU_INT 13
#define PIN_PPG_INT 14
#define PIN_DISP 0  /* P2 */
#define PIN_SCK 1   /* P2 */
#define PIN_MOSI 2  /* P2 */
#define PIN_SCS 5   /* P2 */

#define ADDR_BMI270 0x68
#define ADDR_MAX30101 0x57
#define ADDR_NPM1300 0x6B

/* --------------------------------------------------------- cycle counter */

const uint32_t board_cycles_hz = 128000000;
#define DWT_CTRL   REG32(0xE0001000)
#define DWT_CYCCNT REG32(0xE0001004)
#define DEMCR      REG32(0xE000EDFC)

uint32_t board_cycles(void)
{
    return DWT_CYCCNT;
}

uint64_t board_emu_overhead_cycles(void)
{
    return 0;
}

/* ------------------------------------------------------------- RTT console */

static struct rtt_cb rtt __attribute__((section(".rtt"), used));
static char rtt_up[1024], rtt_down[64];

void board_putc(char c)
{
    rtt_write(&rtt, &c, 1);
}

void board_early_init(void)
{
    rtt_init(&rtt, rtt_up, sizeof(rtt_up), rtt_down, sizeof(rtt_down));
    DEMCR |= 1u << 24; /* TRCENA */
    DWT_CYCCNT = 0;
    DWT_CTRL |= 1u;    /* CYCCNTENA */
    R(NRF_REGULATORS, REG_VREGMAIN_DCDCEN) = 1; /* inductor fitted: use the DC/DC */
    R(NRF_CLOCK, CLOCK_LFCLK_SRC) = 1;          /* LFXO */
    R(NRF_CLOCK, CLOCK_EVENTS_LFCLKSTARTED) = 0;
    R(NRF_CLOCK, CLOCK_TASKS_LFCLKSTART) = 1;
    while (!R(NRF_CLOCK, CLOCK_EVENTS_LFCLKSTARTED))
        ;
}

/* ------------------------------------------------------------------ GRTC */

uint64_t board_lp_read(void)
{
    uint32_t lo, hi;
    R(NRF_GRTC, GRTC_SYSCOUNTER_ACTIVE(SYSCOUNTER_IDX)) = 1;
    do {
        lo = R(NRF_GRTC, GRTC_SYSCOUNTERL(SYSCOUNTER_IDX));
        hi = R(NRF_GRTC, GRTC_SYSCOUNTERH(SYSCOUNTER_IDX));
    } while (hi & GRTC_SYSCOUNTERH_BUSY);
    return ((uint64_t)(hi & 0xFFFFFu) << 32) | lo;
}

void board_lp_alarm(uint64_t compare_raw)
{
    R(NRF_GRTC, GRTC_EVENTS_COMPARE(GRTC_CC_KERNEL)) = 0;
    R(NRF_GRTC, GRTC_CC_CCL(GRTC_CC_KERNEL)) = (uint32_t)compare_raw;
    R(NRF_GRTC, GRTC_CC_CCH(GRTC_CC_KERNEL)) = (uint32_t)(compare_raw >> 32) & 0xFFFFFu;
    R(NRF_GRTC, GRTC_CC_CCEN(GRTC_CC_KERNEL)) = 1;
    R(NRF_GRTC, GRTC_INTENSET(GRTC_IRQ_GROUP)) = 1u << GRTC_CC_KERNEL;
}

void board_lp_alarm_cancel(void)
{
    R(NRF_GRTC, GRTC_INTENCLR(GRTC_IRQ_GROUP)) = 1u << GRTC_CC_KERNEL;
    R(NRF_GRTC, GRTC_CC_CCEN(GRTC_CC_KERNEL)) = 0;
    R(NRF_GRTC, GRTC_EVENTS_COMPARE(GRTC_CC_KERNEL)) = 0;
}

void board_sleep(enum pm_state st, uint32_t wake_mask)
{
    (void)wake_mask; /* wake sources are the enabled NVIC lines + GPIO sense */
    R(NRF_GRTC, GRTC_SYSCOUNTER_ACTIVE(SYSCOUNTER_IDX)) = 0; /* let it sleep */
    if (st >= PM_DEEP)
        SCB_SCR |= SCB_SCR_SLEEPDEEP;
    cpu_wfi();
    SCB_SCR &= ~SCB_SCR_SLEEPDEEP;
}

void board_system_off(void)
{
    /* buttons keep their SENSE configuration: a press resets the chip */
    R(NRF_REGULATORS, REG_SYSTEMOFF) = 1;
    for (;;)
        cpu_wfi();
}

/* ------------------------------------------------------------------ TWIM */

static int twim_xfer(uint8_t addr, const uint8_t *tx, size_t ntx, uint8_t *rx, size_t nrx)
{
    uint32_t guard = 1000000;
    R(NRF_TWIM21, TWIM_ADDRESS) = addr;
    R(NRF_TWIM21, TWIM_EVENTS_STOPPED) = 0;
    R(NRF_TWIM21, TWIM_EVENTS_ERROR) = 0;
    R(NRF_TWIM21, TWIM_TX_PTR) = (uint32_t)(uintptr_t)tx;
    R(NRF_TWIM21, TWIM_TX_MAXCNT) = (uint32_t)ntx;
    R(NRF_TWIM21, TWIM_RX_PTR) = (uint32_t)(uintptr_t)rx;
    R(NRF_TWIM21, TWIM_RX_MAXCNT) = (uint32_t)nrx;
    R(NRF_TWIM21, TWIM_SHORTS) = nrx ? (TWIM_SHORTS_LASTTX_RXSTART | TWIM_SHORTS_LASTRX_STOP)
                                     : TWIM_SHORTS_LASTTX_STOP;
    R(NRF_TWIM21, TWIM_TASKS_TX_START) = 1;
    while (!R(NRF_TWIM21, TWIM_EVENTS_STOPPED) && --guard) {
        if (R(NRF_TWIM21, TWIM_EVENTS_ERROR)) {
            R(NRF_TWIM21, TWIM_TASKS_STOP) = 1;
            return -1;
        }
    }
    return guard ? 0 : -2;
}

static int reg_write(uint8_t addr, uint8_t reg, uint8_t val)
{
    uint8_t b[2] = { reg, val };
    return twim_xfer(addr, b, 2, 0, 0);
}

static int reg_read(uint8_t addr, uint8_t reg, uint8_t *out, size_t n)
{
    return twim_xfer(addr, &reg, 1, out, n);
}

static void delay_us(uint32_t us)
{
    uint64_t end = board_lp_read() + us;
    while (board_lp_read() < end)
        ;
}

/* --------------------------------------------------------------- BMI270 */

/* Bosch's configuration blob (bmi270.c in BoschSensortec/BMI270-Sensor-API,
 * BSD-3-Clause) must be linked in for INIT_CTRL to succeed; it is not
 * vendored here. Weak empty default so the board still links. */
__attribute__((weak)) const uint8_t bmi270_config_file[1];
__attribute__((weak)) const size_t bmi270_config_file_len = 0;

static void bmi270_init(void)
{
    uint8_t id = 0, st = 0;
    size_t off;
    reg_read(ADDR_BMI270, 0x00, &id, 1);
    kprintf("bmi270: chip id 0x%02x (expect 0x24)\n", id);
    reg_write(ADDR_BMI270, 0x7C, 0x00); /* PWR_CONF: adv_power_save off */
    delay_us(450);
    reg_write(ADDR_BMI270, 0x59, 0x00); /* INIT_CTRL: prepare config load */
    for (off = 0; off < bmi270_config_file_len; off += 32) {
        uint8_t b[33];
        size_t n = bmi270_config_file_len - off < 32 ? bmi270_config_file_len - off : 32;
        reg_write(ADDR_BMI270, 0x5B, (uint8_t)((off / 2) & 0x0F)); /* INIT_ADDR_0 */
        reg_write(ADDR_BMI270, 0x5C, (uint8_t)((off / 2) >> 4));   /* INIT_ADDR_1 */
        b[0] = 0x5E;                                              /* INIT_DATA */
        k_memcpy(b + 1, bmi270_config_file + off, n);
        twim_xfer(ADDR_BMI270, b, n + 1, 0, 0);
    }
    reg_write(ADDR_BMI270, 0x59, 0x01); /* INIT_CTRL: config done */
    delay_us(20000);
    reg_read(ADDR_BMI270, 0x21, &st, 1);
    kprintf("bmi270: INTERNAL_STATUS 0x%02x (bit0 = init ok)%s\n", st,
            bmi270_config_file_len ? "" : " - config blob not linked");
    reg_write(ADDR_BMI270, 0x41, 0x01); /* ACC_RANGE +/-4 g: 8192 LSB/g */
    reg_write(ADDR_BMI270, 0x40, 0x16); /* ACC_CONF: 25 Hz, avg2, power-opt */
    reg_write(ADDR_BMI270, 0x49, 0x40); /* FIFO_CONFIG_1: accel, headerless */
    reg_write(ADDR_BMI270, 0x46, (uint8_t)(150 & 0xff)); /* FIFO_WTM: 25 frames */
    reg_write(ADDR_BMI270, 0x47, 0);
    reg_write(ADDR_BMI270, 0x53, 0x0A); /* INT1: output enable, active high */
    reg_write(ADDR_BMI270, 0x58, 0x02); /* INT_MAP_DATA: fwm -> INT1 */
    reg_write(ADDR_BMI270, 0x7D, 0x04); /* PWR_CTRL: accel on */
    reg_write(ADDR_BMI270, 0x7C, 0x03); /* PWR_CONF: adv_power_save + fifo wakeup */
}

int board_accel_read(int16_t (*xyz)[3], int max)
{
    uint8_t len[2], buf[6 * 32];
    int frames, i;
    if (reg_read(ADDR_BMI270, 0x24, len, 2))
        return 0;
    frames = ((len[1] & 0x3F) << 8 | len[0]) / 6;
    if (frames > max)
        frames = max;
    if (frames > 32)
        frames = 32;
    if (frames <= 0 || reg_read(ADDR_BMI270, 0x26, buf, (size_t)frames * 6))
        return 0;
    for (i = 0; i < frames; i++) {
        int k;
        for (k = 0; k < 3; k++) {
            int16_t raw = (int16_t)(buf[6 * i + 2 * k] | buf[6 * i + 2 * k + 1] << 8);
            xyz[i][k] = (int16_t)((int32_t)raw * 1000 / 8192); /* mg */
        }
    }
    return frames;
}

/* ------------------------------------------------------------- MAX30101 */

void board_ppg_enable(int on)
{
    if (!on) {
        reg_write(ADDR_MAX30101, 0x09, 0x80); /* SHDN */
        return;
    }
    reg_write(ADDR_MAX30101, 0x09, 0x40); /* RESET */
    delay_us(1000);
    reg_write(ADDR_MAX30101, 0x08, 0x2F); /* SMP_AVE=2, A_FULL at 15 free */
    reg_write(ADDR_MAX30101, 0x0A, 0x23); /* ADC 4096 nA FS, 50 sps, 411 us (18 bit) */
    reg_write(ADDR_MAX30101, 0x0E, 0x24); /* LED3 (green) 7.2 mA */
    reg_write(ADDR_MAX30101, 0x11, 0x03); /* SLOT1 = LED3 */
    reg_write(ADDR_MAX30101, 0x02, 0x80); /* A_FULL interrupt */
    reg_write(ADDR_MAX30101, 0x04, 0);    /* FIFO pointers */
    reg_write(ADDR_MAX30101, 0x05, 0);
    reg_write(ADDR_MAX30101, 0x06, 0);
    reg_write(ADDR_MAX30101, 0x09, 0x07); /* multi-LED mode */
}

int board_ppg_read(int32_t *out, int max)
{
    uint8_t wr = 0, rd = 0, buf[3 * 32];
    int n, i;
    reg_read(ADDR_MAX30101, 0x04, &wr, 1);
    reg_read(ADDR_MAX30101, 0x06, &rd, 1);
    n = (wr - rd) & 31;
    if (n > max)
        n = max;
    if (n <= 0 || reg_read(ADDR_MAX30101, 0x07, buf, (size_t)n * 3))
        return 0;
    for (i = 0; i < n; i++)
        out[i] = (int32_t)(((uint32_t)buf[3 * i] << 16 | buf[3 * i + 1] << 8 | buf[3 * i + 2]) &
                           0x3FFFF);
    return n;
}

/* --------------------------------------------------------------- nPM1300 */

unsigned board_battery_pct(int *charging)
{
    /* TODO(bring-up): read VBAT via the nPM1300 ADC and run Nordic's fuel
     * gauge model; until then report a placeholder. */
    *charging = 0;
    return 0;
}

/* --------------------------------------------------------------- display */

void board_display_xfer(const uint8_t *buf, size_t n)
{
    R(NRF_P2, GPIO_OUTSET) = 1u << PIN_SCS; /* SCS active high, >= 6 us setup */
    delay_us(6);
    R(NRF_SPIM00, SPIM_EVENTS_END) = 0;
    R(NRF_SPIM00, SPIM_TX_PTR) = (uint32_t)(uintptr_t)buf;
    R(NRF_SPIM00, SPIM_TX_MAXCNT) = (uint32_t)n;
    R(NRF_SPIM00, SPIM_RX_MAXCNT) = 0;
    R(NRF_SPIM00, SPIM_TASKS_START) = 1;
    while (!R(NRF_SPIM00, SPIM_EVENTS_END))
        ;
    delay_us(2); /* SCS hold */
    R(NRF_P2, GPIO_OUTCLR) = 1u << PIN_SCS;
}

/* ------------------------------------------------------------ interrupts */

void board_irq(unsigned irq)
{
    if (irq == IRQ_GRTC_2) {
        R(NRF_GRTC, GRTC_EVENTS_COMPARE(GRTC_CC_KERNEL)) = 0;
        kernel_lp_alarm_isr();
    } else if (irq == IRQ_GPIOTE20_0) {
        uint32_t latch = R(NRF_P1, GPIO_LATCH);
        R(NRF_P1, GPIO_LATCH) = latch; /* write-1-to-clear */
        R(NRF_GPIOTE20, GPIOTE_EVENTS_PORT0_SECURE) = 0;
        if (latch & (7u << PIN_BTN0))
            watch_button_isr((latch >> PIN_BTN0) & 7u);
        /* IMU / PPG / PMIC lines: the watch app currently polls their FIFOs
         * from kernel timers; switch to these interrupts during bring-up. */
    } else {
        nvic_disable(irq);
    }
}

void board_init(void)
{
    int p;
    /* GRTC: run the system counter, compare channel 0 -> kernel wake-up */
    R(NRF_GRTC, GRTC_MODE) = GRTC_MODE_SYSCOUNTEREN | GRTC_MODE_AUTOEN;
    R(NRF_GRTC, GRTC_TASKS_START) = 1;
    nvic_enable(IRQ_GRTC_2, PRIO_TIMER);
    /* buttons + sensor interrupt lines: pull-up, sense low, latched */
    R(NRF_P1, GPIO_DETECTMODE) = 1; /* LDETECT */
    for (p = PIN_BTN0; p < PIN_BTN0 + 3; p++)
        R(NRF_P1, GPIO_PIN_CNF(p)) = PIN_CNF_INPUT_PULLUP_SENSE_LOW;
    R(NRF_GPIOTE20, GPIOTE_INTENSET0) = GPIOTE_PORT0SECURE;
    nvic_enable(IRQ_GPIOTE20_0, PRIO_HIGH);
    R(NRF_P1, GPIO_PIN_CNF(PIN_MOTOR)) = PIN_CNF_OUTPUT;
    /* display pins */
    R(NRF_P2, GPIO_PIN_CNF(PIN_SCS)) = PIN_CNF_OUTPUT;
    R(NRF_P2, GPIO_PIN_CNF(PIN_DISP)) = PIN_CNF_OUTPUT;
    R(NRF_P2, GPIO_OUTSET) = 1u << PIN_DISP;
    R(NRF_SPIM00, SPIM_PSEL_SCK) = PSEL(2, PIN_SCK);
    R(NRF_SPIM00, SPIM_PSEL_MOSI) = PSEL(2, PIN_MOSI);
    R(NRF_SPIM00, SPIM_PSEL_MISO) = 0x80000000u; /* disconnected */
    R(NRF_SPIM00, SPIM_PSEL_CSN) = 0x80000000u;
    R(NRF_SPIM00, SPIM_PRESCALER) = 128;         /* 128 MHz / 128 = 1 MHz */
    R(NRF_SPIM00, SPIM_CONFIG) = 0;              /* MSB first, mode 0 */
    R(NRF_SPIM00, SPIM_ENABLE) = 7;
    /* EXTCOMIN: 1 Hz GRTC PWM on P0.03 (TODO bring-up: PWMCONFIG value) */
    /* I2C */
    R(NRF_TWIM21, TWIM_PSEL_SCL) = PSEL(1, PIN_SCL);
    R(NRF_TWIM21, TWIM_PSEL_SDA) = PSEL(1, PIN_SDA);
    R(NRF_TWIM21, TWIM_FREQUENCY) = TWIM_FREQ_K400;
    R(NRF_TWIM21, TWIM_ENABLE) = 6;
    bmi270_init();
    board_ppg_enable(0);
    (void)ADDR_NPM1300;
    (void)PIN_PMIC_INT;
    (void)PIN_IMU_INT;
    (void)PIN_PPG_INT;
}

/* ---------------------------------------------- emulator-only hooks: none */

void board_snapshot(const char *tag) { (void)tag; }
void board_write_file(const char *name, const char *data, size_t n)
{
    (void)name; (void)data; (void)n;
}
void board_exit(int code)
{
    (void)code;
    board_system_off();
}
uint64_t board_true_steps(void) { return 0; }
