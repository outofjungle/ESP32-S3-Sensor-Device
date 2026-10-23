# Build and Flash

This document describes the build tool, the Docker build, and the host flash step. See
`docs/00-overview.md` for term definitions.

## Build tool

ESP-IDF v6.1, run inside Docker, from the image `espressif/idf:v6.1`.

## Why Docker for building, but not for flashing

Docker Desktop on macOS does not pass a USB device through to a container. This project builds
firmware in Docker, where the toolchain version is fixed and reproducible, and flashes it with
the host's own `esptool`, which does need direct USB access.

## Build-time selection variables

| Variable | Default | Meaning |
|---|---|---|
| `DEVICE` | `atoms3_lite` | Which `main/devices/<DEVICE>/` directory to build. |
| `BOARD` | `atoms3_lite` | Which board profile (`components/board/include/board_<BOARD>.h`) to use. |
| `SENSOR` | `tof` | Which sensor module (`components/sensor_<SENSOR>/`) to build in: `tof` (VL53L1X), `ldc` (LDC1612), or `none` for no sensor module. |

These are read from the environment (`DEVICE=... idf.py build`), not from `idf.py -D`: ESP-IDF
resolves a component's `PRIV_REQUIRES` in an early pass that does not carry a CMake `-D`
variable between `CMakeLists.txt` files, cache entries included. `make build` sets them from
its own `DEVICE`/`BOARD`/`SENSOR` make variables and exports them into the Docker container.

CMake does not know to reconfigure when only an environment variable changes between builds,
so changing one of these without reconfiguring would silently keep building the previous
choice. `make build` removes `build/CMakeCache.txt` first, every time, to force a reconfigure;
ccache still avoids recompiling unchanged files, so this costs a configure step (a few
seconds), not a full rebuild. Running `idf.py build` directly, inside `make shell`, needs the
same `rm build/CMakeCache.txt` by hand before a build whose `DEVICE`, `BOARD`, or `SENSOR`
differs from the previous one.

See `docs/01-architecture.md` for how these become preprocessor defines.

## Make targets

| Command | What it does |
|---|---|
| `make build` | Builds firmware in Docker, for the default `DEVICE`, `BOARD`, and `SENSOR`. |
| `make build SENSOR=none` | Builds with no sensor module compiled in. |
| `make clean` | Cleans build output, in Docker. |
| `make rebuild` | `make clean`, then `make build`. |
| `make menuconfig` | Opens the ESP-IDF configuration menu, in Docker. |
| `make flash` | Flashes firmware using the host's own `esptool`. |
| `make monitor` | Views the log port (CDC1), logging to a file. |
| `make erase` | Erases the device's flash memory. |
| `make web` | Opens the web control panel's launcher (`web/index.html`) in Chrome. The launcher links to one page per sensor module. See `docs/17-web-control-panel.md`. |
| `make image-build`, `make image-pull`, `make image-status` | Docker image management. |
| `make shell` | Opens an interactive shell in the Docker build container. |
| `make fullclean` | Removes all build output, on the host. |
| `make host-test` | Runs the host-compiled unit tests. No Docker, no hardware. See `test/host/README.md`. |
| `make format`, `make format-check` | Format the project's own C code with `clang-format`, or fail if it is not formatted. Needs `clang-format`. |
| `make lint` | Run `cppcheck` on the project's own C code. Needs `cppcheck`. |
| `make complexity` | Fail if any function's cyclomatic complexity is above 15. Needs `lizard`: `pipx install lizard`, then make sure `~/.local/bin` is in `PATH`. |
| `make check` | `format-check`, `lint`, `complexity`, and `host-test`. Run it before every commit. |
| `make host-coverage` | Runs the same tests with coverage, and writes `test/host/coverage.info` for the VS Code Coverage Gutters extension. Needs `gcovr`. |

## Identifying the two USB ports before flashing

`make flash` and `make monitor` autodetect ports (the Makefile sorts matching device paths;
the lower-numbered match is `PORT`, the higher-numbered is `LOG_PORT`). Set `PORT=` or
`LOG_PORT=` on the command line to override. See `docs/13-host-integration.md`.

## Typical first-time sequence

1. `make image-build` -- builds the Docker image once.
2. `make build` -- builds firmware.
3. Enter firmware download mode; see `docs/02-board-atoms3-lite.md`.
4. `make flash`
5. `make monitor`, holding the board's button down through the next reset, to see log output.

## Target chip setting

This project sets `IDF_TARGET` to `esp32s3` in three places, and all three must agree:

| File | Setting |
|---|---|
| `Makefile` | `TARGET := esp32s3` |
| `docker-compose.yml` | `IDF_TARGET=esp32s3` |
| `sdkconfig.defaults` | `CONFIG_IDF_TARGET="esp32s3"` |
