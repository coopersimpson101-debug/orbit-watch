#!/usr/bin/env python3
"""orbit-watch rev A schematic, written as code with SKiDL.

    python hardware/schematic/orbit_watch.py            # -> hardware/out/
        orbit_watch.net    KiCad netlist (import into Pcbnew)
        bom.csv            grouped bill of materials
        erc.txt            SKiDL electrical rules check
        design_checks.txt  project-specific checks (see checks.py)
        pinout.md          MCU pin assignment table

Block diagram:

  USB-C VBUS --ESD--> nPM1300 VBUS        LiPo --> VBAT (NTC on board)
  nPM1300 VSYS --> PVDD (bucks) and LSIN1
     BUCK1 2.2 uH -> VDD_3V0  nRF54L15, LCD, BMI270, I2C pull-ups, motor
     BUCK2 2.2 uH -> VDD_1V8  MAX30101 VDD (can be switched off between readings)
     LSOUT1       -> VLED     MAX30101 LED anode supply (VSYS, switched)
  nRF54L15: SPIM00 -> LS013B7DH05, TWIM21 -> BMI270 + MAX30101 + nPM1300,
            GRTC PWM -> LCD EXTCOMIN, GPIO -> buttons, motor FET, IRQ lines
"""
from __future__ import annotations

import csv
import os
import sys
from collections import OrderedDict
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
for _v in ("KICAD_SYMBOL_DIR", "KICAD6_SYMBOL_DIR", "KICAD7_SYMBOL_DIR", "KICAD8_SYMBOL_DIR",
           "KICAD9_SYMBOL_DIR", "KICAD10_SYMBOL_DIR"):
    os.environ.setdefault(_v, str(HERE))  # no symbol libraries needed: all parts are local

import builtins  # noqa: E402

from skidl import ERC, KICAD9, POWER, Net, Pin, generate_netlist, set_default_tool  # noqa: E402

import checks  # noqa: E402
from parts import (ANTENNA, BATT_CONN, BMI270, BUTTON, C0201, C0402, C0603, DIODE, ESD,  # noqa: E402
                   L0201, L0603, L0805, LCD_FPC, LED, MAX30101, MOTOR_CONN, NMOS, NPM1300,
                   NRF54L15, NTC, R0402, TAGCONNECT, TESTPAD, USB_C, XTAL32K, XTAL32M)

OUT = HERE.parent / "out"
set_default_tool(KICAD9)


def nc():
    """SKiDL's no-connect net (explicitly unused pin)."""
    return builtins.default_circuit.NC


CAP_MPN = {  # Murata GRM155 (0402) unless the call site overrides it
    "100nF": "GRM155R71C104KA88D", "10nF": "GRM155R71H103KA88D",
    "1uF": "GRM155R61A105KE15D", "2.2uF": "GRM155R61A225KE95D",
}
RES_MPN = {"4.7k": "RC0402FR-074K7L", "100k": "RC0402FR-07100KL", "100": "RC0402FR-07100RL"}


def cap(value, a, b, tmpl=C0402, mpn=None, note=""):
    c = tmpl(value=value)
    mpn = mpn or (CAP_MPN.get(value) if tmpl is C0402 else None)
    if mpn:
        c.fields["MPN"] = mpn
    if note:
        c.fields["Note"] = note
    c[1] += a
    c[2] += b
    return c


def res(value, a, b, mpn=None, note=""):
    r = R0402(value=value)
    mpn = mpn or RES_MPN.get(value)
    if mpn:
        r.fields["MPN"] = mpn
    if note:
        r.fields["Note"] = note
    r[1] += a
    r[2] += b
    return r


def build():
    gnd, vbus, vbat, vsys = Net("GND"), Net("VBUS"), Net("VBAT"), Net("VSYS")
    v3, v18, vled = Net("VDD_3V0"), Net("VDD_1V8"), Net("VLED")
    sda, scl = Net("I2C_SDA"), Net("I2C_SCL")
    for n in (gnd, vbus, vbat, vsys, v3, v18, vled):
        n.drive = POWER  # rails are driven by the PMIC / connectors

    # ------------------------------------------------------------ power
    pmic = NPM1300(value="nPM1300")
    usb = USB_C(value="USB-C")
    usb["GND"] += gnd
    usb["SHIELD"] += gnd
    usb["VBUS"] += vbus
    usb["CC1"] += pmic["CC1"]
    usb["CC2"] += pmic["CC2"]
    for p in ("DP1", "DP2", "DN1", "DN2", "SBU1", "SBU2"):
        usb[p] += nc()  # no USB data on rev A (nRF54L15 has no USB)
    esd = ESD(value="TPD1E10B06")
    esd["IO"] += vbus
    esd["GND"] += gnd
    pmic["VBUS"] += vbus
    cap("4.7uF", vbus, gnd, C0603, "GRM188R61E475KE11D", "VBUS input (rated 25 V)")
    pmic["VBUSOUT"] += nc()

    batt = BATT_CONN(value="LiPo 3.7V")
    batt["BAT+"] += vbat
    batt["BAT-"] += gnd
    batt["MP"] += gnd
    pmic["VBAT"] += vbat
    cap("10uF", vbat, gnd, C0603)
    ntc = NTC(value="10k NTC")
    ntc[1] += pmic["NTC"]
    ntc[2] += gnd

    pmic["VSYS"] += vsys
    pmic["PVDD"] += vsys
    cap("10uF", vsys, gnd, C0603)
    cap("10uF", vsys, gnd, C0603, note="PVDD, place at pin 4")

    sw1, sw2 = Net("BUCK1_SW"), Net("BUCK2_SW")
    pmic["SW1"] += sw1
    pmic["SW2"] += sw2
    l1 = L0805(value="2.2uH")
    l1[1] += sw1
    l1[2] += v3
    l2 = L0805(value="2.2uH")
    l2[1] += sw2
    l2[2] += v18
    pmic["VOUT1"] += v3
    pmic["VOUT2"] += v18
    pmic["PVSS1"] += gnd
    pmic["PVSS2"] += gnd
    pmic["AVSS"] += gnd
    cap("10uF", v3, gnd, C0603, note="BUCK1 output")
    cap("10uF", v18, gnd, C0603, note="BUCK2 output")
    res("TBD", pmic["VSET1"], gnd, "RC0402FR-07 (value TBD)",
        "BUCK1 = 3.0 V: value from the nPM1300 PS VSET table")
    res("TBD", pmic["VSET2"], gnd, "RC0402FR-07 (value TBD)",
        "BUCK2 = 1.8 V: value from the nPM1300 PS VSET table")
    pmic["LSIN1/VINLDO1"] += vsys
    pmic["LSOUT1/VOUTLDO1"] += vled
    pmic["LSIN2/VINLDO2"] += nc()
    pmic["LSOUT2/VOUTLDO2"] += nc()
    pmic["VDDIO"] += v3
    cap("100nF", v3, gnd, note="nPM1300 VDDIO")
    pmic["SDA"] += sda
    pmic["SCL"] += scl
    pmic_int = Net("PMIC_INT")
    pmic["GPIO0"] += pmic_int
    for g in ("GPIO1", "GPIO2", "GPIO3", "GPIO4"):
        pmic[g] += nc()
    led = LED(value="red")
    led["A"] += vsys
    led["K"] += Net("CHG_LED")
    pmic["LED0"] += led["K"]
    pmic["LED1"] += nc()
    pmic["LED2"] += nc()
    shphld = Net("SHPHLD")
    pmic["SHPHLD"] += shphld
    swp = BUTTON(value="PWR")
    swp[1] += shphld
    swp[2] += gnd

    # ------------------------------------------------------------- MCU
    mcu = NRF54L15(value="nRF54L15")
    for p in mcu.get_pins("VDD"):
        p += v3
    mcu["VSS"] += gnd
    mcu["VSS_PA"] += gnd
    mcu["EP_VSS"] += gnd
    cap("2.2uF", v3, gnd, note="nRF54L15 VDD bulk (ref. config 1 C1)")
    cap("2.2uF", v3, gnd, note="nRF54L15 VDD bulk (ref. config 1 C2)")
    for i in range(3):
        cap("100nF", v3, gnd, note="nRF54L15 VDD pins 10/22/36, one per pin")
    deca, decd, dcc = Net("DECA"), Net("DECD"), Net("DCC")
    mcu["DECA"] += deca
    mcu["DECRF"] += deca  # datasheet: DECRF must connect to DECA
    mcu["DECD"] += decd
    mcu["DCC"] += dcc
    ldc = L0603(value="4.7uH")
    ldc[1] += dcc
    ldc[2] += deca
    cap("100nF", deca, gnd, note="DECA (verify vs. reference circuitry config 1)")
    cap("100nF", decd, gnd, note="DECD")
    cap("10nF", deca, gnd, note="DECRF, place at pin 33")
    y1 = XTAL32M(value="32MHz")
    y1["X1"] += Net("XC1")
    y1["X2"] += Net("XC2")
    mcu["XC1"] += y1["X1"]
    mcu["XC2"] += y1["X2"]
    for p in y1.get_pins("GND"):
        p += gnd
    y2 = XTAL32K(value="32.768kHz")
    y2[1] += Net("XL1")
    y2[2] += Net("XL2")
    mcu["P1.00/XL1"] += y2[1]
    mcu["P1.01/XL2"] += y2[2]
    # RF: pi match (values from Nordic's config-1 BOM, re-tune on the real layout)
    ant_in, ant_feed = Net("RF_ANT"), Net("RF_FEED")
    mcu["ANT"] += ant_in
    cap("DNP", ant_in, gnd, C0201, "GJM0335C1E1R5WB01", "pi-match shunt 1 (do not populate)")
    lm = L0201(value="2.7nH")
    lm[1] += ant_in
    lm[2] += ant_feed
    cap("1.5pF", ant_feed, gnd, C0201, "GJM0335C1E1R5WB01", "pi-match shunt 2")
    ae = ANTENNA(value="2450AT18A100")
    ae["FEED"] += ant_feed
    ae["NC"] += nc()  # mechanical pad, no copper connection
    # SWD
    swd = TAGCONNECT(value="TC2030")
    swd["VTREF"] += v3
    swd["GND"] += gnd
    for sp, mp, name in (("SWDIO", "SWDIO", "SWDIO"), ("SWCLK", "SWDCLK", "SWDCLK"),
                         ("nRESET", "nRESET", "nRESET"), ("SWO", "P2.07/SWO", "SWO")):
        n = Net(name)
        swd[sp] += n
        mcu[mp] += n

    # --------------------------------------------------------- display
    lcd = LCD_FPC(value="LS013B7DH05")
    for lp, mp in (("SCLK", "P2.01"), ("SI", "P2.02"), ("SCS", "P2.05"), ("DISP", "P2.00"),
                   ("EXTCOMIN", "P0.03/GRTC_PWM")):
        n = Net(f"LCD_{lp}")
        lcd[lp] += n
        mcu[mp] += n
    lcd["EXTMODE"] += v3  # H: VCOM from EXTCOMIN (hardware toggled)
    lcd["VDD"] += v3
    lcd["VDDA"] += v3
    lcd["VSS"] += gnd
    lcd["VSSA"] += gnd
    lcd["MP"] += gnd
    cap("1uF", v3, gnd, note="LCD VDD (datasheet remark 6-5)")
    cap("1uF", v3, gnd, note="LCD VDDA")
    res("100k", mcu["P2.05"], gnd, note="SCS pull-down: panel deselected during reset")

    # ------------------------------------------------------------- I2C
    mcu["P1.11/AIN4"] += scl
    mcu["P1.12/AIN5"] += sda
    res("4.7k", scl, v3, note="I2C pull-up")
    res("4.7k", sda, v3, note="I2C pull-up")

    imu = BMI270(value="BMI270")
    imu["VDD"] += v3
    imu["VDDIO"] += v3
    imu["GND"] += gnd
    imu["GNDIO"] += gnd
    imu["SDO"] += gnd  # I2C address 0x68
    imu["CSB"] += v3   # I2C mode
    imu["SCx"] += scl
    imu["SDx"] += sda
    imu["ASDx"] += v3  # unused aux interface: VDDIO (never GND, table 22 note **)
    imu["ASCx"] += v3
    imu["OCSB"] += nc()
    imu["OSDO"] += nc()
    imu["INT2"] += nc()
    imu_int = Net("IMU_INT1")
    imu["INT1"] += imu_int
    mcu["P1.13/AIN6"] += imu_int
    cap("100nF", v3, gnd, note="BMI270 VDD")
    cap("100nF", v3, gnd, note="BMI270 VDDIO")

    ppg = MAX30101(value="MAX30101")
    ppg["VDD"] += v18
    ppg["GND"] += gnd
    ppg["PGND"] += gnd
    for p in ppg.get_pins("VLED+"):
        p += vled
    ppg["SCL"] += scl
    ppg["SDA"] += sda
    ppg_int = Net("PPG_INT")
    ppg["INT"] += ppg_int
    mcu["P1.14/AIN7"] += ppg_int
    res("4.7k", ppg_int, v3, note="open-drain INT pull-up (pin rated to 6 V)")
    for p in ("R_DRV", "IR_DRV", "G_DRV"):
        ppg[p] += nc()
    for p in ("NC1", "NC8", "NC14"):
        ppg[p] += nc()
    cap("100nF", v18, gnd, note="MAX30101 VDD")
    cap("10uF", vled, gnd, C0603, note="MAX30101 VLED+ bulk")
    cap("100nF", vled, gnd, note="MAX30101 VLED+")

    mcu["P1.10"] += pmic_int
    for pin in ("P1.10", "P1.13/AIN6", "P1.14/AIN7", "P1.04/AIN0", "P1.05/AIN1", "P1.06/AIN2"):
        mcu[pin].func = Pin.types.INPUT  # configured as (interrupt) inputs in firmware

    # ------------------------------------------------- buttons, motor
    for i, pin in enumerate(("P1.04/AIN0", "P1.05/AIN1", "P1.06/AIN2")):
        b = BUTTON(value=f"BTN{i + 1}")
        n = Net(f"BTN{i + 1}")
        b[1] += n
        b[2] += gnd
        mcu[pin] += n
    gate, mneg = Net("MOTOR_GATE"), Net("MOTOR_N")
    mpwm = Net("MOTOR_PWM")
    mcu["P1.07/AIN3"] += mpwm
    res("100", mpwm, gate, note="gate resistor")
    res("100k", gate, gnd, note="gate pull-down: motor off in reset")
    q = NMOS(value="AO3400A")
    q["G"] += gate
    q["S"] += gnd
    q["D"] += mneg
    m = MOTOR_CONN(value="ERM 308-100")
    m["M+"] += v3
    m["M-"] += mneg
    m["MP"] += gnd
    d = DIODE(value="1N4148WS")
    d["K"] += v3
    d["A"] += mneg

    # ------------------------------------------------ debug UART + pads
    for pin, name in (("P0.00", "UART_TX"), ("P0.01", "UART_RX")):
        n = Net(name)
        mcu[pin] += n
        tp = TESTPAD(value=name)
        tp[1] += n
    for net in (vbat, vsys, v3, v18, gnd):
        tp = TESTPAD(value=net.name)
        tp[1] += net
    # unused MCU pins (explicit so the checker can tell "unused" from "forgotten")
    for pin in ("P1.02/NFC1", "P1.03/NFC2", "P1.08", "P1.09", "P2.03", "P2.04", "P2.06",
                "P2.08", "P2.09", "P2.10", "P0.02", "P0.04/CLKOUT32K"):
        mcu[pin] += nc()
    return {"mcu": mcu, "pmic": pmic, "imu": imu, "ppg": ppg, "lcd": lcd}


def write_bom(path: Path, circuit_parts) -> int:
    groups: "OrderedDict[tuple, list]" = OrderedDict()
    for p in sorted(circuit_parts, key=lambda p: (p.ref_prefix, int("".join(c for c in p.ref if c.isdigit()) or 0))):
        key = (p.name, str(p.value), p.footprint, p.fields.get("MPN", ""))
        groups.setdefault(key, []).append(p)
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f, lineterminator="\n")
        w.writerow(["Refs", "Qty", "Value", "Part", "MPN", "Manufacturer", "Footprint",
                    "Datasheet", "Pinmap", "Note"])
        for (name, value, fp, mpn), ps in groups.items():
            p0 = ps[0]
            notes = sorted({p.fields.get("Note", "") for p in ps} - {""})
            w.writerow([" ".join(p.ref for p in ps), len(ps), value, name, mpn,
                        p0.fields.get("Manufacturer", ""), fp, p0.fields.get("Datasheet", ""),
                        p0.fields.get("Pinmap", ""), "; ".join(notes)])
    return len(groups)


def main() -> int:
    import builtins
    default_circuit = builtins.default_circuit
    OUT.mkdir(parents=True, exist_ok=True)
    key = build()
    erc_log = OUT / "erc.txt"
    import io
    import logging
    buf = io.StringIO()
    handler = logging.StreamHandler(buf)
    lg = logging.getLogger("ERC_Logger")
    lg.addHandler(handler)
    ERC()
    lg.removeHandler(handler)
    erc_text = buf.getvalue()
    erc_lines = [ln for ln in erc_text.splitlines()
                 if ln.strip() and "No errors or warnings found" not in ln]
    erc_log.write_text("SKiDL ERC: %d messages\n" % len(erc_lines) + "\n".join(erc_lines) + "\n",
                       newline="\n")
    generate_netlist(file_=str(OUT / "orbit_watch.net"))
    parts = list(default_circuit.parts)
    ngroups = write_bom(OUT / "bom.csv", parts)
    report, failures = checks.run(default_circuit, key)
    (OUT / "design_checks.txt").write_text(report, newline="\n")
    (OUT / "pinout.md").write_text(checks.pinout_table(key["mcu"]), newline="\n")
    nets = [n for n in default_circuit.nets if n.pins]
    print(f"parts {len(parts)}, BOM lines {ngroups}, nets {len(nets)}")
    print(f"ERC: {len(erc_lines)} messages -> {erc_log}")
    print(f"design checks: {failures} failures -> {OUT / 'design_checks.txt'}")
    return 1 if failures or erc_lines else 0


if __name__ == "__main__":
    sys.exit(main())
