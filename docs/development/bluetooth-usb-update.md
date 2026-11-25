# Capability-gated Bluetooth module USB update

This implementation is independent of the generic CLI serial passthrough.
No CLI is needed for the user-facing flow. Only modules announcing
OK+CAPS:1,...,UPDATE_UART offer USB. No probe is sent to legacy CC2540
modules, and their ordinary initialization and FIFOs are unchanged.

Select Bluetooth settings > Update module > USB. EdgeTX reinitializes the
module, confirms AT+UPDATE=UART / OK+UPDATE:UART, and selects USB CDC.
The browser must see OK+BRIDGE:1,115200,READY before requesting
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

With CLI-enabled bench firmware, bluetooth usbstatus reports accepted TX
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
