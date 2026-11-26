/* orbit-watch digital watchface. */
#include "watchface.h"
#include "gfx.h"
#include "kprintf.h"
#include "kstring.h"

static const char *const wday_name[7] = {"MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN"};
static const char *const mon_name[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                         "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

static const uint16_t icon_heart[12] = {
    0x3C78, 0x7EFC, 0xFFFE, 0xFFFE, 0xFFFE, 0x7FFC,
    0x3FF8, 0x1FF0, 0x0FE0, 0x07C0, 0x0380, 0x0100,
};
static const uint16_t icon_shoe[12] = { /* two footprints */
    0x0000, 0x3800, 0x7C00, 0x7C1C, 0x7C3E, 0x383E,
    0x003E, 0x381C, 0x3800, 0x001C, 0x001C, 0x0000,
};

/* Widget rows (inclusive) so they can be cleared independently. */
#define HDR_Y0 2
#define HDR_Y1 14
#define TIME_Y 22
#define TIME_H 56
#define SEC_Y 86
#define SEC_H 14
#define STEP_Y 112
#define HR_Y 146

static void clear_rows(struct mip *m, int y0, int y1)
{
    gfx_fill_rect(m, 0, y0, MIP_W, y1 - y0 + 1, 0);
}

static void draw_header(struct mip *m, const struct watch_state *s)
{
    char buf[24];
    int w, x = 118, y = 3, fill;
    clear_rows(m, HDR_Y0, HDR_Y1);
    k_snprintf(buf, sizeof(buf), "%s %u %s", wday_name[s->wday % 7], s->mday,
               mon_name[(s->mon + 11) % 12]);
    gfx_text(m, 4, 5, buf, 1, 1);
    /* battery: 20 x 10 body + 2 x 4 nub */
    gfx_rect(m, x, y, 20, 10, 1);
    gfx_fill_rect(m, x + 20, y + 3, 2, 4, 1);
    fill = (s->battery_pct > 100 ? 100 : s->battery_pct) * 16 / 100;
    gfx_fill_rect(m, x + 2, y + 2, fill, 6, 1);
    k_snprintf(buf, sizeof(buf), s->charging ? "+%u%%" : "%u%%", s->battery_pct);
    w = gfx_text_width(buf, 1);
    gfx_text(m, x - 4 - w, 5, buf, 1, 1);
}

static void draw_time(struct mip *m, const struct watch_state *s)
{
    const int w = 28, t = 6;
    clear_rows(m, TIME_Y, TIME_Y + TIME_H - 1);
    gfx_seg_digit(m, 4, TIME_Y, w, TIME_H, t, s->hh / 10);
    gfx_seg_digit(m, 36, TIME_Y, w, TIME_H, t, s->hh % 10);
    gfx_fill_rect(m, 68, TIME_Y + 15, 6, 6, 1);
    gfx_fill_rect(m, 68, TIME_Y + 35, 6, 6, 1);
    gfx_seg_digit(m, 80, TIME_Y, w, TIME_H, t, s->mm / 10);
    gfx_seg_digit(m, 112, TIME_Y, w, TIME_H, t, s->mm % 10);
}

static void draw_seconds(struct mip *m, const struct watch_state *s)
{
    char buf[8];
    clear_rows(m, SEC_Y, SEC_Y + SEC_H - 1);
    if (!s->show_seconds)
        return;
    k_snprintf(buf, sizeof(buf), "%02u", s->ss);
    gfx_text(m, MIP_W - 4 - gfx_text_width(buf, 2), SEC_Y, buf, 2, 1);
}

static void draw_steps(struct mip *m, const struct watch_state *s)
{
    char buf[16];
    uint32_t goal = s->step_goal ? s->step_goal : 1;
    uint32_t fill = s->steps >= goal ? 136 : s->steps * 136 / goal;
    clear_rows(m, STEP_Y, STEP_Y + 27);
    gfx_icon16(m, 4, STEP_Y + 1, icon_shoe, 12, 1);
    k_snprintf(buf, sizeof(buf), "%u", (unsigned)s->steps);
    gfx_text(m, 24, STEP_Y, buf, 2, 1);
    gfx_text(m, 24 + gfx_text_width(buf, 2) + 6, STEP_Y + 7, "STEPS", 1, 1);
    gfx_rect(m, 4, STEP_Y + 20, 136, 6, 1);
    gfx_fill_rect(m, 4, STEP_Y + 20, (int)fill, 6, 1);
}

static void draw_hr(struct mip *m, const struct watch_state *s)
{
    char buf[16];
    clear_rows(m, HR_Y, HR_Y + 15);
    gfx_icon16(m, 4, HR_Y + 1, icon_heart, 12, 1);
    if (s->hr_bpm)
        k_snprintf(buf, sizeof(buf), "%u", s->hr_bpm);
    else
        k_strlcpy(buf, "--", sizeof(buf));
    gfx_text(m, 24, HR_Y, buf, 2, 1);
    gfx_text(m, 24 + gfx_text_width(buf, 2) + 6, HR_Y + 7, "BPM", 1, 1);
}

void watchface_init(struct watchface *wf)
{
    k_memset(wf, 0, sizeof(*wf));
}

void watchface_draw(struct watchface *wf, struct mip *m, const struct watch_state *s, int force)
{
    const struct watch_state *o = &wf->last;
    if (force || !wf->drawn) {
        gfx_clear(m, 0);
        gfx_fill_rect(m, 0, 104, MIP_W, 2, 1); /* divider */
        draw_header(m, s);
        draw_time(m, s);
        draw_seconds(m, s);
        draw_steps(m, s);
        draw_hr(m, s);
        wf->widget_redraws += 5;
        wf->drawn = 1;
    } else {
        if (s->wday != o->wday || s->mday != o->mday || s->mon != o->mon ||
            s->battery_pct != o->battery_pct || s->charging != o->charging) {
            draw_header(m, s);
            wf->widget_redraws++;
        }
        if (s->hh != o->hh || s->mm != o->mm) {
            draw_time(m, s);
            wf->widget_redraws++;
        }
        if (s->ss != o->ss || s->show_seconds != o->show_seconds) {
            draw_seconds(m, s);
            wf->widget_redraws++;
        }
        if (s->steps != o->steps || s->step_goal != o->step_goal) {
            draw_steps(m, s);
            wf->widget_redraws++;
        }
        if (s->hr_bpm != o->hr_bpm) {
            draw_hr(m, s);
            wf->widget_redraws++;
        }
    }
    wf->last = *s;
}
