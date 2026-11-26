/* Console over a SEGGER-RTT-compatible memory channel.
 *
 * A J-Link (or probe-rs / OpenOCD) finds the control block in RAM by its
 * "SEGGER RTT" signature and moves bytes through two ring buffers while the
 * CPU runs, so the watch gets a shell without a UART (and without keeping
 * the UART's high-frequency clock alive in deep sleep). The layout below is
 * the publicly documented RTT control block; the code is this project's.
 *
 * Ring convention: the writer owns WrOff, the reader owns RdOff, the buffer
 * is empty when they are equal and one byte is always left free. */
#ifndef OW_RTT_H
#define OW_RTT_H

#include <stddef.h>
#include <stdint.h>

struct rtt_ring {
    const char *name;
    char *buf;
    uint32_t size;
    volatile uint32_t wr;
    volatile uint32_t rd;
    uint32_t flags; /* 0 = skip when full (never block the kernel) */
};

struct rtt_cb {
    char id[16];             /* "SEGGER RTT", written last */
    int32_t max_up, max_down;
    struct rtt_ring up[1];   /* target -> host */
    struct rtt_ring down[1]; /* host -> target */
};

void   rtt_init(struct rtt_cb *cb, char *upbuf, uint32_t upsz, char *downbuf, uint32_t downsz);
/* Write up to n bytes; returns how many fit (the rest is dropped). */
size_t rtt_write(struct rtt_cb *cb, const void *data, size_t n);
/* Read one byte sent by the host, or -1. */
int    rtt_getc(struct rtt_cb *cb);
uint32_t rtt_up_free(const struct rtt_cb *cb);

#endif
