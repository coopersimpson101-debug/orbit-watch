/* RTT ring semantics, with the test playing the debug probe. */
#include <stdint.h>
#include "test.h"
#include "rtt.h"

static struct rtt_cb cb;
static char up[16], down[8];

static int host_read(char *out, int max)
{
    int n = 0;
    while (cb.up[0].rd != cb.up[0].wr && n < max) {
        out[n++] = cb.up[0].buf[cb.up[0].rd];
        cb.up[0].rd = (cb.up[0].rd + 1) % cb.up[0].size;
    }
    return n;
}

static void host_send(const char *s)
{
    for (; *s; s++) {
        cb.down[0].buf[cb.down[0].wr] = *s;
        cb.down[0].wr = (cb.down[0].wr + 1) % cb.down[0].size;
    }
}

int main(void)
{
    char buf[64];
    int n;
    rtt_init(&cb, up, sizeof(up), down, sizeof(down));
    CHECK(strcmp(cb.id, "SEGGER RTT") == 0);
    CHECK(cb.max_up == 1 && cb.max_down == 1);
    CHECK(rtt_up_free(&cb) == 15);
    CHECK(rtt_write(&cb, "hello", 5) == 5);
    n = host_read(buf, 64);
    CHECK(n == 5 && memcmp(buf, "hello", 5) == 0);
    /* wrap around and overflow: only 15 bytes fit, rest dropped, never blocks */
    CHECK(rtt_write(&cb, "0123456789abcdefXYZ", 19) == 15);
    n = host_read(buf, 64);
    CHECK(n == 15 && memcmp(buf, "0123456789abcde", 15) == 0);
    CHECK(rtt_getc(&cb) == -1);
    host_send("ps\r");
    CHECK(rtt_getc(&cb) == 'p');
    CHECK(rtt_getc(&cb) == 's');
    CHECK(rtt_getc(&cb) == '\r');
    CHECK(rtt_getc(&cb) == -1);
    host_send("abcdef"); /* wraps the 8-byte down buffer */
    for (n = 0; n < 6; n++)
        CHECK(rtt_getc(&cb) == 'a' + n);
    TEST_REPORT("test_rtt");
}
