/* PPG heart-rate estimator. See hr.h for the pipeline. */
#include "hr.h"
#include "hr_coeffs.h"

#define REFRACTORY_Q8 (HR_FS * 256 * 3 / 10)   /* 0.30 s */
#define IBI_MIN_Q8    (HR_FS * 256 * 3 / 10)   /* 200 bpm */
#define IBI_MAX_Q8    (HR_FS * 256 * 2)        /* 30 bpm */

static void bq_init(struct hr_biquad *q, int32_t b0, int32_t b1, int32_t b2, int32_t a1, int32_t a2)
{
    q->b0 = b0; q->b1 = b1; q->b2 = b2; q->a1 = a1; q->a2 = a2;
    q->x1 = q->x2 = q->y1 = q->y2 = 0;
}

static int32_t bq_run(struct hr_biquad *q, int32_t x)
{
    int64_t acc = (int64_t)q->b0 * x + (int64_t)q->b1 * q->x1 + (int64_t)q->b2 * q->x2
                - (int64_t)q->a1 * q->y1 - (int64_t)q->a2 * q->y2;
    int32_t y = (int32_t)((acc + ((int64_t)1 << (HR_COEF_Q - 1))) >> HR_COEF_Q);
    q->x2 = q->x1;
    q->x1 = x;
    q->y2 = q->y1;
    q->y1 = y;
    return y;
}

void hr_init(struct hr *h)
{
    int i;
    bq_init(&h->hp, HR_HP_B0, HR_HP_B1, HR_HP_B2, HR_HP_A1, HR_HP_A2);
    bq_init(&h->lp, HR_LP_B0, HR_LP_B1, HR_LP_B2, HR_LP_A1, HR_LP_A2);
    h->primed = 0;
    h->p1 = h->p2 = 0;
    h->env = 0;
    h->dc = 0;
    h->n = 0;
    h->last_peak_q8 = 0;
    h->have_peak = 0;
    for (i = 0; i < HR_NIBI; i++)
        h->ibi_q8[i] = 0;
    h->nibi = h->head = 0;
    h->rejects = 0;
    h->beats = h->rejected_total = 0;
    h->bpm = 0;
}

static uint16_t median_ibi(const struct hr *h)
{
    uint16_t v[HR_NIBI];
    int i, j, n = h->nibi;
    for (i = 0; i < n; i++)
        v[i] = h->ibi_q8[i];
    for (i = 1; i < n; i++) { /* insertion sort, n <= 8 */
        uint16_t t = v[i];
        for (j = i; j > 0 && v[j - 1] > t; j--)
            v[j] = v[j - 1];
        v[j] = t;
    }
    if (!n)
        return 0;
    return n & 1 ? v[n / 2] : (uint16_t)((v[n / 2 - 1] + v[n / 2]) / 2);
}

static void push_ibi(struct hr *h, uint16_t ibi)
{
    h->ibi_q8[h->head] = ibi;
    h->head = (h->head + 1) % HR_NIBI;
    if (h->nibi < HR_NIBI)
        h->nibi++;
}

static void accept_beat(struct hr *h, int64_t t_q8)
{
    int64_t ibi;
    h->beats++;
    if (!h->have_peak) {
        h->have_peak = 1;
        h->last_peak_q8 = t_q8;
        return;
    }
    ibi = t_q8 - h->last_peak_q8;
    h->last_peak_q8 = t_q8;
    if (ibi < IBI_MIN_Q8 || ibi > IBI_MAX_Q8) {
        h->rejected_total++;
        if (++h->rejects >= 4)
            h->nibi = h->head = h->rejects = 0;
        return;
    }
    if (h->nibi >= 3) {
        int32_t med = median_ibi(h);
        int32_t d = (int32_t)ibi - med;
        if (d < 0)
            d = -d;
        if (d * 10 > med * 3) {
            int32_t d2 = (int32_t)ibi - 2 * med; /* missed beat? */
            if (d2 < 0)
                d2 = -d2;
            if (d2 * 10 > med * 3) {
                h->rejected_total++;
                if (++h->rejects >= 4)
                    h->nibi = h->head = h->rejects = 0;
            }
            return;
        }
    }
    h->rejects = 0;
    push_ibi(h, (uint16_t)ibi);
}

int hr_feed(struct hr *h, int32_t raw)
{
    int32_t x = -raw, y;
    int beat = 0;
    if (!h->primed) {
        /* Start the high-pass at steady state for this DC level. */
        h->hp.x1 = h->hp.x2 = x;
        h->dc = raw;
        h->primed = 1;
    }
    y = bq_run(&h->lp, bq_run(&h->hp, x));
    h->dc += (raw - h->dc) / 32;

    h->env -= h->env >> 6;
    if (y > h->env)
        h->env = y;

    if (h->n >= 2 && h->p1 > h->p2 && h->p1 >= y && h->p1 > 0 &&
        h->p1 * 20 > h->env * 9 && h->env >= HR_MIN_AMP) {
        /* parabola through (p2, p1, y): vertex offset in [-0.5, 0.5] */
        int32_t den = h->p2 - 2 * h->p1 + y;
        int64_t off_q8 = 0;
        int64_t t_q8;
        if (den != 0)
            off_q8 = ((int64_t)(h->p2 - y) * 128) / den;
        if (off_q8 > 128)
            off_q8 = 128;
        if (off_q8 < -128)
            off_q8 = -128;
        t_q8 = (int64_t)(h->n - 1) * 256 + off_q8;
        if (!h->have_peak || t_q8 - h->last_peak_q8 >= REFRACTORY_Q8) {
            accept_beat(h, t_q8);
            beat = 1;
        }
    }
    h->p2 = h->p1;
    h->p1 = y;
    h->n++;

    if (h->nibi >= 4 && h->env >= HR_MIN_AMP && h->dc >= HR_MIN_DC) {
        uint32_t med = median_ibi(h);
        h->bpm = med ? (uint16_t)((60u * HR_FS * 256u + med / 2) / med) : 0;
    } else {
        h->bpm = 0;
    }
    /* Lost the pulse for > 3 s: drop the lock. */
    if (h->have_peak && (int64_t)h->n * 256 - h->last_peak_q8 > (int64_t)HR_FS * 256 * 3) {
        h->nibi = h->head = 0;
        h->have_peak = 0;
        h->bpm = 0;
    }
    return beat;
}

uint16_t hr_bpm(const struct hr *h)
{
    return h->bpm;
}

int32_t hr_last_filtered(const struct hr *h)
{
    return h->p1;
}
