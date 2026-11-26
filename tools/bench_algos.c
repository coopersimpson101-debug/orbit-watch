/* Host benchmark for the step counter and the heart-rate estimator.
 *   bench_algos                  synthetic sweeps (JSON)
 *   bench_algos --ppg file.csv   run hr.c over a recorded PPG excerpt
 *                                (columns: t_s,ppg,ref_hr; 25 Hz)
 *   bench_algos --trace out.csv  synthetic PPG trace with detected beats */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hr.h"
#include "steps.h"
#include "synth.h"

static void steps_sweep(void)
{
    static const struct { const char *name; int act; uint32_t cad; int32_t amp; } c[] = {
        {"walk 1.5 Hz", ACT_WALK, 1500, 180}, {"walk 1.8 Hz", ACT_WALK, 1800, 250},
        {"walk 2.0 Hz", ACT_WALK, 2000, 300}, {"walk 2.3 Hz", ACT_WALK, 2300, 350},
        {"run 2.8 Hz", ACT_RUN, 2800, 600}, {"rest", ACT_REST, 0, 0},
        {"typing", ACT_TYPING, 0, 0}, {"arm gestures", ACT_GESTURE, 0, 0},
    };
    unsigned k, nc = sizeof(c) / sizeof(c[0]);
    printf("  \"steps\": [\n");
    for (k = 0; k < nc; k++) {
        unsigned seed;
        unsigned long long truth = 0, counted = 0;
        for (seed = 1; seed <= 10; seed++) {
            struct synth_accel a;
            struct steps s;
            int16_t v[3];
            int i;
            synth_accel_init(&a, STEPS_FS, seed * 7919u);
            synth_accel_set(&a, (enum synth_activity)c[k].act, c[k].cad, c[k].amp);
            steps_init(&s);
            for (i = 0; i < 300 * STEPS_FS; i++) {
                synth_accel_sample(&a, v);
                steps_feed(&s, v[0], v[1], v[2]);
            }
            truth += a.true_steps;
            counted += steps_total(&s);
        }
        printf("    {\"case\": \"%s\", \"seconds\": 3000, \"truth\": %llu, \"counted\": %llu, "
               "\"error_pct\": %.2f}%s\n", c[k].name, truth, counted,
               truth ? 100.0 * ((double)counted - (double)truth) / (double)truth : 0.0,
               k + 1 < nc ? "," : "");
    }
    printf("  ],\n");
}

static void hr_sweep(void)
{
    static const uint32_t bpm[] = {450, 600, 750, 900, 1200, 1500, 1800};
    static const struct { int32_t amp, noise; } q[] = {{600, 20}, {300, 60}, {150, 60}};
    unsigned k, j;
    printf("  \"hr\": [\n");
    for (j = 0; j < 3; j++)
        for (k = 0; k < 7; k++) {
            double err = 0;
            unsigned long long n = 0, have = 0;
            unsigned seed;
            for (seed = 1; seed <= 5; seed++) {
                struct synth_ppg p;
                struct hr h;
                int i;
                synth_ppg_init(&p, HR_FS, seed * 104729u);
                synth_ppg_set(&p, bpm[k], q[j].amp, q[j].noise, 0);
                hr_init(&h);
                for (i = 0; i < 120 * HR_FS; i++) {
                    hr_feed(&h, synth_ppg_sample(&p));
                    if (i >= 15 * HR_FS) {
                        n++;
                        if (hr_bpm(&h)) {
                            have++;
                            err += fabs(hr_bpm(&h) - bpm[k] / 10.0);
                        }
                    }
                }
            }
            printf("    {\"bpm\": %.1f, \"pulse_amp\": %d, \"noise_sigma\": %d, \"mae_bpm\": %.3f, "
                   "\"coverage\": %.3f}%s\n", bpm[k] / 10.0, q[j].amp, q[j].noise,
                   have ? err / have : -1.0, n ? (double)have / n : 0.0,
                   (j == 2 && k == 6) ? "" : ",");
        }
    printf("  ]\n");
}

static int ppg_file(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[256];
    struct hr h;
    double err = 0, t = 0, ref = 0, sum_ref = 0, sum_est = 0;
    unsigned long long n = 0, have = 0, beats = 0;
    int first = 1;
    if (!f) {
        perror(path);
        return 1;
    }
    hr_init(&h);
    while (fgets(line, sizeof(line), f)) {
        double v;
        if (first) { /* header */
            first = 0;
            continue;
        }
        if (sscanf(line, "%lf,%lf,%lf", &t, &v, &ref) != 3)
            continue;
        beats += (unsigned long long)hr_feed(&h, (int32_t)v);
        if (t >= 15.0 && ref > 0) {
            n++;
            sum_ref += ref;
            if (hr_bpm(&h)) {
                have++;
                sum_est += hr_bpm(&h);
                err += fabs(hr_bpm(&h) - ref);
            }
        }
    }
    fclose(f);
    printf("{\"samples\": %llu, \"beats\": %llu, \"mean_ref_bpm\": %.2f, \"mean_est_bpm\": %.2f, "
           "\"mae_bpm\": %.3f, \"coverage\": %.3f}\n", n, beats, n ? sum_ref / n : 0.0,
           have ? sum_est / have : 0.0, have ? err / have : -1.0, n ? (double)have / n : 0.0);
    return 0;
}

static int trace(const char *path)
{
    FILE *f = fopen(path, "w");
    struct synth_ppg p;
    struct hr h;
    int i;
    if (!f)
        return 1;
    synth_ppg_init(&p, HR_FS, 2024);
    synth_ppg_set(&p, 720, 600, 20, 0);
    hr_init(&h);
    fprintf(f, "t_s,raw,filtered,beat,bpm\n");
    for (i = 0; i < 20 * HR_FS; i++) {
        int32_t raw = synth_ppg_sample(&p);
        int beat = hr_feed(&h, raw);
        fprintf(f, "%.2f,%d,%d,%d,%u\n", i / (double)HR_FS, raw, hr_last_filtered(&h), beat,
                hr_bpm(&h));
    }
    fclose(f);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[1], "--ppg"))
        return ppg_file(argv[2]);
    if (argc == 3 && !strcmp(argv[1], "--trace"))
        return trace(argv[2]);
    printf("{\n");
    steps_sweep();
    hr_sweep();
    printf("}\n");
    return 0;
}
