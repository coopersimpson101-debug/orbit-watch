/* Preemptive priority scheduler for Cortex-M33. See sched.h. */
#include "sched.h"
#include "board.h"
#include "buddy.h"
#include "cm33.h"
#include "kernel.h"
#include "kstring.h"

struct thread *volatile sched_cur;

static struct thread threads[OW_MAX_THREADS];
static int nthreads;
static struct runqueue rq;
static struct timerq timers;
static volatile uint64_t ticks;

#define STACK_FILL 0xdeadbeefu

uint64_t ticks_now(void)
{
    uint32_t f = irq_save();
    uint64_t t = ticks;
    irq_restore(f);
    return t;
}

uint64_t uptime_us(void)
{
    return ticks_now() * (1000000u / OW_HZ);
}

uint64_t sched_lp_now(void)
{
    return board_lp_read();
}

static void thread_exit(void)
{
    uint32_t f = irq_save();
    sched_cur->state = T_DEAD;
    irq_restore(f);
    pendsv_trigger();
    for (;;)
        __asm__ volatile("nop");
}

static void timeout_fn(struct ktimer *t, void *arg)
{
    struct thread *th = arg;
    (void)t;
    th->timed_out = 1;
    if (th->state == T_BLOCKED) {
        th->state = T_READY;
        rq_push(&rq, &th->rq, th->prio);
    }
}

void sched_init(void)
{
    rq_init(&rq);
    tq_init(&timers);
    nthreads = 0;
    ticks = 0;
    sched_cur = 0;
}

struct thread *thread_create(const char *name, void (*fn)(void *), void *arg, int prio,
                             size_t stack_bytes)
{
    struct thread *t;
    uint32_t *sp, *base;
    size_t i, words;
    unsigned order;
    if (nthreads >= OW_MAX_THREADS || prio < 0 || prio >= RQ_NPRIO)
        return 0;
    order = buddy_order_for(stack_bytes);
    base = buddy_alloc(&kernel_pages, order);
    if (!base)
        return 0;
    t = &threads[nthreads];
    k_memset(t, 0, sizeof(*t));
    t->id = nthreads;
    t->name = name;
    t->prio = prio;
    t->stack_base = base;
    t->stack_size = PAGE_SIZE << order;
    words = t->stack_size / 4;
    for (i = 0; i < words; i++)
        base[i] = STACK_FILL;
    t->stack_limit = base + 8; /* keep a small guard band above PSPLIM */
    sp = base + words;
    /* hardware-stacked frame (popped by exception return) */
    *--sp = 0x01000000u;              /* xPSR: Thumb bit */
    *--sp = (uint32_t)(uintptr_t)fn;  /* PC */
    *--sp = (uint32_t)(uintptr_t)thread_exit; /* LR */
    *--sp = 0;                        /* r12 */
    *--sp = 0;                        /* r3 */
    *--sp = 0;                        /* r2 */
    *--sp = 0;                        /* r1 */
    *--sp = (uint32_t)(uintptr_t)arg; /* r0 */
    /* software-saved part: r4-r11 + EXC_RETURN (see context.S) */
    *--sp = 0xFFFFFFFDu; /* Thread mode, PSP, Secure, basic frame */
    for (i = 0; i < 8; i++)
        *--sp = 0;
    t->sp = sp;
    ktimer_setup(&t->timeout, timeout_fn, t);
    t->state = T_READY;
    rq_push(&rq, &t->rq, prio);
    nthreads++;
    return t;
}

int sched_thread_count(void)
{
    return nthreads;
}

struct thread *sched_thread(int i)
{
    return i >= 0 && i < nthreads ? &threads[i] : 0;
}

size_t thread_stack_high_water(const struct thread *t)
{
    size_t i, words = t->stack_size / 4;
    for (i = 0; i < words && t->stack_base[i] == STACK_FILL; i++)
        ;
    return (words - i) * 4;
}

/* Called from PendSV with interrupts masked. */
struct thread *sched_switch_hook(void)
{
    struct thread *prev = sched_cur, *next;
    struct rq_entry *e;
    uint32_t now_c = board_cycles();
    if (prev) {
        prev->cpu_cycles += (uint32_t)(now_c - prev->run_start);
        if (prev->state == T_RUNNING) {
            prev->state = T_READY;
            rq_push(&rq, &prev->rq, prev->prio);
        }
    }
    e = rq_pop(&rq);
    next = container_of(e, struct thread, rq); /* idle is always runnable */
    next->state = T_RUNNING;
    next->slice = OW_SLICE_TICKS;
    next->run_start = now_c;
    if (next != prev)
        next->switches++;
    sched_cur = next;
    return next;
}

static void maybe_preempt(void)
{
    int hp = rq_highest_prio(&rq);
    if (sched_cur && hp >= 0 && hp < sched_cur->prio)
        pendsv_trigger();
}

void ktimer_start(struct ktimer *t, uint64_t expires_tick)
{
    uint32_t f = irq_save();
    tq_add(&timers, t, expires_tick);
    irq_restore(f);
}

void ktimer_stop(struct ktimer *t)
{
    uint32_t f = irq_save();
    tq_del(&timers, t);
    irq_restore(f);
}

uint64_t sched_next_event(void)
{
    uint64_t n = tq_next(&timers);
    /* round-robin: another thread at the running level needs a slice end */
    if (sched_cur && rq_highest_prio(&rq) == sched_cur->prio && sched_cur->prio != PRIO_IDLE) {
        uint64_t s = ticks + sched_cur->slice;
        if (s < n)
            n = s;
    }
    return n;
}

static void run_timers(void)
{
    struct ktimer *t;
    while ((t = tq_pop_expired(&timers, ticks)) != 0)
        t->fn(t, t->arg);
}

void sched_tick_isr(void)
{
    uint32_t f = irq_save();
    ticks++;
    run_timers();
    if (sched_cur && sched_cur->prio != PRIO_IDLE && sched_cur->slice && --sched_cur->slice == 0 &&
        rq_highest_prio(&rq) == sched_cur->prio)
        pendsv_trigger();
    maybe_preempt();
    irq_restore(f);
}

void sched_advance(uint64_t abs_tick)
{
    uint32_t f = irq_save();
    if (abs_tick > ticks)
        ticks = abs_tick;
    run_timers();
    maybe_preempt();
    irq_restore(f);
}

void sched_start(void)
{
    /* PSP must be valid before the first PendSV even though nothing is saved */
    static uint32_t boot_psp[16];
    __asm__ volatile("msr psp, %0" ::"r"(boot_psp + 16));
    SCB_SHPR3 = (SCB_SHPR3 & 0x0000FFFFu) | ((uint32_t)PRIO_TIMER << 24) |
                ((uint32_t)PRIO_PENDSV << 16); /* SysTick, PendSV */
    sched_cur = 0;
    pendsv_trigger();
    __asm__ volatile("cpsie i" ::: "memory");
    for (;;)
        cpu_wfi(); /* not reached: PendSV switches to the first thread */
}

void thread_yield(void)
{
    pendsv_trigger();
}

uint32_t ev_wait(uint32_t mask, uint32_t timeout)
{
    struct thread *me = sched_cur;
    uint32_t got;
    uint32_t f = irq_save();
    me->timed_out = 0;
    if (!(me->ev_pending & mask) && timeout)
        tq_add(&timers, &me->timeout, ticks + timeout);
    /* Loop, do not assume one PendSV = one block: a pended exception may be
     * recognised a few instructions late (QEMU 8.2 takes it only after the
     * next irq_save/irq_restore pair), and a stale wake-up must not return. */
    while (!(me->ev_pending & mask) && !me->timed_out) {
        me->ev_mask = mask;
        me->state = T_BLOCKED;
        irq_restore(f);
        pendsv_trigger(); /* the switch happens here, with interrupts enabled */
        f = irq_save();
    }
    if (me->timeout.armed)
        tq_del(&timers, &me->timeout);
    if (me->state != T_RUNNING) {
        /* woken before the pended switch ran: we are still on the CPU */
        if (me->rq.queued)
            rq_remove(&rq, &me->rq);
        me->state = T_RUNNING;
    }
    got = me->ev_pending & mask;
    me->ev_pending &= ~got;
    me->ev_mask = 0;
    irq_restore(f);
    return got;
}

void ev_post(struct thread *t, uint32_t bits)
{
    uint32_t f = irq_save();
    t->ev_pending |= bits;
    if (t->state == T_BLOCKED && (t->ev_pending & t->ev_mask)) {
        t->state = T_READY;
        rq_push(&rq, &t->rq, t->prio);
        maybe_preempt();
    }
    irq_restore(f);
}

void thread_sleep(uint32_t n)
{
    ev_wait(0, n ? n : 1);
}
