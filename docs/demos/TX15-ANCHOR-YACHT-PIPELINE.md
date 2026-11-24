# TX15 / Anchor / Yacht complete Bluetooth trainer pipeline

This branch is the **integrated demonstration** of the completed trainer,
reliability, performance, and update-module work. It is **not** intended as a
single upstream PR. Incomplete GPS work and temporary UART bench helpers are
intentionally excluded.

| Component | Repository | Role |
| --- | --- | --- |
| Yacht | https://github.com/Doncuppjr/yacht | nRF52840 head tracker and BLE trainer transmitter |
| Anchor | https://github.com/Doncuppjr/anchor | ESP32-C5 Bluetooth-to-FrSky UART trainer bridge and module OTA |
| EdgeTX | https://github.com/Doncuppjr/edgetx | TX15 Bluetooth trainer reception, recovery, and update UI |

## Integrated EdgeTX changes

- `ff84d55`: actively drive module disable GPIO during reset.
- `dccd3b7`: bound AT startup and refresh attempts, and reset retries after success.
- `bde9586`: reconfigure the module when active Bluetooth roles change.
- `e22a758`: clear stale telemetry connected state on disconnection.
- `251e038`: drain received trainer data at mixer cadence to reduce latency.
- `ec07acb`: optional generic `AT+OTA` request and **Update module** button.
- `cbe82c5`: prevent duplicate trainer draining in the integrated branch.
- `d42446e`: **Back** from module update dialog cancels OTA mode and restores Bluetooth.

These are all included via ancestry from `bluetooth/tx15-ota-integration`.

## Demonstration sequence

1. Build and flash the matching Yacht firmware, Anchor firmware, and TX15
   EdgeTX firmware from their separate repositories. Keep the model disarmed.
2. Select the TX15 Bluetooth trainer role, pair with the Anchor bridge, and
   show live Yacht head motion arriving as trainer channel updates.
3. Exercise sleep/wake, connection loss and recovery, role switching,
   reconnect, and trainer latency at the mixer cadence.
4. In TX15 Bluetooth settings, press **Update module** and confirm the
   interruption of Trainer/Telemetry. Anchor's optional `AT+OTA` extension
   enters module update mode.
5. Press **Back** on the Bluetooth update dialog to cancel and return to
   normal Bluetooth use.

The Anchor USB installer (https://doncuppjr.github.io/anchor/) is for full
USB provisioning. An OTA upload must use an **application-only** image, not a
merged/full-flash image.

## Scope

This demonstration intentionally excludes unfinished GPS status/USB bridge
experiments, and uncommitted UART echo/bridge bench helpers in another checkout.
Individual upstream PRs should remain focused on independent fixes.
