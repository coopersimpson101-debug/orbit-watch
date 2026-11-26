"""Part templates for the orbit-watch rev A schematic (SKiDL).

None of the main ICs are in KiCad's official symbol library, so each one is
defined here from its datasheet pin table. `PINMAP` records where every
pin-out came from:

  "datasheet"   pin numbers/names typed from the manufacturer's table
                (document named in the part's `pinmap_ref`)
  "kicad-fp"    two-terminal / connector parts whose pad numbers follow the
                official KiCad footprint (checked by tools/check_footprints.py)
  "memory"      taken from the vendor datasheet from memory, NOT re-checked
                against the document here: verify before ordering boards

Footprints are names from the official KiCad 9 footprint library.
"""
from __future__ import annotations

from skidl import SKIDL, TEMPLATE, Part, Pin

P = Pin.types
PWR, PAS, IN, OUT, BI, OC, NC = (P.PWRIN, P.PASSIVE, P.INPUT, P.OUTPUT, P.BIDIR, P.OPENCOLL,
                                 P.NOCONNECT)
PWROUT = P.PWROUT


def template(name, ref, footprint, pins, *, mpn, mfr, datasheet, desc, pinmap, pinmap_ref=""):
    t = Part(tool=SKIDL, name=name, ref_prefix=ref, dest=TEMPLATE, footprint=footprint,
             pins=[Pin(num=str(n), name=nm, func=f) for n, nm, f in pins])
    t.fields["MPN"] = mpn
    t.fields["Manufacturer"] = mfr
    t.fields["Datasheet"] = datasheet
    t.fields["Description"] = desc
    t.fields["Pinmap"] = pinmap
    t.fields["PinmapRef"] = pinmap_ref
    return t


# --- nRF54L15-QFAA, QFN48 6x6 mm (pin table: nRF54L15 datasheet, "QFN48 (QFAA)
#     package pin assignments"; exposed pad = pin 49, VSS)
NRF54_PINS = [
    (1, "P1.00/XL1", BI), (2, "P1.01/XL2", BI), (3, "P1.02/NFC1", BI), (4, "P1.03/NFC2", BI),
    (5, "P1.04/AIN0", BI), (6, "P1.05/AIN1", BI), (7, "P1.06/AIN2", BI), (8, "P1.07/AIN3", BI),
    (9, "P1.08", BI), (10, "VDD", PWR), (11, "P2.00", BI), (12, "P2.01", BI), (13, "P2.02", BI),
    (14, "P2.03", BI), (15, "P2.04", BI), (16, "P2.05", BI), (17, "P2.06", BI),
    (18, "P2.07/SWO", BI), (19, "P2.08", BI), (20, "P2.09", BI), (21, "P2.10", BI),
    (22, "VDD", PWR), (23, "P0.00", BI), (24, "P0.01", BI), (25, "SWDIO", BI),
    (26, "SWDCLK", IN), (27, "P0.02", BI), (28, "P0.03/GRTC_PWM", BI),
    (29, "P0.04/CLKOUT32K", BI), (30, "nRESET", IN), (31, "ANT", PAS), (32, "VSS_PA", PWR),
    (33, "DECRF", PAS), (34, "XC1", PAS), (35, "XC2", PAS), (36, "VDD", PWR), (37, "P1.09", BI),
    (38, "P1.10", BI), (39, "P1.11/AIN4", BI), (40, "P1.12/AIN5", BI), (41, "P1.13/AIN6", BI),
    (42, "P1.14/AIN7", BI), (43, "DECA", PAS), (44, "VSS", PWR), (45, "DECD", PAS),
    (46, "DCC", PAS), (47, "VDD", PWR), (48, "VDD", PWR), (49, "EP_VSS", PWR),
]
NRF54L15 = template(
    "nRF54L15-QFAA", "U", "Package_DFN_QFN:QFN-48-1EP_6x6mm_P0.4mm_EP4.6x4.6mm", NRF54_PINS,
    mpn="NRF54L15-QFAA-R", mfr="Nordic Semiconductor",
    datasheet="https://docs.nordicsemi.com/bundle/ps_nrf54l15/page/chapters/pin.html",
    desc="Cortex-M33 128 MHz BLE SoC, 1.5 MB RRAM, 256 KB RAM, QFN48",
    pinmap="datasheet", pinmap_ref="nRF54L15 datasheet, QFN48 (QFAA) pin assignments "
                                   "(EP size 4.6 mm: verify against the package drawing)")

# --- nPM1300-QEAA, QFN32 5x5 mm (nPM1300 product specification, QFN32 pin assignments)
NPM1300_PINS = [
    (1, "VOUT1", PWR), (2, "PVSS1", PWR), (3, "SW1", PWROUT), (4, "PVDD", PWR), (5, "SW2", PWROUT),
    (6, "PVSS2", PWR), (7, "GPIO0", BI), (8, "GPIO1", BI), (9, "GPIO2", BI), (10, "GPIO3", BI),
    (11, "GPIO4", BI), (12, "VDDIO", PWR), (13, "SDA", BI), (14, "SCL", IN), (15, "SHPHLD", IN),
    (16, "VSET2", PAS), (17, "VSET1", PAS), (18, "NTC", PAS), (19, "VBAT", PWR),
    (20, "VSYS", PWROUT), (21, "VBUS", PWR), (22, "VBUSOUT", PWROUT), (23, "CC1", PAS),
    (24, "CC2", PAS), (25, "LED0", OC), (26, "LED1", OC), (27, "LED2", OC),
    (28, "LSIN1/VINLDO1", PWR), (29, "LSOUT1/VOUTLDO1", PWROUT), (30, "LSIN2/VINLDO2", PWR),
    (31, "LSOUT2/VOUTLDO2", PWROUT), (32, "VOUT2", PWR), (33, "AVSS", PWR),
]
NPM1300 = template(
    "nPM1300-QEAA", "U", "Package_DFN_QFN:QFN-32-1EP_5x5mm_P0.5mm_EP3.45x3.45mm", NPM1300_PINS,
    mpn="NPM1300-QEAA-R7", mfr="Nordic Semiconductor",
    datasheet="https://docs.nordicsemi.com/bundle/ps_npm1300/page/pin.html",
    desc="PMIC: 800 mA charger, 2x 200 mA buck, 2x load switch/LDO, fuel gauge, ship mode",
    pinmap="datasheet", pinmap_ref="nPM1300 PS, QFN32 pin assignments (EP size: verify)")

# --- Bosch BMI270, LGA-14 2.5x3.0 mm (BST-BMI270-DS000-08, table 22)
BMI270 = template(
    "BMI270", "U", "Package_LGA:Bosch_LGA-14_3x2.5mm_P0.5mm",
    [(1, "SDO", BI), (2, "ASDx", BI), (3, "ASCx", BI), (4, "INT1", OUT), (5, "VDDIO", PWR),
     (6, "GNDIO", PWR), (7, "GND", PWR), (8, "VDD", PWR), (9, "INT2", OUT), (10, "OCSB", IN),
     (11, "OSDO", OUT), (12, "CSB", IN), (13, "SCx", IN), (14, "SDx", BI)],
    mpn="BMI270", mfr="Bosch Sensortec",
    datasheet="https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/"
              "bst-bmi270-ds000.pdf",
    desc="6-axis IMU, accel low-power mode down to 4 uA, 2 KB FIFO, I2C/SPI",
    pinmap="datasheet", pinmap_ref="BST-BMI270-DS000-08, table 22")

# --- Maxim/ADI MAX30101EFD+, 14-pin OESIP 5.6x3.3 mm (datasheet 19-7742, pin description)
MAX30101 = template(
    "MAX30101", "U", "OptoDevice:Maxim_OLGA-14_3.3x5.6mm_P0.8mm",
    [(1, "NC1", NC), (2, "SCL", IN), (3, "SDA", BI), (4, "PGND", PWR), (5, "R_DRV", PAS),
     (6, "IR_DRV", PAS), (7, "G_DRV", PAS), (8, "NC8", NC), (9, "VLED+", PWR), (10, "VLED+", PWR),
     (11, "VDD", PWR), (12, "GND", PWR), (13, "INT", OC), (14, "NC14", NC)],
    mpn="MAX30101EFD+T", mfr="Analog Devices (Maxim)",
    datasheet="https://www.analog.com/media/en/technical-documentation/data-sheets/MAX30101.pdf",
    desc="PPG module: red/IR/green LEDs + photodiode, 18-bit ADC, I2C 0x57",
    pinmap="datasheet", pinmap_ref="MAX30101 datasheet, pin description (page 9)")

# --- Sharp LS013B7DH05 1.26" 144x168 memory LCD, 10-pin FPC tail -> Hirose FH12-10S-0.5SH
LCD_FPC = template(
    "LS013B7DH05_FPC", "J", "Connector_FFC-FPC:Hirose_FH12-10S-0.5SH_1x10-1MP_P0.50mm_Horizontal",
    [(1, "SCLK", IN), (2, "SI", IN), (3, "SCS", IN), (4, "EXTCOMIN", IN), (5, "DISP", IN),
     (6, "VDDA", PWR), (7, "VDD", PWR), (8, "EXTMODE", IN), (9, "VSS", PWR), (10, "VSSA", PWR),
     ("MP", "MP", PAS)],
    mpn="FH12-10S-0.5SH(55) + LS013B7DH05", mfr="Hirose / Sharp",
    datasheet="https://www.hirose.com/product/series/FH12",
    desc="FPC connector for the Sharp LS013B7DH05 memory-in-pixel LCD",
    pinmap="datasheet", pinmap_ref="Sharp LD-26912A table 4-1 (panel terminals 1-10); "
                                   "connector contact side vs. FPC orientation: verify")

USB_C = template(
    "USB_C_PowerOnly", "J", "Connector_USB:USB_C_Receptacle_GCT_USB4105-xx-A_16P_TopMnt_Horizontal",
    [("A1", "GND", PAS), ("B1", "GND", PAS), ("A12", "GND", PAS), ("B12", "GND", PAS),
     ("A4", "VBUS", PAS), ("B4", "VBUS", PAS), ("A9", "VBUS", PAS), ("B9", "VBUS", PAS),
     ("A5", "CC1", PAS), ("B5", "CC2", PAS), ("A6", "DP1", PAS), ("B6", "DP2", PAS),
     ("A7", "DN1", PAS), ("B7", "DN2", PAS), ("A8", "SBU1", PAS), ("B8", "SBU2", PAS),
     ("SH", "SHIELD", PAS)],
    mpn="USB4105-GF-A", mfr="GCT", datasheet="https://gct.co/connector/usb4105",
    desc="USB-C receptacle (charging only on rev A)", pinmap="kicad-fp")

TAGCONNECT = template(
    "SWD_TC2030", "J", "Connector:Tag-Connect_TC2030-IDC-NL_2x03_P1.27mm_Vertical",
    [(1, "VTREF", PAS), (2, "SWDIO", PAS), (3, "nRESET", PAS), (4, "SWCLK", PAS), (5, "GND", PAS),
     (6, "SWO", PAS)],
    mpn="TC2030-IDC-NL (cable, no part on board)", mfr="Tag-Connect",
    datasheet="https://www.tag-connect.com/product/tc2030-idc-nl",
    desc="SWD programming footprint (ARM 10-pin cable mapping)", pinmap="memory",
    pinmap_ref="TC2030 Cortex pin-out from memory: verify against the cable adapter")


def two_pin(name, ref, fp, *, mpn, mfr, datasheet, desc, pin_names=("1", "2"), funcs=(PAS, PAS)):
    return template(name, ref, fp, [(1, pin_names[0], funcs[0]), (2, pin_names[1], funcs[1])],
                    mpn=mpn, mfr=mfr, datasheet=datasheet, desc=desc, pinmap="kicad-fp")


R0402 = two_pin("R", "R", "Resistor_SMD:R_0402_1005Metric", mpn="RC0402FR-07xxxxL",
                mfr="Yageo", datasheet="https://www.yageo.com/upload/media/product/products/"
                "datasheet/rchip/PYu-RC_Group_51_RoHS_L_12.pdf", desc="resistor 0402 1 %")
C0402 = two_pin("C", "C", "Capacitor_SMD:C_0402_1005Metric", mpn="GRM155 series (X5R/X7R)",
                mfr="Murata", datasheet="https://www.murata.com/en-global/products/capacitor/ceramiccapacitor",
                desc="MLCC 0402")
C0603 = two_pin("C", "C", "Capacitor_SMD:C_0603_1608Metric", mpn="GRM188R61A106KE69D",
                mfr="Murata", datasheet="https://www.murata.com/en-global/products/productdetail?partno=GRM188R61A106KE69%23",
                desc="MLCC 10 uF 10 V X5R 0603")
C0201 = two_pin("C", "C", "Capacitor_SMD:C_0201_0603Metric", mpn="GJM0335 series",
                mfr="Murata", datasheet="https://www.murata.com/en-global/products/capacitor/ceramiccapacitor",
                desc="RF MLCC 0201 (antenna match)")
L0201 = two_pin("L", "L", "Inductor_SMD:L_0201_0603Metric", mpn="LQP03HQ2N7B02",
                mfr="Murata", datasheet="https://www.murata.com/en-global/products/inductor",
                desc="RF inductor 0201 (antenna match)")
L0603 = two_pin("L", "L", "Inductor_SMD:L_0603_1608Metric", mpn="DFE18SAN4R7MG0L",
                mfr="Murata", datasheet="https://www.murata.com/en-global/products/inductor",
                desc="power inductor 4.7 uH 0603 (nRF54L15 DC/DC)")
L0805 = two_pin("L", "L", "Inductor_SMD:L_0805_2012Metric", mpn="DFE201210U-2R2M",
                mfr="Murata", datasheet="https://www.murata.com/en-global/products/inductor",
                desc="power inductor 2.2 uH 0805 (nPM1300 buck)")
XTAL32M = template(
    "XTAL_32MHz", "Y", "Crystal:Crystal_SMD_2016-4Pin_2.0x1.6mm",
    [(1, "X1", PAS), (2, "GND", PAS), (3, "X2", PAS), (4, "GND", PAS)],
    mpn="XRCGB32M000F1H00R0", mfr="Murata", datasheet="https://www.murata.com/en-global/products/timingdevice",
    desc="32 MHz crystal 2016 (load caps internal to nRF54L15)", pinmap="kicad-fp")
XTAL32K = two_pin("XTAL_32k768", "Y", "Crystal:Crystal_SMD_2012-2Pin_2.0x1.2mm",
                  mpn="ABS06-32.768KHZ-T", mfr="Abracon",
                  datasheet="https://abracon.com/Resonators/abs06.pdf",
                  desc="32.768 kHz crystal 2012 (LFXO, internal load caps)")
ANTENNA = two_pin("Antenna_2G4", "AE", "RF_Antenna:Johanson_2450AT18x100",
                  mpn="2450AT18A100E", mfr="Johanson Technology",
                  datasheet="https://www.johansontechnology.com/datasheets/2450AT18A100/2450AT18A100.pdf",
                  desc="2.4 GHz chip antenna", pin_names=("FEED", "NC"))
BUTTON = two_pin("SW_Push", "SW", "Button_Switch_SMD:SW_SPST_EVQP7A", mpn="EVQ-P7A01P",
                 mfr="Panasonic", datasheet="https://industrial.panasonic.com/cdbs/www-data/pdf/ATK0000/ATK0000CE28.pdf",
                 desc="side-actuated tact switch")
BATT_CONN = template(
    "Battery_JST_SH2", "J", "Connector_JST:JST_SH_SM02B-SRSS-TB_1x02-1MP_P1.00mm_Horizontal",
    [(1, "BAT+", PAS), (2, "BAT-", PAS), ("MP", "MP", PAS)],
    mpn="SM02B-SRSS-TB(LF)(SN)", mfr="JST", datasheet="https://www.jst-mfg.com/product/pdf/eng/eSH.pdf",
    desc="LiPo connector (cell: 3.7 V ~200 mAh, e.g. 402030 size, with PCM)", pinmap="kicad-fp")
MOTOR_CONN = template(
    "Motor_JST_SH2", "J", "Connector_JST:JST_SH_SM02B-SRSS-TB_1x02-1MP_P1.00mm_Horizontal",
    [(1, "M+", PAS), (2, "M-", PAS), ("MP", "MP", PAS)],
    mpn="SM02B-SRSS-TB(LF)(SN) + Precision Microdrives 308-100", mfr="JST / Precision Microdrives",
    datasheet="https://www.precisionmicrodrives.com/product/308-100-8mm-vibration-motor-3mm-type",
    desc="8 mm coin ERM vibration motor (3 V) on a connector", pinmap="kicad-fp")
NMOS = template(
    "AO3400A", "Q", "Package_TO_SOT_SMD:SOT-23",
    [(1, "G", IN), (2, "S", PAS), (3, "D", PAS)],
    mpn="AO3400A", mfr="Alpha & Omega", datasheet="https://www.aosmd.com/res/datasheets/AO3400A.pdf",
    desc="N-MOSFET 30 V 5.7 A, motor driver", pinmap="memory",
    pinmap_ref="SOT-23 G/S/D = 1/2/3 from memory: verify")
DIODE = two_pin("1N4148WS", "D", "Diode_SMD:D_SOD-323", mpn="1N4148WS", mfr="onsemi",
                datasheet="https://www.onsemi.com/pdf/datasheet/1n4148ws-d.pdf",
                desc="flyback diode across the motor", pin_names=("K", "A"))
ESD = two_pin("TPD1E10B06", "D", "Diode_SMD:D_0402_1005Metric", mpn="TPD1E10B06DPYR", mfr="TI",
              datasheet="https://www.ti.com/lit/ds/symlink/tpd1e10b06.pdf",
              desc="ESD protection on VBUS", pin_names=("IO", "GND"))
LED = two_pin("LED_red", "D", "LED_SMD:LED_0402_1005Metric", mpn="150040RS75000", mfr="Wurth",
              datasheet="https://www.we-online.com/catalog/datasheet/150040RS75000.pdf",
              desc="charge indicator LED (driven by nPM1300 LED0 sink)", pin_names=("K", "A"))
NTC = two_pin("NTC_10k", "RT", "Resistor_SMD:R_0402_1005Metric", mpn="NCP15XH103F03RC",
              mfr="Murata", datasheet="https://www.murata.com/en-global/products/productdetail?partno=NCP15XH103F03RC",
              desc="10 k B3380 NTC for the charger (move onto the cell if possible)")
TESTPAD = template("TestPad", "TP", "TestPoint:TestPoint_Pad_1.0x1.0mm", [(1, "1", PAS)],
                   mpn="(copper pad)", mfr="-", datasheet="-", desc="test pad", pinmap="kicad-fp")
