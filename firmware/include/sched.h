/* orbit-watch kernel: preemptive priority scheduler for Cortex-M33.
 *
 * Design carried over from orbit-os (priority run-queue with a bitmap,
 * round-robin within a level, sleeping threads on a timer queue), ported
 * from AArch64 exception levels to the M-profile model:
 *   - threads run in Thread mode on PSP, the kernel's ISRs on MSP
 *   - context switch in PendSV (lowest priority), so it never preempts ISRs
 *   - PSPLIM per thread: a stack overflow raises a UsageFault instead of
 *     silently corrupting the neighbouring stack
 *   - SysTick provides the periodic tick while the system is busy; the idle
 *     thread suppresses it (tickless.c) when nothing is due for a while
 * Threads block on event flags with an optional timeout; ISRs post events.
 */
#ifndef OW_SCHED_H
#define OW_SCHED_H

#include <stddef.h>
#include <stdint.h>
#include "runqueue.h"
#include "timerq.h"

#define OW_HZ 100            /* kernel tick rate */
#define OW_SLICE_TICKS 5     /* round-robin slice inside a priority level */
#define OW_MAX_THREADS 8
#define PRIO_IDLE (RQ_NPRIO - 1)

enum thread_state { T_READY, T_RUNNING, T_BLOCKED, T_DEAD };

struct thread {
    uint32_t *sp;            /* saved PSP (offset 0, used by context.S) */
    uint32_t *stack_limit;   /* PSPLIM value (offset 4, used by context.S) */
    struct rq_entry rq;
    struct ktimer timeout;
    const char *name;
    int id, prio;
    enum thread_state state;
    uint32_t ev_pending, ev_mask;
    uint32_t slice;
    uint64_t switches;
    uint64_t cpu_cycles;     /* board_cycles() spent running (incl. ISRs) */
    uint32_t run_start;
    uint32_t *stack_base;
    size_t stack_size;
};

extern struct thread *volatile sched_cur;

void sched_init(void);
struct thread *thread_create(const char *name, void (*fn)(void *), void *arg, int prio,
                             size_t stack_bytes);
void sched_start(void) __attribute__((noreturn));
void thread_yield(void);
void thread_sleep(uint32_t ticks);
/* Block until any event in `mask` is posted or `timeout` ticks pass
 * (0 = forever). Returns and clears the events received (0 on timeout). */
uint32_t ev_wait(uint32_t mask, uint32_t timeout);
void ev_post(struct thread *t, uint32_t bits); /* ISR-safe */

uint64_t ticks_now(void);
uint64_t uptime_us(void);
/* Timers: callbacks run with interrupts masked, keep them short. */
void ktimer_start(struct ktimer *t, uint64_t expires_tick);
void ktimer_stop(struct ktimer *t);
uint64_t sched_next_event(void);
/* Called by the SysTick ISR, and by idle after a tickless sleep. */
void sched_tick_isr(void);
void sched_advance(uint64_t ticks);
/* Low-power counter: board-provided free-running clock used for cpu
 * accounting and for tickless idle. */
uint64_t sched_lp_now(void);

int  sched_thread_count(void);
struct thread *sched_thread(int i);
size_t thread_stack_high_water(const struct thread *t);

/* Scheduler entry used by PendSV (context.S). */
struct thread *sched_switch_hook(void);

#endif
