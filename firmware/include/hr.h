/* Heart rate from a PPG stream (integer only).
 *
 *   raw ADC counts (MAX30101 green/IR, 18-bit) at HR_FS Hz
 *     -> invert (more blood = more absorption = fewer counts, so pulses
 *        become positive peaks)
 *     -> band-pass 0.5-4 Hz: two Butterworth biquads, Q28, direct form I
 *        (coefficients from tools/design_filters.py)
 *     -> peak picking: local maximum above 0.45 x a decaying amplitude
 *        envelope, with a 0.3 s refractory period (max ~200 bpm)
 *     -> parabolic interpolation of the peak time to 1/256 sample, so a
 *        25 Hz stream still resolves inter-beat intervals to ~0.2 ms
 *     -> inter-beat-interval (IBI) gate: 0.3..2.0 s and within 30 % of the
 *        running median; an IBI near 2x the median is treated as a missed
 *        beat and skipped; four rejections in a row reset the history
 *     -> bpm = 60 / median(last 8 IBIs), reported once 4 IBIs are held
 * A reading is withheld (0) while the raw DC level is below HR_MIN_DC (no
 * skin reflecting the LED light back) or the pulse amplitude is below
 * HR_MIN_AMP.
 */
#ifndef OW_HR_H
#define OW_HR_H

#include <stdint.h>

#define HR_FS 25
#define HR_NIBI 8
#define HR_MIN_AMP 8     /* filtered counts */
#define HR_MIN_DC 10000  /* raw counts: below this the LED light is not coming back off skin */

struct hr_biquad {
    int32_t b0, b1, b2, a1, a2;
    int32_t x1, x2, y1, y2;
};

struct hr {
    struct hr_biquad hp, lp;
    int primed;
    int32_t p1, p2;          /* last two filtered samples */
    int32_t env;
    int32_t dc;              /* slow mean of the raw signal (contact detection) */
    uint32_t n;
    int64_t last_peak_q8;    /* time of last accepted beat, samples * 256 */
    int have_peak;
    uint16_t ibi_q8[HR_NIBI];
    int nibi, head;
    int rejects;             /* consecutive IBI rejections */
    uint32_t beats, rejected_total;
    uint16_t bpm;
};

void hr_init(struct hr *h);
/* Feed one raw sample. Returns 1 if a beat was detected at this sample. */
int  hr_feed(struct hr *h, int32_t raw);
/* Current estimate in bpm, or 0 if not locked / no contact. */
uint16_t hr_bpm(const struct hr *h);
/* Band-pass output for the last sample (for plots and tests). */
int32_t hr_last_filtered(const struct hr *h);

#endif
