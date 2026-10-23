#pragma once

// Error codes returned by internal functions. proto_cmd.c and sensor_core's
// command handlers turn these into the text codes in docs/11-error-codes.md
// when they build an ERR response.
#define ERR_NOCH   (-1) // ENOCH: no such channel or key
#define ERR_ACCESS (-2) // EACCESS: wrong access mode for this channel
#define ERR_RANGE  (-3) // ERANGE: value out of range
#define ERR_STATE  (-4) // ESTATE: command does not apply right now
#define ERR_ARG    (-5) // EARG: bad argument
#define ERR_VERIFY (-6) // EVERIFY: the device wrote a value, but read back something different
