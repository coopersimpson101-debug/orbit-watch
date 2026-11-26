/* Step counter against synthetic gait with known ground truth, and
 * rejection of non-walking motion. Signals: firmware/app/synth.c. */
#include <stdint.h>
#include "test.h"
#include "steps.h"
#include "synth.h"

struct res { uint32_t counted; uint64_t truth; };

static struct res run(enum synth_activity act, uint32_t cad_mhz, int32_t amp, int seconds,
                      uint32_t seed)
{
    struct synth_accel a;
    struct steps s;
    int16_t v[3];
    int i;
    struct res r;
    synth_accel_init(&a, STEPS_FS, seed);
    synth_accel_set(&a, act, cad_mhz, amp);
    steps_init(&s);
    for (i = 0; i < seconds * STEPS_FS; i++) {
        synth_accel_sample(&a, v);
        steps_feed(&s, v[0], v[1], v[2]);
    }
    r.counted = steps_total(&s);
    r.truth = a.true_steps;
    return r;
}

static void check_walk(uint32_t cad, int32_t amp, uint32_t seed)
{
    struct res r = run(cad > 2500 ? ACT_RUN : ACT_WALK, cad, amp, 120, seed);
    long err = (long)r.counted - (long)r.truth;
    printf("  cadence %.2f Hz amp %d mg: counted %u, truth %llu (err %+ld)\n", cad / 1000.0,
           amp, r.counted, (unsigned long long)r.truth, err);
    CHECK(r.truth > 100);
    CHECK(labs(err) * 100 <= (long)r.truth * 5); /* within 5 % */
}

static void test_walking(void)
{
    check_walk(1500, 180, 1);
    check_walk(1800, 250, 2);
    check_walk(2000, 300, 3);
    check_walk(2300, 350, 4);
    check_walk(2800, 600, 5); /* running */
}

static void test_rejection(void)
{
    struct res rest = run(ACT_REST, 0, 0, 300, 11);
    struct res typing = run(ACT_TYPING, 0, 0, 300, 12);
    struct res gest = run(ACT_GESTURE, 0, 0, 300, 13);
    printf("  5 min rest %u, typing %u, random gestures %u steps\n", rest.counted,
           typing.counted, gest.counted);
    CHECK(rest.counted == 0);
    CHECK(typing.counted == 0);
    CHECK(gest.counted <= 10); /* isolated swings must not open a bout */
}

static void test_bout_boundary(void)
{
    /* walking interrupted by rest: steps resume being counted after re-lock */
    struct synth_accel a;
    struct steps s;
    int16_t v[3];
    int i, seg;
    uint64_t truth = 0;
    synth_accel_init(&a, STEPS_FS, 99);
    steps_init(&s);
    for (seg = 0; seg < 6; seg++) {
        uint64_t before = a.true_steps;
        synth_accel_set(&a, seg & 1 ? ACT_REST : ACT_WALK, 1900, 260);
        for (i = 0; i < 20 * STEPS_FS; i++) {
            synth_accel_sample(&a, v);
            steps_feed(&s, v[0], v[1], v[2]);
        }
        truth += a.true_steps - before;
    }
    printf("  3 x 20 s walking with rests: counted %u, truth %llu\n", steps_total(&s),
           (unsigned long long)truth);
    CHECK(labs((long)steps_total(&s) - (long)truth) * 100 <= (long)truth * 6);
}

static void test_isqrt(void)
{
    uint32_t v;
    for (v = 0; v < 2000000; v += 997) {
        uint32_t r = ow_isqrt32(v);
        CHECK((uint64_t)r * r <= v && (uint64_t)(r + 1) * (r + 1) > v);
    }
    CHECK(ow_isqrt32(0xffffffffu) == 65535);
}

int main(void)
{
    RUN(test_isqrt);
    RUN(test_walking);
    RUN(test_rejection);
    RUN(test_bout_boundary);
    TEST_REPORT("test_steps");
}
