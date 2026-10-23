# Adding a Device

This document describes adding a board profile or a new device assembly. See
`docs/00-overview.md` for term definitions.

## Adding a board profile

A board profile is one header, naming pins and bus settings; nothing else in the tree should
name a GPIO number directly.

1. Create `components/board/include/board_<name>.h`, following
   `board_atoms3_lite.h`'s pattern: one `static const board_profile_t s_board_profile` value,
   naming the Grove port's I2C pins and clock speed, the status LED pin (or `GPIO_NUM_NC` if
   there is none), and the button pin (or `GPIO_NUM_NC`).
2. Add an `#elif defined(BOARD_<NAME>)` branch to `components/board/src/board.c`'s include
   guard.
3. Build it with `make build BOARD=<name>`.

A board with more than one Grove port needs more than one binding passed to
`sensor_core_register()` -- one bus (or one address on a shared bus) per port -- which is a
device assembly's job, not the board profile's. The board profile only names what is
available; a device assembly decides what to connect to what.

## Adding a device assembly

A device assembly is one `main/devices/<name>/` directory: which board, which sensor instance
names, the device's own channels, and the init order.

1. Create `main/devices/<name>/device.h` and `main/devices/<name>/app_main.c`.
2. In `device.h`, name the device's display name and any instance names it will register
   (e.g. `TOF_INSTANCE_NAME "tof0"`).
3. In `app_main.c`, following `main/devices/atoms3_lite/app_main.c`'s pattern:
   - Initialize NVS, `device_config_init()`, and `status_led_init()`.
   - Call `proto_cmd_init()` with the device's display name, then
     `proto_cmd_set_on_get_hook()` if the LED should flash on a one-shot read.
   - Register the device's own channels (anything not owned by a sensor module) with
     `channel_registry_register()`.
   - Call `sensor_core_init()`.
   - For each sensor module built in (guarded by `#if defined(SENSOR_<NAME>)`), call
     `board_i2c_bus_init()` (or reuse a bus already created for an earlier sensor on the same
     port) and the module's own `sensor_<name>_register()`.
   - Call `usb_cdc_init()` and `device_stream_init()`.

   `main/devices/atoms3_lite/app_main.c` has two such blocks today, one per sensor module
   this project supports, each behind its own `#if`:

   ```c
   #if defined(SENSOR_TOF)
       {
           i2c_master_bus_handle_t i2c_bus;
           ESP_ERROR_CHECK(board_i2c_bus_init(&i2c_bus));
           sensor_board_binding_t binding = { .i2c_bus = i2c_bus, .i2c_addr = TOF_I2C_ADDR };
           ESP_ERROR_CHECK(sensor_tof_register(TOF_INSTANCE_NAME, &binding));
       }
   #elif defined(SENSOR_LDC)
       {
           i2c_master_bus_handle_t i2c_bus;
           ESP_ERROR_CHECK(board_i2c_bus_init(&i2c_bus));
           sensor_board_binding_t binding = { .i2c_bus = i2c_bus, .i2c_addr = LDC_I2C_ADDR };
           ESP_ERROR_CHECK(sensor_ldc_register(LDC_INSTANCE_NAME, &binding));
       }
   #endif
   ```

   Only one `#if` branch compiles into a given firmware image, since `SENSOR` takes one value
   per build. A device assembly with more than one Grove port, built with two sensor modules
   both selected, would need both blocks to run (not `#elif`) and the second block would reuse
   the first block's `i2c_bus` handle if the two sensors share a bus, or call
   `board_i2c_bus_init()` again for a second, independent port. This project's one device
   assembly has one port, so it does not need that case.
4. Build it with `make build DEVICE=<name>`.

No change to `components/device_proto`, `components/sensor_core`, or any existing sensor
module is needed.

## A device with no sensor module

`make build SENSOR=none` builds a device assembly with no `sensor_<name>` component required
at all -- useful for testing the core (the transport, the LED, the device-level channels) with
no sensor attached. An `app_main.c` written as in the steps above, with its sensor-registration
block inside `#if defined(SENSOR_<NAME>)`, already supports this with no extra code: the
`#if` is simply false, and `MINIMAL_BUILD` (see `docs/01-architecture.md`) means no sensor
module is compiled at all.
