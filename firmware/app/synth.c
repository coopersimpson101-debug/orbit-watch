/* Synthetic accelerometer and PPG models. See synth.h. */
#include "synth.h"
#include "steps.h" /* ow_isqrt32 */

uint32_t synth_rand(struct synth_rng *r)
{
    uint32_t x = r->s ? r->s : 0x9e3779b9u; /* xorshift32 */
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    r->s = x;
    return x;
}

int32_t synth_gauss(struct synth_rng *r, int32_t sigma)
{
    int64_t sum = 0;
    int i;
    for (i = 0; i < 4; i++)
        sum += (int32_t)(synth_rand(r) & 0xffff) - 32768;
    /* sum of 4 uniforms has std 65536 / sqrt(3) = 37837 */
    return (int32_t)(sum * sigma / 37837);
}

int32_t synth_sin_q15(uint32_t phase16)
{
    uint32_t p = phase16 & 0xffff, q = p >> 14, f = p & 0x3fff;
    int64_t x, x2, t, r;
    if (q & 1)
        f = 0x4000 - f;
    x = (int64_t)f * 2; /* Q15 in [0, 1] */
    x2 = (x * x) >> 15;
    t = 21167 - ((x2 * 2611) >> 15);
    t = 51472 - ((x2 * t) >> 15);
    r = (x * t) >> 15;
    if (r > 32767)
        r = 32767;
    return (int32_t)(q & 2 ? -r : r);
}

/* ---------------------------------------------------------- accelerometer */

void synth_accel_init(struct synth_accel *a, uint32_t fs, uint32_t seed)
{
    a->rng.s = seed;
    a->fs = fs;
    a->n = 0;
    a->phase = 0;
    a->orient_phase = 0;
    a->gesture_left = 0;
    a->true_steps = 0;
    a->noise_mg = 12;
    synth_accel_set(a, ACT_REST, 0, 0);
}

void synth_accel_set(struct synth_accel *a, enum synth_activity act, uint32_t cadence_mhz,
                     int32_t amp_mg)
{
    a->act = act;
    a->cadence_mhz = cadence_mhz;
    a->amp_mg = amp_mg;
}

void synth_accel_sample(struct synth_accel *a, int16_t out[3])
{
    int32_t gx, gy, gz, x, y, z, bounce = 0, swing = 0;
    int32_t g2;

    /* wrist orientation drifts with a 23 s period */
    a->orient_phase += 65536u / (a->fs * 23u);
    gx = (int32_t)(250 * synth_sin_q15(a->orient_phase) >> 15);
    gy = 300 + (int32_t)(150 * synth_sin_q15(a->orient_phase * 3u + 9000u) >> 15);
    g2 = 1000 * 1000 - gx * gx - gy * gy;
    gz = (int32_t)ow_isqrt32((uint32_t)(g2 > 0 ? g2 : 0));

    if (a->act == ACT_WALK || a->act == ACT_RUN) {
        uint32_t d = (uint32_t)((uint64_t)a->cadence_mhz * 65536u / (1000u * a->fs));
        uint32_t ph = a->phase + d;
        if (ph >= 65536u) {
            ph -= 65536u;
            a->true_steps++;
        }
        a->phase = ph;
        /* heel strike: fundamental + 2nd harmonic */
        bounce = (int32_t)((int64_t)a->amp_mg * synth_sin_q15(ph) >> 15) +
                 (int32_t)((int64_t)a->amp_mg * 3 / 10 * synth_sin_q15(2 * ph + 12000u) >> 15);
        /* arm swing at stride (= half step) frequency */
        swing = (int32_t)((int64_t)a->amp_mg * 6 / 10 *
                          synth_sin_q15((ph >> 1) + ((a->true_steps & 1) ? 32768u : 0u)) >> 15);
    } else if (a->act == ACT_TYPING) {
        uint32_t ph = (uint32_t)((uint64_t)a->n * 8u * 65536u / a->fs); /* ~8 Hz tremor */
        bounce = (int32_t)(25 * synth_sin_q15(ph) >> 15) + synth_gauss(&a->rng, 10);
    } else if (a->act == ACT_GESTURE) {
        if (!a->gesture_left && synth_rand(&a->rng) % (a->fs * 3u) == 0)
            a->gesture_left = a->fs * 4u / 5u;
        if (a->gesture_left) {
            uint32_t len = a->fs * 4u / 5u, k = len - a->gesture_left;
            swing = (int32_t)((int64_t)650 * synth_sin_q15((uint32_t)(k * 32768u / len)) >> 15);
            bounce = swing / 2;
            a->gesture_left--;
        }
    }
    x = gx + swing + bounce * 4 / 10 + synth_gauss(&a->rng, a->noise_mg);
    y = gy + swing / 3 + synth_gauss(&a->rng, a->noise_mg);
    z = gz + bounce * 8 / 10 + synth_gauss(&a->rng, a->noise_mg);
    out[0] = (int16_t)x;
    out[1] = (int16_t)y;
    out[2] = (int16_t)z;
    a->n++;
}

/* -------------------------------------------------------------------- PPG */

/* exp(-z^2/2) ~ (1 - z^2/16)^8 for |z| < 4, z and result in Q16 */
static int64_t bump_q16(int64_t z_q16)
{
    int64_t t, r;
    int i;
    if (z_q16 <= -4 * 65536 || z_q16 >= 4 * 65536)
        return 0;
    t = 65536 - ((z_q16 * z_q16) >> 20); /* 1 - z^2/16 */
    r = t;
    for (i = 0; i < 3; i++)
        r = (r * r) >> 16; /* ^8 */
    return r;
}

static uint32_t ibi_from_bpm(struct synth_ppg *p)
{
    /* samples per beat * 65536, with +/-3 % beat-to-beat variability */
    uint64_t base = (uint64_t)p->fs * 600u * 65536u / (p->bpm_x10 ? p->bpm_x10 : 600u);
    int32_t hrv = synth_gauss(&p->rng, 1500); /* ~2.3 % of 65536 */
    return (uint32_t)((int64_t)base + (int64_t)base * hrv / 65536);
}

void synth_ppg_init(struct synth_ppg *p, uint32_t fs, uint32_t seed)
{
    p->rng.s = seed;
    p->fs = fs;
    p->n = 0;
    p->dc = 120000;
    p->beats = 0;
    synth_ppg_set(p, 700, 600, 20, 0);
    p->beat_pos_q16 = 0;
    p->ibi_samples_q16 = ibi_from_bpm(p);
    p->last_ibi_q16 = p->ibi_samples_q16;
}

void synth_ppg_set(struct synth_ppg *p, uint32_t bpm_x10, int32_t pulse_amp, int32_t noise,
                   int32_t motion)
{
    p->bpm_x10 = bpm_x10;
    p->pulse_amp = pulse_amp;
    p->noise = noise;
    p->motion = motion;
}

int32_t synth_ppg_sample(struct synth_ppg *p)
{
    int64_t u, shape, amp, v;
    uint32_t t_ph;
    p->beat_pos_q16 += 65536u;
    if (p->beat_pos_q16 >= p->ibi_samples_q16) {
        p->beat_pos_q16 -= p->ibi_samples_q16;
        p->beats++;
        p->last_ibi_q16 = p->ibi_samples_q16;
        p->ibi_samples_q16 = ibi_from_bpm(p);
    }
    u = ((int64_t)p->beat_pos_q16 << 16) / p->ibi_samples_q16; /* beat phase, Q16 */
    shape = bump_q16((u - 11796) * 65536 / 4588)                 /* systole at 0.18, w 0.07 */
          + bump_q16((u - 29491) * 65536 / 5898) * 35 / 100;     /* dicrotic at 0.45, w 0.09 */
    /* respiration (0.25 Hz) modulates the pulse amplitude by +/-15 % */
    t_ph = (uint32_t)((uint64_t)p->n * 65536u / (p->fs * 4u));
    amp = (int64_t)p->pulse_amp * (32768 + (synth_sin_q15(t_ph) * 15 / 100)) >> 15;
    v = p->dc - ((amp * shape) >> 16);
    /* baseline wander at 0.07 Hz, twice the pulse amplitude */
    v += (int64_t)p->pulse_amp * 2 * synth_sin_q15((uint32_t)((uint64_t)p->n * 65536u * 7u / (p->fs * 100u))) >> 15;
    v += synth_gauss(&p->rng, p->noise);
    if (p->motion)
        v += (int64_t)p->motion *
             synth_sin_q15((uint32_t)((uint64_t)p->n * 65536u * 17u / (p->fs * 10u))) >> 15;
    p->n++;
    return (int32_t)v;
}
