# Contributing to orbit-watch

Thanks for your interest! Bug reports, fixes, layout work and hardware results are all welcome.

## Setup

```bash
pip install ziglang pytest pyelftools skidl
python build.py                     # firmware for both boards
python -m pytest -q tests           # host tests (+ QEMU tests if qemu-system-arm is installed)
python tools/qemu_run.py --profile smoke
python hardware/schematic/orbit_watch.py
```

## Ground rules

- Keep CI green: host tests, both firmware builds, the QEMU AN505 run and the schematic checks.
- Portable logic (time accounting, encoders, filters, detectors, queues) gets a host C unit test.
- Every number in the README or docs must come from code in this repository (say which machine
  and date), or be labelled a datasheet value, an assumption, or an empty slot.
- Hardware claims need hardware: say which board, firmware and instrument you used.
- Schematic changes: run `python hardware/schematic/orbit_watch.py` and commit the regenerated
  `hardware/out/` files. Pin numbers must come from a datasheet table; record the source in
  `parts.py`.
- One logical change per PR; follow the surrounding style (`.editorconfig`).
