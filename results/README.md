# results/

Everything in this directory was written by `python tools/collect_results.py` on the machine
described in `meta.json` (26 Nov 2026, Windows 10 desktop, zig 0.17.1, QEMU 11.1.3). CI
regenerates it on every push and uploads the result as the `orbit-watch-ci` artifact.

| file | produced by | kind |
|---|---|---|
| `bench_display.json` | `tools/bench_display.c` | host benchmark (SPI bytes per refresh strategy, one simulated hour) |
| `bench_algos.json` | `tools/bench_algos.c` | host benchmark on synthetic signals (steps, HR) |
| `bidmc.json` | `tools/bench_algos.c --ppg data/bidmc/*.csv` | HR on real fingertip PPG (BIDMC) |
| `ppg_trace.csv` | `tools/bench_algos.c --trace` | synthetic PPG, filtered signal, detected beats |
| `qemu_active.json`, `qemu_lowpower.json`, `qemu_fullrefresh.json` | `tools/qemu_run.py` | 🧪 emulated: `STATS` line and `pm` output of the firmware on QEMU mps2-an505 |
| `frames/qemu_*.pbm` | firmware `snap` command (virtual panel) | 🧪 emulated panel memory, 144 x 168, binary PBM |
| `frames/host_render.pbm` | `tools/render_face.c` | host render through the same encoder + decoder |
| `power_budget.json`, `power_budget.csv` | `tools/power_budget.py` | **model**: datasheet values x emulated duty cycles x assumptions |
| `meta.json` | `tools/collect_results.py` | machine, date, tool versions |
