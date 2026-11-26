/* nRF54L15 application-core (Secure) register subset used by orbit-watch.
 *
 * Base addresses and offsets were transcribed from Nordic's MDK headers
 * (nrfx, bsp/stable/mdk/nrf54l/nrf54l15/nrf54l15_global.h and
 * nrf54l15_types.h, commit 5822caf, BSD-3-Clause). Only numbers are taken;
 * no MDK code is included. NOT YET EXERCISED ON SILICON.
 */
#ifndef OW_NRF54L15_REGS_H
#define OW_NRF54L15_REGS_H

#include "cm33.h"

#define NRF_SPIM00   0x5004A000u
#define NRF_P2       0x50050400u
#define NRF_UARTE20  0x500C6000u
#define NRF_TWIM21   0x500C7000u
#define NRF_P1       0x500D8200u
#define NRF_GPIOTE20 0x500DA000u
#define NRF_GRTC     0x500E2000u
#define NRF_P0       0x5010A000u
#define NRF_CLOCK    0x5010E000u
#define NRF_REGULATORS 0x50120000u

/* IRQ numbers (nrf54l15_application.h) */
#define IRQ_SERIAL00   74
#define IRQ_SERIAL21   199
#define IRQ_GPIOTE20_0 218
#define IRQ_GRTC_2     228 /* verify which GRTC line the Secure app core owns */

/* GPIO */
#define GPIO_OUTSET   0x004
#define GPIO_OUTCLR   0x008
#define GPIO_IN       0x00C
#define GPIO_DIRSET   0x014
#define GPIO_LATCH    0x020
#define GPIO_DETECTMODE 0x024
#define GPIO_PIN_CNF(n) (0x080 + 4 * (n))
#define PIN_CNF_OUTPUT  (1u << 0)
#define PIN_CNF_INPUT_PULLUP_SENSE_LOW ((3u << 2) | (3u << 16))

/* GPIOTE */
#define GPIOTE_EVENTS_PORT0_SECURE 0x144
#define GPIOTE_INTENSET0 0x304
#define GPIOTE_PORT0SECURE (1u << 17)

/* GRTC */
#define GRTC_TASKS_START    0x060
#define GRTC_EVENTS_COMPARE(n) (0x100 + 4 * (n))
#define GRTC_INTENSET(g)    (0x304 + 0x10 * (g))
#define GRTC_INTENCLR(g)    (0x308 + 0x10 * (g))
#define GRTC_MODE           0x510
#define GRTC_MODE_AUTOEN    (1u << 0)
#define GRTC_MODE_SYSCOUNTEREN (1u << 1)
#define GRTC_CC_CCL(n)      (0x520 + 0x10 * (n))
#define GRTC_CC_CCH(n)      (0x524 + 0x10 * (n))
#define GRTC_CC_CCEN(n)     (0x52C + 0x10 * (n))
#define GRTC_PWMCONFIG      0x710
#define GRTC_TASKS_PWMSTART 0x06C
#define GRTC_SYSCOUNTERL(i) (0x720 + 0x10 * (i))
#define GRTC_SYSCOUNTERH(i) (0x724 + 0x10 * (i))
#define GRTC_SYSCOUNTER_ACTIVE(i) (0x728 + 0x10 * (i))
#define GRTC_SYSCOUNTERH_BUSY (1u << 30)

/* SPIM */
#define SPIM_TASKS_START 0x000
#define SPIM_EVENTS_END  0x108
#define SPIM_ENABLE      0x500
#define SPIM_PRESCALER   0x52C
#define SPIM_CONFIG      0x554
#define SPIM_PSEL_SCK    0x600
#define SPIM_PSEL_MOSI   0x604
#define SPIM_PSEL_MISO   0x608
#define SPIM_PSEL_CSN    0x610
#define SPIM_RX_MAXCNT   0x708
#define SPIM_TX_PTR      0x73C
#define SPIM_TX_MAXCNT   0x740

/* TWIM */
#define TWIM_TASKS_STOP     0x004
#define TWIM_TASKS_RX_START 0x028
#define TWIM_TASKS_TX_START 0x050
#define TWIM_EVENTS_STOPPED 0x104
#define TWIM_EVENTS_ERROR   0x114
#define TWIM_SHORTS         0x200
#define TWIM_ERRORSRC       0x4C4
#define TWIM_ENABLE         0x500
#define TWIM_FREQUENCY      0x524
#define TWIM_ADDRESS        0x588
#define TWIM_PSEL_SCL       0x600
#define TWIM_PSEL_SDA       0x604
#define TWIM_RX_PTR         0x704
#define TWIM_RX_MAXCNT      0x708
#define TWIM_TX_PTR         0x73C
#define TWIM_TX_MAXCNT      0x740
#define TWIM_SHORTS_LASTTX_RXSTART (1u << 7)
#define TWIM_SHORTS_LASTTX_STOP    (1u << 9)
#define TWIM_SHORTS_LASTRX_STOP    (1u << 12)
#define TWIM_FREQ_K400 0x06400000u

/* CLOCK / REGULATORS */
#define CLOCK_TASKS_LFCLKSTART   0x010
#define CLOCK_EVENTS_LFCLKSTARTED 0x108
#define CLOCK_LFCLK_SRC          0x440
#define REG_SYSTEMOFF            0x500
#define REG_VREGMAIN_DCDCEN      0x600

#define PSEL(port, pin) (((uint32_t)(port) << 5) | (uint32_t)(pin))

#endif
