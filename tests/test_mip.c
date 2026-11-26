/* MIP display: SPI frame format against the LS013B7DH05 datasheet, encoder
 * vs. bit-level decoder round trips, dirty tracking and identical-row
 * suppression, and the watchface's partial updates. */
#include <stdint.h>
#include "test.h"
#include "mip.h"
#include "gfx.h"
#include "watchface.h"

static struct mip m;
static struct mip_panel panel;
static uint8_t frame[MIP_FRAME_MAX];
static uint8_t fbcopy[MIP_H][MIP_STRIDE];

static uint32_t rng = 7;
static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static int panel_matches_fb(void)
{
    mip_panel_to_fb(&panel, fbcopy);
    return memcmp(fbcopy, m.fb, sizeof(fbcopy)) == 0;
}

static void test_datasheet_bits(void)
{
    size_t n;
    mip_init(&m);
    mip_panel_init(&panel);
    gfx_pixel(&m, 0, 0, 1);            /* top-left pixel black */
    gfx_pixel(&m, MIP_W - 1, MIP_H - 1, 1);
    n = mip_encode_update(&m, frame, 0);
    CHECK(n == 2 + 2 * 20);
    CHECK(frame[0] == 0x80);           /* M0=1 M1=0 M2=0 + 5 dummy */
    /* gate line 1: AG0..AG7 = 1 0 0 0 0 0 0 0 (datasheet table 6-7) */
    CHECK(frame[1] == 0x80);
    CHECK(frame[2] == 0x7f);           /* D1 = 0 (black), D2..D8 white */
    CHECK(frame[20] == 0x00);          /* dummy after the line */
    /* gate line 168: AG0..AG7 = 0 0 0 1 0 1 0 1 */
    CHECK(frame[21] == 0x15);
    CHECK(frame[21 + 18] == 0xfe);     /* D144 = 0 */
    CHECK(frame[n - 1] == 0x00 && frame[n - 2] == 0x00); /* 16 dummy clocks */
    CHECK(mip_panel_feed(&panel, frame, n) == 0);
    CHECK(panel.lines_written == 2 && panel.errors == 0);
    CHECK(panel_matches_fb());
    CHECK(mip_bitrev8(0x01) == 0x80 && mip_bitrev8(0xa8) == 0x15);
}

static void test_full_round_trip(void)
{
    size_t n;
    int y, x, trial;
    for (trial = 0; trial < 20; trial++) {
        mip_init(&m);
        mip_panel_init(&panel);
        for (y = 0; y < MIP_H; y++)
            for (x = 0; x < MIP_STRIDE; x++)
                m.fb[y][x] = (uint8_t)rnd();
        n = mip_encode_update(&m, frame, 1);
        CHECK(n == 2 + 20 * MIP_H); /* 3362 bytes for a full frame */
        CHECK(mip_panel_feed(&panel, frame, n) == 0);
        CHECK(panel_matches_fb());
    }
}

static void test_partial_and_compare(void)
{
    size_t n;
    uint64_t skipped;
    mip_init(&m);
    mip_panel_init(&panel);
    gfx_text(&m, 10, 10, "ORBIT", 2, 1);
    n = mip_encode_update(&m, frame, 0);
    CHECK(n == 2 + 20 * 14); /* 7 rows x scale 2 */
    mip_panel_feed(&panel, frame, n);
    CHECK(panel_matches_fb());
    /* redraw identical text: dirty, but every row matches the shadow */
    gfx_fill_rect(&m, 10, 10, 60, 14, 0);
    gfx_text(&m, 10, 10, "ORBIT", 2, 1);
    CHECK(mip_dirty_count(&m) == 14);
    skipped = m.st.lines_skipped_same;
    n = mip_encode_update(&m, frame, 0);
    CHECK(n == 0);
    CHECK(m.st.lines_skipped_same - skipped == 14);
    CHECK(mip_dirty_count(&m) == 0);
    /* T -> F: glyph row 0 is identical, so 2 of the 14 rows are not sent */
    gfx_fill_rect(&m, 10, 10, 60, 14, 0);
    gfx_text(&m, 10, 10, "ORBIF", 2, 1);
    n = mip_encode_update(&m, frame, 0);
    CHECK(n == 2 + 20 * 12);
    mip_panel_feed(&panel, frame, n);
    CHECK(panel_matches_fb());
    /* with comparison disabled, every dirty row goes out */
    m.compare = 0;
    gfx_fill_rect(&m, 10, 10, 60, 14, 0);
    gfx_text(&m, 10, 10, "ORBIF", 2, 1);
    CHECK(mip_encode_update(&m, frame, 0) == 2 + 20 * 14);
}

static void test_vcom_and_clear(void)
{
    mip_init(&m);
    mip_panel_init(&panel);
    CHECK(mip_encode_vcom(&m, frame) == 2);
    CHECK(frame[0] == 0x40);
    mip_panel_feed(&panel, frame, 2);
    CHECK(panel.vcom == 1);
    CHECK(mip_encode_vcom(&m, frame) == 2 && frame[0] == 0x00);
    gfx_fill_rect(&m, 0, 0, MIP_W, MIP_H, 1);
    mip_panel_feed(&panel, frame, mip_encode_update(&m, frame, 0));
    CHECK(panel.mem[100][5] == 0x00);
    CHECK(mip_encode_clear(&m, frame) == 2 && frame[0] == 0x20);
    mip_panel_feed(&panel, frame, 2);
    CHECK(panel.clears == 1 && panel.mem[100][5] == 0xff);
    /* truncated stream is a protocol error */
    frame[0] = 0x80;
    frame[1] = 0x80;
    CHECK(mip_panel_feed(&panel, frame, 5) == -1);
    CHECK(panel.errors == 1);
}

static void test_watchface_partial(void)
{
    struct watchface wf;
    struct watch_state s = { 10, 9, 0, 2, 26, 11, 4321, 8000, 72, 87, 0, 1 };
    size_t n_full, n;
    int y, rows_sec = 0;
    mip_init(&m);
    mip_panel_init(&panel);
    watchface_init(&wf);
    watchface_draw(&wf, &m, &s, 0);
    n_full = mip_encode_update(&m, frame, 0);
    mip_panel_feed(&panel, frame, n_full);
    CHECK(panel_matches_fb());
    CHECK(n_full > 2000); /* most rows carry ink or a divider */
    /* one second later: only the seconds widget changes */
    s.ss = 1;
    watchface_draw(&wf, &m, &s, 0);
    for (y = 0; y < MIP_H; y++)
        rows_sec += mip_is_dirty(&m, y);
    CHECK(rows_sec == 14);
    n = mip_encode_update(&m, frame, 0);
    CHECK(n > 0 && n <= 2 + 20 * 14);
    mip_panel_feed(&panel, frame, n);
    CHECK(panel_matches_fb());
    /* 10:09:59 -> 10:10:00: minute digits and seconds */
    s.ss = 59;
    watchface_draw(&wf, &m, &s, 0);
    mip_panel_feed(&panel, frame, mip_encode_update(&m, frame, 0));
    s.mm = 10;
    s.ss = 0;
    watchface_draw(&wf, &m, &s, 0);
    n = mip_encode_update(&m, frame, 0);
    CHECK(n > 2 + 20 * 14 && n < n_full);
    mip_panel_feed(&panel, frame, n);
    CHECK(panel_matches_fb());
    /* no change at all -> nothing to send */
    watchface_draw(&wf, &m, &s, 0);
    CHECK(mip_encode_update(&m, frame, 0) == 0);
    printf("  full draw %u B, seconds tick %u B\n", (unsigned)n_full, (unsigned)(2 + 20 * 14));
}

int main(void)
{
    RUN(test_datasheet_bits);
    RUN(test_full_round_trip);
    RUN(test_partial_and_compare);
    RUN(test_vcom_and_clear);
    RUN(test_watchface_partial);
    TEST_REPORT("test_mip");
}
