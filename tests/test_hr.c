/* PPG heart-rate estimator: filter response, accuracy on synthetic PPG
 * across rates and noise, tracking a rate step, no-contact behaviour. */
#include <math.h>
#include <stdint.h>
#include "test.h"
#include "hr.h"
#include "synth.h"

static void test_filter_response(void)
{
    /* feed sinusoids through the band-pass and measure steady-state gain */
    double freqs[] = {0.1, 1.0, 2.0, 10.0};
    double lo[] = {0.0, 0.93, 0.93, 0.0}, hi[] = {0.08, 1.01, 1.01, 0.06};
    int k;
    for (k = 0; k < 4; k++) {
        struct hr h;
        int i, peak = 0;
        hr_init(&h);
        for (i = 0; i < 25 * 120; i++) {
            int32_t x = 100000 + (int32_t)lrint(1000.0 * sin(2 * M_PI * freqs[k] * i / 25.0));
            hr_feed(&h, x);
            if (i > 25 * 60 && abs(hr_last_filtered(&h)) > peak)
                peak = abs(hr_last_filtered(&h));
        }
        printf("  band-pass gain at %.1f Hz: %.3f\n", freqs[k], peak / 1000.0);
        CHECK(peak / 1000.0 >= lo[k] && peak / 1000.0 <= hi[k]);
    }
}

/* run for `sec` seconds and return the mean absolute error of the reported
 * bpm against the generator's rate, over samples after `settle` seconds */
static double run_rate(uint32_t bpm_x10, int32_t amp, int32_t noise, int sec, int settle,
                       uint32_t seed, double *coverage)
{
    struct synth_ppg p;
    struct hr h;
    int i, n = 0, have = 0;
    double err = 0;
    synth_ppg_init(&p, HR_FS, seed);
    synth_ppg_set(&p, bpm_x10, amp, noise, 0);
    hr_init(&h);
    for (i = 0; i < sec * HR_FS; i++) {
        hr_feed(&h, synth_ppg_sample(&p));
        if (i >= settle * HR_FS) {
            n++;
            if (hr_bpm(&h)) {
                have++;
                err += fabs(hr_bpm(&h) - bpm_x10 / 10.0);
            }
        }
    }
    *coverage = n ? (double)have / n : 0;
    return have ? err / have : 1e9;
}

static void test_rates(void)
{
    uint32_t rates[] = {450, 600, 750, 1000, 1400, 1800};
    int k;
    for (k = 0; k < 6; k++) {
        double cov, mae = run_rate(rates[k], 600, 20, 90, 15, 100 + k, &cov);
        printf("  %5.1f bpm: MAE %.2f bpm, coverage %.0f %%\n", rates[k] / 10.0, mae, cov * 100);
        CHECK(mae < 3.0);
        CHECK(cov > 0.9);
    }
}

static void test_noise(void)
{
    double cov, mae = run_rate(720, 300, 60, 120, 15, 7, &cov); /* SNR ~ 5:1 */
    printf("  noisy (amp 300, sigma 60): MAE %.2f bpm, coverage %.0f %%\n", mae, cov * 100);
    CHECK(mae < 4.0);
    CHECK(cov > 0.7);
}

static void test_step_change(void)
{
    struct synth_ppg p;
    struct hr h;
    int i, settled_at = -1;
    synth_ppg_init(&p, HR_FS, 42);
    synth_ppg_set(&p, 650, 600, 20, 0);
    hr_init(&h);
    for (i = 0; i < 30 * HR_FS; i++)
        hr_feed(&h, synth_ppg_sample(&p));
    CHECK(abs((int)hr_bpm(&h) - 65) <= 3);
    synth_ppg_set(&p, 1200, 600, 20, 0); /* sudden 65 -> 120 bpm */
    for (i = 0; i < 40 * HR_FS; i++) {
        hr_feed(&h, synth_ppg_sample(&p));
        if (settled_at < 0 && abs((int)hr_bpm(&h) - 120) <= 3)
            settled_at = i;
    }
    printf("  65 -> 120 bpm step: re-locked after %.1f s\n", settled_at / (double)HR_FS);
    CHECK(settled_at >= 0 && settled_at < 20 * HR_FS);
    CHECK(abs((int)hr_bpm(&h) - 120) <= 3);
}

static void test_no_contact(void)
{
    struct synth_ppg p;
    struct hr h;
    int i, any = 0;
    synth_ppg_init(&p, HR_FS, 5);
    synth_ppg_set(&p, 700, 0, 3, 0); /* no pulse, tiny noise */
    p.dc = 2000;                     /* ambient light only */
    hr_init(&h);
    for (i = 0; i < 60 * HR_FS; i++) {
        hr_feed(&h, synth_ppg_sample(&p));
        any |= hr_bpm(&h) != 0;
    }
    CHECK(!any);
}

int main(void)
{
    RUN(test_filter_response);
    RUN(test_rates);
    RUN(test_noise);
    RUN(test_step_change);
    RUN(test_no_contact);
    TEST_REPORT("test_hr");
}
