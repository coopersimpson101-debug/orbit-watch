/* Deterministic synthetic sensor signals (integer only).
 *
 * The emulated board (QEMU mps2-an505) has no IMU or PPG sensor, so the
 * firmware replays these models instead; the host tests use the same code
 * with known ground truth. They are models, not recordings:
 *   accel  gravity (slowly re-orienting wrist) + gait: vertical bounce at
 *          the step frequency with a 2nd harmonic, arm swing at half the
 *          step frequency, plus noise; activity segments switch between
 *          rest, walking, running, typing and random arm gestures
 *   ppg    DC level minus a pulse train (systolic + dicrotic Gaussian
 *          bumps per beat), heart rate following a programmed profile with
 *          beat-to-beat variability, baseline wander, respiratory amplitude
 *          modulation, white noise and optional motion bursts
 */
#ifndef OW_SYNTH_H
#define OW_SYNTH_H

#include <stdint.h>

enum synth_activity { ACT_REST, ACT_WALK, ACT_RUN, ACT_TYPING, ACT_GESTURE };

struct synth_rng {
    uint32_t s;
};
uint32_t synth_rand(struct synth_rng *r);
int32_t  synth_gauss(struct synth_rng *r, int32_t sigma); /* approx N(0, sigma^2) */
/* sin(2 pi phase / 65536) in Q15 */
int32_t  synth_sin_q15(uint32_t phase16);

struct synth_accel {
    struct synth_rng rng;
    uint32_t fs;
    uint32_t n;                 /* sample counter */
    enum synth_activity act;
    uint32_t cadence_mhz;       /* step frequency, milli-Hz */
    int32_t amp_mg;             /* bounce amplitude */
    int32_t noise_mg;
    uint32_t phase;             /* step phase, Q16 of a cycle (wraps = 1 step) */
    uint32_t orient_phase;
    uint32_t gesture_left;      /* samples left in the current gesture */
    uint64_t true_steps;
};

void synth_accel_init(struct synth_accel *a, uint32_t fs, uint32_t seed);
void synth_accel_set(struct synth_accel *a, enum synth_activity act, uint32_t cadence_mhz,
                     int32_t amp_mg);
void synth_accel_sample(struct synth_accel *a, int16_t out[3]);

struct synth_ppg {
    struct synth_rng rng;
    uint32_t fs;
    uint32_t n;
    uint32_t bpm_x10;           /* current target heart rate, 0.1 bpm */
    int32_t dc;                 /* ADC counts */
    int32_t pulse_amp;          /* counts */
    int32_t noise;              /* counts sigma */
    int32_t motion;             /* motion-artifact amplitude (0 = off) */
    uint32_t beat_pos_q16;      /* position inside the current beat */
    uint32_t ibi_samples_q16;   /* current beat length, samples * 65536 */
    uint64_t beats;             /* ground truth beat count */
    uint32_t last_ibi_q16;
};

void synth_ppg_init(struct synth_ppg *p, uint32_t fs, uint32_t seed);
void synth_ppg_set(struct synth_ppg *p, uint32_t bpm_x10, int32_t pulse_amp, int32_t noise,
                   int32_t motion);
int32_t synth_ppg_sample(struct synth_ppg *p);

#endif
