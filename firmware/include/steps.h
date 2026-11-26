/* Wrist step counter from 3-axis accelerometer samples (integer only).
 *
 * Pipeline per sample (fs = STEPS_FS Hz, input in mg):
 *   1. |a| = isqrt(ax^2 + ay^2 + az^2)    orientation-independent magnitude
 *   2. DC removal: slow IIR tracks gravity (time constant ~1.3 s), x = |a| - dc
 *   3. 4-tap moving average                 suppresses >6 Hz jitter
 *   4. peak picking with an adaptive threshold: a decaying envelope of |x|,
 *      threshold = max(env / 2, STEPS_MIN_THR); a peak must be followed by
 *      a valley below -threshold/4 before the next one can count (hysteresis)
 *   5. timing gate: 0.25 s <= step interval <= 2.0 s
 *   6. regularity gate: the first STEPS_RUN consecutive steps of a bout are
 *      held back and only committed once the bout is confirmed; a gap
 *      > 2 s ends the bout. This rejects isolated arm gestures.
 * Approach after the "peak + adaptive threshold + regularity" pedometer
 * pattern (e.g. N. Zhao, "Full-Featured Pedometer Design Realized with
 * 3-Axis Digital Accelerometer", Analog Dialogue 44-06, 2010); the code and
 * constants are this project's own. */
#ifndef OW_STEPS_H
#define OW_STEPS_H

#include <stdint.h>

#define STEPS_FS 25
#define STEPS_RUN 4
#define STEPS_MIN_THR 40 /* mg */

struct steps {
    int32_t dc_q8;        /* gravity estimate, mg * 256 */
    int32_t ma[4];
    int ma_i, ma_n;
    int32_t env;          /* envelope of |x|, mg */
    int32_t prev1, prev2; /* last two filtered samples */
    int armed;            /* valley seen since last peak */
    uint32_t n;           /* samples processed */
    uint32_t last_step_n; /* sample index of last accepted step */
    int run;              /* consecutive regular steps in the current bout */
    int counting;         /* bout confirmed */
    uint32_t total;       /* committed steps */
    uint32_t candidates;  /* all peaks that passed the timing gate */
    uint32_t prev_gap;    /* previous step interval (samples), 0 = none */
};

void steps_init(struct steps *s);
/* Feed one sample; returns the number of steps committed by this sample
 * (0, 1 or STEPS_RUN when a bout gets confirmed). */
int  steps_feed(struct steps *s, int16_t ax, int16_t ay, int16_t az);
uint32_t steps_total(const struct steps *s);

uint32_t ow_isqrt32(uint32_t v);

#endif
