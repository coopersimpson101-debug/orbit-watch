#!/usr/bin/env python3
"""Regenerate the README images in docs/assets/ from results/ (real outputs).

    python tools/collect_results.py      # builds, benchmarks, QEMU runs -> results/
    python docs/assets/make_figures.py   # -> docs/assets/*.svg, *.png

* watchface.png     the panel memory dumped by the emulated firmware after one
                    hour of running (QEMU mps2-an505 virtual panel, decoded
                    bit-by-bit from the SPI frames), scaled 3x, with the rows the
                    next seconds tick resends highlighted
* power_budget.svg  modelled battery current per component (datasheet values +
                    stated assumptions + emulated duty cycles; not measured)
* display_bytes.svg SPI bytes per minute: full / dirty-rows / partial refresh
* hr_trace.svg      band-passed synthetic PPG with the beats hr.c detected
* terminal.svg      real UART transcript of the emulated one-hour run
* banner.svg        title + the emulated watchface rendered as accent pixels
Needs matplotlib and Pillow. banner.png: node render_png.mjs banner.svg banner.png
"""
from __future__ import annotations

import csv
import json
import re
import subprocess
import sys
from pathlib import Path
from xml.sax.saxutils import escape

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
from PIL import Image, ImageDraw  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(__file__).resolve().parent
RES = ROOT / "results"
ACCENT = "#4ADE80"
BG, FG, MUTED, GRID = "#0d1117", "#e6edf3", "#8b949e", "#161b22"
MONO = "ui-monospace, SFMono-Regular, Menlo, Consolas, monospace"
W, H, STRIDE = 144, 168, 18
sys.path.insert(0, str(ROOT))
import build  # noqa: E402


def read_pbm(path: Path) -> list[list[int]]:
    data = path.read_bytes()
    m = re.match(rb"P4\s+(\d+)\s+(\d+)\s", data)
    w, h = int(m.group(1)), int(m.group(2))
    px = data[m.end():]
    return [[(px[y * STRIDE + x // 8] >> (7 - x % 8)) & 1 for x in range(w)] for y in range(h)]


def changed_rows(a, b):
    return [y for y in range(H) if a[y] != b[y]]


def host_frames():
    """Two consecutive seconds rendered on the host -> rows a seconds tick changes."""
    exe = build.host_exe("render_face")
    if not exe.exists():
        build.build_benches()
    frames = []
    for ss in (42, 43):
        p = RES / "frames" / f"_tmp_{ss}.pbm"
        subprocess.run([str(exe), str(p), "10", "9", str(ss), "6241", "72", "87", "1"], check=True)
        frames.append(read_pbm(p))
        p.unlink()
    return frames


def watchface_png(face, rows):
    s = 3
    paper, ink, mark = (201, 207, 197), (32, 36, 42), (74, 222, 128)
    img = Image.new("RGB", (W * s + 48, H * s + 48), (13, 17, 23))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([8, 8, W * s + 40, H * s + 40], radius=22, fill=(40, 44, 52))
    ox = oy = 24
    d.rectangle([ox, oy, ox + W * s - 1, oy + H * s - 1], fill=paper)
    for y in range(H):
        for x in range(W):
            if face[y][x]:
                d.rectangle([ox + x * s, oy + y * s, ox + x * s + s - 1, oy + y * s + s - 1], fill=ink)
    for y in rows:  # rows the next seconds tick resends
        d.rectangle([ox + W * s + 4, oy + y * s, ox + W * s + 9, oy + y * s + s - 1], fill=mark)
    img.save(OUT / "watchface.png", optimize=True)


def power_svg():
    pb = json.loads((RES / "power_budget.json").read_text())["profiles"]
    order = ["lowpower", "active", "fullrefresh"]
    labels = {"lowpower": "minute face,\n10 s IMU batch", "active": "seconds face,\n1 s IMU batch",
              "fullrefresh": "seconds face,\nfull refresh"}
    items = list(pb["active"]["battery_ua"].keys())
    colors = ["#2f81f7", "#4ADE80", "#a5d6a7", "#f2cc60", "#db6d28", "#8b949e", "#bc8cff",
              "#f778ba", "#56d4dd"]
    fig, ax = plt.subplots(figsize=(9.5, 5.0), dpi=100)
    fig.patch.set_facecolor(BG)
    ax.set_facecolor(BG)
    for i, prof in enumerate(order):
        left = 0
        for j, it in enumerate(items):
            v = pb[prof]["battery_ua"][it]
            ax.barh(i, v, left=left, color=colors[j % len(colors)], edgecolor=BG,
                    label=it if i == 0 else None)
            left += v
        ax.text(left + 1, i, f"{pb[prof]['total_ua']:.0f} uA  ->  {pb[prof]['battery_life_days']:.0f} days",
                va="center", color=FG, fontsize=9, family="monospace")
    ax.set_yticks(range(3), [labels[p] for p in order], color=FG, fontsize=9)
    ax.invert_yaxis()
    ax.set_xlim(0, max(pb[p]["total_ua"] for p in order) * 1.45)
    ax.set_xlabel("battery current (uA), 200 mAh cell, no BLE", color=MUTED, fontsize=9)
    ax.tick_params(colors=MUTED, labelsize=8)
    for sp in ax.spines.values():
        sp.set_color(GRID)
    ax.set_title("MODELLED power budget: datasheet typicals x emulated duty cycles (not measured)",
                 color=FG, fontsize=10, loc="left")
    ax.legend(fontsize=7.5, loc="upper center", bbox_to_anchor=(0.45, -0.2), frameon=False,
              labelcolor=FG, ncol=3)
    fig.tight_layout()
    fig.savefig(OUT / "power_budget.svg", facecolor=BG)
    plt.close(fig)


def display_svg():
    rows = json.loads((RES / "bench_display.json").read_text())
    fig, ax = plt.subplots(figsize=(7.5, 3.2), dpi=100)
    fig.patch.set_facecolor(BG)
    ax.set_facecolor(BG)
    strategies = ["full", "dirty", "partial"]
    cols = {"full": "#8b949e", "dirty": "#2f81f7", "partial": ACCENT}
    for k, sec in enumerate((1, 0)):
        for j, st in enumerate(strategies):
            r = next(r for r in rows if r["strategy"] == st and r["seconds"] == sec)
            x = k * 4 + j
            ax.bar(x, r["bytes_per_minute"], color=cols[st], label=st if k == 0 else None)
            ax.text(x, r["bytes_per_minute"] * 1.15, f"{r['bytes_per_minute']:,.0f}", ha="center",
                    color=FG, fontsize=8, family="monospace")
    ax.set_yscale("log")
    ax.set_ylim(300, 1e6)
    ax.set_xticks([1, 5], ["seconds shown (1 update/s)", "minute face (1 update/min)"], color=FG)
    ax.set_ylabel("SPI bytes / minute", color=MUTED)
    ax.tick_params(colors=MUTED, labelsize=8)
    for sp in ax.spines.values():
        sp.set_color(GRID)
    ax.legend(frameon=False, labelcolor=FG, fontsize=8)
    ax.set_title("LS013B7DH05 refresh traffic, host benchmark (bench_display)", color=FG,
                 fontsize=10, loc="left")
    fig.tight_layout()
    fig.savefig(OUT / "display_bytes.svg", facecolor=BG)
    plt.close(fig)


def hr_svg():
    t, f, beats = [], [], []
    with open(RES / "ppg_trace.csv") as fh:
        for row in csv.DictReader(fh):
            t.append(float(row["t_s"]))
            f.append(int(row["filtered"]))
            if row["beat"] == "1":
                beats.append((float(row["t_s"]) - 0.04, f[-2] if len(f) > 1 else f[-1]))
    fig, ax = plt.subplots(figsize=(7.5, 2.6), dpi=100)
    fig.patch.set_facecolor(BG)
    ax.set_facecolor(BG)
    ax.plot(t, f, color="#2f81f7", lw=1.2, label="band-passed PPG (0.5-4 Hz)")
    ax.scatter([b[0] for b in beats], [b[1] for b in beats], color=ACCENT, s=18, zorder=3,
               label="detected beats")
    ax.set_xlabel("time (s)", color=MUTED)
    ax.tick_params(colors=MUTED, labelsize=8)
    for sp in ax.spines.values():
        sp.set_color(GRID)
    ax.legend(frameon=False, labelcolor=FG, fontsize=8, loc="upper right")
    ax.set_title("hr.c on synthetic PPG (72 bpm, 25 Hz)", color=FG, fontsize=10, loc="left")
    fig.tight_layout()
    fig.savefig(OUT / "hr_trace.svg", facecolor=BG)
    plt.close(fig)


def terminal_svg():
    log = (ROOT / "build" / "qemu" / "active" / "uart.log").read_text(encoding="utf-8")
    log = log.replace("\r", "")
    lines = log.split("\n")
    keep = []
    for i, ln in enumerate(lines):
        if ln.startswith("orbit-watch 0.1.0 on") or ln.startswith("clocks:") or ln.startswith("mm:"):
            keep.append(ln)
    start = next(i for i, ln in enumerate(lines) if ln.startswith("watch> pm"))
    keep += lines[start:start + 7]
    s = next(i for i, ln in enumerate(lines) if ln.startswith("watch> ps") and i > start)
    keep += lines[s:s + 6]
    lh, pad = 18, 16
    width, height = 980, pad * 2 + 30 + lh * len(keep)
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
           f'viewBox="0 0 {width} {height}">',
           f'<rect width="{width}" height="{height}" rx="10" fill="{BG}" stroke="#30363d"/>',
           '<circle cx="20" cy="18" r="6" fill="#ff5f56"/><circle cx="40" cy="18" r="6" fill="#ffbd2e"/>'
           '<circle cx="60" cy="18" r="6" fill="#27c93f"/>',
           f'<text x="{width // 2}" y="22" text-anchor="middle" font-family="{MONO}" font-size="12" '
           f'fill="{MUTED}">qemu-system-arm -M mps2-an505 (emulated) - after `run 3600`</text>']
    y = 30 + pad + 4
    for ln in keep:
        color = ACCENT if ln.startswith("watch>") else FG
        out.append(f'<text x="{pad}" y="{y}" font-family="{MONO}" font-size="13" fill="{color}" '
                   f'xml:space="preserve">{escape(ln)}</text>')
        y += lh
    out.append("</svg>")
    (OUT / "terminal.svg").write_text("\n".join(out) + "\n", newline="\n")


def banner_svg(face, rows):
    out = ['<svg xmlns="http://www.w3.org/2000/svg" width="1280" height="400" viewBox="0 0 1280 400">',
           '<defs><pattern id="g" width="32" height="32" patternUnits="userSpaceOnUse">'
           f'<path d="M32 0H0V32" fill="none" stroke="{GRID}" stroke-width="1"/></pattern></defs>',
           f'<rect width="1280" height="400" fill="{BG}"/><rect width="1280" height="400" fill="url(#g)"/>',
           f'<rect x="72" y="118" width="56" height="6" rx="3" fill="{ACCENT}"/>',
           f'<text x="68" y="210" font-family="{MONO}" font-size="88" font-weight="700" '
           f'fill="{FG}">orbit-watch</text>',
           f'<text x="72" y="258" font-family="{MONO}" font-size="22" fill="{MUTED}">'
           'A smartwatch OS and board for the nRF54L15</text>',
           f'<text x="72" y="306" font-family="{MONO}" font-size="17" fill="{MUTED}">tickless idle '
           f'<tspan fill="{ACCENT}">&#183;</tspan> deep sleep <tspan fill="{ACCENT}">&#183;</tspan> '
           f'MIP partial refresh <tspan fill="{ACCENT}">&#183;</tspan> steps <tspan fill="{ACCENT}">'
           '&#183;</tspan> heart rate</text>']
    s, ox, oy = 2, 880, 32
    out.append(f'<rect x="{ox - 14}" y="{oy - 14}" width="{W * s + 28}" height="{H * s + 28}" rx="26" '
               f'fill="none" stroke="#30363d" stroke-width="2"/>')
    for y in range(H):
        x = 0
        while x < W:
            if face[y][x]:
                x0 = x
                while x < W and face[y][x]:
                    x += 1
                out.append(f'<rect x="{ox + x0 * s}" y="{oy + y * s}" width="{(x - x0) * s}" '
                           f'height="{s}" fill="{ACCENT}"/>')
            else:
                x += 1
    for y in rows:
        out.append(f'<rect x="{ox + W * s + 22}" y="{oy + y * s}" width="6" height="{s}" '
                   f'fill="{ACCENT}" opacity="0.55"/>')
    out.append("</svg>")
    (OUT / "banner.svg").write_text("\n".join(out) + "\n", newline="\n")


def main() -> int:
    face = read_pbm(RES / "frames" / "qemu_face_1h.pbm")
    a, b = host_frames()
    rows = changed_rows(a, b)
    watchface_png(face, rows)
    banner_svg(face, rows)
    power_svg()
    display_svg()
    hr_svg()
    terminal_svg()
    print("figures written to", OUT, f"(a seconds tick changes {len(rows)} rows)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
