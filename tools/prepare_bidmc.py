#!/usr/bin/env python3
"""Make the small PPG test excerpts in data/bidmc/ from the BIDMC dataset.

Source: BIDMC PPG and Respiration Dataset v1.0.0, PhysioNet
(https://physionet.org/content/bidmc/1.0.0/), Open Data Commons Attribution
License v1.0. Pimentel M.A.F. et al., "Toward a robust estimation of
respiratory rate from pulse oximeters", IEEE TBME 64(8), 2017; Goldberger
A.L. et al., PhysioBank, PhysioToolkit and PhysioNet, Circulation 101(23), 2000.

For each record the first SECONDS of the fingertip PLETH channel (125 Hz)
are averaged in blocks of 5 samples -> 25 Hz (what the MAX30101's FIFO
averaging does), scaled to ADC-like counts, and paired with the monitor's
ECG-derived heart rate (Numerics "HR", 1 Hz, held between samples) as the
reference. PLETH rises with blood volume, while a reflective PPG ADC reading
falls, so the excerpt stores counts = 200000 - 100000 * PLETH.

    python tools/prepare_bidmc.py <dir with bidmc_NN_Signals.csv + _Numerics.csv>
"""
from __future__ import annotations

import csv
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "data" / "bidmc"
SECONDS = 120
RECORDS = [f"{i:02d}" for i in range(1, 9)]


def main(src: Path) -> int:
    OUT.mkdir(parents=True, exist_ok=True)
    for rec in RECORDS:
        sig = list(csv.reader(open(src / f"bidmc_{rec}_Signals.csv")))
        num = list(csv.reader(open(src / f"bidmc_{rec}_Numerics.csv")))
        h = [c.strip() for c in sig[0]]
        ip = h.index("PLETH")
        hn = [c.strip() for c in num[0]]
        ihr = hn.index("HR")
        hr = {}
        for row in num[1:]:
            try:
                hr[int(float(row[0]))] = float(row[ihr])
            except (ValueError, IndexError):
                pass
        rows = sig[1:1 + SECONDS * 125]
        out = OUT / f"bidmc_{rec}_25hz.csv"
        with open(out, "w", newline="") as f:
            w = csv.writer(f, lineterminator="\n")
            w.writerow(["t_s", "ppg", "ref_hr"])
            for k in range(0, len(rows) - 4, 5):
                block = [float(r[ip]) for r in rows[k:k + 5]]
                t = k / 125.0
                ref = hr.get(int(t), hr.get(int(t) - 1, 0.0))
                w.writerow([f"{t:.2f}", int(round(200000 - 100000 * sum(block) / 5)), f"{ref:g}"])
        print("wrote", out)
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    sys.exit(main(Path(sys.argv[1])))
