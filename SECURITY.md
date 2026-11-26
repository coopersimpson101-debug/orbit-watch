# Security policy

orbit-watch is firmware for a wearable with a battery charger. Please report anything that could
make the hardware unsafe (charger or PMIC misconfiguration, a driver that could leave the motor
or LEDs on, wrong limits in the power design) as a security issue, as well as classic bugs such
as memory corruption reachable from the shell or from sensor data.

## Supported versions

Only the latest commit on `main`. The project is pre-hardware: nothing has run on a real board yet.

## Reporting a vulnerability

Please do not open a public issue. Use GitHub's
[private vulnerability reporting](https://github.com/coopersimpson101-debug/orbit-watch/security/advisories/new)
("Security" tab, then "Report a vulnerability"). Include the commit, the board (QEMU AN505 or
hardware revision), and exact steps to reproduce.
