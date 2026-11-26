#!/usr/bin/env python3
"""Record the pad names of every footprint the schematic uses.

Reads the footprint names from hardware/schematic/parts.py, then takes each
.kicad_mod either from a local KiCad install (--lib-dir, e.g.
/usr/share/kicad/footprints inside the kicad/kicad Docker image) or from the
official library repository (gitlab.com/kicad/libraries/kicad-footprints, tag KICAD_LIB_TAG),
and writes hardware/kicad_pads.json: {"Lib:Footprint": ["1", "2", ...]}.
Only pad names are stored, nothing else from the library.

    python tools/fetch_kicad_pads.py                    # download (needs network)
    python tools/fetch_kicad_pads.py --lib-dir /usr/share/kicad/footprints --check
"""
from __future__ import annotations

import argparse
import json
import re
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "hardware" / "kicad_pads.json"
# Pinned to the KiCad 9 library release that the kicad/kicad:9.0 image ships, because
# pad names change between major versions (e.g. the USB4105 shield pad is "S1" in 9.x
# and "SH" on master).
KICAD_LIB_TAG = "9.0.9"
RAW = ("https://gitlab.com/kicad/libraries/kicad-footprints/-/raw/" + KICAD_LIB_TAG +
       "/{lib}.pretty/{fp}.kicad_mod")


def footprints() -> list[str]:
    src = (ROOT / "hardware" / "schematic" / "parts.py").read_text()
    return sorted(set(re.findall(r'"([A-Za-z_-]+:[A-Za-z0-9_.\-]+)"', src)))


def pads_of(text: str) -> list[str]:
    return sorted(set(re.findall(r'\(pad\s+"([^"]*)"', text)) - {""},
                  key=lambda s: (len(s), s))


def load(fp: str, lib_dir: Path | None) -> str:
    lib, name = fp.split(":", 1)
    if lib_dir:
        return (lib_dir / f"{lib}.pretty" / f"{name}.kicad_mod").read_text(encoding="utf-8")
    with urllib.request.urlopen(RAW.format(lib=lib, fp=name), timeout=60) as r:
        return r.read().decode("utf-8")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--lib-dir", type=Path)
    ap.add_argument("--check", action="store_true",
                    help="compare against the committed JSON instead of writing it")
    a = ap.parse_args()
    result, missing = {}, []
    for fp in footprints():
        try:
            text = load(fp, a.lib_dir)
        except Exception as e:  # noqa: BLE001
            missing.append(f"{fp}: {e}")
            continue
        if "(footprint" not in text and "(module" not in text:
            missing.append(f"{fp}: not a footprint file")
            continue
        result[fp] = pads_of(text)
    for m in missing:
        print("MISSING", m)
    if a.check:
        old = json.loads(OUT.read_text())
        diff = {k for k in set(old) | set(result) if old.get(k) != result.get(k)}
        for k in sorted(diff):
            print("DIFF", k, old.get(k), "->", result.get(k))
        print(f"{len(result)} footprints checked against {OUT.name}: "
              f"{len(diff)} differences, {len(missing)} missing")
        return 1 if diff or missing else 0
    OUT.write_text(json.dumps(result, indent=1, sort_keys=True) + "\n", newline="\n")
    print(f"wrote {len(result)} footprints to {OUT}")
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
