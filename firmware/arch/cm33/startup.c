/* Cortex-M33 reset, vector table and fault reporting. */
#include <stddef.h>
#include <stdint.h>
#include "board.h"
#include "cm33.h"
#include "kernel.h"

#define OW_NIRQ 288 /* nRF54L15 needs ~270 vectors; the AN505 uses fewer */

extern uint32_t _estack[], _sidata[], _sdata[], _edata[], _sbss[], _ebss[], __msp_limit[];
extern int main(void);
void Reset_Handler(void);
void PendSV_Handler(void);
void SysTick_Handler(void);
void Fault_Handler(void);
void Irq_Handler(void);

__attribute__((section(".vectors"), used)) void (*const vector_table[16 + OW_NIRQ])(void) = {
    [0] = (void (*)(void))(uintptr_t)_estack,
    [1] = Reset_Handler,
    [2] = Fault_Handler,  /* NMI */
    [3] = Fault_Handler,  /* HardFault */
    [4] = Fault_Handler,  /* MemManage */
    [5] = Fault_Handler,  /* BusFault */
    [6] = Fault_Handler,  /* UsageFault (incl. stack-limit STKOF) */
    [7] = Fault_Handler,  /* SecureFault */
    [11] = Fault_Handler, /* SVCall: unused */
    [12] = Fault_Handler, /* DebugMonitor */
    [14] = PendSV_Handler,
    [15] = SysTick_Handler,
    [16 ... 16 + OW_NIRQ - 1] = Irq_Handler,
};

void Irq_Handler(void)
{
    uint32_t ipsr;
    __asm__ volatile("mrs %0, ipsr" : "=r"(ipsr));
    board_irq(ipsr - 16);
}

__attribute__((used)) void fault_report(uint32_t *frame, uint32_t exc_return)
{
    uint32_t ipsr;
    __asm__ volatile("mrs %0, ipsr" : "=r"(ipsr));
    kprintf("\n*** fault: exception %u  CFSR %08x  HFSR %08x  MMFAR %08x  BFAR %08x\n",
            (unsigned)ipsr, (unsigned)SCB_CFSR, (unsigned)SCB_HFSR, (unsigned)SCB_MMFAR,
            (unsigned)SCB_BFAR);
    kprintf("    pc %08x  lr %08x  xpsr %08x  sp %08x  exc_return %08x\n", (unsigned)frame[6],
            (unsigned)frame[5], (unsigned)frame[7], (unsigned)(uintptr_t)frame,
            (unsigned)exc_return);
    if (SCB_CFSR & (1u << 20))
        kputs("    UsageFault STKOF: a thread overflowed its stack (PSPLIM)\n");
    board_exit(3);
}

__attribute__((naked)) void Fault_Handler(void)
{
    __asm__ volatile(
        "tst lr, #4\n"
        "ite eq\n"
        "mrseq r0, msp\n"
        "mrsne r0, psp\n"
        "mov r1, lr\n"
        "ldr r2, =__msp_limit\n"
        "msr msplim, r2\n"
        "b fault_report\n");
}

void Reset_Handler(void)
{
    uint32_t *s, *d;
    __asm__ volatile("cpsid i");
    __asm__ volatile("msr msplim, %0" ::"r"(__msp_limit));
    for (s = _sidata, d = _sdata; d < _edata;)
        *d++ = *s++;
    for (d = _sbss; d < _ebss;)
        *d++ = 0;
    SCB_VTOR = (uint32_t)(uintptr_t)vector_table;
    SCB_CCR |= SCB_CCR_DIV_0_TRP;
    /* enable MemManage, BusFault, UsageFault, SecureFault as separate faults */
    SCB_SHCSR |= (1u << 16) | (1u << 17) | (1u << 18) | (1u << 19);
    main();
    board_exit(0);
}

/* Freestanding builds still get these calls from the compiler. */
void *memcpy(void *d, const void *s, size_t n)
{
    unsigned char *a = d;
    const unsigned char *b = s;
    while (n--)
        *a++ = *b++;
    return d;
}

void *memset(void *d, int c, size_t n)
{
    unsigned char *a = d;
    while (n--)
        *a++ = (unsigned char)c;
    return d;
}
