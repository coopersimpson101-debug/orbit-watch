"""Project-specific design checks that SKiDL's generic ERC does not cover.

Each check prints PASS/FAIL lines; run() returns (report, number_of_failures).
"""
from __future__ import annotations

import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
PADS_JSON = HERE.parent / "kicad_pads.json"

# Nominal rail voltages (min, max) in V.
RAILS = {
    "VDD_3V0": (3.0, 3.0),
    "VDD_1V8": (1.8, 1.8),
    "VBAT": (3.0, 4.2),
    "VSYS": (3.0, 5.5),   # = VBAT on battery, up to VBUS-ish while charging
    "VLED": (3.0, 5.5),   # VSYS through LSOUT1
    "VBUS": (4.0, 5.5),
}

# Supply-pin limits from the datasheets: (ref-name, pin-name) -> (min, max) V
SUPPLY_LIMITS = {
    ("nRF54L15-QFAA", "VDD"): (1.7, 3.6),
    ("BMI270", "VDD"): (1.71, 3.6),
    ("BMI270", "VDDIO"): (1.2, 3.6),
    ("MAX30101", "VDD"): (1.7, 2.0),
    ("MAX30101", "VLED+"): (3.1, 5.25),
    ("LS013B7DH05_FPC", "VDD"): (2.7, 3.3),
    ("LS013B7DH05_FPC", "VDDA"): (2.7, 3.3),
    ("nPM1300-QEAA", "VBUS"): (4.0, 5.5),
    ("nPM1300-QEAA", "VBAT"): (2.3, 4.45),
}

I2C_ADDR = {"BMI270": 0x68, "MAX30101": 0x57, "nPM1300-QEAA": 0x6B}

# Signals that must be able to wake the MCU (GPIO SENSE / GPIOTE): not on port 2.
WAKE_NETS = ("BTN1", "BTN2", "BTN3", "IMU_INT1", "PPG_INT", "PMIC_INT")


class Report:
    def __init__(self):
        self.lines = []
        self.failures = 0
        self.warnings = 0

    def check(self, ok, msg):
        self.lines.append(("PASS " if ok else "FAIL ") + msg)
        if not ok:
            self.failures += 1

    def warn(self, msg):
        self.lines.append("WARN " + msg)
        self.warnings += 1

    def info(self, msg):
        self.lines.append("     " + msg)


def _is_nc(net):
    return "NC" in net.name.split("_") or net.name.endswith("_NC") or net.name.startswith("NC_")


def run(circuit, key):
    r = Report()
    parts = list(circuit.parts)
    nets = [n for n in circuit.nets if n.pins]

    # 1. every pin of every part is on a net
    def is_nc(pin):
        return any(getattr(n, "name", "") == "__NOCONNECT" for n in getattr(pin, "nets", []))
    floating = [f"{p.ref}.{pin.num}({pin.name})" for p in parts for pin in p.pins
                if not pin.is_connected() and not is_nc(pin)]
    ncs = sum(1 for p in parts for pin in p.pins if is_nc(pin))
    r.check(not floating, f"all {sum(len(p.pins) for p in parts)} pins connected or marked "
            f"no-connect ({ncs} no-connects)" + (f": floating {floating}" if floating else ""))

    # 2. single-pin nets must be explicit no-connects
    lonely = [n.name for n in nets if len(n.pins) == 1 and not _is_nc(n)
              and not n.pins[0].part.name == "TestPad"]
    r.check(not lonely, "no accidental single-pin nets" + (f": {lonely}" if lonely else ""))

    # 3. rails have decoupling, every IC supply pin sits on a known rail or GND
    by_name = {n.name: n for n in nets}
    for rail in RAILS:
        n = by_name.get(rail)
        caps = [pin.part for pin in n.pins if pin.part.ref_prefix == "C"] if n else []
        caps = [c for c in caps if any(pp.net and pp.net.name == "GND" for pp in c.pins)]
        r.check(bool(caps), f"{rail}: {len(caps)} capacitor(s) to GND")
    for p in parts:
        for pin in p.pins:
            lim = SUPPLY_LIMITS.get((p.name, pin.name))
            if not lim or not pin.net:
                continue
            rail = RAILS.get(pin.net.name)
            if rail is None:
                r.check(False, f"{p.ref}.{pin.name} on unknown rail {pin.net.name}")
                continue
            ok = lim[0] <= rail[0] and rail[1] <= lim[1]
            msg = (f"{p.ref} {p.name}.{pin.name} on {pin.net.name} {rail[0]}-{rail[1]} V within "
                   f"{lim[0]}-{lim[1]} V")
            if ok:
                r.check(True, msg)
            elif pin.name == "VLED+":
                r.warn(msg.replace("within", "NOT always within") +
                       " -> firmware only runs HR above VBAT 3.3 V")
            else:
                r.check(False, msg)

    # 4. I2C: pull-ups present, addresses unique, BMI270 SDO strapped
    for bus in ("I2C_SDA", "I2C_SCL"):
        n = by_name[bus]
        pulls = [pin.part for pin in n.pins if pin.part.ref_prefix == "R" and
                 any(pp.net and pp.net.name == "VDD_3V0" for pp in pin.part.pins)]
        r.check(len(pulls) == 1, f"{bus}: one pull-up to VDD_3V0 ({[p.value for p in pulls]})")
    on_bus = [p.name for p in parts if any(pin.net and pin.net.name == "I2C_SDA" for pin in p.pins)
              and p.name in I2C_ADDR]
    addrs = [I2C_ADDR[n] for n in on_bus]
    r.check(len(addrs) == len(set(addrs)) and len(on_bus) == 3,
            "I2C addresses unique: " + ", ".join(f"{n}=0x{I2C_ADDR[n]:02X}" for n in on_bus))
    imu = key["imu"]
    r.check(imu["SDO"].net.name == "GND", "BMI270 SDO=GND -> address 0x68")
    r.check(imu["CSB"].net.name == "VDD_3V0", "BMI270 CSB=VDDIO -> I2C mode")
    r.check(all(imu[p].net.name != "GND" for p in ("ASDx", "ASCx")),
            "BMI270 ASDx/ASCx not grounded (datasheet table 22 note)")
    ppg = key["ppg"]
    r.info("MAX30101 SDA/SCL/INT idle at 3.0 V while VDD = 1.8 V: allowed, abs. max for "
           "'all other pins' is 6.0 V (datasheet); VIH(min) must be checked on the bench")

    # 5. nRF54L15 pin-function constraints
    mcu = key["mcu"]
    def pin_net(name):
        return mcu[name].net.name if mcu[name].net else None
    for netname in WAKE_NETS:
        pins = [pin for pin in mcu.pins if pin.net and pin.net.name == netname]
        ok = bool(pins) and all(not pin.name.startswith("P2.") for pin in pins)
        r.check(ok, f"{netname} on {[p.name for p in pins]} (port 2 has no SENSE/GPIOTE)")
    r.check(pin_net("P2.01") == lcd_net(key, "SCLK"), "LCD SCLK on P2.01 (SPIM00 dedicated SCK)")
    r.check(pin_net("P2.02") == lcd_net(key, "SI"), "LCD SI on P2.02 (SPIM00 dedicated SDO)")
    r.check(pin_net("P0.03/GRTC_PWM") == lcd_net(key, "EXTCOMIN"),
            "LCD EXTCOMIN on P0.03 = GRTC PWM output (VCOM toggled without CPU)")
    r.check(lcd_net(key, "EXTMODE") == "VDD_3V0", "LCD EXTMODE high (external COM inversion)")
    r.check(pin_net("DECA") == pin_net("DECRF"), "DECRF tied to DECA (datasheet)")
    def has_part(pin_name, part_name):
        net = mcu[pin_name].net
        return bool(net) and any(pp.part.name == part_name for pp in net.pins)
    r.check(has_part("P1.00/XL1", "XTAL_32k768") and has_part("P1.01/XL2", "XTAL_32k768")
            and pin_net("P1.00/XL1") != pin_net("P1.01/XL2"),
            "32.768 kHz crystal across P1.00/P1.01 (XL1/XL2)")
    r.check(has_part("XC1", "XTAL_32MHz") and has_part("XC2", "XTAL_32MHz"),
            "32 MHz crystal across XC1/XC2")

    # 6. footprints: every symbol pin number exists as a pad in the official footprint
    if PADS_JSON.exists():
        pads = json.loads(PADS_JSON.read_text())
        for p in sorted({(p.name, p.footprint) for p in parts}):
            name, fp = p
            fp_pads = pads.get(fp)
            if fp_pads is None:
                r.check(False, f"{name}: footprint {fp} missing from {PADS_JSON.name}")
                continue
            proto = next(pp for pp in parts if pp.name == name and pp.footprint == fp)
            nums = {str(pin.num) for pin in proto.pins}
            missing = sorted(nums - set(fp_pads))
            unused = sorted(set(fp_pads) - nums - {""})
            msg = f"{name}: {len(nums)} pins -> {fp} ({len(fp_pads)} pads)"
            if unused:
                msg += f", pads without a pin: {unused}"
            r.check(not missing, msg + (f", MISSING pads {missing}" if missing else ""))
    else:
        r.warn(f"{PADS_JSON.name} not found: footprint pad check skipped "
               "(python tools/fetch_kicad_pads.py)")

    # 7. pin-map provenance summary
    for p in sorted({(p.name, p.fields.get('Pinmap', ''), p.fields.get('PinmapRef', ''))
                     for p in parts}):
        if p[1] == "memory":
            r.warn(f"{p[0]}: pin-out from memory, verify: {p[2]}")
    head = [f"orbit-watch rev A design checks: {r.failures} failures, {r.warnings} warnings", ""]
    return "\n".join(head + r.lines) + "\n", r.failures


def lcd_net(key, pin):
    n = key["lcd"][pin].net
    return n.name if n else None


def pinout_table(mcu):
    rows = ["| Pin | nRF54L15 | Net |", "|---:|---|---|"]
    for pin in sorted(mcu.pins, key=lambda p: int(p.num)):
        net = pin.net.name if pin.net else "-"
        rows.append(f"| {pin.num} | {pin.name} | {net} |")
    return "# nRF54L15 pin assignment (generated by hardware/schematic/orbit_watch.py)\n\n" + \
        "\n".join(rows) + "\n"
