#!/usr/bin/env python3
"""orbit-watch build driver.

Uses the Zig toolchain (``pip install ziglang``) as a hermetic C compiler:
``zig cc`` cross-compiles the firmware for Cortex-M33 (thumbv8m.main, soft
float) and also builds the host unit tests and benchmarks natively.

    python build.py                    # firmware for every board
    python build.py --board an505      # one board
    python build.py host-tests         # compile + run the C unit tests
    python build.py bench              # compile the host benchmark programs
    python build.py clean

Firmware outputs in build/<board>/: orbit-watch.elf, orbit-watch.bin, .map
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
BUILD = ROOT / "build"
FW = ROOT / "firmware"

BOARDS = {
    # QEMU `-M mps2-an505`: Arm MPS2+ FPGA image AN505, Cortex-M33 (Secure state)
    "an505": {"ld": "an505.ld", "defs": ["BOARD_AN505=1"]},
    # Nordic nRF54L15 (QFN48): builds and links; not yet run on hardware
    "nrf54l15": {"ld": "nrf54l15.ld", "defs": ["BOARD_NRF54L15=1"]},
}

COMMON_SOURCES = [
    "arch/cm33/startup.c",
    "arch/cm33/context.S",
    "kernel/sched.c",
    "kernel/runqueue.c",
    "kernel/tickless.c",
    "kernel/timerq.c",
    "kernel/pm.c",
    "kernel/idle.c",
    "kernel/console.c",
    "kernel/shell.c",
    "kernel/main.c",
    "lib/kprintf.c",
    "lib/kstring.c",
    "lib/shell_parse.c",
    "lib/rtt.c",
    "mm/buddy.c",
    "mm/kheap.c",
    "display/mip.c",
    "display/gfx.c",
    "app/watchface.c",
    "app/steps.c",
    "app/hr.c",
    "app/synth.c",
    "app/watch.c",
]

# Host unit tests: test name -> firmware sources it links against.
HOST_TESTS = {
    # inherited from orbit-os (default 4 KiB-page configuration)
    "test_printf": ["lib/kprintf.c", "lib/kstring.c"],
    "test_string": ["lib/kstring.c"],
    "test_buddy": ["mm/buddy.c"],
    "test_kheap": ["mm/kheap.c", "mm/buddy.c"],
    "test_runqueue": ["kernel/runqueue.c"],
    "test_shell_parse": ["lib/shell_parse.c"],
    # orbit-watch
    "test_mm_mcu": ["mm/kheap.c", "mm/buddy.c"],
    "test_tickless": ["kernel/tickless.c"],
    "test_pm": ["kernel/pm.c"],
    "test_timerq": ["kernel/timerq.c"],
    "test_mip": ["display/mip.c", "display/gfx.c", "app/watchface.c", "lib/kprintf.c",
                 "lib/kstring.c"],
    "test_steps": ["app/steps.c", "app/synth.c"],
    "test_hr": ["app/hr.c", "app/synth.c", "app/steps.c"],
    "test_rtt": ["lib/rtt.c", "lib/kstring.c"],
}
HOST_TEST_DEFS = {
    "test_mm_mcu": ["BUDDY_PAGE_SHIFT=10", "BUDDY_ORDERS=8", "KHEAP_PAGE_SIZE=1024",
                    "KHEAP_NCLASSES=6"],
}

# Host programs that print measurements (JSON) used by docs/assets/make_figures.py.
BENCHES = {
    "bench_display": ["display/mip.c", "display/gfx.c", "app/watchface.c", "lib/kprintf.c",
                      "lib/kstring.c"],
    "bench_algos": ["app/steps.c", "app/hr.c", "app/synth.c"],
    "render_face": ["display/mip.c", "display/gfx.c", "app/watchface.c", "lib/kprintf.c",
                    "lib/kstring.c"],
}

MCU_DEFS = ["BUDDY_PAGE_SHIFT=10", "BUDDY_ORDERS=8", "KHEAP_PAGE_SIZE=1024", "KHEAP_NCLASSES=6"]


def zig() -> list[str]:
    exe = shutil.which("zig")
    if exe and os.environ.get("OW_USE_SYSTEM_ZIG"):
        return [exe]
    return [sys.executable, "-m", "ziglang"]


def run(cmd: list, verbose: bool) -> None:
    if verbose:
        print(" ".join(str(c) for c in cmd))
    r = subprocess.run([str(c) for c in cmd])
    if r.returncode != 0:
        raise SystemExit(f"command failed ({r.returncode}): {' '.join(map(str, cmd))}")


def cross_cflags(board: str) -> list[str]:
    defs = BOARDS[board]["defs"] + MCU_DEFS
    return [
        "-target", "thumb-freestanding-eabi", "-mcpu=cortex_m33", "-mthumb",
        "-mfloat-abi=soft",
        "-std=gnu11", "-Os", "-g",
        "-ffreestanding", "-fno-builtin", "-fno-stack-protector", "-fno-sanitize=all",
        "-fno-pic", "-fno-pie", "-ffunction-sections", "-fdata-sections",
        "-fno-unwind-tables", "-fno-asynchronous-unwind-tables",
        "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
        f"-I{FW / 'include'}", f"-I{FW / 'boards' / board}",
    ] + [f"-D{d}" for d in defs]


def build_board(name: str, verbose: bool = False) -> Path:
    out = BUILD / name
    objdir = out / "obj"
    cflags = cross_cflags(name)
    srcs = [FW / s for s in COMMON_SOURCES] + [FW / "boards" / name / "board.c"]
    objs = []
    for src in srcs:
        obj = objdir / src.relative_to(FW).with_suffix(".o")
        obj.parent.mkdir(parents=True, exist_ok=True)
        run(zig() + ["cc"] + cflags + ["-c", src, "-o", obj], verbose)
        objs.append(obj)
    elf = out / "orbit-watch.elf"
    # board MEMORY block + shared SECTIONS -> one script (zig cc has no -L for scripts)
    ld = out / "link.ld"
    board_ld = (FW / "ld" / BOARDS[name]["ld"]).read_text().replace("INCLUDE common.ld", "")
    ld.write_text(board_ld + (FW / "ld" / "common.ld").read_text())
    # Link through `zig cc` so compiler-rt (64-bit division helpers) is linked in.
    run(zig() + ["cc", "-target", "thumb-freestanding-eabi", "-mcpu=cortex_m33",
                 "-nostartfiles", "-static",
                 "-T", ld,
                 "-Wl,--gc-sections", "-Wl,--entry=Reset_Handler",
                 "-Wl,--build-id=none", "-o", elf] + objs, verbose)
    run(zig() + ["objcopy", "-O", "binary", elf, out / "orbit-watch.bin"], verbose)
    print(f"[{name}] orbit-watch.elf, orbit-watch.bin "
          f"{(out / 'orbit-watch.bin').stat().st_size} bytes -> {out}")
    return elf


def host_exe(name: str) -> Path:
    return BUILD / "host" / (name + (".exe" if os.name == "nt" else ""))


def build_host(name: str, srcs: list[str], src_dir: Path, defs: list[str], verbose: bool) -> Path:
    hdir = BUILD / "host"
    hdir.mkdir(parents=True, exist_ok=True)
    exe = host_exe(name)
    cmd = zig() + ["cc", "-std=gnu11", "-O2", "-g", "-Wall", "-Wextra", "-Werror",
                   "-Wno-unused-parameter", "-fno-sanitize=all", "-DOW_HOST=1",
                   f"-I{FW / 'include'}", f"-I{ROOT / 'tests'}",
                   src_dir / f"{name}.c"]
    cmd += [f"-D{d}" for d in defs]
    cmd += [FW / s for s in srcs] + ["-o", exe]
    run(cmd, verbose)
    return exe


def build_host_tests(verbose: bool = False) -> list[Path]:
    return [build_host(t, s, ROOT / "tests", HOST_TEST_DEFS.get(t, []), verbose)
            for t, s in HOST_TESTS.items()]


def build_benches(verbose: bool = False) -> list[Path]:
    return [build_host(b, s, ROOT / "tools", [], verbose) for b, s in BENCHES.items()]


def run_host_tests(verbose: bool) -> int:
    failed = 0
    for exe in build_host_tests(verbose):
        r = subprocess.run([str(exe)])
        print(("PASS " if r.returncode == 0 else "FAIL ") + exe.stem)
        failed += r.returncode != 0
    return failed


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("target", nargs="?", default="all",
                    choices=["all", "host-tests", "bench", "clean"])
    ap.add_argument("--board", default="all", choices=["all"] + list(BOARDS))
    ap.add_argument("-v", "--verbose", action="store_true")
    a = ap.parse_args()
    if a.target == "clean":
        shutil.rmtree(BUILD, ignore_errors=True)
        return 0
    if a.target == "host-tests":
        return 1 if run_host_tests(a.verbose) else 0
    if a.target == "bench":
        build_benches(a.verbose)
        return 0
    for name in (BOARDS if a.board == "all" else [a.board]):
        build_board(name, a.verbose)
    return 0


if __name__ == "__main__":
    sys.exit(main())
