/* Digital watchface for the 144 x 168 MIP panel. Widgets are redrawn only
 * when the value they show changes, so the dirty-line map stays small. */
#ifndef OW_WATCHFACE_H
#define OW_WATCHFACE_H

#include <stdint.h>
#include "mip.h"

struct watch_state {
    uint8_t hh, mm, ss;
    uint8_t wday;          /* 0 = Monday */
    uint8_t mday, mon;     /* 1-based */
    uint32_t steps, step_goal;
    uint16_t hr_bpm;       /* 0 = no reading */
    uint8_t battery_pct;
    uint8_t charging;
    uint8_t show_seconds;
};

struct watchface {
    struct watch_state last;
    int drawn;             /* 0 until the first full draw */
    int widget_redraws;
};

void watchface_init(struct watchface *wf);
/* Draw the changes from the last state; force = redraw everything. */
void watchface_draw(struct watchface *wf, struct mip *m, const struct watch_state *s, int force);

#endif
