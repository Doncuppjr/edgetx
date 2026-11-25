# Capability-gated Bluetooth module USB update

This implementation is independent of the generic CLI serial passthrough.
No CLI is needed for the user-facing flow. Only modules announcing
OK+CAPS:1,...,UPDATE_UART offer USB. No probe is sent to legacy CC2540
modules, and their ordinary initialization and FIFOs are unchanged.

Select Bluetooth settings > Update module > USB. EdgeTX reinitializes the
module, confirms AT+UPDATE=UART / OK+UPDATE:UART, and selects USB CDC.
After binary serial setup, the browser sends AT+BRIDGE? and must see OK+BRIDGE:2,115200,READY before requesting
AT+DOWNLOAD. USB presence, CDC initialization, and host DTR are distinct.
Bridge protocol 2 completes through a fresh management connection after
verified flash and watchdog restart. The installer sends AT+BRIDGE=FINISH
before any AT+BRIDGE? query and requires OK+BRIDGE:FINISH. It is rejected
before ROM entry and never parsed from binary ROM payload. The radio waits
until the acknowledgment completes on USB before beginning normal Bluetooth
initialization, and confirms restart only after its ordinary AT role handshake.
This replaces the protocol-1 RTS request, which failed on the Windows bench.

## Ownership and buffering

The normal Bluetooth FIFO definitions remain 64 TX / 256 RX. Maintenance
uses its own 64 TX / 256 RX buffers. The USB OUT endpoint buffer is retained
until every byte has been accepted by checked UART TX enqueue. Until then
the endpoint is not rearmed, so USB NAK applies backpressure. The retained
packet is not copied into a large software FIFO.

USB SOF services bounded forwarding every frame without suspending the
scheduler. Normal Bluetooth driver reads/writes, CLI UART commands and
CLI/debug CDC output are excluded. UART-to-host forwarding checks USB
space before reading UART; outstanding USB IN data remains owned until its
transfer-complete callback. UART overflow/error terminates forwarding and
is surfaced as recovery, never success.

## Cancellation and recovery

Back, DTR close, or 120 seconds without starting ROM entry cancels an armed
application session and restores normal initialization. After DOWNLOAD has
been requested, PH11 cannot reset a chip running ROM. Back dismisses the
status dialog immediately and queues cancellation for the Bluetooth task.
An interrupted ROM session retains UART ownership in the background;
Update module > USB update status reopens it. It does not offer Wi-Fi/BLE
while that UART session is active. Host close, USB
disconnect, a UART error, or 300 seconds without traffic retains exclusive
UART ownership and displays recovery. Reopen the installer to resync in ROM;
its banner becomes OK+BRIDGE:1,115200,RECOVERY. A retry does not reissue
AT commands to ROM. Alternatively power-cycle the module/radio.

After a verified write the browser uses the target-specific RTC watchdog
reset from Espressif esptool 5.3.1, not standard USB serial auto-reset.
The FLASH_END-based soft restart did not restore the tested C5 application.
esptool-js 0.7.1 leaves C5 watchdogReset as a TODO, so the installer uses its
writeReg API with the same C3/C5 reset-register sequence as maintained
Python esptool. The detected chip family is verified before any reset write. Interrupted writes may leave an unbootable image;
do not treat a PH11 pulse or a closed host port as successful recovery.
C5 full-image writing, readback, recovery reconnect, and watchdog restart
have been validated on the TX15. C3 hardware remains unvalidated.

## Measurements

With CLI-enabled bench firmware, bt usbstatus reports accepted TX
and forwarded RX bytes; UART RX/TX and USB pending-packet high-water marks;
RX FIFO overruns and UART framing/overrun errors; maximum USB service gap;
and maximum host-packet/module-queue age in microseconds. Queue ages are
conservative bounds for the oldest queued data, not per-byte timestamps.
DWT cycle differences are sampled during forwarding; no serial logging runs
in the forwarding ISR. Compute sustained throughput from byte deltas and
wall-clock session duration. An explicit canceled session discards its
remaining USB input so an in-flight host write can unwind; those bytes are
never sent to the module.

## Regression status

The endpoint ownership host test covers FS/HS packet sizes, stalled UART
consumption, and byte-exact repeated transfers. CI compiles TX15 with CLI,
Bluetooth and internal GPS. Compilation and host tests are not hardware
validation. Bench checks must cover close/disconnect/retry, successful
readback and restart, Trainer, Telemetry, Wi-Fi/BLE OTA, and sleep/wake.


## TX15 bench results (2026-11-25 UTC)

CI validated Anchor 3c45caf8343c484a42bcd194b4be43c47191f9bf in
pipeline 2694: C5/C3 production and debug, unit tests, generated installer
and artifact integration tests all passed. EdgeTX 7e265035c47a67ac1e7b06b9bcc72c846e854ced
passed TX15 build and host tests in pipeline 2697.

The C5 application was uploaded through the existing Wi-Fi portal, which
returned "Update verified. ANCHOR will restart." Its SHA-256 was
65987a8d4cd27f713f5fb80c5378efc4441f93dd870ff6b5dabba78811e26ec5.
The TX15 UF2 SHA-256 was
9e2e38b953623610ed7fb5ed00b249413f7e14c3dd62846fecec3bd45c61dc38.
The bootloader reported Board-ID: tx15 before UPDATE.UF2 was copied,
synced and unmounted. The known-good pre-update backup was preserved.

The operator confirmed USB appears in the update method list and manual
Back cancellation restores Bluetooth. Host DTR assertion then produced
OK+BRIDGE:1,115200,READY at 115200; the host closed without sending DOWNLOAD.
The resulting automatic-cancel UI needed an explicit canceled message.
ROM entry, flash readback, restart, recovery, and throughput measurements
remain pending at this point. C3 hardware and legacy CC2540 hardware have
not been tested by this bench session.


The first ROM-entry bench attempt received OK+DOWNLOAD followed by the C5
ROM log with boot:0x8 (DOWNLOAD(UART0/USB)) and waiting for download.
Synchronization did not receive a reply. Recovery reconnect returned the
RECOVERY banner, but raw synchronization still did not receive a reply;
no flash erase/write has been performed. Investigation is ongoing.

A USB trace also exposed a Linux CDC open race: the kernel asserted
DTR/RTS before pyserial applied raw termios, and echoed an unsolicited
READY banner back as host OUT data. The bridge now remains silent until
AT+BRIDGE? arrives from the configured host. Unknown/echoed pre-ROM lines
cannot request download or force recovery. DTR-close cancellation starts
only after this handshake, so initial driver control-line changes cannot
cancel an otherwise valid armed session. On a recovery connection,
AT+BRIDGE=STATUS? retrieves counters before AT+BRIDGE? starts binary
forwarding. CLI is still not needed by the normal installer.


With the queried handshake and old Anchor application, the ROM-entry /
failed-sync run measured TX=243 and RX=161 bytes, zero FIFO overruns, zero
UART errors, no bytes pending in UART TX, RX high water 12/255 usable bytes,
TX high water 44/63 usable bytes, and a maximum USB service gap of 1014 us.
These are short-session measurements, not sustained flashing throughput.

A query sent immediately after DTR assertion could arrive before the first
SOF observed the open port. The open handler incorrectly discarded that
new OUT packet. Only close now discards pending input. Closing or changing
owners also preserves any submitted USB IN ring segment until its actual
completion callback; queued, unsubmitted output can be discarded safely.
The host regression test covers adoption of a normal IN transfer into
maintenance, discard/exit before completion, and ring wrap.


## C5 full-image and restart validation (2026-11-25 UTC)

EdgeTX f77aec3a73756c971ccdba8346aa0896a2f0c71a passed pipeline 2703
and was installed after tx15 board-ID and UF2 checksum verification.
Anchor 95c63993e66c30367a62d9275db293652052b086 passed pipeline 2704,
including all four firmware variants and installer packaging. Its C5
application was bootstrapped through the existing Wi-Fi portal, which
returned "Update verified. ANCHOR will restart."

Restoring UART0 native RX IOMUX before the authorized software reset fixed
ROM synchronization: the bridge identified ESP32-C5, chip ID 23.
A deliberately closed identification session reopened with RECOVERY and
successfully completed a full ROM write at 115200 without a stub.

Full image: 8388608 bytes; SHA-256
6990702f1b3ad57a87c9410a0799d46ddd17100190abe20919867e4870f4e11c.
Esptool compressed it to 1048054 bytes and reported 121.9 seconds for the
write. The default esptool readback and a second explicit whole-image MD5
both passed. Its write-plus-first-verification API call took 144.985 seconds.

Post-run counters: accepted UART TX 1084801 bytes, forwarded RX 15796,
RX high water 12/255, TX high water 63/63, USB packet high water 64,
zero RX overruns, zero UART errors, maximum USB service gap 1030 us,
maximum host-packet age 5962 us, maximum module-queue age 1003 us.
The full TX FIFO is handled with endpoint backpressure; no normal FIFO
increase was needed. These measurements include ROM and protocol overhead.

FLASH_END-based soft restart did not complete normal initialization.
Power cycling restored Bluetooth, confirming the flashed image boots.
A separate no-write ROM session using esptool's C5 watchdog_reset returned
to the application's fresh role handshake, and the operator confirmed
"Module restarted. Bluetooth restored."

The dialog Back UX is being changed so it never waits for UART recovery.
Restart confirmation now has a 20-second deadline / exhausted-init failure
message and continues to block Telemetry until a fresh role ACK and identity
reply. Browser end-to-end, interrupted-write, Trainer/Telemetry payload,
BLE OTA, sleep/wake, C3 hardware, and CC2540 hardware tests remain pending.

## Windows host follow-up (2026-11-25 UTC)

The installed radio still runs f77aec3a. Windows enumerates the radio's
class-zero serial device as a composite parent with separate interface children:
control COM24 Code 10 and data interface Code 28. No browser flash was started.
The proposed fix identifies Serial mode as one CDC ACM device (class/subclass
02/02), matching Microsoft's Usbser binding guidance:
https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/usb-driver-installation-based-on-compatible-ids
The other-speed qualifier is updated consistently. HID, storage and DFU device
identification stays unchanged. Commit 2d83ea0e762dba4586d498838ce4347bf2d75be9
is in CI; Windows enumeration must be retested after installation.

The same pending UF2 includes immediate Back dismissal with deferred UART
cancellation, a 20-second restart deadline, preserving CDC while arming, and
disabling/clearing the USB IRQ before peripheral clock teardown. SOF forwarding
also checks configured state and live class data. These address identified
unsafe paths; they do not prove the cause of the operator-reported Emergency
mode during USB connection.

Existing successful ROM flashing and watchdog-reset results above remain
valid. Browser end-to-end, interrupted-write recovery, repeated full sessions,
hot-plug, Trainer/Telemetry payloads, BLE OTA, sleep/wake, C3 hardware and
CC2540 hardware remain unvalidated. Codex browser control on the workstation
also currently fails to launch with CreateProcessWithLogonW error 1920.


## Windows validation and completion correction (2026-11-25 UTC)

EdgeTX 2d83ea0e passed pipeline 2709 and was installed with checksum/board-ID
verification. Windows enumerated one healthy COM25 using usbser (Problem 0),
replacing the split Code 10/28 interfaces. The operator confirmed pre-ROM
host-close cancellation and immediate one-press Back dismissal.

A Windows Python esptool run identified C5 chip 23, closed/reopened into ROM
RECOVERY and synchronized again, wrote the same verified 8 MiB C5 full image,
and passed both the normal readback and a second whole-image MD5. Write
time was 124.8 seconds; write plus first verification was 144.359 seconds.
The watchdog reset was requested, but Windows RTS completion was not observed:
the radio returned to "Update not confirmed." A longer RTS hold also failed.
This is a failed completion test, not a successful end-to-end update.
A post-restart status reported zero RX overruns and one UART error; the latter
was sampled after module boot and must not be described as an error-free session.

Protocol 2 removes dependence on RTS and acknowledges FINISH through the
serial data channel on a fresh connection. This correction and its installer
counterpart still require CI and hardware validation.

## Protocol-2 hardware results (2026-11-25 UTC)

EdgeTX 0925e24aa69d8978c54dceb26529aebb2dc15e93 passed pipeline 2714.
Its UF2 SHA-256 was
57ccb44be66e253fb239640768a159876020e43bdb334b06ab8a155f524c8901.
The bootloader reported Board-ID tx15; the image was copied, synced and
unmounted at 19:15 UTC. Anchor installer commit 3eb142c passed pipeline 2715
for all firmware variants, host tests and installer packaging.

A no-write C5 identification/watchdog session received OK+BRIDGE:FINISH
after reopening the serial port, and the operator confirmed "Module
restarted. Bluetooth restored." CLI status also reported USB phase 5.

An intentional interrupted write then acknowledged one compressed ROM
block and sent only 528 bytes of the next frame before closing DTR/port.
The next connection returned RECOVERY and synchronized with C5 again.
The complete 8 MiB CI95 image was rewritten, passed esptool's readback plus
a second explicit whole-image MD5, and received the FINISH acknowledgment
after watchdog restart. The operator again confirmed Bluetooth restoration.
Reported write time was 125.2 seconds; write plus first verification was
140.578 seconds.

Post-recovery counters: UART TX 1090679 bytes, RX 17083, RX high water 12/255,
TX high water 63/63, USB packet high water 64, zero RX overruns, zero UART
errors, maximum USB service gap 1028 us, maximum host-packet age 5951 us,
maximum module-queue age 1005 us, USB phase 5. These counters include the
interrupted attempt, protocol overhead and restart. They do not justify
increasing the normal Bluetooth FIFO sizes.

A separate no-write ROM session tested physically unplugging and reconnecting
the Windows USB cable while the radio remained powered. The port reappeared,
RECOVERY synchronized, watchdog restart and FINISH were acknowledged, and
the operator confirmed Bluetooth restoration. No Emergency mode was
reported during this tested reconnect cycle.

The operator confirmed head-tracker channel movement with the expected
mapping/direction after the USB updates. Radio power cycle with the tracker
already on, and tracker power cycle with the radio already on, both restored
channel movement automatically without pairing again.

Back dismissal remains intermittent: several early sessions closed on one
press, but after the cable-reconnect session the operator reported several
physical RTN presses and a delay before dismissal. Do not mark this UX issue
resolved. A host test of the production Layer stack reproduced deletion of
the wrong layer/focus group when removing a non-top popup. A separate fix
repairs removal and restoration references; its host test passes, but its
relationship to the hardware delay still needs validation after installation.

The bench also exposed an early-open race during radio AT preparation. No
ROM request was sent by that failed attempt. The installer now polls the
non-establishing STATUS query until maintenance is ready before sending its
single PROBE/DOWNLOAD sequence. This preparation change passes 15 JS host tests.
Browser end-to-end testing remains pending.


## Popup-focus fix hardware follow-up (2026-11-25 UTC)

EdgeTX 14d056438ae3e2b83483aea43538202502b42032 passed pipeline 2716,
including the production popup-stack host regression. UF2 SHA-256:
9556e1a05e391f6aa18141057cd10def9e78bef577c7e77fc6846face566c7e2.
Board-ID tx15, source checksum and backup were checked before installation.

The radio remained connected to AiPal, so this repeat exercised Linux CDC.
A no-write C5 ROM identification session closed, the operator unplugged and
reconnected USB while the radio stayed on, and AiPal observed re-enumeration.
RECOVERY synchronized and watchdog restart/FINISH were acknowledged with
zero UART errors and overruns. The operator confirmed the restored message
and prompt closure on the first short physical RTN tap.

This is one tested repeat of the previously troublesome cable-reconnect
sequence. The concrete popup-stack lifetime defect is fixed and regression-
tested; the hardware result does not prove every intermittent delay or the
earlier Emergency-mode report has the same cause.

Anchor cb5f04cfa3617f56836f7cee8dff5c0ae1cee64b passed pipeline 2717:
all four firmware variants, 15 installer host tests and packaging/integration
checks. Browser end-to-end and remaining OTA/hardware checks are still tracked
separately; do not infer their completion from these CI results.

## BLE OTA protocol validation and throughput (2026-11-25 UTC)

The Linux bench client exercised the existing dedicated BLE update service
on C5 production firmware 95c63993. The first connection attempt timed out
before authentication or any firmware write. A fresh attempt authenticated
one displayed session-PIN challenge, then successfully read the protected
status characteristic.

The client began an application-only update, committed 239 bytes, sent
ABORT, and received status 3 / offset 0. It then restarted the upload and
sent the entire 1663632-byte CI application, checking every committed
offset. Its SHA-256 before transmission was
44768d537140ac78b26ce09c9e5e3d5528afd6943db57ca93f7a6ab80ec51284.
The module returned status 2 with the exact final byte count, confirming
its image-validation and boot-selection path. The data upload took
809.885 seconds. Post-restart Trainer operation is awaiting operator
confirmation; do not infer it from the final GATT acknowledgment.

This was a Linux protocol test, not a Web Bluetooth browser validation.
It used 239 data bytes plus the five-byte command/offset header. Direct
BlueZ characteristic properties reported negotiated MTU 247. Bleak's
mtu_size accessor printed its default placeholder 23, which was not the
observed characteristic MTU.

AiPal's controller reported LE1MTX/LE1MRX only; its BR/EDR 2M/3M capabilities
are not LE 2M PHY support. The initial transfer rate was about 1094 bytes/s.
During the same connection, BlueZ's native HCI helper requested a 15 ms
interval. The controller's Connection Update Complete event reported
success, interval 15 ms, latency zero and supervision timeout five seconds.
The subsequent measured rate was about 3566 bytes/s. This indicates
connection timing and the per-chunk write/read acknowledgment cycle are
major throughput limits in this test.

The interval adjustment was confined to the bench connection. Adapter
defaults remained min 24 / max 40 (1.25 ms units), latency zero. No firmware
connection-profile or browser pacing change was made. The experimental
browser still uses conservative 15-byte data payloads; its performance
must not be inferred from this 239-byte bench client.

Results and timing evidence are preserved under the AiPal USB bench
directory as ble-ota-result.json, ble-ota-measurements.json and
ble-connection-timing.json. These files omit the session PIN.


## BLE restart confirmation (2026-11-25 UTC)

The operator confirmed Trainer channel operation returned after the completed
BLE application update and restart. This closes the post-restart functional
check for that Linux BLE protocol test. No separate assertion is made about
Back-button timing after this BLE session. Browser end-to-end validation and
the untested hardware regressions remain pending.
