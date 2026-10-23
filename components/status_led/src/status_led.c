#include "status_led.h"
#include "status_led_logic.h"
#include "board.h"
#include "device_config.h"
#include "led_strip.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "status_led";

static bool s_present = false;
static led_strip_handle_t s_strip;
static esp_timer_handle_t s_tick_timer;

// Serializes the two LED hardware calls below against the three tasks
// plus the tick timer that can all reach render(): the 50ms tick, the
// task that calls status_led_set_state(), and the USB command task's
// one-shot flash. Nothing else made those mutually exclusive before. See
// AGENTS.md, "Firmware mistakes to avoid".
static SemaphoreHandle_t s_hw_mutex;

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static led_tick_t s_tick;
static bool s_manual = false;
static uint8_t s_manual_rgb[3] = { 0, 0, 0 };
static uint8_t s_last_rgb[3] = { 0, 0, 0 }; // whatever is actually on the wire right now

static void render(void)
{
    if (!s_present) {
        return;
    }
    uint8_t rgb[3];

    portENTER_CRITICAL(&s_mux);
    bool flashing = led_tick_flashing(&s_tick);
    led_pick_rgb(flashing, s_manual, s_manual_rgb, s_tick.state, s_tick.blink_phase_on, rgb);
    portEXIT_CRITICAL(&s_mux);

    // led_strip_set_pixel/refresh block on the RMT peripheral, so they
    // must not run under the spinlock above -- but they still need
    // serializing against each other, since more than one task (plus the
    // tick timer) can call render() at once.
    if (xSemaphoreTake(s_hw_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    led_strip_set_pixel(s_strip, 0, rgb[0], rgb[1], rgb[2]);
    led_strip_refresh(s_strip);
    // Updated only now, after the color actually reached the strip, so a
    // concurrent GET led0 cannot report a color that was never latched.
    portENTER_CRITICAL(&s_mux);
    memcpy(s_last_rgb, rgb, 3);
    portEXIT_CRITICAL(&s_mux);
    xSemaphoreGive(s_hw_mutex);
}

static void tick_cb(void *arg)
{
    (void)arg;
    portENTER_CRITICAL(&s_mux);
    bool need_render = led_tick_step(&s_tick);
    portEXIT_CRITICAL(&s_mux);

    if (need_render) {
        render();
    }
}

static void on_led_mode_changed(const char *mode)
{
    if (strcmp(mode, "STATUS") == 0) {
        status_led_use_status_mode();
    }
    // "MANUAL" with no color yet set is left showing whatever is already
    // on the wire; a host that asks for MANUAL mode without first calling
    // SET led0 gets the LED's last color, not a surprise blackout.
}

esp_err_t status_led_init(void)
{
    const board_profile_t *b = board_get_profile();
    if (b->led_gpio == GPIO_NUM_NC) {
        s_present = false;
        return ESP_OK;
    }
    s_present = true;
    led_tick_init(&s_tick);

    s_hw_mutex = xSemaphoreCreateMutex();
    if (s_hw_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    led_strip_config_t strip_config = {
        .strip_gpio_num = b->led_gpio,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
    };
    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "led_strip_new_rmt_device failed: %d", err);
        return err;
    }
    led_strip_clear(s_strip);

    device_config_register_led_mode_hook(on_led_mode_changed);

    const esp_timer_create_args_t args = {
        .callback = tick_cb,
        .name = "status_led",
    };
    err = esp_timer_create(&args, &s_tick_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_create failed: %d", err);
        return err;
    }
    return esp_timer_start_periodic(s_tick_timer, LED_TICK_MS * 1000);
}

void status_led_set_state(status_led_state_t state)
{
    if (!s_present) {
        return;
    }
    portENTER_CRITICAL(&s_mux);
    led_tick_set_state(&s_tick, state);
    portEXIT_CRITICAL(&s_mux);
    render();
}

void status_led_flash_oneshot(void)
{
    if (!s_present) {
        return;
    }
    portENTER_CRITICAL(&s_mux);
    led_tick_start_flash(&s_tick);
    portEXIT_CRITICAL(&s_mux);
    render();
}

esp_err_t status_led_set_manual_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_present) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    portENTER_CRITICAL(&s_mux);
    s_manual = true;
    s_manual_rgb[0] = r;
    s_manual_rgb[1] = g;
    s_manual_rgb[2] = b;
    portEXIT_CRITICAL(&s_mux);
    device_config_note_led_mode_changed_externally("MANUAL");
    render();
    return ESP_OK;
}

void status_led_get_rgb(uint8_t out[3])
{
    portENTER_CRITICAL(&s_mux);
    memcpy(out, s_last_rgb, 3);
    portEXIT_CRITICAL(&s_mux);
}

void status_led_use_status_mode(void)
{
    if (!s_present) {
        return;
    }
    portENTER_CRITICAL(&s_mux);
    s_manual = false;
    portEXIT_CRITICAL(&s_mux);
    render();
}

bool status_led_is_manual(void)
{
    portENTER_CRITICAL(&s_mux);
    bool v = s_manual;
    portEXIT_CRITICAL(&s_mux);
    return v;
}
