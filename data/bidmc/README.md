# BIDMC PPG excerpts

`bidmc_NN_25hz.csv` (NN = 01 to 08) are derived from the **BIDMC PPG and Respiration Dataset**
v1.0.0 on PhysioNet: <https://physionet.org/content/bidmc/1.0.0/>.

* Licence: [Open Data Commons Attribution License v1.0](https://opendatacommons.org/licenses/by/1-0/)
* Cite: Pimentel, M.A.F. et al. "Toward a robust estimation of respiratory rate from pulse
  oximeters." IEEE Transactions on Biomedical Engineering 64(8), 1914-1923 (2017); and
  Goldberger, A.L. et al. "PhysioBank, PhysioToolkit, and PhysioNet." Circulation 101(23),
  e215-e220 (2000).

What was done (`tools/prepare_bidmc.py`): downloaded `bidmc_csv/bidmc_NN_Signals.csv` and
`bidmc_NN_Numerics.csv` from the PhysioNet page above, took the first 120 s of the fingertip
`PLETH` channel (125 Hz), averaged blocks of 5 samples to 25 Hz (like the MAX30101's FIFO
averaging), and mapped it to ADC-like counts (`200000 - 100000 * PLETH`; the plethysmogram
rises with blood volume while a reflective PPG ADC reading falls). The reference column
`ref_hr` is the monitor's ECG-derived heart rate (`HR`, 1 Hz).

Columns: `t_s, ppg, ref_hr`. These are ICU fingertip recordings, so they test the filtering and
beat detection on real pulse morphology, not wrist PPG during motion.
