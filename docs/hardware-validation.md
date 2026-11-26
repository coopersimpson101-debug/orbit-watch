# Hardware validation / TODO for the author

Everything below needs the physical board, so it has **not been done**. Each section gives the
exact procedure and an empty results table. Please fill the tables in as you go, and replace the
⏳ in the README once a row has real data.

Equipment assumed: the rev A board, a Nordic PPK2 (Power Profiler Kit II) or a Joulescope,
a J-Link (or any CMSIS-DAP probe with RTT support), a Tag-Connect TC2030-IDC-NL cable, a
200 mAh 3.7 V LiPo with protection circuit, and a USB-C charger.

## 0. Layout and fab ⏳

1. Import `hardware/out/orbit_watch.net` into KiCad 9 Pcbnew (File > Import > Netlist). Every
   footprint comes from the official libraries.
2. Work through the layout checklist in [hardware.md](hardware.md#layout-checklist).
3. Run DRC with the fab's rule set. Export gerbers, drill and pick-and-place.
4. Board renders: in Pcbnew use View > 3D Viewer > File > Export Current View as PNG (top,
   bottom, isometric). With KiCad 9's CLI you can also run
   `kicad-cli pcb render --side top -o docs/assets/board_top.png orbit_watch.kicad_pcb`.
   Commit the renders and the `.kicad_pcb`.

| item | done | notes |
|---|---|---|
| layout + DRC clean | ⏳ | |
| renders committed (top / bottom / iso) | ⏳ | |
| boards ordered (fab, date) | ⏳ | |

## 1. Power-up without the MCU running ⏳

1. Connect the battery through the PPK2 in ammeter mode (PPK2 between cell and J2), with no SWD probe.
2. Measure VSYS, VDD_3V0, VDD_1V8 at the test pads with a DMM. Fit R1/R2 first: the VSET values
   come from the nPM1300 PS.
3. Plug in USB-C: check that the charge LED (D2) lights and the charge current is about the
   configured value.

| rail | expected | measured |
|---|---|---|
| VSYS (battery only) | about VBAT | |
| VDD_3V0 | 3.00 V | |
| VDD_1V8 | 1.80 V | |
| charge current (USB) | as configured | |

## 2. Firmware bring-up ⏳

```sh
python build.py --board nrf54l15                 # -> build/nrf54l15/orbit-watch.bin
printf 'loadbin build/nrf54l15/orbit-watch.bin 0\nr\ng\nq\n' > flash.jlink
JLinkExe -device nRF54L15_M33 -if SWD -speed 4000 -autoconnect 1 -CommandFile flash.jlink
JLinkRTTViewer                                   # the shell is on RTT channel 0
```

The firmware's nRF54L15 board file (`firmware/boards/nrf54l15/board.c`) compiles but has never
run. Expect bring-up fixes. The known open points are marked in that file:

* the GRTC SYSCOUNTER index and IRQ line owned by the Secure application core
* the GRTC PWM configuration for the 1 Hz EXTCOMIN signal
* linking Bosch's BMI270 configuration blob (BSD-3-Clause, from BoschSensortec/BMI270-Sensor-API)
* nPM1300 register setup (BUCK voltages over I2C, LOADSW1 control, charger current, fuel gauge)
* turning the IMU/PPG FIFO interrupts into wake-ups (the app currently polls on kernel timers)
* making TWIM transfers interrupt-driven instead of busy-polling

| check | expected | result |
|---|---|---|
| RTT banner `orbit-watch 0.1.0 on orbit-watch-revA` | yes | |
| `bmi270: chip id 0x24` | 0x24 | |
| `ps`, `pm`, `uptime` work over RTT | yes | |
| watchface on the LCD | yes | |
| step count while walking 100 steps (counted) | 95 to 105 | |
| HR vs chest strap / pulse oximeter, 3 x 1 min | within 5 bpm | |

## 3. Sleep current ⏳

Power the board from the PPK2 in source mode at 3.7 V (battery removed), with the SWD cable
**unplugged** (a connected debugger keeps the debug domain powered).

1. `deep on`, `face seconds off`, `imu batch 10`, then leave the watch untouched.
2. Record 10 minutes in the PPK2 app at 100 kS/s and export the CSV.
3. Report the floor current (between wake-ups), the average, and the charge per wake-up.

| state | modelled (results/power_budget.json) | measured floor | measured average |
|---|---|---|---|
| minute face, no HR session | (lowpower minus HR terms) | | |
| seconds face | | | |
| HR session (30 s) | | | |
| System OFF (`poweroff`) | 0.9 µA (nRF) + nPM1300 Iq | | |

## 4. Battery life ⏳

1. Charge the cell fully (charger terminates), then unplug USB and note the time.
2. Default profile: minute face, 10 s IMU batching, HR every 10 minutes, no BLE.
3. Log `VBAT` once an hour: over RTT with `stats`, or with the PPK2 in ammeter mode if you also
   want the charge consumed.
4. End of test: the watch's low-battery cut-off, or VBAT < 3.3 V.

| cell (mAh, model) | profile | start | end | days | modelled days | notes |
|---|---|---|---|---|---|---|
| | minute face, HR every 10 min | | | | 140 (model) | |
| | seconds face | | | | 128 (model) | |

The modelled numbers come from `tools/power_budget.py`. They leave out BLE, assume the
display's brochure power (60 µW) and several CPU/DMA parameters (see
`hardware/power/datasheet_values.csv`). They are a sanity check, not a prediction.

## 5. On-wrist photo ⏳

1. Fit the board, display and battery into the case (3D-printed, strap with 22 mm spring bars).
2. Daylight, display showing the live watchface (time + steps + HR), on a wrist.
3. Save as `docs/assets/wrist.jpg` (under 2 MB) and add it to the README hero.

| item | done |
|---|---|
| case printed | ⏳ |
| photo committed | ⏳ |
