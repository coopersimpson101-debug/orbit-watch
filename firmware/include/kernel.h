/* Kernel-wide services: memory, console, version. */
#ifndef OW_KERNEL_H
#define OW_KERNEL_H

#include <stddef.h>
#include <stdint.h>
#include "buddy.h"
#include "kheap.h"
#include "pm.h"
#include "tickless.h"

#define OW_VERSION "0.1.0"

extern struct buddy kernel_pages;
extern struct kheap kernel_heap;

void *kmalloc(size_t n);
void  kfree(void *p);

void kputs(const char *s);
int  kprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* Console input queue (filled by the board RX interrupt or RTT polling). */
int  console_getc(void);           /* -1 when empty */
void console_set_reader(void *thread);

/* Power management state shared by idle and the shell. */
struct idle_state {
    struct tickless tl;
    struct pm_stats pm;
    uint32_t locks;
    uint32_t wake_sources;
    uint64_t wakeups;
    uint64_t boot_lp;
    uint64_t sleep_lp[PM_NSTATES];
    uint64_t sleep_cycles;    /* board_cycles() spent inside board_sleep() */
    int deep_allowed;
};
extern struct idle_state idle;
void idle_init(void);
void idle_thread(void *arg);
void pm_lock(uint32_t bits);
void pm_unlock(uint32_t bits);
uint64_t lp_elapsed_since_boot(void);

void shell_thread(void *arg);
void watch_start(void);
void watch_print_stats(void);
void watch_shell_cmd(int argc, char **argv);

#endif
