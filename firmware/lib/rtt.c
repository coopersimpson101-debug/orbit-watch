/* RTT-compatible console rings. See rtt.h. */
#include "rtt.h"
#include "kstring.h"

#define BARRIER() __asm__ volatile("" ::: "memory")

void rtt_init(struct rtt_cb *cb, char *upbuf, uint32_t upsz, char *downbuf, uint32_t downsz)
{
    static const char sig[] = "SEGGER RTT";
    k_memset(cb, 0, sizeof(*cb));
    cb->max_up = 1;
    cb->max_down = 1;
    cb->up[0].name = "Terminal";
    cb->up[0].buf = upbuf;
    cb->up[0].size = upsz;
    cb->down[0].name = "Terminal";
    cb->down[0].buf = downbuf;
    cb->down[0].size = downsz;
    BARRIER();
    /* signature last, so a probe never sees a half-initialised block */
    k_memcpy(cb->id, sig, sizeof(sig));
}

uint32_t rtt_up_free(const struct rtt_cb *cb)
{
    const struct rtt_ring *r = &cb->up[0];
    uint32_t rd = r->rd, wr = r->wr;
    return rd > wr ? rd - wr - 1 : r->size - (wr - rd) - 1;
}

size_t rtt_write(struct rtt_cb *cb, const void *data, size_t n)
{
    struct rtt_ring *r = &cb->up[0];
    const char *p = data;
    uint32_t avail = rtt_up_free(cb), wr = r->wr;
    size_t i, k = n < avail ? n : avail;
    for (i = 0; i < k; i++) {
        r->buf[wr] = p[i];
        wr = wr + 1 == r->size ? 0 : wr + 1;
    }
    BARRIER();
    r->wr = wr;
    return k;
}

int rtt_getc(struct rtt_cb *cb)
{
    struct rtt_ring *r = &cb->down[0];
    uint32_t rd = r->rd;
    int c;
    if (rd == r->wr)
        return -1;
    c = (unsigned char)r->buf[rd];
    BARRIER();
    r->rd = rd + 1 == r->size ? 0 : rd + 1;
    return c;
}
