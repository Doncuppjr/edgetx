# Capability-gated Bluetooth module USB update

This implementation is independent of the generic CLI serial passthrough.
No CLI is needed for the user-facing flow. Only modules announcing
OK+CAPS:1,...,UPDATE_UART offer USB. No probe is sent to legacy CC2540
modules, and their ordinary initialization and FIFOs are unchanged.

Select Bluetooth settings > Update module > USB. EdgeTX reinitializes the
module, confirms AT+UPDATE=UART / OK+UPDATE:UART, and selects USB CDC.
After binary serial setup, the browser sends AT+BRIDGE? and must see OK+BRIDGE:1,115200,READY before requesting
AT+DOWNLOAD. USB presence, CDC initialization, and host DTR are distinct.
RTS is not wired to the ESP reset: after verified flash and software restart,
the browser raises RTS to request return to normal Bluetooth. The radio does
not confirm restart until its ordinary AT role handshake completes.

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
been requested, PH11 cannot reset a chip running ROM. Host close, USB
disconnect, a UART error, or 300 seconds without traffic retains exclusive
UART ownership and displays recovery. Reopen the installer to resync in ROM;
its banner becomes OK+BRIDGE:1,115200,RECOVERY. A retry does not reissue
AT commands to ROM. Alternatively power-cycle the module/radio.

After a verified write the browser uses ROM software restart, not standard
USB serial auto-reset. Interrupted writes may leave an unbootable image;
do not treat a PH11 pulse or a closed host port as successful recovery.
Hardware validation of both chips and reset paths remains required.

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
