/* 1-bpp drawing primitives on the MIP framebuffer. Every primitive marks
 * the rows it touches dirty, so partial refresh needs no extra bookkeeping. */
#ifndef OW_GFX_H
#define OW_GFX_H

#include <stdint.h>
#include "mip.h"

void gfx_clear(struct mip *m, int ink);
void gfx_pixel(struct mip *m, int x, int y, int ink);
void gfx_fill_rect(struct mip *m, int x, int y, int w, int h, int ink);
void gfx_hline(struct mip *m, int x, int y, int w, int ink);
void gfx_rect(struct mip *m, int x, int y, int w, int h, int ink);
/* 5x7 font, scale >= 1, returns the x after the last glyph. Supports
 * space, 0-9, A-Z, and : . % / - < > ! ? + (lower case maps to upper). */
int  gfx_text(struct mip *m, int x, int y, const char *s, int scale, int ink);
int  gfx_text_width(const char *s, int scale);
/* Seven-segment digit (0-9), w x h box, segment thickness t. */
void gfx_seg_digit(struct mip *m, int x, int y, int w, int h, int t, int digit);
/* 1-bpp icon from rows of up to 16 bits (bit 15 = leftmost). */
void gfx_icon16(struct mip *m, int x, int y, const uint16_t *rows, int nrows, int scale);

#endif
