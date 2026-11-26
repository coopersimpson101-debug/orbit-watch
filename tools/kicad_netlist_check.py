#!/usr/bin/env python3
"""Check the SKiDL netlist against KiCad's own footprint loader.

Runs inside the official kicad/kicad Docker image (CI job `kicad`), where
KiCad's `pcbnew` Python module and the official footprint libraries are
installed. For every component in hardware/out/orbit_watch.net it loads the
footprint with pcbnew.FootprintLoad() and checks that every pin the netlist
connects exists as a pad. Nothing is placed or routed; no board file is written.

    python3 tools/kicad_netlist_check.py [--fp-dir /usr/share/kicad/footprints]
"""
from __future__ import annotations

import argparse
import re
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
NET = ROOT / "hardware" / "out" / "orbit_watch.net"


def parse_netlist(text: str):
    comps = {}
    for m in re.finditer(r'\(comp\s+\(ref "([^"]+)"\).*?\(footprint "([^"]*)"\)', text, re.S):
        comps[m.group(1)] = m.group(2)
    pins = defaultdict(set)
    for m in re.finditer(r'\(node\s+\(ref "([^"]+)"\)\s+\(pin "([^"]+)"\)', text):
        pins[m.group(1)].add(m.group(2))
    return comps, pins


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--fp-dir", default="/usr/share/kicad/footprints")
    a = ap.parse_args()
    import pcbnew  # only available with KiCad installed
    comps, pins = parse_netlist(NET.read_text())
    bad = 0
    cache = {}
    for ref, fp in sorted(comps.items()):
        lib, name = fp.split(":", 1)
        if fp not in cache:
            mod = pcbnew.FootprintLoad(str(Path(a.fp_dir) / f"{lib}.pretty"), name)
            cache[fp] = None if mod is None else {p.GetNumber() for p in mod.Pads()}
        pads = cache[fp]
        if pads is None:
            print(f"FAIL {ref}: footprint {fp} not found")
            bad += 1
            continue
        missing = sorted(pins[ref] - pads)
        if missing:
            print(f"FAIL {ref} ({fp}): netlist pins without a pad: {missing}")
            bad += 1
        else:
            print(f"PASS {ref} ({fp}): {len(pins[ref])} connected pins on {len(pads)} pads")
    print(f"{len(comps)} components, {len(cache)} footprints, {bad} failures "
          f"(KiCad {pcbnew.GetBuildVersion()})")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
