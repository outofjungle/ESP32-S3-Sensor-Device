# Flow Control and Backpressure

This document states the rules that keep the device responsive when the host is slow, absent,
or has not opened the port at all. See `docs/00-overview.md` for term definitions.

## The problem

USB is a Full Speed bus here, at 64 bytes per packet, and a host may not read for an arbitrary
stretch of time -- a busy script, a disconnected cable with the port not yet closed, or a
browser tab in the background. Nothing in this firmware may stall waiting for the host to
catch up.

## Rule 1: a logging call must never wait for USB

`esp_log_set_vprintf()`'s replacement in `usb_cdc.c` writes into a ring buffer with a zero
timeout. A full buffer drops the line and counts it; it never blocks the calling task.

## Rule 2: log text goes through a buffer, not directly to USB

The ring buffer decouples whatever task calls `ESP_LOGx` (any task, or an interrupt service
routine) from the one task that actually writes to CDC1.

## Rule 3: one task writes the log buffer to USB

`log_drain_task` is the only writer of CDC1. It blocks on the ring buffer with a bounded
timeout, not forever, so it notices a connection change promptly.

## Rule 4: check the connection before writing

`log_drain_task` checks `tud_cdc_n_connected()` before writing. Writing to a USB endpoint with
no host attached does not itself block in this stack, but checking first avoids queuing data
that will never be read.

## Rule 5: a lost sample must be reported, not hidden

`usb_cdc_send_sample_frame()` never blocks. If a sample frame cannot be queued (the transmit
buffer is full), it is dropped, a counter increases, and the next sample frame that does send
successfully carries the `DROP` flag bit. See `docs/08-channel-model-and-types.md`. A host
that sees `DROP` knows its view of the stream has a gap, rather than silently sampling at a
lower rate than it asked for.

## Rule 6: a command response may block briefly; a sample must not

`usb_cdc_send_response_frame()` is allowed a short blocking flush (50 milliseconds): the host
is synchronously waiting for this one frame, so a brief wait is the right tradeoff.
`usb_cdc_send_sample_frame()` never waits at all, for the reason in Rule 5.

## Rule 7: a channel's `get()` must never block

Every channel's `get()` callback -- a device's own, or a sensor module's -- reads a value
already in memory. A sensor module that needs a blocking read (an I2C transaction) does that
work on its own poll task, and `get()` only reads the cache that task fills. See
`docs/15-adding-a-sensor.md`. This is what keeps `GET` and the streaming sampler responsive
regardless of how slow or fast any one sensor's own measurement cycle is.

## Buffer sizes

| Buffer | Size |
|---|---|
| CDC0 (command/sample port) RX | 512 bytes |
| CDC0 TX | 1024 bytes |
| Log ring buffer (CDC1) | 2048 bytes |
| One log line | 160 bytes |

## How fast a buffer fills

| Case | Frame size | Sample rate | Time to fill the 1024-byte TX buffer |
|---|---|---|---|
| One `uint` channel, streaming | About 20 bytes | 10 Hz (the default `PERIOD_US`) | About 5 seconds, if nothing drains it at all |
| One `uint` channel, streaming | About 20 bytes | 1 kHz | About 50 milliseconds |

A host that stops reading does not stall the device; it starts seeing `DROP` flags instead,
within the time shown above.

## The receiver's own state machine

`usb_cdc.c`'s receiver collects incoming bytes until it sees the frame delimiter, in a small
state machine of its own. See `docs/20-state-machines.md` for a diagram.
