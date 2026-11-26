/* Step counter. See steps.h for the algorithm. */
#include "steps.h"

#define MIN_GAP (STEPS_FS / 4)  /* 0.25 s -> max 4 steps/s */
#define MAX_GAP (STEPS_FS * 2)  /* 2.0 s ends a bout */

uint32_t ow_isqrt32(uint32_t v)
{
    uint32_t r = 0, bit = 1u << 30;
    while (bit > v)
        bit >>= 2;
    while (bit) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}

void steps_init(struct steps *s)
{
    int i;
    s->dc_q8 = -1;
    for (i = 0; i < 4; i++)
        s->ma[i] = 0;
    s->ma_i = s->ma_n = 0;
    s->env = 0;
    s->prev1 = s->prev2 = 0;
    s->armed = 1;
    s->n = 0;
    s->last_step_n = 0;
    s->run = 0;
    s->counting = 0;
    s->total = 0;
    s->candidates = 0;
    s->prev_gap = 0;
}

int steps_feed(struct steps *s, int16_t ax, int16_t ay, int16_t az)
{
    int32_t mag, x, y, thr, ax_ = ax, ay_ = ay, az_ = az;
    int ret = 0, i;

    mag = (int32_t)ow_isqrt32((uint32_t)(ax_ * ax_) + (uint32_t)(ay_ * ay_) + (uint32_t)(az_ * az_));
    if (s->dc_q8 < 0)
        s->dc_q8 = mag << 8;
    s->dc_q8 += ((mag << 8) - s->dc_q8) >> 5;
    x = mag - (s->dc_q8 >> 8);

    s->ma[s->ma_i] = x;
    s->ma_i = (s->ma_i + 1) & 3;
    if (s->ma_n < 4)
        s->ma_n++;
    y = 0;
    for (i = 0; i < 4; i++)
        y += s->ma[i];
    y /= 4;

    s->env -= s->env >> 5;
    if ((y < 0 ? -y : y) > s->env)
        s->env = y < 0 ? -y : y;
    thr = s->env / 2;
    if (thr < STEPS_MIN_THR)
        thr = STEPS_MIN_THR;

    if (y < -thr / 4)
        s->armed = 1;

    if (s->n >= 2 && s->armed && s->prev1 > s->prev2 && s->prev1 >= y && s->prev1 > thr) {
        uint32_t at = s->n - 1;
        uint32_t gap = at - s->last_step_n;
        s->armed = 0;
        if (s->run == 0 || gap > MAX_GAP) {
            /* first peak of a (new) bout */
            s->counting = 0;
            s->run = 1;
            s->last_step_n = at;
            s->prev_gap = 0;
            s->candidates++;
        } else if (gap >= MIN_GAP) {
            uint32_t pg = s->prev_gap;
            s->prev_gap = gap;
            s->last_step_n = at;
            s->candidates++;
            if (s->counting) {
                s->total++;
                ret = 1;
            } else if (pg && (gap > pg + pg / 2 || gap + pg / 2 < pg)) {
                s->run = 2; /* irregular: restart the search, keep this pair */
            } else if (++s->run >= STEPS_RUN) {
                s->counting = 1;
                s->total += (uint32_t)s->run;
                ret = s->run;
            }
        }
        /* gap < MIN_GAP: bounce on the same step, ignore */
    }
    if (s->run && s->n - s->last_step_n > MAX_GAP) {
        s->run = 0;
        s->counting = 0;
    }
    s->prev2 = s->prev1;
    s->prev1 = y;
    s->n++;
    return ret;
}

uint32_t steps_total(const struct steps *s)
{
    return s->total;
}
