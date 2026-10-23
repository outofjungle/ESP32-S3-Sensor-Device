// Device assembly: M5Stack AtomS3 Lite + one Grove-port sensor. This file
// is the one place that is explicitly aware of both the board and the
// sensor module built in -- every component below it is generic. See
// docs/01-architecture.md and docs/16-adding-a-device.md.

#include "device.h"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "cbor.h"

#include "board.h"
#include "channel_registry.h"
#include "device_config.h"
#include "device_stream.h"
#include "proto_cmd.h"
#include "usb_cdc.h"
#include "err_codes.h"
#include "sensor_core.h"
#include "status_led.h"

#if defined(SENSOR_TOF)
#include "sensor_tof.h"
#elif defined(SENSOR_LDC)
#include "sensor_ldc.h"
#endif

static const char *TAG = "app_main";

// ---------------------------------------------------------------------------
// The device's own channels: led0 and uptime_us. Not a sensor's, so they
// are registered directly here rather than through sensor_core. See
// docs/08-channel-model-and-types.md.
// ---------------------------------------------------------------------------

static esp_err_t get_led0(void *ctx, CborEncoder *enc)
{
    (void)ctx;
    uint8_t rgb[3];
    status_led_get_rgb(rgb);
    return sensor_cbor_to_esp(cbor_encode_byte_string(enc, rgb, sizeof(rgb)));
}

static int set_led0(void *ctx, CborValue *val)
{
    (void)ctx;
    if (!cbor_value_is_byte_string(val)) {
        return ERR_RANGE;
    }
    uint8_t rgb[3];
    size_t len = sizeof(rgb);
    if (cbor_value_copy_byte_string(val, rgb, &len, NULL) != CborNoError || len != 3) {
        return ERR_RANGE;
    }
    return (status_led_set_manual_rgb(rgb[0], rgb[1], rgb[2]) == ESP_OK) ? 0 : ERR_RANGE;
}

static esp_err_t get_uptime_us(void *ctx, CborEncoder *enc)
{
    (void)ctx;
    return sensor_cbor_to_esp(cbor_encode_uint(enc, (uint64_t)esp_timer_get_time()));
}

// ---------------------------------------------------------------------------
// Status LED: reflects fault / ready / streaming+presence. See
// docs/10-led-status.md. This logic is sensor-aware on purpose -- it is
// this device assembly's job, not status_led.c's or sensor_core.c's.
// ---------------------------------------------------------------------------

static void led_monitor_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (sensor_core_any_fault()) {
            status_led_set_state(LED_STATE_FAULT);
        } else if (!device_config_is_streaming()) {
            status_led_set_state(LED_STATE_READY);
        } else {
#if defined(SENSOR_TOF)
            status_led_set_state(sensor_tof_is_present() ? LED_STATE_STREAM_PRESENT : LED_STATE_STREAM_IDLE);
#elif defined(SENSOR_LDC)
            status_led_set_state(sensor_ldc_is_present() ? LED_STATE_STREAM_PRESENT : LED_STATE_STREAM_IDLE);
#else
            status_led_set_state(LED_STATE_STREAM_IDLE);
#endif
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

// ---------------------------------------------------------------------------
// Boot
// ---------------------------------------------------------------------------

static void init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

// Holding the board's button through boot enables the debug (log) port,
// CDC1. See docs/02-board-atoms3-lite.md and
// docs/12-flow-control-and-backpressure.md.
static bool button_pressed_at_boot(void)
{
    const board_profile_t *b = board_get_profile();
    if (b->button_gpio == GPIO_NUM_NC) {
        return false;
    }
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << b->button_gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = b->button_active_low ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
    };
    gpio_config(&cfg);
    int level = gpio_get_level(b->button_gpio);
    return b->button_active_low ? (level == 0) : (level == 1);
}

void app_main(void)
{
    init_nvs();
    ESP_ERROR_CHECK(device_config_init());
    ESP_ERROR_CHECK(status_led_init());

    proto_cmd_init(DEVICE_DISPLAY_NAME);
    proto_cmd_set_on_get_hook(status_led_flash_oneshot);

    channel_registry_register("led0", "bytes", "RW", NULL, get_led0, set_led0);
    channel_registry_register("uptime_us", "uint", "RO", NULL, get_uptime_us, NULL);

    ESP_ERROR_CHECK(sensor_core_init());

#if defined(SENSOR_TOF)
    {
        i2c_master_bus_handle_t i2c_bus;
        SemaphoreHandle_t i2c_bus_mutex;
        ESP_ERROR_CHECK(board_i2c_bus_init(&i2c_bus, &i2c_bus_mutex));
        sensor_board_binding_t binding = {
            .i2c_bus = i2c_bus,
            .i2c_addr = TOF_I2C_ADDR,
            .i2c_bus_mutex = i2c_bus_mutex,
        };
        ESP_ERROR_CHECK(sensor_tof_register(TOF_INSTANCE_NAME, &binding));
    }
#elif defined(SENSOR_LDC)
    {
        i2c_master_bus_handle_t i2c_bus;
        SemaphoreHandle_t i2c_bus_mutex;
        ESP_ERROR_CHECK(board_i2c_bus_init(&i2c_bus, &i2c_bus_mutex));
        sensor_board_binding_t binding = {
            .i2c_bus = i2c_bus,
            .i2c_addr = LDC_I2C_ADDR,
            .i2c_bus_mutex = i2c_bus_mutex,
        };
        ESP_ERROR_CHECK(sensor_ldc_register(LDC_INSTANCE_NAME, &binding));
    }
#endif

    bool enable_debug_port = button_pressed_at_boot();
    ESP_ERROR_CHECK(usb_cdc_init(DEVICE_DISPLAY_NAME, enable_debug_port));
    ESP_ERROR_CHECK(device_stream_init());

    xTaskCreate(led_monitor_task, "led_monitor", 3072, NULL, 2, NULL);

    ESP_LOGI(TAG, "%s, serial %s, debug port %s", DEVICE_DISPLAY_NAME, usb_cdc_get_serial(),
             enable_debug_port ? "on" : "off");
}
