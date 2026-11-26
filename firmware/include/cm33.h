/* Cortex-M33 (ARMv8-M Mainline) core registers used by the kernel.
 * Addresses from the Armv8-M Architecture Reference Manual (System Control
 * Space at 0xE000E000). */
#ifndef OW_CM33_H
#define OW_CM33_H

#include <stdint.h>

#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))

/* SysTick */
#define SYST_CSR   REG32(0xE000E010)
#define SYST_RVR   REG32(0xE000E014)
#define SYST_CVR   REG32(0xE000E018)
#define SYST_CALIB REG32(0xE000E01C)
#define SYST_CSR_ENABLE    (1u << 0)
#define SYST_CSR_TICKINT   (1u << 1)
#define SYST_CSR_CLKSOURCE (1u << 2)
#define SYST_CSR_COUNTFLAG (1u << 16)

/* NVIC */
#define NVIC_ISER(n) REG32(0xE000E100 + 4 * (n))
#define NVIC_ICER(n) REG32(0xE000E180 + 4 * (n))
#define NVIC_ISPR(n) REG32(0xE000E200 + 4 * (n))
#define NVIC_ICPR(n) REG32(0xE000E280 + 4 * (n))
#define NVIC_IPR8(n) (*(volatile uint8_t *)(uintptr_t)(0xE000E400 + (n)))

/* System control block */
#define SCB_ICSR  REG32(0xE000ED04)
#define SCB_VTOR  REG32(0xE000ED08)
#define SCB_AIRCR REG32(0xE000ED0C)
#define SCB_SCR   REG32(0xE000ED10)
#define SCB_CCR   REG32(0xE000ED14)
#define SCB_SHPR2 REG32(0xE000ED1C)
#define SCB_SHPR3 REG32(0xE000ED20)
#define SCB_SHCSR REG32(0xE000ED24)
#define SCB_CFSR  REG32(0xE000ED28)
#define SCB_HFSR  REG32(0xE000ED2C)
#define SCB_MMFAR REG32(0xE000ED34)
#define SCB_BFAR  REG32(0xE000ED38)
#define SCB_ICSR_PENDSVSET (1u << 28)
#define SCB_SCR_SLEEPONEXIT (1u << 1)
#define SCB_SCR_SLEEPDEEP   (1u << 2)
#define SCB_SCR_SEVONPEND   (1u << 4)
#define SCB_AIRCR_KEY       (0x05FAu << 16)
#define SCB_AIRCR_SYSRESETREQ (1u << 2)
#define SCB_CCR_DIV_0_TRP   (1u << 4)
#define SCB_CCR_STKOFHFNMIGN (1u << 10)

static inline uint32_t irq_save(void)
{
    uint32_t primask;
    __asm__ volatile("mrs %0, primask\n cpsid i" : "=r"(primask)::"memory");
    return primask;
}

static inline void irq_restore(uint32_t primask)
{
    __asm__ volatile("msr primask, %0" ::"r"(primask) : "memory");
}

static inline void cpu_wfi(void)
{
    __asm__ volatile("dsb\n wfi\n isb" ::: "memory");
}

static inline void nvic_enable(unsigned irq, uint8_t prio)
{
    NVIC_IPR8(irq) = prio;
    NVIC_ICPR(irq >> 5) = 1u << (irq & 31);
    NVIC_ISER(irq >> 5) = 1u << (irq & 31);
}

static inline void nvic_disable(unsigned irq)
{
    NVIC_ICER(irq >> 5) = 1u << (irq & 31);
}

static inline void pendsv_trigger(void)
{
    SCB_ICSR = SCB_ICSR_PENDSVSET;
    /* make the pended exception take effect before the next instruction */
    __asm__ volatile("dsb\n isb" ::: "memory");
}

/* Interrupt priorities (upper bits significant; PendSV lowest). */
#define PRIO_HIGH   0x40
#define PRIO_TIMER  0x80
#define PRIO_PENDSV 0xFF

#endif
