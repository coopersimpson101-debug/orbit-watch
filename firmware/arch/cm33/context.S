/* PendSV context switch for Cortex-M33 (ARMv8-M Mainline, no FPU context).
 *
 * Exception entry has already pushed r0-r3, r12, lr, pc, xPSR onto the
 * outgoing thread's PSP. We push the callee-saved r4-r11 plus EXC_RETURN,
 * let the scheduler pick the next thread, then unwind its frame the same
 * way. PSPLIM is zeroed while PSP moves, then set to the new thread's limit
 * (a later overflow raises a UsageFault STKOF instead of corrupting memory).
 *
 *   struct thread { uint32_t *sp; uint32_t *stack_limit; ... }
 */
    .syntax unified
    .thumb
    .text

    .global PendSV_Handler
    .type PendSV_Handler, %function
    .thumb_func
PendSV_Handler:
    cpsid   i
    mrs     r0, psp
    ldr     r3, =sched_cur
    ldr     r1, [r3]
    cbz     r1, 1f                  /* first switch: nothing to save */
    stmdb   r0!, {r4-r11, lr}
    str     r0, [r1]                /* cur->sp */
1:  bl      sched_switch_hook       /* r0 = next thread */
    ldr     r1, [r0, #4]            /* next->stack_limit */
    movs    r2, #0
    msr     psplim, r2
    ldr     r2, [r0]                /* next->sp */
    ldmia   r2!, {r4-r11, lr}
    msr     psp, r2
    msr     psplim, r1
    isb
    cpsie   i
    bx      lr
    .size PendSV_Handler, . - PendSV_Handler
