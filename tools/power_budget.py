#!/usr/bin/env python3
"""Battery-life model for orbit-watch rev A: datasheet currents x duty cycles.

Inputs
  hardware/power/datasheet_values.csv   every current/energy number, each row
                                        tagged datasheet / config / assumption
  results/qemu_<profile>.json           duty cycles measured on the emulated
                                        firmware (QEMU mps2-an505, -icount):
                                        wake-ups, instructions executed,
                                        display bytes, sensor samples
Output
  results/power_budget.json, results/power_budget.csv

THIS IS A MODEL, NOT A MEASUREMENT. The instruction counts are real counts
for this firmware binary, but CPI, per-wake overhead, DMA current, battery
capacity and buck efficiency are assumptions (see the CSV), and the display
and sensor figures are datasheet typicals. Battery life measured on the real
board goes in docs/hardware-validation.md.

    python tools/power_budget.py
"""
from __future__ import annotations

import csv
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DS = ROOT / "hardware" / "power" / "datasheet_values.csv"
RES = ROOT / "results"

PROFILES = {
    "lowpower": "minute face, 10 s IMU batches",
    "active": "seconds face, 1 s IMU batches",
    "fullrefresh": "seconds face, full-frame refresh",
}


def load_ds() -> dict:
    out = {}
    with open(DS, newline="") as f:
        for row in csv.DictReader(f):
            out[row["key"]] = row
    return out


def val(ds, key, unit_scale=1.0) -> float:
    return float(ds[key]["value"]) * unit_scale


def model(ds: dict, st: dict, icount_shift: int = 0) -> dict:
    up = st["uptime_s"]
    hz = st["cycles_hz"]
    # -icount shift=N: one instruction per 2**N ns of virtual time
    insn_per_tick = 1e9 / hz / (1 << icount_shift)
    awake = sum(st["threads"].values()) - st["emu_overhead_cycles"]
    insn_per_s = awake * insn_per_tick / up
    cpu_ma = val(ds, "nrf_cpu")
    f_cpu = 128e6
    cpu_active_s = insn_per_s * val(ds, "nrf_cpi") / f_cpu           # seconds per second
    wake_s = st["wakeups"] / up * val(ds, "nrf_wake_us") * 1e-6
    hr_duty = st["ppg_samples"] / 25.0 / up
    spi_s = st["display_bytes"] / up * 8 / val(ds, "spi_hz")
    i2c_bytes = val(ds, "imu_bytes_per_s") + val(ds, "ppg_bytes_per_s") * hr_duty
    i2c_s = i2c_bytes * 9 / val(ds, "i2c_hz")
    dma_s = spi_s + i2c_s

    # currents in uA on each rail
    v3 = {
        "nRF54L15 sleep (System ON idle, 256 KB RAM)": val(ds, "nrf_idle_256k"),
        "nRF54L15 CPU (instructions x CPI)": cpu_ma * 1000 * cpu_active_s,
        "nRF54L15 wake-up overhead": cpu_ma * 1000 * wake_s,
        "SPI/I2C EasyDMA (display + sensors)": val(ds, "nrf_dma_ma") * 1000 * dma_s,
        "BMI270 accel low-power": val(ds, "imu_lp"),
        "LS013B7DH05 display": val(ds, "lcd_static") / 3.0,  # uW / 3.0 V
    }
    v18 = {
        "MAX30101 analog (sessions)": val(ds, "ppg_idd") * hr_duty + val(ds, "ppg_shdn") * (1 - hr_duty),
    }
    led = val(ds, "ppg_led_ma") * 1000 * val(ds, "ppg_pw_us") * 1e-6 * val(ds, "ppg_sps")
    vbat = {
        "MAX30101 green LED (from VSYS)": led * hr_duty,
        "nPM1300 quiescent": val(ds, "pmic_iq"),
    }
    vb, eff = val(ds, "v_batt"), val(ds, "buck_eff")
    items = []
    for name, ua in v3.items():
        items.append((name, ua * 3.0 / (vb * eff)))
    for name, ua in v18.items():
        items.append((name, ua * 1.8 / (vb * eff)))
    items += list(vbat.items())
    total = sum(u for _, u in items)
    cap = val(ds, "battery_mah") * val(ds, "battery_usable")
    return {
        "inputs": {
            "uptime_s": up, "wakeups_per_s": st["wakeups"] / up,
            "instructions_per_s": insn_per_s, "cpu_active_fraction": cpu_active_s,
            "display_bytes_per_s": st["display_bytes"] / up, "hr_duty": hr_duty,
            "dma_fraction": dma_s,
        },
        "battery_ua": {n: round(u, 3) for n, u in items},
        "total_ua": round(total, 2),
        "battery_life_days": round(cap / (total / 1000.0) / 24.0, 1),
    }


def main() -> int:
    ds = load_ds()
    out = {"note": "modelled from datasheet typicals + assumptions + emulated duty cycles; "
                   "not a measurement", "profiles": {}}
    for prof, desc in PROFILES.items():
        p = RES / f"qemu_{prof}.json"
        if not p.exists():
            print(f"skip {prof}: {p} missing (python tools/collect_results.py)")
            continue
        run = json.loads(p.read_text())
        m = model(ds, run["stats"], run.get("icount_shift", 0))
        m["description"] = desc
        out["profiles"][prof] = m
        print(f"{prof:12s} {m['total_ua']:8.2f} uA  -> {m['battery_life_days']:6.1f} days  ({desc})")
    (RES / "power_budget.json").write_text(json.dumps(out, indent=2) + "\n", newline="\n")
    with open(RES / "power_budget.csv", "w", newline="") as f:
        w = csv.writer(f, lineterminator="\n")
        w.writerow(["profile", "item", "battery_current_uA"])
        for prof, m in out["profiles"].items():
            for item, ua in m["battery_ua"].items():
                w.writerow([prof, item, ua])
            w.writerow([prof, "TOTAL", m["total_ua"]])
    return 0


if __name__ == "__main__":
    sys.exit(main())
