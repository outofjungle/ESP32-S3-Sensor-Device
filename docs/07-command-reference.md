# Command Reference

This document lists every command verb. See `docs/00-overview.md` for term definitions, and
`docs/11-error-codes.md` for what each error code means.

## System commands

### PING

No arguments. Replies `[1, seq, "PING"]`. Confirms the device is listening.

### ID

No arguments. Replies `[1, seq, "ID", name, fw_version, serial]`.

### CAPS

No arguments. One `[3, seq, "CAPS", channel, type, access]` row per channel, then
`[1, seq, "CAPS", count]`. Lists every channel: the device's own (`led0`, `uptime_us`) and
every registered sensor instance's. See `docs/08-channel-model-and-types.md`.

### STAT

No arguments. Replies `[1, seq, "STAT", uptime_us, frames_rx, frames_tx, crc_errors,
samples_dropped, log_lines_dropped]`.

### RST

No arguments. Replies `[1, seq, "RST"]`, then restarts the device after a short delay.

### ECHO

One argument, text, at most 128 bytes. Replies `[1, seq, "ECHO", text]`.

## Data commands

### GET

One argument: a channel name, or `"*"` for every channel. `GET channel` replies
`[1, seq, "GET", value]`. `GET "*"` sends one `[3, seq, "GET", channel, value]` row per
channel, then `[1, seq, "GET", count]`. Flashes the status LED once; see
`docs/10-led-status.md`.

### SET

Two arguments: a channel name and a value. Replies `[1, seq, "SET"]`. Fails `EACCESS` if the
channel is read-only.

## Streaming commands

### CFG

One or two arguments: a key, and optionally a new value. Replies `[1, seq, "CFG",
current_value]` -- the value after any write, always read back. These are device-level
streaming settings, not sensor settings; see `docs/09-sensor-configuration.md` for the `SCFG`
verb instead.

| Key | Type | Values |
|---|---|---|
| `PERIOD_US` | uint | 1000 or more. Microseconds between samples. |
| `BATCH` | uint | 1 to 32. Stored, not currently used by the transmit path. |
| `MODE` | text | `PERIODIC`, `ONCHANGE`, or `BOTH`. Stored; this version always behaves as `PERIODIC`. |
| `ENCODING` | text | Always `"CBOR"`. |
| `LED_MODE` | text | `STATUS` or `MANUAL`. See `docs/10-led-status.md`. |

### SUB / UNSUB

One argument: a channel name. Subscribes or unsubscribes it from the streaming set. `SUB` is
idempotent -- subscribing an already-subscribed channel is not an error.

### START / STOP

No arguments. Starts or stops sending `DATA` frames for every subscribed channel, at
`PERIOD_US` intervals. `START` fails `ESTATE` if already streaming, or if no channel is
subscribed.

## Storage commands

### SAVE / LOAD

No arguments. Saves or loads the device's `CFG` settings and subscription list, and every
sensor instance's configuration keys, to or from flash. `LOAD` fails `ESTATE`
(`ESP_ERR_NVS_NOT_FOUND`) if nothing was ever saved.

## Sensor commands

See `docs/09-sensor-configuration.md` for `SENS`, `SCAPS`, `SCFG`, and `CAL`.

## Unknown verb

A frame whose verb matches none of the above gets `[2, seq, verb, "ECMD"]`.
