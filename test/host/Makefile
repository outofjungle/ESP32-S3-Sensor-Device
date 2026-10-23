# Host-compiled unit tests for the pure-logic pieces of this firmware --
# no ESP-IDF toolchain, no Docker, no target hardware needed. See
# README.md.

CC     ?= cc
CFLAGS := -std=c11 -Wall -Wextra -Wno-unused-parameter -g $(EXTRA_CFLAGS) -I. -Istubs \
          -I../../components/device_proto/include \
          -I../../components/sensor_core/include \
          -I../../components/sensor_ldc/src \
          -I../../components/sensor_tof/src \
          -I../../components/status_led/src -I../../components/status_led/include \
          -I../../components/tinycbor/src

DEVICE_PROTO_SRC := ../../components/device_proto/src
TINYCBOR_SRC     := ../../components/tinycbor/src

TINYCBOR_SOURCES := \
	$(TINYCBOR_SRC)/cborencoder.c \
	$(TINYCBOR_SRC)/cborencoder_close_container_checked.c \
	$(TINYCBOR_SRC)/cborencoder_float.c \
	$(TINYCBOR_SRC)/cborerrorstrings.c \
	$(TINYCBOR_SRC)/cborparser.c \
	$(TINYCBOR_SRC)/cborparser_float.c

.PHONY: test clean coverage

test: test_input_validate test_proto_util test_cobs test_crc16 test_proto_frame test_channel_registry test_sensor_cfg test_ldc_logic test_tof_logic test_status_led_logic test_device_config test_proto_cmd test_rx_accum test_stream_logic
	@echo "--- test_input_validate ---"
	./test_input_validate
	@echo "--- test_proto_util ---"
	./test_proto_util
	@echo "--- test_cobs ---"
	./test_cobs
	@echo "--- test_crc16 ---"
	./test_crc16
	@echo "--- test_proto_frame ---"
	./test_proto_frame
	@echo "--- test_channel_registry ---"
	./test_channel_registry
	@echo "--- test_sensor_cfg ---"
	./test_sensor_cfg
	@echo "--- test_ldc_logic ---"
	./test_ldc_logic
	@echo "--- test_tof_logic ---"
	./test_tof_logic
	@echo "--- test_status_led_logic ---"
	./test_status_led_logic
	@echo "--- test_device_config ---"
	./test_device_config
	@echo "--- test_proto_cmd ---"
	./test_proto_cmd
	@echo "--- test_rx_accum ---"
	./test_rx_accum
	@echo "--- test_stream_logic ---"
	./test_stream_logic

test_input_validate: test_input_validate.c $(DEVICE_PROTO_SRC)/input_validate.c tinytest.h
	$(CC) $(CFLAGS) -o $@ test_input_validate.c $(DEVICE_PROTO_SRC)/input_validate.c

test_proto_util: test_proto_util.c $(DEVICE_PROTO_SRC)/proto_util.c $(TINYCBOR_SOURCES) tinytest.h
	$(CC) $(CFLAGS) -o $@ test_proto_util.c $(DEVICE_PROTO_SRC)/proto_util.c $(TINYCBOR_SOURCES) -lm

# Builds with coverage instrumentation, runs the tests, and writes
# coverage.info (lcov format) for the VS Code "Coverage Gutters" extension.
# Needs gcovr: brew install gcovr. Only the two production files under test
# are reported (see the --filter list below); vendored tinycbor is excluded.
coverage:
	$(MAKE) clean
	$(MAKE) test EXTRA_CFLAGS=--coverage
	gcovr --root ../.. \
	      --filter '.*/device_proto/src/(input_validate|proto_util|cobs|crc16|proto_frame|channel_registry|device_config|device_config_logic|proto_reply|proto_cmd|rx_accum|stream_logic)\.c' \
	      --filter '.*/sensor_core/src/sensor_cfg_logic\.c' \
	      --filter '.*/sensor_ldc/src/ldc_logic\.c' \
	      --filter '.*/sensor_tof/src/tof_logic\.c' \
	      --filter '.*/status_led/src/status_led_logic\.c' \
	      --lcov coverage.info --txt

test_cobs: test_cobs.c $(DEVICE_PROTO_SRC)/cobs.c tinytest.h
	$(CC) $(CFLAGS) -o $@ test_cobs.c $(DEVICE_PROTO_SRC)/cobs.c

test_crc16: test_crc16.c $(DEVICE_PROTO_SRC)/crc16.c tinytest.h
	$(CC) $(CFLAGS) -o $@ test_crc16.c $(DEVICE_PROTO_SRC)/crc16.c

test_proto_frame: test_proto_frame.c $(DEVICE_PROTO_SRC)/proto_frame.c $(DEVICE_PROTO_SRC)/cobs.c $(DEVICE_PROTO_SRC)/crc16.c tinytest.h
	$(CC) $(CFLAGS) -o $@ test_proto_frame.c $(DEVICE_PROTO_SRC)/proto_frame.c $(DEVICE_PROTO_SRC)/cobs.c $(DEVICE_PROTO_SRC)/crc16.c

test_channel_registry: test_channel_registry.c $(DEVICE_PROTO_SRC)/channel_registry.c tinytest.h
	$(CC) $(CFLAGS) -o $@ test_channel_registry.c $(TINYCBOR_SOURCES) -lm

SENSOR_CORE_SRC := ../../components/sensor_core/src

test_sensor_cfg: test_sensor_cfg.c $(SENSOR_CORE_SRC)/sensor_cfg_logic.c $(DEVICE_PROTO_SRC)/input_validate.c $(DEVICE_PROTO_SRC)/proto_util.c $(TINYCBOR_SOURCES) tinytest.h
	$(CC) $(CFLAGS) -o $@ test_sensor_cfg.c $(SENSOR_CORE_SRC)/sensor_cfg_logic.c $(DEVICE_PROTO_SRC)/input_validate.c $(DEVICE_PROTO_SRC)/proto_util.c $(TINYCBOR_SOURCES) -lm

SENSOR_LDC_SRC := ../../components/sensor_ldc/src

test_ldc_logic: test_ldc_logic.c $(SENSOR_LDC_SRC)/ldc_logic.c $(SENSOR_CORE_SRC)/sensor_cfg_logic.c $(DEVICE_PROTO_SRC)/input_validate.c $(DEVICE_PROTO_SRC)/proto_util.c $(TINYCBOR_SOURCES) tinytest.h
	$(CC) $(CFLAGS) -o $@ test_ldc_logic.c $(SENSOR_LDC_SRC)/ldc_logic.c $(SENSOR_CORE_SRC)/sensor_cfg_logic.c $(DEVICE_PROTO_SRC)/input_validate.c $(DEVICE_PROTO_SRC)/proto_util.c $(TINYCBOR_SOURCES) -lm

SENSOR_TOF_SRC := ../../components/sensor_tof/src

test_tof_logic: test_tof_logic.c $(SENSOR_TOF_SRC)/tof_logic.c $(SENSOR_CORE_SRC)/sensor_cfg_logic.c $(DEVICE_PROTO_SRC)/input_validate.c $(DEVICE_PROTO_SRC)/proto_util.c $(TINYCBOR_SOURCES) tinytest.h
	$(CC) $(CFLAGS) -o $@ test_tof_logic.c $(SENSOR_TOF_SRC)/tof_logic.c $(SENSOR_CORE_SRC)/sensor_cfg_logic.c $(DEVICE_PROTO_SRC)/input_validate.c $(DEVICE_PROTO_SRC)/proto_util.c $(TINYCBOR_SOURCES) -lm

test_status_led_logic: test_status_led_logic.c ../../components/status_led/src/status_led_logic.c tinytest.h
	$(CC) $(CFLAGS) -o $@ test_status_led_logic.c ../../components/status_led/src/status_led_logic.c

test_device_config: test_device_config.c $(DEVICE_PROTO_SRC)/device_config.c $(DEVICE_PROTO_SRC)/device_config_logic.c $(DEVICE_PROTO_SRC)/channel_registry.c $(DEVICE_PROTO_SRC)/input_validate.c tinytest.h
	$(CC) $(CFLAGS) -o $@ test_device_config.c $(DEVICE_PROTO_SRC)/device_config_logic.c $(DEVICE_PROTO_SRC)/input_validate.c $(TINYCBOR_SOURCES) -lm

test_proto_cmd: test_proto_cmd.c $(DEVICE_PROTO_SRC)/proto_cmd.c $(DEVICE_PROTO_SRC)/proto_reply.c $(DEVICE_PROTO_SRC)/device_config.c $(DEVICE_PROTO_SRC)/channel_registry.c tinytest.h
	$(CC) $(CFLAGS) -o $@ test_proto_cmd.c $(DEVICE_PROTO_SRC)/device_config_logic.c $(DEVICE_PROTO_SRC)/input_validate.c $(DEVICE_PROTO_SRC)/proto_util.c $(TINYCBOR_SOURCES) -lm

test_rx_accum: test_rx_accum.c $(DEVICE_PROTO_SRC)/rx_accum.c tinytest.h
	$(CC) $(CFLAGS) -o $@ test_rx_accum.c $(DEVICE_PROTO_SRC)/rx_accum.c

test_stream_logic: test_stream_logic.c $(DEVICE_PROTO_SRC)/stream_logic.c tinytest.h
	$(CC) $(CFLAGS) -o $@ test_stream_logic.c $(DEVICE_PROTO_SRC)/stream_logic.c $(TINYCBOR_SOURCES) -lm

clean:
	rm -f test_input_validate test_proto_util test_cobs test_crc16 test_proto_frame test_channel_registry test_sensor_cfg test_ldc_logic test_tof_logic test_status_led_logic test_device_config test_proto_cmd test_rx_accum test_stream_logic coverage.info *.gcda *.gcno; rm -rf *.dSYM
