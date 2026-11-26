/* Sharp Memory-in-Pixel LCD: dirty-line tracking, SPI frame encoder and a
 * bit-level reference decoder. See mip.h for the protocol summary. */
#include "mip.h"
#include "kstring.h"

uint8_t mip_bitrev8(uint8_t v)
{
    v = (uint8_t)(((v & 0xf0) >> 4) | ((v & 0x0f) << 4));
    v = (uint8_t)(((v & 0xcc) >> 2) | ((v & 0x33) << 2));
    v = (uint8_t)(((v & 0xaa) >> 1) | ((v & 0x55) << 1));
    return v;
}

void mip_init(struct mip *m)
{
    k_memset(m, 0, sizeof(*m));
    m->compare = 1;
    /* After an all-clear the panel is white; mirror that in the shadow. */
    mip_mark_all(m);
}

void mip_mark_dirty(struct mip *m, int y0, int y1)
{
    int y;
    if (y0 < 0)
        y0 = 0;
    if (y1 >= MIP_H)
        y1 = MIP_H - 1;
    for (y = y0; y <= y1; y++)
        m->dirty[y >> 5] |= 1u << (y & 31);
}

void mip_mark_all(struct mip *m)
{
    mip_mark_dirty(m, 0, MIP_H - 1);
}

int mip_is_dirty(const struct mip *m, int y)
{
    if (y < 0 || y >= MIP_H)
        return 0;
    return (m->dirty[y >> 5] >> (y & 31)) & 1;
}

unsigned mip_dirty_count(const struct mip *m)
{
    unsigned n = 0, i;
    for (i = 0; i < MIP_DIRTY_WORDS; i++)
        n += (unsigned)__builtin_popcount(m->dirty[i]);
    return n;
}

size_t mip_encode_update(struct mip *m, uint8_t *out, int full)
{
    size_t n = 0;
    int y, lines = 0;
    out[n++] = (uint8_t)(MIP_CMD_UPDATE | (m->vcom ? MIP_CMD_VCOM : 0));
    for (y = 0; y < MIP_H; y++) {
        int x;
        if (!full && !mip_is_dirty(m, y))
            continue;
        if (!full && m->compare && k_memcmp(m->fb[y], m->shadow[y], MIP_STRIDE) == 0) {
            m->st.lines_skipped_same++;
            continue;
        }
        out[n++] = mip_bitrev8((uint8_t)(y + 1)); /* AG0 first */
        for (x = 0; x < MIP_STRIDE; x++)
            out[n++] = (uint8_t)~m->fb[y][x]; /* ink=1 -> panel 0 (black) */
        out[n++] = 0;                         /* 8 dummy clocks */
        k_memcpy(m->shadow[y], m->fb[y], MIP_STRIDE);
        lines++;
    }
    for (y = 0; y < MIP_DIRTY_WORDS; y++)
        m->dirty[y] = 0;
    if (!lines)
        return 0;
    out[n++] = 0; /* trailer: 16 dummy clocks in total after the last line */
    m->st.flushes++;
    m->st.lines_sent += (uint64_t)lines;
    m->st.bytes_sent += n;
    return n;
}

size_t mip_encode_vcom(struct mip *m, uint8_t *out)
{
    m->vcom ^= 1;
    out[0] = m->vcom ? MIP_CMD_VCOM : 0; /* display mode: M0 = 0, M2 = 0 */
    out[1] = 0;
    m->st.bytes_sent += 2;
    return 2;
}

size_t mip_encode_clear(struct mip *m, uint8_t *out)
{
    out[0] = (uint8_t)(MIP_CMD_CLEAR | (m->vcom ? MIP_CMD_VCOM : 0));
    out[1] = 0;
    k_memset(m->shadow, 0, sizeof(m->shadow)); /* panel all white = no ink */
    m->st.bytes_sent += 2;
    return 2;
}

/* ---------------- reference decoder (models the panel) ---------------- */

struct bitrd {
    const uint8_t *buf;
    size_t nbits, pos;
};

static int rd_bit(struct bitrd *r)
{
    int b;
    if (r->pos >= r->nbits)
        return -1;
    b = (r->buf[r->pos >> 3] >> (7 - (r->pos & 7))) & 1; /* MSB-first SPI */
    r->pos++;
    return b;
}

void mip_panel_init(struct mip_panel *p)
{
    k_memset(p, 0, sizeof(*p));
    k_memset(p->mem, 0xff, sizeof(p->mem)); /* white */
}

int mip_panel_feed(struct mip_panel *p, const uint8_t *buf, size_t n)
{
    struct bitrd r = { buf, n * 8, 0 };
    int m0, m1, m2, i;
    m0 = rd_bit(&r);
    m1 = rd_bit(&r);
    m2 = rd_bit(&r);
    if (m0 < 0 || m1 < 0 || m2 < 0)
        goto bad;
    p->vcom = (uint8_t)m1;
    for (i = 0; i < 5; i++)
        if (rd_bit(&r) < 0)
            goto bad;
    if (m2) { /* all clear: needs >= 13 more clocks (we send 8 + 8) */
        k_memset(p->mem, 0xff, sizeof(p->mem));
        p->clears++;
        return 0;
    }
    if (!m0) /* display mode: only the VCOM bit matters */
        return 0;
    for (;;) {
        int addr = 0, x, b;
        for (i = 0; i < 8; i++) {
            b = rd_bit(&r);
            if (b < 0)
                goto bad; /* stream must end with the dummy trailer */
            addr |= b << i;
        }
        if (addr == 0) /* trailer dummy byte: end of update */
            break;
        if (addr > MIP_H)
            goto bad;
        for (x = 0; x < MIP_W; x++) {
            uint8_t *byte = &p->mem[addr - 1][x >> 3];
            uint8_t bit = (uint8_t)(0x80 >> (x & 7));
            b = rd_bit(&r);
            if (b < 0)
                goto bad;
            if (b)
                *byte |= bit;
            else
                *byte &= (uint8_t)~bit;
        }
        for (i = 0; i < 8; i++)
            if (rd_bit(&r) < 0)
                goto bad;
        p->lines_written++;
    }
    p->frames++;
    return 0;
bad:
    p->errors++;
    return -1;
}

void mip_panel_to_fb(const struct mip_panel *p, uint8_t fb[MIP_H][MIP_STRIDE])
{
    int y, x;
    for (y = 0; y < MIP_H; y++)
        for (x = 0; x < MIP_STRIDE; x++)
            fb[y][x] = (uint8_t)~p->mem[y][x];
}
