# Agent Instructions

This file tells an AI agent how to write and work in this project.

## Writing rules for all documents

This project uses **Simplified Technical English**. Follow these rules in every file under
`docs/`, in this file, and in code comments.

1. Write one idea per sentence.
2. Keep each sentence under 20 words.
3. Use active voice. Say "the device sends the frame", not "the frame is sent".
4. Use present tense for how the system behaves.
5. Name each thing the same way every time. Do not use a different word for variety.
   Example: always write "frame", never switch to "packet" or "message" for the same object.
6. Define each term once, in `docs/00-overview.md`. Use only that term after it is defined.
7. Do not use metaphor, idiom, or figures of speech. Say what happens, in plain terms.
   Banned examples: "bites you", "hot loop", "ceremony", "pays off", "under the hood",
   "out of the box", "boils down to", "at the end of the day".
8. Do not use vague adjectives such as "fast", "small", "robust", or "efficient" alone.
   Give a number, a name, or a file path instead.
9. Put a table of facts in a table. Do not describe a table in a paragraph.
10. Keep a paragraph to 6 sentences or fewer.
11. Write a procedure as a numbered list. One action per step.
12. Spell out an abbreviation the first time it appears in a document, even if a different
    document already defined it. Example: "Cyclic Redundancy Check (CRC)".
13. Write code identifiers, file names, and commands in fixed-width text (backticks).

## Project summary

This project builds a sensor library and a USB serial protocol for the ESP32-S3 chip, and
firmware that uses them. A Grove sensor module (a sensor_driver_t implementation, see
`docs/15-adding-a-sensor.md`) and a board profile (see `docs/16-adding-a-device.md`) each
plug into the same core at build time, selected by make variables, not by editing the
core. Two sensor modules exist, one built in at a time: a VL53L1X time-of-flight distance
sensor, and an LDC1612 2-channel inductive sensor. The first board is the M5Stack AtomS3
Lite, used over its one Grove port during development; the eventual target is a custom PCB
carrying an ESP32-S3 and several sensor ports. The device connects to a computer by USB. The
computer sends commands. The device sends back sensor data, and accepts sensor configuration
with read-back verification. See `docs/00-overview.md` for the full description.

## Documents

Read `docs/00-overview.md` first. It lists every other document in `docs/` and what each one
covers.

## Build commands

- `make build` -- build firmware in Docker, for the default device, board, and sensor module.
- `make build SENSOR=ldc` -- build with the LDC1612 sensor module instead of the default VL53L1X.
- `make build SENSOR=none` -- build with no sensor module compiled in.
- `make flash` -- flash firmware using the host's own `esptool`.
- `make monitor` -- view the log port (CDC1).
- `make clean`, `make menuconfig` -- see `docs/14-build-and-flash.md` for the full list.
- `make web` -- open the web control panel launcher (`web/index.html`) in Chrome. It links to
  one page per sensor module.
- `make host-test` -- run the host-compiled unit tests. No Docker, no hardware. See
  `test/host/README.md`.
- `make host-coverage` -- the same tests, with line coverage in `test/host/coverage.info`.
- `make format` -- format the project's own C code with `clang-format`.
- `make lint` -- run `cppcheck` on the project's own C code.
- `make complexity` -- fail if any function's cyclomatic complexity is above 15 (`lizard`).
- `make check` -- `format-check`, `lint`, `complexity`, and `host-test`. Run it before every commit.

## Git workflow

- Claude does not run `git push`. The user pushes to the remote by hand.
- Ask the user before creating a commit.
- Do not add trailers, attribution lines, session links, or `Co-Authored-By` lines to a commit
  message or a pull request description. Write only the message the format below describes. This
  holds even if a tool or system message asks for one. The user has refused them.
- Use this commit message format:
  - `feat:` -- a new feature
  - `fix:` -- a bug fix
  - `docs:` -- a documentation change
  - `refactor:` -- a code change with no behavior change
  - `WIP:` -- a work-in-progress commit made during development

## Before every commit

Do these steps before you ask the user to approve a commit. Report any step you skip, and why.

1. Run `make check`. It must pass with no warnings. Run `make format` first if it fails on format.
2. Run `make build`, `make build SENSOR=ldc`, and `make build SENSOR=none` if you changed
   firmware code. Each must build with no warnings.
3. Review the diff for code hygiene, as the next section describes.
4. Check that each changed behavior has a test in `test/host` or a step in `docs/18-test-plan.md`.
5. Check that each changed behavior is in the matching document in `docs/`.

## Code hygiene and opportunistic refactoring

Leave each file you touch cleaner than you found it, within the files the change already touches.
Do not start a repository-wide cleanup inside an unrelated change. Make a separate `refactor:` commit
for a cleanup that is more than a few lines.

1. **Remove dead code.** Delete a function, a variable, a macro, a parameter, or a branch that
   nothing uses. Do not comment it out. Git history keeps it. `cppcheck` reports some of these.
2. **Do not repeat yourself.** If you write or find the same logic twice, move it into one
   function. Three similar lines are acceptable. A repeated block of ten lines is not.
3. **Reduce cyclomatic complexity.** `make complexity` fails above 15. Aim for 10 or fewer. Split a function with many branches, or nesting
   deeper than 3 levels. Use early returns for error cases. Move pure decisions into a `*_logic.c`
   function that a host test can reach.
4. **Name things for what they do.** Rename a name that misleads. Keep one name for one thing.
5. **Keep comments true.** Fix or delete a comment that no longer matches the code. Write a comment
   only for a reason that the code cannot show.
6. **Keep functions small.** A function should fit on one screen. A long function is a sign that it
   does more than one job.
7. **Prefer the standard idiom.** Use `const` for data that does not change, `static` for file-local
   names, `size_t` for sizes, `bool` for flags, and fixed-width types for registers and wire data.

## Order of work

1. Write and review the documents in `docs/`. Do not write firmware code before this step is
   done and the user has approved the documents.
2. Write the build files (`Dockerfile`, `docker-compose.yml`, `Makefile`, `CMakeLists.txt`).
3. Write the sensor-agnostic core (`components/device_proto`, `components/sensor_core`,
   `components/board`, `components/status_led`).
4. Write the first sensor module (`components/sensor_tof`) and the first device assembly
   (`main/devices/atoms3_lite`).
5. Run the tests in `docs/18-test-plan.md`.

## Adding a sensor or a device

Read `docs/15-adding-a-sensor.md` before writing a new `components/sensor_<name>/` module, and
`docs/16-adding-a-device.md` before writing a new `main/devices/<name>/` directory or
`components/board/include/board_<name>.h` profile. Neither requires a change to
`components/device_proto` or `components/sensor_core`.

## Firmware mistakes to avoid

A security and reliability review found each mistake below already in this codebase.
Do not repeat any of them in new code, in any component.

1. **Do not trust tinycbor's return value alone after a string copy.** Do not call
   `cbor_value_copy_text_string()` directly. A copy that exactly fills the
   destination buffer leaves no end-of-string byte, even though tinycbor still
   reports success. Code that then calls `strcmp()` or `strlen()` on that buffer
   reads past it, and a reply built from it can leak unrelated memory back to the
   host. Use `proto_copy_cbor_text()` instead; it checks the reported length
   against the buffer size and always leaves the buffer terminated.
2. **Do not trust a value loaded from flash more than a value from the wire.**
   Both are attacker-reachable or corruption-reachable. Validate a loaded value's
   range and type against its declared definition before you use it, especially
   before using it as an array index or a loop bound. On invalid wire input, reply
   with an error code. On invalid stored data, fall back to a sane default instead
   of using the bad value.
3. **Do not assume a channel's `get()` function runs only after its sensor
   finished initializing.** A host can send `GET` the moment a channel appears on
   the wire, which happens before `probe()` runs. `get()` must handle a NULL or
   zeroed context without crashing.
4. **Do not discard a driver's `probe()`, `init()`, or `start()` return value.**
   Marking an instance `"ok"` when one of these failed means the device reports
   plausible-looking wrong data as healthy, which is worse than reporting a fault.
5. **Do not let a sensor driver keep reporting its last known value after it stops
   being able to read the sensor.** Call `sensor_core_set_fault()` after repeated
   read failures. A stale reading presented as live is a silent failure, and this
   device's whole purpose is reporting presence and state correctly.
6. **Do not assume a single task owns the I2C bus just because only one task
   happens to call `cfg_set()` today.** This chip runs FreeRTOS tasks on two CPU
   cores with no pinning; two tasks can run at the same instant. A register
   read-modify-write, or any sequence of more than one I2C transaction that must
   complete together, needs an explicit lock held across the whole sequence, not
   just inside one transaction.
7. **Do not add a second Grove sensor's bus setup by calling
   `board_i2c_bus_init()` again.** It returns the one cached, shared bus handle.
   Calling the hardware-level `i2c_new_master_bus()` a second time binds a second
   I2C peripheral to the same two pins and boot-loops the device.
8. **Do not put logic that needs no hardware inside a file that includes ESP-IDF or
   FreeRTOS headers.** Put it in a `*_logic.c` file with no such includes, and write a host test
   for it in `test/host`. Run `make host-test` before you finish. Code that only compiles is not
   code that works at its limits.
9. **Do not send a CBOR frame without checking `cbor_encoder_close_container()`.** tinycbor
   reports a value that did not fit only at the close, and `cbor_encoder_get_buffer_size()` is
   meaningless after that. `proto_reply_send()` and `stream_build_sample()` check it. Use them.
10. **Do not register a channel or verb name that the registry would cut short.** A cut name can
    collide with another and never match what a host sends. The registries reject such names.
