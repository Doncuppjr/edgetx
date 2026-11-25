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
