/* Render a watchface frame on the host through the same encoder and
 * reference panel decoder the emulator uses; writes a binary PBM.
 *   render_face <out.pbm> [hh mm ss steps hr battery seconds] */
#include <stdio.h>
#include <stdlib.h>
#include "mip.h"
#include "watchface.h"

int main(int argc, char **argv)
{
    static struct mip m;
    static struct mip_panel panel;
    static uint8_t frame[MIP_FRAME_MAX], fb[MIP_H][MIP_STRIDE];
    struct watchface wf;
    struct watch_state s = { 10, 9, 42, 3, 26, 11, 6241, 8000, 72, 87, 0, 1 };
    FILE *f;
    if (argc < 2) {
        fprintf(stderr, "usage: %s out.pbm [hh mm ss steps hr battery seconds]\n", argv[0]);
        return 2;
    }
    if (argc >= 9) {
        s.hh = (uint8_t)atoi(argv[2]);
        s.mm = (uint8_t)atoi(argv[3]);
        s.ss = (uint8_t)atoi(argv[4]);
        s.steps = (uint32_t)atoi(argv[5]);
        s.hr_bpm = (uint16_t)atoi(argv[6]);
        s.battery_pct = (uint8_t)atoi(argv[7]);
        s.show_seconds = (uint8_t)atoi(argv[8]);
    }
    mip_init(&m);
    mip_panel_init(&panel);
    watchface_init(&wf);
    watchface_draw(&wf, &m, &s, 1);
    mip_panel_feed(&panel, frame, mip_encode_update(&m, frame, 1));
    mip_panel_to_fb(&panel, fb);
    f = fopen(argv[1], "wb");
    if (!f)
        return 1;
    fprintf(f, "P4\n%d %d\n", MIP_W, MIP_H);
    fwrite(fb, 1, sizeof(fb), f);
    fclose(f);
    return 0;
}
