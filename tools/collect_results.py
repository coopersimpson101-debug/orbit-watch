#!/usr/bin/env python3
"""Regenerate everything in results/ from code in this repository.

  1. build the firmware (both boards) and the host benchmark programs
  2. bench_display  -> results/bench_display.json  (SPI bytes per refresh strategy)
  3. bench_algos    -> results/bench_algos.json    (steps + HR on synthetic signals)
                    -> results/bidmc.json          (HR on the BIDMC excerpts in data/)
                    -> results/ppg_trace.csv
  4. QEMU mps2-an505 profiles (if qemu-system-arm is available)
                    -> results/qemu_<profile>.json, results/frames/*.pbm
  5. tools/power_budget.py -> results/power_budget.{json,csv}
  6. results/meta.json: machine, date, tool versions

    python tools/collect_results.py [--no-qemu]
"""
from __future__ import annotations

import argparse
import datetime
import json
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RES = ROOT / "results"
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "tools"))

import build  # noqa: E402


def run_json(cmd: list[str]):
    out = subprocess.run([str(c) for c in cmd], check=True, capture_output=True, text=True).stdout
    return json.loads(out)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-qemu", action="store_true")
    a = ap.parse_args()
    RES.mkdir(exist_ok=True)
    (RES / "frames").mkdir(exist_ok=True)
    for b in build.BOARDS:
        build.build_board(b)
    build.build_benches()
    bd = run_json([build.host_exe("bench_display")])
    (RES / "bench_display.json").write_text(json.dumps(bd, indent=1) + "\n", newline="\n")
    ba = run_json([build.host_exe("bench_algos")])
    (RES / "bench_algos.json").write_text(json.dumps(ba, indent=1) + "\n", newline="\n")
    bid = {}
    for f in sorted((ROOT / "data" / "bidmc").glob("*.csv")):
        bid[f.stem] = run_json([build.host_exe("bench_algos"), "--ppg", f])
    (RES / "bidmc.json").write_text(json.dumps(bid, indent=1) + "\n", newline="\n")
    subprocess.run([str(build.host_exe("bench_algos")), "--trace", str(RES / "ppg_trace.csv")],
                   check=True)
    subprocess.run([str(build.host_exe("render_face")), str(RES / "frames" / "host_render.pbm")],
                   check=True)

    meta = {
        "date": datetime.date.today().isoformat(),
        "machine": f"{platform.system()} {platform.release()}, {platform.machine()}, "
                   f"{os.cpu_count()} logical CPUs",
        "python": platform.python_version(),
    }
    try:
        meta["zig"] = subprocess.run(build.zig() + ["version"], capture_output=True,
                                     text=True).stdout.strip()
    except OSError:
        pass
    if not a.no_qemu:
        import qemu_run
        exe = qemu_run.find_qemu()
        meta["qemu"] = subprocess.run([exe, "--version"], capture_output=True,
                                      text=True).stdout.splitlines()[0]
        for prof in ("active", "lowpower", "fullrefresh"):
            out = ROOT / "build" / "qemu" / prof
            r = qemu_run.run(qemu_run.PROFILES[prof], out, qemu_run.ELF, 1500)
            if r["exit_code"] != 0 or "stats" not in r:
                raise SystemExit(f"QEMU profile {prof} failed: {r['exit_code']}")
            keep = {k: r[k] for k in ("stats", "exit_code", "host_seconds", "commands")}
            keep["pm_output"] = r["outputs"].get("pm", "")
            (RES / f"qemu_{prof}.json").write_text(json.dumps(keep, indent=1) + "\n",
                                                   newline="\n")
            for pbm in out.glob("*.pbm"):
                shutil.copy(pbm, RES / "frames" / f"qemu_{pbm.name}")
    subprocess.run([sys.executable, str(ROOT / "tools" / "power_budget.py")], check=True)
    (RES / "meta.json").write_text(json.dumps(meta, indent=1) + "\n", newline="\n")
    print("results written to", RES)
    return 0


if __name__ == "__main__":
    sys.exit(main())
