# Testing

| layer | what | where it runs |
|---|---|---|
| C unit tests (`tests/test_*.c`) | tickless accounting (drift-free soak, wrap-around, early/late wakes), PM policy, timer queue, MIP encoder vs datasheet bit tables and the reference decoder, watchface partial updates, step counter, heart rate, RTT rings, MCU allocator config, plus orbit-os's buddy/kheap/run-queue/printf/string/shell tests | host (zig cc), locally and in CI |
| benchmarks (`tools/bench_*.c`) | SPI bytes per refresh strategy; step and HR accuracy sweeps; HR on BIDMC | host |
| firmware images | both boards build with `-Werror`; vector table and reset vector checked | host + CI |
| emulated system (`tools/qemu_run.py`) | the AN505 firmware boots under `qemu-system-arm -M mps2-an505`, the shell is driven over a TCP serial port, the watch runs for an hour of virtual time, and the virtual panel image and statistics are collected | locally (QEMU 11.1 from MSYS2) and in CI (Ubuntu's QEMU) |
| schematic (`hardware/schematic/orbit_watch.py`) | SKiDL ERC + project design checks + footprint pad check | host + CI; KiCad's own footprint loader in the `kicad/kicad` container (CI only) |

`python -m pytest -q tests` runs all of it. The QEMU tests are skipped when `qemu-system-arm`
is not found (set `QEMU_SYSTEM_ARM` to point at a binary), and the schematic test is skipped
when `skidl` is not installed.

## What the emulator does and does not tell you

* QEMU runs with `-icount shift=0,sleep=off`. Every guest instruction advances virtual time by
  1 ns, and when the guest sleeps in WFI the clock jumps to the next timer deadline. Timers,
  SysTick and the low-power counter therefore all follow one deterministic virtual clock, and
  an hour of watch time takes a few seconds.
* Instruction counts are real for this binary. Cycle counts, cache behaviour, wake-up latency
  and current draw are not modelled. `tools/power_budget.py` turns instruction counts into time
  with an assumed CPI and labels the result as a model.
* There are no sensors or display on the AN505. The firmware reads synthetic accelerometer and
  PPG signals (`app/synth.c`) following a one-hour activity script, and the display SPI frames
  are decoded by the virtual panel. Time spent on that emulator-only work is measured and
  subtracted before modelling.
* The time between typed shell commands also advances the guest clock. `run N` waits for an
  absolute uptime, so the measured windows do not depend on typing speed.
