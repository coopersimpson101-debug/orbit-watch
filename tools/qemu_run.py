#!/usr/bin/env python3
"""Boot orbit-watch on QEMU `-M mps2-an505` (Cortex-M33) and drive its shell.

The UART is connected over a local TCP socket, commands are typed one at a
time after each prompt, and the transcript is saved. QEMU runs with
`-icount shift=0,sleep=off`: guest time is derived from the instruction
count and jumps ahead whenever the guest sleeps in WFI, so an hour of
watch time takes seconds of host time, and the low-power counter, SysTick
and the wake-up timer all follow the same deterministic virtual clock.

Semihosting is enabled so the firmware can write the virtual panel image
(`snap <tag>` -> <outdir>/<tag>.pbm) and exit with a status code.

    python tools/qemu_run.py                         # default scripted session
    python tools/qemu_run.py --profile lowpower      # seconds off, 10 s IMU batching
    python tools/qemu_run.py --cmd ps --cmd "run 60" --cmd stats
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import socket
import struct
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ELF = ROOT / "build" / "an505" / "orbit-watch.elf"
# one guest instruction = 2**ICOUNT_SHIFT ns of virtual time (recorded in the
# results so tools/power_budget.py can turn timer counts into instructions)
ICOUNT_SHIFT = 0
LOOPBACK = socket.inet_ntoa(struct.pack("!I", socket.INADDR_LOOPBACK))  # IPv4 loopback

PROFILES = {
    # seconds on the face, IMU FIFO drained every second
    "active": ["info", "ps", "mem", "snap boot", "run 3600", "snap face_1h", "stats", "pm", "ps",
               "poweroff"],
    # seconds hidden, minute updates, 10 s IMU batches (the low-power target)
    "lowpower": ["face seconds off", "imu batch 10", "run 3600", "snap face_lowpower", "stats",
                 "pm", "ps", "poweroff"],
    # same face, but every update resends the whole frame (for comparison)
    "fullrefresh": ["face full on", "run 600", "stats", "poweroff"],
    "smoke": ["help", "info", "ps", "mem", "uptime", "face", "run 60", "stats", "pm", "snap smoke",
              "poweroff"],
}


def find_qemu() -> str:
    exe = os.environ.get("QEMU_SYSTEM_ARM") or shutil.which("qemu-system-arm")
    if not exe:
        raise SystemExit("qemu-system-arm not found (set QEMU_SYSTEM_ARM or add it to PATH)")
    return exe


def free_port() -> int:
    with socket.socket() as s:
        s.bind((LOOPBACK, 0))
        return s.getsockname()[1]


class Session:
    def __init__(self, sock: socket.socket, log):
        self.sock = sock
        self.buf = b""
        self.log = log

    def read_until(self, token: bytes, timeout: float) -> bytes:
        end = time.time() + timeout
        while token not in self.buf:
            left = end - time.time()
            if left <= 0:
                raise TimeoutError(f"timed out waiting for {token!r}")
            self.sock.settimeout(left)
            try:
                chunk = self.sock.recv(4096)
            except socket.timeout:
                continue
            if not chunk:
                raise EOFError("QEMU closed the serial port")
            self.log.write(chunk.decode(errors="replace"))
            self.log.flush()
            self.buf += chunk
        i = self.buf.index(token) + len(token)
        out, self.buf = self.buf[:i], self.buf[i:]
        return out

    def command(self, cmd: str, timeout: float) -> str:
        self.sock.sendall(cmd.encode() + b"\r")
        if cmd == "poweroff":
            return ""
        return self.read_until(b"watch> ", timeout).decode(errors="replace")


def run(cmds: list[str], outdir: Path, elf: Path, timeout: float) -> dict:
    outdir.mkdir(parents=True, exist_ok=True)
    port = free_port()
    qemu = [find_qemu(), "-M", "mps2-an505", "-display", "none", "-monitor", "none",
            "-serial", f"tcp:{LOOPBACK}:{port},server=on,wait=on",
            "-semihosting-config", "enable=on,target=native",
            "-icount", f"shift={ICOUNT_SHIFT},sleep=off", "-kernel", str(elf)]
    log_path = outdir / "uart.log"
    t0 = time.time()
    proc = subprocess.Popen(qemu, cwd=outdir, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    sock = None
    for _ in range(200):
        try:
            sock = socket.create_connection((LOOPBACK, port), timeout=1)
            break
        except OSError:
            time.sleep(0.05)
    if sock is None:
        proc.kill()
        raise SystemExit("could not connect to QEMU's serial port")
    result = {"commands": cmds, "outputs": {}, "qemu": qemu[0], "icount_shift": ICOUNT_SHIFT}
    with open(log_path, "w", encoding="utf-8", newline="\n") as log:
        s = Session(sock, log)
        s.read_until(b"watch> ", timeout)
        for c in cmds:
            result["outputs"][c] = s.command(c, timeout)
    try:
        rc = proc.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        proc.kill()
        rc = -1
    sock.close()
    result["exit_code"] = rc
    result["host_seconds"] = round(time.time() - t0, 2)
    text = log_path.read_text(encoding="utf-8")
    m = re.findall(r"STATS (\{.*\})", text)
    if m:
        result["stats"] = json.loads(m[-1])
        result["all_stats"] = [json.loads(x) for x in m]
    (outdir / "result.json").write_text(json.dumps(result, indent=2), newline="\n")
    return result


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--profile", default="smoke", choices=sorted(PROFILES))
    ap.add_argument("--cmd", action="append", help="custom command list (overrides --profile)")
    ap.add_argument("--elf", type=Path, default=ELF)
    ap.add_argument("--out", type=Path, default=None)
    ap.add_argument("--timeout", type=float, default=600)
    a = ap.parse_args()
    cmds = a.cmd or PROFILES[a.profile]
    out = a.out or ROOT / "build" / "qemu" / (a.profile if not a.cmd else "custom")
    r = run(cmds, out, a.elf, a.timeout)
    print(f"\n[qemu exit {r['exit_code']}, {r['host_seconds']} s host time, log {out / 'uart.log'}]")
    return 0 if r["exit_code"] == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
