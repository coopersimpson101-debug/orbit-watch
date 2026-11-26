"""pytest entry point for orbit-watch.

* compiles the portable firmware modules natively with zig cc and runs the C
  unit tests (tickless, PM policy, timer queue, MIP encoder/decoder, step
  counter, heart rate, RTT, plus the allocator/scheduler pieces from orbit-os)
* cross-builds the firmware for both boards and checks the images
* runs the host benchmarks and checks their invariants
* checks the heart-rate estimator on the BIDMC PPG excerpts in data/bidmc
* generates the SKiDL netlist and runs the design checks (needs `skidl`)
* boots the AN505 image under QEMU and drives the shell (needs qemu-system-arm)
"""
from __future__ import annotations

import importlib.util
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "tools"))

import build  # noqa: E402


@pytest.fixture(scope="session")
def host_bins():
    build.build_host_tests()
    return {name: build.host_exe(name) for name in build.HOST_TESTS}


@pytest.fixture(scope="session")
def benches():
    build.build_benches()
    return {name: build.host_exe(name) for name in build.BENCHES}


@pytest.fixture(scope="session")
def firmware():
    return {b: build.build_board(b) for b in build.BOARDS}


def run(cmd, **kw):
    r = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, timeout=900, **kw)
    print(r.stdout)
    print(r.stderr, file=sys.stderr)
    return r


@pytest.mark.parametrize("name", list(build.HOST_TESTS))
def test_c_unit(host_bins, name):
    r = run([host_bins[name]])
    assert r.returncode == 0, r.stderr
    assert " 0 failures" in r.stdout


def test_filter_coefficients_are_current():
    assert run([sys.executable, ROOT / "tools" / "design_filters.py", "--check"]).returncode == 0


@pytest.mark.parametrize("board", list(build.BOARDS))
def test_firmware_image(firmware, board):
    elf = firmware[board]
    binf = elf.with_suffix(".bin")
    assert binf.stat().st_size < 64 * 1024
    data = elf.read_bytes()
    assert data[:4] == b"\x7fELF" and data[18] == 40  # EM_ARM
    elftools = pytest.importorskip("elftools.elf.elffile")
    with open(elf, "rb") as f:
        e = elftools.ELFFile(f)
        sym = e.get_section_by_name(".symtab")
        vt = sym.get_symbol_by_name("vector_table")[0]["st_value"]
        assert vt == {"an505": 0x10000000, "nrf54l15": 0}[board]
        reset = sym.get_symbol_by_name("Reset_Handler")[0]["st_value"]
        vec = e.get_section_by_name(".vectors").data()
        assert int.from_bytes(vec[4:8], "little") == reset | 1  # Thumb bit set
        assert sym.get_symbol_by_name("PendSV_Handler")


def test_display_bench(benches):
    rows = json.loads(run([benches["bench_display"]]).stdout)
    by = {(r["strategy"], r["seconds"]): r for r in rows}
    for sec in (0, 1):
        full, dirty, part = (by[(s, sec)]["bytes_per_hour"] for s in ("full", "dirty", "partial"))
        assert part < dirty < full
        assert all(by[(s, sec)]["mismatched_frames"] == 0 for s in ("full", "dirty", "partial"))
    assert by[("full", 1)]["bytes_per_hour"] == 3600 * (2 + 20 * 168)
    assert by[("full", 1)]["bytes_per_hour"] / by[("partial", 1)]["bytes_per_hour"] > 5


def test_algo_bench(benches):
    res = json.loads(run([benches["bench_algos"]]).stdout)
    for c in res["steps"]:
        if c["truth"]:
            assert abs(c["error_pct"]) < 3, c
        elif c["case"] in ("rest", "typing"):
            assert c["counted"] == 0, c
    for c in res["hr"]:
        if c["pulse_amp"] >= 300:
            assert c["mae_bpm"] < 3 and c["coverage"] > 0.95, c


@pytest.mark.parametrize("rec", sorted(p.name for p in (ROOT / "data" / "bidmc").glob("*.csv")))
def test_hr_on_bidmc(benches, rec):
    r = json.loads(run([benches["bench_algos"], "--ppg", ROOT / "data" / "bidmc" / rec]).stdout)
    assert r["coverage"] > 0.9, r
    assert r["mae_bpm"] < 3.0, r


def test_host_render(benches, tmp_path):
    out = tmp_path / "face.pbm"
    assert run([benches["render_face"], out]).returncode == 0
    data = out.read_bytes()
    assert data.startswith(b"P4\n144 168\n") and len(data) == 11 + 18 * 168
    assert 0 < sum(bin(b).count("1") for b in data[11:]) < 144 * 168 // 2


def test_footprint_table_covers_parts():
    import fetch_kicad_pads
    pads = json.loads((ROOT / "hardware" / "kicad_pads.json").read_text())
    assert set(fetch_kicad_pads.footprints()) <= set(pads)


HAVE_SKIDL = importlib.util.find_spec("skidl") is not None


@pytest.mark.skipif(not HAVE_SKIDL, reason="pip install skidl to generate the netlist")
def test_schematic_netlist(tmp_path):
    # write to a temp dir (and run there: SKiDL drops .log/.erc files in the cwd)
    out = tmp_path / "out"
    r = run([sys.executable, ROOT / "hardware" / "schematic" / "orbit_watch.py", "--out", out],
            cwd=tmp_path)
    assert r.returncode == 0, r.stdout[-2000:]
    net = (out / "orbit_watch.net").read_text()
    for token in ("nRF54L15-QFAA", "nPM1300-QEAA", "BMI270", "MAX30101", "LS013B7DH05_FPC",
                  "I2C_SDA", "VDD_3V0", "VDD_1V8", "IMU_INT1"):
        assert token in net
    checks = (out / "design_checks.txt").read_text()
    assert checks.startswith("orbit-watch rev A design checks: 0 failures")
    assert "FAIL" not in checks
    # the committed outputs are reproducible and up to date
    committed = ROOT / "hardware" / "out"
    for f in ("orbit_watch.net", "bom.csv", "design_checks.txt", "erc.txt", "pinout.md"):
        assert (out / f).read_bytes() == (committed / f).read_bytes(), f"{f} is stale"


def test_power_budget_ordering():
    pb = ROOT / "results" / "power_budget.json"
    if not pb.exists():
        pytest.skip("results/ not generated")
    r = run([sys.executable, ROOT / "tools" / "power_budget.py"])
    assert r.returncode == 0
    prof = json.loads(pb.read_text())["profiles"]
    assert prof["lowpower"]["total_ua"] < prof["active"]["total_ua"] < prof["fullrefresh"]["total_ua"]


def qemu_exe():
    return os.environ.get("QEMU_SYSTEM_ARM") or shutil.which("qemu-system-arm")


@pytest.mark.skipif(not qemu_exe(), reason="qemu-system-arm not installed (runs in CI)")
def test_qemu_smoke(firmware, tmp_path):
    import qemu_run
    r = qemu_run.run(qemu_run.PROFILES["smoke"], tmp_path, firmware["an505"], 60)
    assert r["exit_code"] == 0
    log = (tmp_path / "uart.log").read_text()
    assert "orbit-watch 0.1.0 on qemu-mps2-an505" in log
    assert "protocol errors 0" in log
    st = r["stats"]
    assert st["frames"] > 0 and st["display_bytes"] > 0
    assert st["hr_locked"] >= 1 and 55 <= st["hr_bpm"] <= 70  # script: 62 bpm at rest
    pbm = (tmp_path / "smoke.pbm").read_bytes()
    assert pbm.startswith(b"P4\n144 168\n")


@pytest.mark.skipif(not qemu_exe(), reason="qemu-system-arm not installed (runs in CI)")
def test_qemu_tickless_lowpower(firmware, tmp_path):
    import qemu_run
    # Measure over a window that starts after the configuration commands: guest
    # time keeps running (seconds face on) while the harness types them.
    cmds = ["face seconds off", "imu batch 10", "stats", "run +900", "stats", "poweroff"]
    r = qemu_run.run(cmds, tmp_path, firmware["an505"], 120)
    assert r["exit_code"] == 0
    a, b = r["all_stats"][-2], r["all_stats"][-1]
    up = b["uptime_s"] - a["uptime_s"]
    assert 899 <= up <= 960  # plus the guest time that passes while "run +900" is typed
    # one wake per IMU batch, per minute and per PPG second, not 100 per second
    assert b["wakeups"] - a["wakeups"] < up / 3
    deep = (b["lp_sleep"][2] - a["lp_sleep"][2]) / (b["lp_total"] - a["lp_total"])
    assert deep > 0.9
    assert abs(b["steps"] - b["true_steps"]) <= max(10, b["true_steps"] * 0.03)
