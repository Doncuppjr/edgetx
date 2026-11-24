# TX15 M10 GPS diagnostics: experimental additions

This branch extends the existing diagnostic firmware. No receiver configuration
is applied automatically on boot. Every configuration command acts only on
receiver RAM (UBX-CFG-VALSET layer 1); settings are neither written to BBR nor
flash, and may revert when receiver power cycles.

## Read-only diagnostics

`gps status` includes UBX NAV-PVT horizontal/vertical accuracy estimates
(in millimetres), speed accuracy estimate (mm/s), PDOP (in units of 0.01),
fix flags, and the latest UBX ACK/NAK class, ID and running ACK count.

The color LCD statistics screen displays hAcc, vAcc and PDOP. The numbers
are **receiver estimates**, not measurements of absolute position error.
NMEA-only receivers leave these new UBX-specific fields at zero; this is
not a reliable indication of perfect accuracy.

## Experimental GPS control commands

- `gps rate 1` / `gps rate 2` / `gps rate 5` / `gps rate 10`: request
  corresponding navigation measurement rate using CFG-RATE-MEAS.
- `gps constellation galileo on`, `gps constellation beidou on`,
  `gps constellation glonass off`, etc.: request enabled state for
  GPS, SBAS, Galileo, BeiDou, QZSS and GLONASS via CFG-SIGNAL keys.
- `gps constellation gps off` is intentionally blocked to prevent
  removing the last known major constellation; enabling/disabling other
  constellations can also cause loss of fix or reset of GNSS acquisition.

All configuration commands require an active UBX protocol. A command reporting
"request sent" confirms only that transmission was queued, **not** that the
receiver accepted or applied it. Check `gps status` after allowing time for
an ACK/NAK. Validate effective rate by comparing NAV-PVT counter changes over
real time; verify signal changes by observing acquisition/satellites.

The receiver tested on Nov 24, 2026 identified as `ROM SPG 5.10` with a
reported UBX protocol 34.10. Direct polling produced ACKs for NAV-SAT and
CFG-VALGET but not the expected payload responses; therefore configuration
readback and true constellation state remain unverified. No persistent changes
should be implemented without verified response handling.

## Deferred (not in this branch)

- UART baud changes require an atomic switch to the new speed, communication
  verification, timeout and automatic recovery to the old speed.
- Start-position assistance (UBX-MGA-INI with known approximate position and
  time where supported) is distinct from requesting a **cold start** via
  CFG-RST. Cold start erases assistance and may worsen time to first fix.
- Persistent configuration and GUI editing require receiver discovery,
  validation, confirmation, and explicit save actions.

## Files

Changes relative to the latest TX15 GPS diagnostics baseline include
`radio/src/gps.h`, `gps_ubx.h`, `gps_ubx.cpp`, `cli.cpp`, and the GPS
section of `gui/colorlcd/mainview/view_statistics.cpp`.
