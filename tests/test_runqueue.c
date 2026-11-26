/* From orbit-os tests/test_runqueue.c (MIT), unchanged. */
#include "test.h"
#include "runqueue.h"

struct task {
    int id;
    struct rq_entry e;
};

static struct task *T(struct rq_entry *e)
{
    return e ? container_of(e, struct task, e) : NULL;
}

static void test_priority_order(void)
{
    struct runqueue rq;
    struct task t[5] = { {.id = 0}, {.id = 1}, {.id = 2}, {.id = 3}, {.id = 4} };
    rq_init(&rq);
    CHECK(rq_pop(&rq) == NULL);
    CHECK(rq_highest_prio(&rq) == -1);
    rq_push(&rq, &t[0].e, 5);
    rq_push(&rq, &t[1].e, 1);
    rq_push(&rq, &t[2].e, 3);
    rq_push(&rq, &t[3].e, 1);
    rq_push(&rq, &t[4].e, 7);
    CHECK(rq.count == 5);
    CHECK(rq_highest_prio(&rq) == 1);
    CHECK(T(rq_pop(&rq))->id == 1); /* prio 1, FIFO */
    CHECK(T(rq_pop(&rq))->id == 3);
    CHECK(T(rq_pop(&rq))->id == 2); /* prio 3 */
    CHECK(T(rq_pop(&rq))->id == 0); /* prio 5 */
    CHECK(T(rq_pop(&rq))->id == 4); /* prio 7 */
    CHECK(rq_pop(&rq) == NULL);
    CHECK(rq.bitmap == 0 && rq.count == 0);
}

static void test_round_robin(void)
{
    /* Simulate the scheduler: pop, run, push back -> rotates within a level. */
    struct runqueue rq;
    struct task t[3] = { {.id = 0}, {.id = 1}, {.id = 2} };
    int seq[9];
    rq_init(&rq);
    for (int i = 0; i < 3; i++)
        rq_push(&rq, &t[i].e, 2);
    for (int i = 0; i < 9; i++) {
        struct task *x = T(rq_pop(&rq));
        seq[i] = x->id;
        rq_push(&rq, &x->e, 2);
    }
    for (int i = 0; i < 9; i++)
        CHECK(seq[i] == i % 3);
}

static void test_remove_and_errors(void)
{
    struct runqueue rq;
    struct task a = {.id = 0}, b = {.id = 1};
    rq_init(&rq);
    CHECK(rq_push(&rq, &a.e, -1) == -1);
    CHECK(rq_push(&rq, &a.e, RQ_NPRIO) == -1);
    CHECK(rq_push(&rq, &a.e, 4) == 0);
    CHECK(rq_push(&rq, &a.e, 4) == -1); /* already queued */
    CHECK(rq_push(&rq, &b.e, 4) == 0);
    CHECK(rq_remove(&rq, &a.e) == 0);
    CHECK(rq_remove(&rq, &a.e) == -1);
    CHECK(rq.bitmap == (1u << 4));
    CHECK(rq_remove(&rq, &b.e) == 0);
    CHECK(rq.bitmap == 0);
    CHECK(rq_peek(&rq) == NULL);
}

int main(void)
{
    RUN(test_priority_order);
    RUN(test_round_robin);
    RUN(test_remove_and_errors);
    TEST_REPORT("test_runqueue");
}
