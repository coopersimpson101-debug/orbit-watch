/* UART / RTT shell. Line editing and tokenising come from orbit-os
 * (shell_parse.c); the commands are orbit-watch's. */
#include "board.h"
#include "cm33.h"
#include "kernel.h"
#include "kstring.h"
#include "sched.h"
#include "shell_parse.h"

#define EV_CONSOLE (1u << 31)

static void echo(void *ctx, const char *s, size_t n)
{
    (void)ctx;
    while (n--)
        board_putc(*s++);
}

static void cmd_help(void)
{
    kputs("commands:\n"
          "  help              this list\n"
          "  info              board, clocks, build\n"
          "  ps                threads, cpu share, stack use\n"
          "  mem               page allocator and heap\n"
          "  uptime            kernel ticks and low-power counter\n"
          "  pm                sleep-state residency and tickless stats\n"
          "  deep on|off       allow PM_DEEP\n"
          "  face              watch state; face seconds on|off; face full on|off\n"
          "  imu batch <s>     IMU FIFO batch period\n"
          "  hr now            start a heart-rate measurement\n"
          "  stats             watch statistics (JSON)\n"
          "  snap <tag>        dump the panel image (emulator)\n"
          "  run <s> | +<s>    run until uptime <s>, or for <s> more seconds\n"
          "  poweroff          exit (emulator) / System OFF (hardware)\n");
}

static void cmd_ps(void)
{
    int i;
    uint64_t total = lp_elapsed_since_boot();
    kprintf("  id name     prio state  switches  cpu%%   stack used/size\n");
    for (i = 0; i < sched_thread_count(); i++) {
        struct thread *t = sched_thread(i);
        static const char *const st[] = {"ready", "run", "block", "dead"};
        uint64_t tc = total * board_cycles_hz / board.lp_hz;
        uint64_t cpu = t->cpu_cycles - (t->prio == PRIO_IDLE ? idle.sleep_cycles : 0);
        uint64_t pm = tc ? cpu * 10000 / tc : 0;
        kprintf("  %2d %-8s %4d %-6s %8u  %3u.%02u  %5u/%u\n", t->id, t->name, t->prio,
                st[t->state], (unsigned)t->switches, (unsigned)(pm / 100), (unsigned)(pm % 100),
                (unsigned)thread_stack_high_water(t), (unsigned)t->stack_size);
    }
}

static void cmd_mem(void)
{
    kprintf("pages: %u free of %u (%u B each)\n", (unsigned)buddy_free_pages(&kernel_pages),
            (unsigned)kernel_pages.managed_pages, (unsigned)PAGE_SIZE);
    kprintf("heap: %u B in use, %u slab pages, %u large pages, %u allocs, %u frees\n",
            (unsigned)kernel_heap.st.bytes_in_use, (unsigned)kernel_heap.st.slab_pages,
            (unsigned)kernel_heap.st.large_pages, (unsigned)kernel_heap.st.allocs,
            (unsigned)kernel_heap.st.frees);
}

static void cmd_pm(void)
{
    int s;
    uint64_t total = lp_elapsed_since_boot(), slept = 0;
    for (s = 0; s < PM_NSTATES; s++)
        slept += idle.sleep_lp[s];
    kprintf("uptime %u s, wakeups %u, locks 0x%x, deep %s\n",
            (unsigned)(total / board.lp_hz), (unsigned)idle.wakeups, (unsigned)idle.locks,
            idle.deep_allowed ? "allowed" : "off");
    for (s = 0; s < PM_OFF; s++) {
        uint64_t pm = total ? idle.sleep_lp[s] * 10000 / total : 0;
        kprintf("  %-9s entries %8u  residency %3u.%02u %%\n", pm_states[s].name,
                (unsigned)idle.pm.entries[s], (unsigned)(pm / 100), (unsigned)(pm % 100));
    }
    {
        uint64_t act = total > slept ? total - slept : 0;
        uint64_t pm = total ? act * 10000 / total : 0;
        kprintf("  %-9s                   %3u.%02u %%\n", "active", (unsigned)(pm / 100),
                (unsigned)(pm % 100));
    }
    kprintf("tickless: %u sleeps, %u ticks planned, %u credited, %u early, %u late, "
            "%u rejected\n", (unsigned)idle.tl.st.sleeps, (unsigned)idle.tl.st.planned_ticks,
            (unsigned)idle.tl.st.slept_ticks, (unsigned)idle.tl.st.early_wakes,
            (unsigned)idle.tl.st.late_wakes, (unsigned)idle.tl.st.rejected);
}

static void dispatch(int argc, char **argv)
{
    const char *c = argv[0];
    if (!k_strcmp(c, "help")) {
        cmd_help();
    } else if (!k_strcmp(c, "info")) {
        kprintf("orbit-watch %s, board %s, cpu %u Hz, lp %u Hz/%u bit, RAM %u KiB\n",
                OW_VERSION, board.name, board.cpu_hz, board.lp_hz, board.lp_bits,
                (unsigned)(board.ram_bytes / 1024));
    } else if (!k_strcmp(c, "ps")) {
        cmd_ps();
    } else if (!k_strcmp(c, "mem")) {
        cmd_mem();
    } else if (!k_strcmp(c, "uptime")) {
        kprintf("ticks %u (%u Hz), lp %u cycles since boot\n", (unsigned)ticks_now(), OW_HZ,
                (unsigned)lp_elapsed_since_boot());
    } else if (!k_strcmp(c, "pm")) {
        cmd_pm();
    } else if (!k_strcmp(c, "deep") && argc == 2) {
        idle.deep_allowed = !k_strcmp(argv[1], "on");
        kprintf("deep sleep %s\n", idle.deep_allowed ? "allowed" : "disabled");
    } else if (!k_strcmp(c, "poweroff")) {
        kputs("powering off\n");
        board_system_off();
    } else {
        watch_shell_cmd(argc, argv); /* face, imu, hr, stats, snap, run */
    }
}

void shell_thread(void *arg)
{
    struct line_editor le;
    (void)arg;
    le_init(&le, echo, 0);
    kputs("orbit-watch shell, type 'help'\nwatch> ");
    for (;;) {
        int ch = console_getc();
        if (ch < 0) {
            ev_wait(EV_CONSOLE, 0);
            continue;
        }
        switch (le_feed(&le, (char)ch)) {
        case LINE_READY: {
            char *argv[SHELL_MAX_ARGS];
            int argc = shell_tokenize(le.buf, argv, SHELL_MAX_ARGS);
            if (argc < 0)
                kputs("parse error\n");
            else if (argc > 0)
                dispatch(argc, argv);
            kputs("watch> ");
            break;
        }
        case LINE_CANCELLED:
            kputs("watch> ");
            break;
        default:
            break;
        }
    }
}
