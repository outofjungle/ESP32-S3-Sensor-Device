# ESP32-S3 Sensor Device - Makefile
# Docker-based build environment (default) with host tools for flashing/monitoring.
# See docs/14-build-and-flash.md.
#
# Docker builds (default):
#   make build, make clean, make menuconfig, make rebuild
#   make build SENSOR=none              (no sensor module compiled in)
#   make build DEVICE=atoms3_lite BOARD=atoms3_lite SENSOR=tof   (explicit, same as default)
#
# Flash & Monitor (host tools):
#   make flash, make erase, make monitor

.PHONY: all build clean fullclean rebuild flash monitor erase \
        menuconfig shell image-build image-pull image-status help web host-test host-coverage format format-check lint complexity check

# `web` must stay in .PHONY above: a real directory named web/ exists (the
# control panel's own home), and without .PHONY, make would see that
# directory, consider the "web" target already up to date, and silently
# skip the recipe below instead of opening it in Chrome.

all: build

TARGET := esp32s3

# Which device assembly, board profile, and sensor module to build. See
# docs/01-architecture.md, docs/15-adding-a-sensor.md, and
# docs/16-adding-a-device.md.
DEVICE ?= atoms3_lite
BOARD  ?= atoms3_lite
SENSOR ?= tof

# Autodetect serial ports (macOS: cu.usbmodem*, Linux: ttyUSB*/ttyACM*).
# CDC0 (commands/samples) and the download-mode port are the lower-numbered
# match. CDC1 (logs) is the higher-numbered match once the app is running.
# See docs/13-host-integration.md.
PORT ?= $(shell ls /dev/cu.usbmodem* /dev/ttyUSB* /dev/ttyACM* 2>/dev/null | sort | sed -n '1p')
LOG_PORT ?= $(shell ls /dev/cu.usbmodem* /dev/ttyACM* 2>/dev/null | sort | sed -n '2p')

LOGS_DIR := logs
LOG_FILE := $(LOGS_DIR)/monitor_$(shell date +%Y%m%d_%H%M%S).log

DOCKER_COMPOSE := docker-compose
# DEVICE/BOARD/SENSOR are passed into the container as environment
# variables, not as `idf.py -D` cache variables: ESP-IDF resolves a
# component's PRIV_REQUIRES in an early pass that does not carry CMake
# variables between CMakeLists.txt files, cache entries included. See the
# comment at the top of CMakeLists.txt and docs/14-build-and-flash.md.
DOCKER_RUN := $(DOCKER_COMPOSE) run --rm -e DEVICE=$(DEVICE) -e BOARD=$(BOARD) -e SENSOR=$(SENSOR) esp-idf

#------------------------------------------------------------------------------
# Docker Image Management
#------------------------------------------------------------------------------

image-build: ## Build the Docker image
	$(DOCKER_COMPOSE) build

image-pull: ## Pull the base ESP-IDF image
	docker pull espressif/idf:v6.1

image-status: ## Show locally built ESP-IDF related images
	@docker images | grep -E "REPOSITORY|esp|sensor-device" || echo "No images found"

#------------------------------------------------------------------------------
# Docker Build
#------------------------------------------------------------------------------

build: ## Build firmware in Docker
	@# CMake only reconfigures when a tracked input (a -D cache variable, a
	@# CMakeLists.txt) changes. DEVICE/BOARD/SENSOR are plain environment
	@# variables (see the comment atop CMakeLists.txt), so a changed one
	@# between builds would not otherwise trigger a reconfigure, and a
	@# stale previous choice would silently stay linked in. Removing the
	@# cache forces one every time; ccache still avoids recompiling
	@# unchanged files, so this costs a configure step, not a full rebuild.
	rm -f build/CMakeCache.txt
	$(DOCKER_RUN) idf.py -C /project -D IDF_TARGET=$(TARGET) -D SDKCONFIG_DEFAULTS=/project/sdkconfig.defaults build

test_status_led_logic: test_status_led_logic.c ../../components/status_led/src/status_led_logic.c tinytest.h
	$(CC) $(CFLAGS) -o $@ test_status_led_logic.c ../../components/status_led/src/status_led_logic.c

clean: ## Clean build output, in Docker
	$(DOCKER_RUN) idf.py -C /project fullclean

fullclean: ## Remove all build output, on the host
	rm -rf build managed_components sdkconfig sdkconfig.old dependencies.lock

rebuild: clean build ## Clean, then build

menuconfig: ## Open the ESP-IDF configuration menu, in Docker
	$(DOCKER_RUN) idf.py -C /project menuconfig

shell: ## Open an interactive shell in the Docker build container
	$(DOCKER_RUN) bash

#------------------------------------------------------------------------------
# Flash & Monitor (host tools)
#------------------------------------------------------------------------------

flash: ## Flash firmware to the device using host esptool
	@test -n "$(PORT)" || (echo "Error: No device found. Set PORT=<device>" && exit 1)
	@test -f build/flash_args || (echo "Error: Build first with 'make build'" && exit 1)
	cd build && esptool --port $(PORT) write_flash @flash_args

erase: ## Erase the device's flash memory
	@test -n "$(PORT)" || (echo "Error: No device found. Set PORT=<device>" && exit 1)
	esptool --port $(PORT) erase_flash

monitor: ## View the log port (CDC1), with logging to file
	@test -n "$(LOG_PORT)" || (echo "Error: No log port found. Set LOG_PORT=<device>" && exit 1)
	@mkdir -p $(LOGS_DIR)
	@if [ -n "$$(ls -A $(LOGS_DIR)/*.log 2>/dev/null)" ]; then \
		mkdir -p $(LOGS_DIR).bak; \
		echo "Backing up existing logs to $(LOGS_DIR).bak/"; \
		mv $(LOGS_DIR)/*.log $(LOGS_DIR).bak/ 2>/dev/null || true; \
	fi
	@echo "Logging to $(LOG_FILE)"
	@if command -v esp-idf-monitor >/dev/null 2>&1; then \
		echo "Using esp-idf-monitor (Ctrl+] to exit)"; \
		esp-idf-monitor --port $(LOG_PORT) build/ESP32-S3-Sensor-Device.elf 2>&1 | tee $(LOG_FILE); \
	else \
		echo "Using screen for monitoring (Ctrl+A then K to exit)"; \
		stty -f $(LOG_PORT) 115200 raw -echo; \
		cat $(LOG_PORT) | while IFS= read -r line; do \
			echo "$$(date '+%H:%M:%S.%3N') $$line" | tee -a $(LOG_FILE); \
		done; \
	fi

#------------------------------------------------------------------------------
# Host-side unit tests
#------------------------------------------------------------------------------

host-test: ## Run the host-compiled unit tests (no Docker, no hardware)
	$(MAKE) -C test/host test

host-coverage: ## Run the host unit tests with coverage; writes test/host/coverage.info
	$(MAKE) -C test/host coverage

#------------------------------------------------------------------------------
# Code quality. Vendored code (tinycbor, ST's ULD) is never touched.
# Needs: brew install clang-format cppcheck
#------------------------------------------------------------------------------

SRC_FILES = $(shell git ls-files -co --exclude-standard 'components/*.c' 'components/*.h' 'main/*.c' \
	'main/*.h' 'test/host/*.c' 'test/host/*.h' 'test/host/stubs/*' | \
	grep -v -E 'tinycbor|managed_components|vl53l1x_uld/(src/VL53L1X_|include/VL53L1X_|include/vl53l1_)')

format: ## Format the project's own C code in place
	clang-format -i $(SRC_FILES)

format-check: ## Fail if any project C file is not formatted
	clang-format --dry-run -Werror $(SRC_FILES)

lint: ## Static analysis of the project's own C code (cppcheck)
	cppcheck --quiet --error-exitcode=1 --enable=warning,style,performance,portability \
	  --inline-suppr --suppress=missingIncludeSystem --suppress=unusedFunction \
	  --std=c11 -I components/device_proto/include -I components/sensor_core/include \
	  $(filter %.c,$(filter-out test/host/%,$(SRC_FILES)))

# Cyclomatic complexity limit per function. Install with: pipx install lizard
COMPLEXITY_MAX ?= 15

complexity: ## Fail if any function's cyclomatic complexity is above COMPLEXITY_MAX
	lizard -l c -C $(COMPLEXITY_MAX) -w $(filter %.c,$(filter-out test/host/%,$(SRC_FILES)))

check: format-check lint complexity host-test ## Everything to run before a commit

#------------------------------------------------------------------------------
# Companion web app
#------------------------------------------------------------------------------

web: ## Open the web control panel in Chrome (needed for the Web Serial API)
	@echo "Opening web/index.html in Chrome"
	@open -a "Google Chrome" web/index.html 2>/dev/null || \
		google-chrome web/index.html 2>/dev/null || google-chrome-stable web/index.html 2>/dev/null || \
		echo "Could not find Chrome. Open web/index.html in Chrome or Edge by hand -- the Web Serial API needs one of them."

#------------------------------------------------------------------------------
# Utilities
#------------------------------------------------------------------------------

help: ## Show this help
	@echo "DOCKER BUILD"
	@echo "  make build                 Build firmware in Docker"
	@echo "  make build SENSOR=none     Build with no sensor module compiled in"
	@echo "  make clean                 Clean build output, in Docker"
	@echo "  make rebuild                Clean, then build"
	@echo "  make menuconfig             Open the ESP-IDF configuration menu"
	@echo ""
	@echo "FLASH & MONITOR (host tools)"
	@echo "  make flash                  Flash firmware using host esptool"
	@echo "  make monitor                View the log port (CDC1)"
	@echo "  make erase                  Erase the device's flash memory"
	@echo ""
	@echo "COMPANION WEB APP"
	@echo "  make web                     Open the web control panel in Chrome"
	@echo ""
	@echo "CODE QUALITY (run 'make check' before every commit)"
	@echo "  make format                  Format the project's C code in place (clang-format)"
	@echo "  make format-check            Fail if any file is not formatted"
	@echo "  make lint                    Static analysis (cppcheck)"
	@echo "  make complexity              Fail if a function's complexity is above 15 (lizard)"
	@echo "  make check                   format-check, lint, complexity, and host-test"
	@echo ""
	@echo "TESTS"
	@echo "  make host-test               Run the host-compiled unit tests (no Docker, no hardware)"
	@echo "  make host-coverage           Same, with coverage; writes test/host/coverage.info"
	@echo ""
	@echo "DOCKER MANAGEMENT"
	@echo "  make image-build             Build the Docker image"
	@echo "  make image-pull              Pull the base ESP-IDF image"
	@echo "  make image-status            Show local images"
	@echo ""
	@echo "UTILITIES"
	@echo "  make shell                   Open a shell in the Docker build container"
	@echo "  make fullclean               Remove all build output, on the host"
	@echo ""
	@echo "Current DEVICE: $(DEVICE)   BOARD: $(BOARD)   SENSOR: $(SENSOR)"
	@echo "Current PORT: $(PORT)"
	@echo "Current LOG_PORT: $(LOG_PORT)"
