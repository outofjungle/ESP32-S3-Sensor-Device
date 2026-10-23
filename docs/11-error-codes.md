# Error Codes

This document lists every error code, and the order the device checks for them. See
`docs/00-overview.md` for term definitions.

## Error code table

| Code | Meaning | What the host should do |
|---|---|---|
| `EFRAME` | The frame did not match the expected structure (wrong kind, or too few items). | Check the frame is a well-formed command. |
| `ECMD` | The verb is not a known command. | Check spelling, or that the right sensor module was built in. |
| `EARG` | An argument is missing, extra, or the wrong type. | Check the command's argument list. |
| `ERANGE` | A value is outside the range this channel or key accepts. | Check `CAPS` or `SCAPS` for the valid range. |
| `ESTATE` | The command does not apply to the device's current state (e.g. `START` while already streaming, or `SCFG` on a faulted sensor). | Check `STAT` or `SENS`, then retry. |
| `EVERIFY` | The device wrote a configuration value, but read it back as something different. | Read the key again with `SCFG` to see its true current value. |
| `ENOCH` | The channel, configuration key, or instance name is not known. | Check `CAPS` or `SCAPS`. |
| `EACCESS` | The channel's access mode does not allow this (writing a read-only channel). | Check `CAPS` for the channel's access. |
| `EINTERNAL` | The device found an internal fault carrying out the command (e.g. an I2C transaction failed). | Retry; if it persists, check the sensor's wiring. |

A frame that fails its Cyclic Redundancy Check (CRC), is longer than the receive buffer (320 bytes
before the delimiter), has a sequence number outside 0 to 65535, or has a CBOR structure too broken to
recover even a trustworthy sequence number, gets no `ERR` response at all -- only a counter
increase, visible through `STAT`. See `docs/06-cbor-encoding.md`.

## How the device chooses an error code

For a command whose frame is readable enough to find a sequence number and a verb, the device
checks, in order:

1. Is the verb known? If not, `ECMD`.
2. Are the arguments present, absent, and typed as this verb expects? If not, `EARG`.
3. Does the named channel, key, or instance exist? If not, `ENOCH`.
4. Does the access mode allow this? If not, `EACCESS`.
5. Is the value within range? If not, `ERANGE`.
6. Does the device's current state allow this command right now? If not, `ESTATE`.
7. For `SCFG` only: did the value read back from the chip match what was written? If not,
   `EVERIFY`.
8. Did carrying out the command hit an internal fault? If so, `EINTERNAL`.

## A note on step 7

`EVERIFY` can only happen after a write has already reached the chip. The configuration key's
new value may or may not be what the device reports afterward; reading the key again is the
way to find out, not resending the same write.
