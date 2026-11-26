/* Kernel console: formatted output to the board console, input ring filled
 * from the RX interrupt. */
#include <stdarg.h>
#include "board.h"
#include "cm33.h"
#include "kernel.h"
#include "kprintf.h"
#include "sched.h"

#define RXQ 256 /* holds a whole scripted session (tools/qemu_run.py) */
static volatile char rxq[RXQ];
static volatile unsigned rx_head, rx_tail;
static struct thread *reader;

#define EV_CONSOLE (1u << 31)

void kputs(const char *s)
{
    for (; *s; s++) {
        if (*s == '\n')
            board_putc('\r');
        board_putc(*s);
    }
}

int kprintf(const char *fmt, ...)
{
    char buf[192];
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = k_vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    kputs(buf);
    return n;
}

void console_set_reader(void *thread)
{
    reader = thread;
}

void console_rx_isr(char c)
{
    unsigned next = (rx_head + 1) % RXQ;
    if (next != rx_tail) {
        rxq[rx_head] = c;
        rx_head = next;
    }
    if (reader)
        ev_post(reader, EV_CONSOLE);
}

int console_getc(void)
{
    uint32_t f = irq_save();
    int c = -1;
    if (rx_tail != rx_head) {
        c = (unsigned char)rxq[rx_tail];
        rx_tail = (rx_tail + 1) % RXQ;
    }
    irq_restore(f);
    return c;
}
