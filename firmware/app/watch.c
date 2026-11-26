/* The watch application: clock, watchface, step counting and periodic
 * heart-rate measurement, as two event-driven threads.
 *
 *   ui      wakes once per second (seconds shown) or once per minute,
 *           redraws the changed widgets and pushes the dirty rows to the
 *           MIP panel
 *   sensor  wakes on the IMU FIFO watermark (every `imu_batch` seconds) and
 *           on the PPG FIFO during a heart-rate session (every second for
 *           HR_SESSION_S seconds, every HR_PERIOD_S seconds)
 * On the nRF54L15 board the FIFO wake-ups are the BMI270 INT1 and MAX30101
 * INT lines; the emulated board has no sensors, so kernel timers stand in
 * for the interrupts and boards/an505 replays synthetic signals.
 */
#include "board.h"
#include "cm33.h"
#include "hr.h"
#include "kernel.h"
#include "kstring.h"
#include "mip.h"
#include "sched.h"
#include "shell_parse.h"
#include "steps.h"
#include "watchface.h"

#define EV_TICK   (1u << 0)
#define EV_BUTTON (1u << 1)
#define EV_REDRAW (1u << 2)
#define EV_IMU    (1u << 3)
#define EV_PPG    (1u << 4)
#define EV_HR_GO  (1u << 5)

#define HR_PERIOD_S 600
#define HR_SESSION_S 30

static struct mip panel_fb;
static struct watchface face;
static struct steps stepper;
static struct hr hrm;
static struct thread *ui_thr, *sensor_thr;
static struct ktimer tick_timer, imu_timer, ppg_timer, hr_timer;
static uint8_t frame[MIP_FRAME_MAX];

static struct {
    int show_seconds;
    int full_refresh;    /* send every row on every update (for comparison) */
    uint32_t imu_batch_s;
    /* wall clock at boot */
    uint8_t hh, mm, ss, wday, mday, mon;
    /* live values */
    uint16_t hr_bpm, hr_last_good;
    int hr_active;
    uint32_t hr_session_left;
    /* statistics */
    uint64_t ui_updates, frames_sent, display_bytes, display_rows;
    uint64_t imu_wakes, imu_samples, ppg_wakes, ppg_samples, hr_sessions, hr_locked;
} w;

/* -------------------------------------------------------------- clock */

static const uint8_t mdays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

static void wall_time(struct watch_state *s)
{
    uint64_t secs = ticks_now() / OW_HZ + w.hh * 3600u + w.mm * 60u + w.ss;
    uint64_t days = secs / 86400u;
    unsigned mday = w.mday, mon = w.mon;
    s->hh = (uint8_t)(secs / 3600 % 24);
    s->mm = (uint8_t)(secs / 60 % 60);
    s->ss = (uint8_t)(secs % 60);
    s->wday = (uint8_t)((w.wday + days) % 7);
    while (days--) {
        if (++mday > mdays[(mon - 1) % 12]) {
            mday = 1;
            mon = mon % 12 + 1;
        }
    }
    s->mday = (uint8_t)mday;
    s->mon = (uint8_t)mon;
}

/* Re-arm the UI tick on the next second or minute boundary. */
static void arm_tick(void)
{
    uint64_t now = ticks_now();
    uint64_t period = w.show_seconds ? OW_HZ : 60u * OW_HZ;
    uint64_t phase = w.show_seconds ? 0 : (uint64_t)(w.ss % 60) * OW_HZ;
    uint64_t next = ((now + phase) / period + 1) * period - phase;
    tick_timer.period = (uint32_t)period;
    ktimer_start(&tick_timer, next);
}

/* ------------------------------------------------------------- timers */

static void tick_fn(struct ktimer *t, void *arg) { (void)t; (void)arg; ev_post(ui_thr, EV_TICK); }
static void imu_fn(struct ktimer *t, void *arg) { (void)t; (void)arg; ev_post(sensor_thr, EV_IMU); }
static void ppg_fn(struct ktimer *t, void *arg) { (void)t; (void)arg; ev_post(sensor_thr, EV_PPG); }
static void hr_fn(struct ktimer *t, void *arg) { (void)t; (void)arg; ev_post(sensor_thr, EV_HR_GO); }

void watch_button_isr(unsigned buttons)
{
    (void)buttons;
    if (ui_thr)
        ev_post(ui_thr, EV_BUTTON);
}

/* -------------------------------------------------------------- threads */

static void flush_display(void)
{
    size_t n = mip_encode_update(&panel_fb, frame, w.full_refresh);
    if (!n)
        return;
    pm_lock(PM_LOCK_SPI);
    board_display_xfer(frame, n);
    pm_unlock(PM_LOCK_SPI);
    w.frames_sent++;
    w.display_bytes += n;
    w.display_rows += (n - 2) / (2 + MIP_STRIDE);
}

static void build_state(struct watch_state *s)
{
    int charging = 0;
    wall_time(s);
    s->steps = steps_total(&stepper);
    s->step_goal = 8000;
    s->hr_bpm = w.hr_last_good;
    s->battery_pct = (uint8_t)board_battery_pct(&charging);
    s->charging = (uint8_t)charging;
    s->show_seconds = (uint8_t)w.show_seconds;
}

static void ui_thread(void *arg)
{
    struct watch_state s;
    (void)arg;
    /* power-on sequence: all-clear, then the first full frame */
    board_display_xfer(frame, mip_encode_clear(&panel_fb, frame));
    build_state(&s);
    watchface_draw(&face, &panel_fb, &s, 1);
    flush_display();
    arm_tick();
    for (;;) {
        uint32_t ev = ev_wait(EV_TICK | EV_BUTTON | EV_REDRAW, 0);
        build_state(&s);
        watchface_draw(&face, &panel_fb, &s, (ev & EV_REDRAW) != 0);
        flush_display();
        w.ui_updates++;
    }
}

static void hr_start(void)
{
    if (w.hr_active)
        return;
    hr_init(&hrm);
    board_ppg_enable(1);
    pm_lock(PM_LOCK_PPG);
    w.hr_active = 1;
    w.hr_session_left = HR_SESSION_S;
    w.hr_sessions++;
    ppg_timer.period = OW_HZ;
    ktimer_start(&ppg_timer, ticks_now() + OW_HZ);
}

static void hr_stop(void)
{
    ktimer_stop(&ppg_timer);
    board_ppg_enable(0);
    pm_unlock(PM_LOCK_PPG);
    w.hr_active = 0;
    if (hr_bpm(&hrm)) {
        w.hr_last_good = hr_bpm(&hrm);
        w.hr_locked++;
    }
    ev_post(ui_thr, EV_TICK); /* show the new reading now */
}

static void sensor_thread(void *arg)
{
    static int16_t acc[256][3];
    static int32_t ppg[64];
    (void)arg;
    steps_init(&stepper);
    for (;;) {
        uint32_t ev = ev_wait(EV_IMU | EV_PPG | EV_HR_GO, 0);
        if (ev & EV_IMU) {
            int n, i;
            w.imu_wakes++;
            while ((n = board_accel_read(acc, 256)) > 0) {
                for (i = 0; i < n; i++)
                    steps_feed(&stepper, acc[i][0], acc[i][1], acc[i][2]);
                w.imu_samples += (uint64_t)n;
            }
        }
        if (ev & EV_HR_GO)
            hr_start();
        if ((ev & EV_PPG) && w.hr_active) {
            int n, i;
            w.ppg_wakes++;
            while ((n = board_ppg_read(ppg, 64)) > 0) {
                for (i = 0; i < n; i++)
                    hr_feed(&hrm, ppg[i]);
                w.ppg_samples += (uint64_t)n;
            }
            if (--w.hr_session_left == 0)
                hr_stop();
        }
    }
}

void watch_start(void)
{
    w.show_seconds = 1;
    w.full_refresh = 0;
    w.imu_batch_s = 1;
    w.hh = 9; w.mm = 59; w.ss = 30;    /* emulated boot time: Thu 26 Nov, 09:59:30 */
    w.wday = 3; w.mday = 26; w.mon = 11;
    mip_init(&panel_fb);
    watchface_init(&face);
    ktimer_setup(&tick_timer, tick_fn, 0);
    ktimer_setup(&imu_timer, imu_fn, 0);
    ktimer_setup(&ppg_timer, ppg_fn, 0);
    ktimer_setup(&hr_timer, hr_fn, 0);
    ui_thr = thread_create("ui", ui_thread, 0, 2, 2048);
    sensor_thr = thread_create("sensor", sensor_thread, 0, 1, 4096);
    imu_timer.period = w.imu_batch_s * OW_HZ;
    ktimer_start(&imu_timer, imu_timer.period);
    hr_timer.period = HR_PERIOD_S * OW_HZ;
    ktimer_start(&hr_timer, 20 * OW_HZ); /* first reading 20 s after boot */
}

/* ---------------------------------------------------------------- shell */

void watch_print_stats(void)
{
    struct watch_state s;
    uint64_t lp = lp_elapsed_since_boot();
    int i;
    build_state(&s);
    kprintf("STATS {\"uptime_s\": %u, \"lp_hz\": %u, \"show_seconds\": %d, \"full_refresh\": %d, "
            "\"imu_batch_s\": %u, ", (unsigned)(lp / board.lp_hz), board.lp_hz, w.show_seconds,
            w.full_refresh, (unsigned)w.imu_batch_s);
    kprintf("\"ui_updates\": %u, \"frames\": %u, \"display_bytes\": %llu, \"display_rows\": %u, ",
            (unsigned)w.ui_updates, (unsigned)w.frames_sent, (unsigned long long)w.display_bytes,
            (unsigned)w.display_rows);
    kprintf("\"rows_skipped_same\": %u, ", (unsigned)panel_fb.st.lines_skipped_same);
    kprintf("\"true_steps\": %u, ", (unsigned)board_true_steps());
    kprintf("\"steps\": %u, \"imu_wakes\": %u, \"imu_samples\": %u, ", (unsigned)s.steps,
            (unsigned)w.imu_wakes, (unsigned)w.imu_samples);
    kprintf("\"hr_bpm\": %u, \"hr_sessions\": %u, \"hr_locked\": %u, \"ppg_wakes\": %u, "
            "\"ppg_samples\": %u, ", w.hr_last_good, (unsigned)w.hr_sessions,
            (unsigned)w.hr_locked, (unsigned)w.ppg_wakes, (unsigned)w.ppg_samples);
    kprintf("\"wakeups\": %u, \"tickless_sleeps\": %u, \"early_wakes\": %u, ",
            (unsigned)idle.wakeups, (unsigned)idle.tl.st.sleeps,
            (unsigned)idle.tl.st.early_wakes);
    kprintf("\"lp_total\": %llu, \"lp_sleep\": [", (unsigned long long)lp);
    for (i = 0; i < PM_OFF; i++)
        kprintf("%s%llu", i ? ", " : "", (unsigned long long)idle.sleep_lp[i]);
    kprintf("], \"pm_entries\": [");
    for (i = 0; i < PM_OFF; i++)
        kprintf("%s%u", i ? ", " : "", (unsigned)idle.pm.entries[i]);
    kprintf("], \"threads\": {");
    for (i = 0; i < sched_thread_count(); i++) {
        struct thread *t = sched_thread(i);
        uint64_t c = t->cpu_cycles - (t->prio == PRIO_IDLE ? idle.sleep_cycles : 0);
        kprintf("%s\"%s\": %llu", i ? ", " : "", t->name, (unsigned long long)c);
    }
    kprintf("}, \"cycles_hz\": %u, \"emu_overhead_cycles\": %llu, \"sleep_cycles\": %llu, ",
            (unsigned)board_cycles_hz, (unsigned long long)board_emu_overhead_cycles(),
            (unsigned long long)idle.sleep_cycles);
    kprintf("\"time\": \"%02u:%02u:%02u\"}\n", s.hh, s.mm, s.ss);
}

void watch_shell_cmd(int argc, char **argv)
{
    const char *c = argv[0];
    uint64_t v;
    if (!k_strcmp(c, "face")) {
        struct watch_state s;
        if (argc == 3 && !k_strcmp(argv[1], "seconds")) {
            w.show_seconds = !k_strcmp(argv[2], "on");
            ktimer_stop(&tick_timer);
            arm_tick();
            ev_post(ui_thr, EV_TICK);
        } else if (argc == 3 && !k_strcmp(argv[1], "full")) {
            w.full_refresh = !k_strcmp(argv[2], "on");
        }
        build_state(&s);
        kprintf("%02u:%02u:%02u  steps %u  hr %u bpm  battery %u%%  seconds %s  refresh %s\n",
                s.hh, s.mm, s.ss, (unsigned)s.steps, s.hr_bpm, s.battery_pct,
                w.show_seconds ? "on" : "off", w.full_refresh ? "full" : "partial");
    } else if (!k_strcmp(c, "imu") && argc == 3 && !k_strcmp(argv[1], "batch") &&
               !parse_u64(argv[2], &v) && v >= 1 && v <= 60) {
        w.imu_batch_s = (uint32_t)v;
        imu_timer.period = w.imu_batch_s * OW_HZ;
        ktimer_start(&imu_timer, ticks_now() + imu_timer.period);
        kprintf("imu fifo batch %u s\n", (unsigned)w.imu_batch_s);
    } else if (!k_strcmp(c, "hr") && argc == 2 && !k_strcmp(argv[1], "now")) {
        ev_post(sensor_thr, EV_HR_GO);
        kputs("heart-rate session started\n");
    } else if (!k_strcmp(c, "stats")) {
        watch_print_stats();
    } else if (!k_strcmp(c, "snap") && argc == 2) {
        board_snapshot(argv[1]);
    } else if (!k_strcmp(c, "run") && argc == 2 &&
               !parse_u64(argv[1][0] == '+' ? argv[1] + 1 : argv[1], &v)) {
        /* "run N": until uptime N s; "run +N": for N s from now */
        uint64_t target = v * OW_HZ + (argv[1][0] == '+' ? ticks_now() : 0);
        while (ticks_now() < target) {
            uint64_t left = target - ticks_now();
            thread_sleep((uint32_t)(left > 600u * OW_HZ ? 600u * OW_HZ : left));
            kprintf("[t=%u s] steps %u, hr %u, wakeups %u\n", (unsigned)(ticks_now() / OW_HZ),
                    (unsigned)steps_total(&stepper), w.hr_last_good, (unsigned)idle.wakeups);
        }
    } else {
        kputs("unknown command (try 'help')\n");
    }
}
