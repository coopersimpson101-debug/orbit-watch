/* Host benchmark: SPI bytes sent to the MIP panel over one simulated hour of
 * watchface updates, for three refresh strategies. Prints JSON.
 *   full      every update resends all 168 rows
 *   dirty     rows touched by redrawn widgets (no comparison)
 *   partial   dirty rows that also differ from the panel contents (default)
 * Each strategy is run with the seconds display on (1 update/s) and off
 * (1 update/min). Every frame is also decoded by the reference panel model
 * and compared with the framebuffer, so the byte counts are for frames that
 * really reproduce the picture. */
#include <stdio.h>
#include <string.h>
#include "mip.h"
#include "watchface.h"

static struct mip m;
static struct mip_panel panel;
static uint8_t frame[MIP_FRAME_MAX];
static uint8_t fbcopy[MIP_H][MIP_STRIDE];

static void run(const char *name, int full, int compare, int seconds, int first)
{
    struct watchface wf;
    struct watch_state s = { 9, 59, 30, 3, 26, 11, 0, 8000, 0, 87, 0, (uint8_t)seconds };
    unsigned long long bytes = 0, frames = 0, rows = 0, mismatch = 0;
    int t, step = seconds ? 1 : 60;
    mip_init(&m);
    m.compare = (uint8_t)compare;
    mip_panel_init(&panel);
    watchface_init(&wf);
    for (t = 0; t < 3600; t += step) {
        int secs = 30 + t;
        size_t n;
        s.ss = (uint8_t)(secs % 60);
        s.mm = (uint8_t)((59 + secs / 60) % 60);
        s.hh = (uint8_t)(9 + (59 + secs / 60) / 60);
        s.steps = (uint32_t)(t > 300 ? (t - 300) * 18 / 10 : 0); /* walking from t=300 s */
        s.hr_bpm = (uint16_t)(t >= 20 ? 62 + (t / 600) * 7 : 0);  /* a reading every 10 min */
        watchface_draw(&wf, &m, &s, 0);
        n = mip_encode_update(&m, frame, full);
        if (n) {
            mip_panel_feed(&panel, frame, n);
            frames++;
            bytes += n;
            rows += (n - 2) / 20;
        }
        mip_panel_to_fb(&panel, fbcopy);
        mismatch += memcmp(fbcopy, m.fb, sizeof(fbcopy)) != 0;
    }
    printf("%s  {\"strategy\": \"%s\", \"seconds\": %d, \"updates\": %d, \"frames\": %llu, "
           "\"bytes_per_hour\": %llu, \"bytes_per_minute\": %.1f, \"rows_per_update\": %.2f, "
           "\"spi_ms_per_hour_at_1MHz\": %.1f, \"mismatched_frames\": %llu}",
           first ? "" : ",\n", name, seconds, 3600 / step, frames, bytes, bytes / 60.0,
           (double)rows / (3600 / step), bytes * 8 / 1000.0, mismatch);
}

int main(void)
{
    int sec;
    printf("[\n");
    for (sec = 1; sec >= 0; sec--) {
        run("full", 1, 1, sec, sec == 1);
        run("dirty", 0, 0, sec, 0);
        run("partial", 0, 1, sec, 0);
    }
    printf("\n]\n");
    return 0;
}
