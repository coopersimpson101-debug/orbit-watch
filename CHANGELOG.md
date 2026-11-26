# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/).

## [Unreleased]

### Fixed
- AN505 board on QEMU 8.2 (Ubuntu 24.04, CI): UART0 RX is IRQ 33 there (TX/RX lines were
  swapped until QEMU 9.1), so the shell never saw input; both IRQs are now enabled.
- AN505 wake-up alarm moved from a re-armed CMSDK APB timer, which fired late or not at all
  on QEMU 8.2, to the CMSDK dual timer in one-shot mode; the timer:lp clock ratio is measured at boot.
- `ev_wait()` no longer assumes the pended context switch happens immediately.
- KiCad pad table pinned to the KiCad 9.0.9 library (USB4105 shield pad is `S1` in 9.x).
- QEMU harness sends the command list as one script: with `-icount sleep=off`, host latency
  between interactive commands added guest time (minutes on a loaded CI runner), which shifted
  the synthetic activity script and the measured windows. Console RX queue is now 256 bytes.
- Netlist output is deterministic (fixed SKiDL tags, no date/path); tests write to a temp dir.

## [0.1.0] - 2026-11-26

### Added
- Cortex-M33 kernel derived from orbit-os: PendSV context switch, PSPLIM stack guards,
  priority round-robin scheduler, event flags, kernel timer queue, buddy + slab heap with
  1 KiB pages, UART/RTT shell.
- Tickless idle slaved to a low-power counter (drift-free tick grid), power-state policy
  (idle / tickless / deep / off) with wake-source masks and driver locks.
- Sharp LS013B7DH05 memory-LCD driver: dirty-row tracking, identical-row suppression,
  datasheet-exact SPI frame encoder and a bit-level reference decoder (virtual panel).
- Watchface, integer step counter, PPG heart-rate estimator (Butterworth band-pass,
  interpolated peak timing, IBI median), synthetic sensor models.
- Boards: QEMU mps2-an505 (runs in CI) and nRF54L15 rev A (builds; not yet run on silicon).
- SKiDL schematic of the rev A board (nRF54L15, nPM1300, BMI270, MAX30101, LS013B7DH05)
  generating a KiCad netlist, BOM, ERC and design checks.
- Datasheet-based power model, host benchmarks, BIDMC PPG test excerpts, QEMU harness.

[0.1.0]: https://github.com/coopersimpson101-debug/orbit-watch/releases/tag/v0.1.0
