/* Sharp Memory-in-Pixel LCD driver core (portable, host-tested).
 *
 * Panel: Sharp LS013B7DH05, 144 x 168, 1 bit per pixel, SPI write-only.
 * Protocol (datasheet LD-26912A, sections 6-6-1..6-6-4 and 6-7), bits in
 * transmission order with an MSB-first SPI:
 *
 *   mode byte   M0 M1 M2 + 5 dummy     M0=1 data update, M1=VCOM, M2=all clear
 *   per line    AG0..AG7               gate line address 1..168, LSB first
 *               D1..D144               pixel data, D1 = leftmost
 *               8 dummy bits
 *   trailer     8 more dummy bits      (16 dummy clocks after the last line)
 *
 * so one multi-line update of n lines is 2 + 20 n bytes. The panel keeps
 * every pixel in its own 1-bit memory: lines that were not sent stay as
 * they were. That is what makes partial refresh pay off, and the driver
 * exploits it twice:
 *   1. drawing primitives mark the rows they touch dirty (a 168-bit map);
 *   2. at flush time each dirty row is compared with a shadow copy of what
 *      the panel already holds, so rows redrawn with identical content
 *      (e.g. the hour digits every second) are dropped.
 *
 * Framebuffer convention: bit set = black ink, x = 0 in bit 7 of byte 0
 * (the same layout as a binary PBM). The panel shows "1" as white
 * (reflective), so data bits are inverted on the way out.
 */
#ifndef OW_MIP_H
#define OW_MIP_H

#include <stddef.h>
#include <stdint.h>

#define MIP_W 144
#define MIP_H 168
#define MIP_STRIDE (MIP_W / 8)
#define MIP_DIRTY_WORDS ((MIP_H + 31) / 32)
#define MIP_FRAME_MAX (2 + MIP_H * (2 + MIP_STRIDE))

#define MIP_CMD_UPDATE 0x80 /* M0 */
#define MIP_CMD_VCOM   0x40 /* M1 */
#define MIP_CMD_CLEAR  0x20 /* M2 */

struct mip_stats {
    uint64_t flushes;
    uint64_t lines_sent;
    uint64_t lines_skipped_same; /* dirty but identical to the panel */
    uint64_t bytes_sent;
};

struct mip {
    uint8_t fb[MIP_H][MIP_STRIDE];     /* what we want on screen */
    uint8_t shadow[MIP_H][MIP_STRIDE]; /* what the panel holds */
    uint32_t dirty[MIP_DIRTY_WORDS];
    uint8_t vcom;                      /* M1 state for EXTMODE=L panels */
    uint8_t compare;                   /* 1: drop rows identical to shadow */
    struct mip_stats st;
};

void mip_init(struct mip *m);
void mip_mark_dirty(struct mip *m, int y0, int y1); /* inclusive, clipped */
void mip_mark_all(struct mip *m);
int  mip_is_dirty(const struct mip *m, int y);
unsigned mip_dirty_count(const struct mip *m);

/* Build the SPI byte stream for all dirty rows (all rows if `full`).
 * Returns the number of bytes written to out (0 when nothing changed).
 * out must hold MIP_FRAME_MAX bytes. Updates shadow + stats, clears dirty. */
size_t mip_encode_update(struct mip *m, uint8_t *out, int full);

/* "Display mode" frame that only toggles VCOM (2 bytes) for panels wired
 * with EXTMODE=L; with EXTMODE=H the board toggles EXTCOMIN in hardware. */
size_t mip_encode_vcom(struct mip *m, uint8_t *out);
size_t mip_encode_clear(struct mip *m, uint8_t *out);

uint8_t mip_bitrev8(uint8_t v);

/* Reference decoder: a model of the panel's own memory, driven bit by bit
 * from the SPI stream exactly as the datasheet timing charts describe.
 * Used by the AN505 "virtual panel" and by the host tests. */
struct mip_panel {
    uint8_t mem[MIP_H][MIP_STRIDE];    /* panel convention: 1 = white */
    uint8_t vcom;
    uint32_t frames, lines_written, clears, errors;
};
void mip_panel_init(struct mip_panel *p);
/* Feed one chip-select-framed transfer. Returns 0 or -1 on protocol error. */
int  mip_panel_feed(struct mip_panel *p, const uint8_t *buf, size_t n);
/* Convert the panel memory to framebuffer convention (1 = black). */
void mip_panel_to_fb(const struct mip_panel *p, uint8_t fb[MIP_H][MIP_STRIDE]);

#endif
