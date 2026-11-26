/* Kernel timer queue ordering, deletion, periodic re-arm and catch-up. */
#include <stdint.h>
#include "test.h"
#include "timerq.h"

static int fired[8];
static void cb(struct ktimer *t, void *arg) { (void)t; fired[(intptr_t)arg]++; }

static void test_order(void)
{
    struct timerq q;
    struct ktimer t[5];
    uint64_t exp[5] = {50, 10, 30, 10, 20};
    int i, order[5], n = 0;
    struct ktimer *x;
    tq_init(&q);
    CHECK(tq_next(&q) == TQ_NEVER);
    for (i = 0; i < 5; i++) {
        ktimer_setup(&t[i], cb, (void *)(intptr_t)i);
        tq_add(&q, &t[i], exp[i]);
    }
    CHECK(q.count == 5);
    CHECK(tq_next(&q) == 10);
    CHECK(tq_pop_expired(&q, 9) == NULL);
    while ((x = tq_pop_expired(&q, 100)) != NULL)
        order[n++] = (int)(x - t);
    CHECK(n == 5);
    /* FIFO among equal expiries: t1 before t3 */
    CHECK(order[0] == 1 && order[1] == 3 && order[2] == 4 && order[3] == 2 && order[4] == 0);
    CHECK(q.count == 0);
}

static void test_del_rearm(void)
{
    struct timerq q;
    struct ktimer a, b;
    tq_init(&q);
    ktimer_setup(&a, cb, 0);
    ktimer_setup(&b, cb, 0);
    tq_add(&q, &a, 5);
    tq_add(&q, &b, 7);
    CHECK(tq_del(&q, &a) == 0);
    CHECK(tq_del(&q, &a) == -1);
    CHECK(tq_next(&q) == 7);
    tq_add(&q, &b, 3); /* re-arming moves it */
    CHECK(q.count == 1 && tq_next(&q) == 3);
}

static void test_periodic(void)
{
    struct timerq q;
    struct ktimer p;
    struct ktimer *x;
    tq_init(&q);
    ktimer_setup(&p, cb, (void *)(intptr_t)7);
    p.period = 100;
    tq_add(&q, &p, 100);
    x = tq_pop_expired(&q, 100);
    CHECK(x == &p && p.armed && p.expires == 200);
    /* slept through 3.5 periods: next expiry realigned to the grid, once */
    x = tq_pop_expired(&q, 550);
    CHECK(x == &p && p.expires == 600);
    CHECK(tq_pop_expired(&q, 550) == NULL);
}

int main(void)
{
    RUN(test_order);
    RUN(test_del_rearm);
    RUN(test_periodic);
    TEST_REPORT("test_timerq");
}
