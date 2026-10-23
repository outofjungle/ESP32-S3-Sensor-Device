#include "device_config.h"
#include "device_config_logic.h"
#include "channel_registry.h"
#include "err_codes.h"
#include "input_validate.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include <string.h>

#define NVS_NAMESPACE       "dev_cfg"
#define NVS_KEY             "cfg"
#define CFG_VERSION         DC_CFG_VERSION
#define MAX_SUBS            DC_MAX_SUBS
#define CHANNEL_NAME_MAXLEN DC_CHANNEL_NAME_MAXLEN
#define MAX_PERSIST_HOOKS   4

static const char *TAG = "device_config";

static config_state_t s_cfg;

// s_cfg is written by the USB RX task (handling CFG/SUB/UNSUB/START/STOP)
// and read every sample period by device_stream.c's esp_timer callback
// task. Neither caller is ever an ISR, so a plain critical section is the
// correct, sufficient tool here.
static portMUX_TYPE s_cfg_mux = portMUX_INITIALIZER_UNLOCKED;

typedef struct {
    esp_err_t (*save)(void);
    esp_err_t (*load)(void);
} persist_hook_t;

static persist_hook_t s_hooks[MAX_PERSIST_HOOKS];
static size_t s_hook_count = 0;

static void (*s_led_mode_hook)(const char *mode) = NULL;

static void set_defaults(void)
{
    dc_set_defaults(&s_cfg);
}

esp_err_t device_config_init(void)
{
    set_defaults();
    esp_err_t err = device_config_load();
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "device_config_load failed: %d, using defaults", err);
    }
    return ESP_OK;
}

uint32_t device_config_get_period_us(void)
{
    portENTER_CRITICAL(&s_cfg_mux);
    uint32_t v = s_cfg.period_us;
    portEXIT_CRITICAL(&s_cfg_mux);
    return v;
}

int device_config_set_period_us(uint32_t value)
{
    if (value < DC_PERIOD_US_MIN || value > DC_PERIOD_US_MAX) {
        return ERR_RANGE;
    }
    portENTER_CRITICAL(&s_cfg_mux);
    s_cfg.period_us = value;
    portEXIT_CRITICAL(&s_cfg_mux);
    return 0;
}

const char *device_config_get_mode(void)
{
    // Safe without a lock: callers only ever read this into a CBOR encoder
    // synchronously on the task that called this function, and the buffer
    // is only ever written whole (strncpy + explicit terminator) under the
    // lock in device_config_set_mode, never partially.
    return s_cfg.mode;
}

int device_config_set_mode(const char *value)
{
    if (!dc_mode_valid(value)) {
        return ERR_RANGE;
    }
    portENTER_CRITICAL(&s_cfg_mux);
    strncpy(s_cfg.mode, value, sizeof(s_cfg.mode) - 1);
    s_cfg.mode[sizeof(s_cfg.mode) - 1] = '\0';
    portEXIT_CRITICAL(&s_cfg_mux);
    return 0;
}

uint16_t device_config_get_batch(void)
{
    portENTER_CRITICAL(&s_cfg_mux);
    uint16_t v = s_cfg.batch;
    portEXIT_CRITICAL(&s_cfg_mux);
    return v;
}

int device_config_set_batch(uint16_t value)
{
    if (value < DC_BATCH_MIN || value > DC_BATCH_MAX) {
        return ERR_RANGE;
    }
    portENTER_CRITICAL(&s_cfg_mux);
    s_cfg.batch = value;
    portEXIT_CRITICAL(&s_cfg_mux);
    return 0;
}

const char *device_config_get_encoding(void)
{
    return "CBOR";
}

const char *device_config_get_led_mode(void)
{
    return s_cfg.led_mode; // see device_config_get_mode's comment
}

int device_config_set_led_mode(const char *value)
{
    if (!dc_led_mode_valid(value)) {
        return ERR_RANGE;
    }
    portENTER_CRITICAL(&s_cfg_mux);
    strncpy(s_cfg.led_mode, value, sizeof(s_cfg.led_mode) - 1);
    s_cfg.led_mode[sizeof(s_cfg.led_mode) - 1] = '\0';
    portEXIT_CRITICAL(&s_cfg_mux);
    if (s_led_mode_hook != NULL) {
        s_led_mode_hook(value);
    }
    return 0;
}

void device_config_register_led_mode_hook(void (*on_change)(const char *mode))
{
    s_led_mode_hook = on_change;
}

void device_config_note_led_mode_changed_externally(const char *value)
{
    portENTER_CRITICAL(&s_cfg_mux);
    strncpy(s_cfg.led_mode, value, sizeof(s_cfg.led_mode) - 1);
    s_cfg.led_mode[sizeof(s_cfg.led_mode) - 1] = '\0';
    portEXIT_CRITICAL(&s_cfg_mux);
}

int device_config_subscribe(const char *channel_name)
{
    if (channel_registry_find(channel_name) == NULL) {
        return ERR_NOCH;
    }
    portENTER_CRITICAL(&s_cfg_mux);
    int rc = 0;
    bool already = false;
    for (uint32_t i = 0; i < s_cfg.sub_count; i++) {
        if (strcmp(s_cfg.subs[i], channel_name) == 0) {
            already = true;
            break;
        }
    }
    if (already) {
        rc = 0;
    } else if (s_cfg.sub_count >= MAX_SUBS) {
        rc = ERR_RANGE;
    } else {
        strncpy(s_cfg.subs[s_cfg.sub_count], channel_name, CHANNEL_NAME_MAXLEN - 1);
        s_cfg.subs[s_cfg.sub_count][CHANNEL_NAME_MAXLEN - 1] = '\0';
        s_cfg.sub_count++;
    }
    portEXIT_CRITICAL(&s_cfg_mux);
    return rc;
}

int device_config_unsubscribe(const char *channel_name)
{
    portENTER_CRITICAL(&s_cfg_mux);
    int rc = ERR_STATE;
    for (uint32_t i = 0; i < s_cfg.sub_count; i++) {
        if (strcmp(s_cfg.subs[i], channel_name) == 0) {
            for (uint32_t j = i; j + 1 < s_cfg.sub_count; j++) {
                strcpy(s_cfg.subs[j], s_cfg.subs[j + 1]);
            }
            s_cfg.sub_count--;
            rc = 0;
            break;
        }
    }
    portEXIT_CRITICAL(&s_cfg_mux);
    return rc;
}

size_t device_config_sub_count(void)
{
    portENTER_CRITICAL(&s_cfg_mux);
    size_t v = s_cfg.sub_count;
    portEXIT_CRITICAL(&s_cfg_mux);
    return v;
}

const char *device_config_sub_at(size_t index)
{
    // See device_config_get_mode's comment: whole-buffer writes only, safe
    // to read without a lock once the index itself is known to be in range.
    if (index >= s_cfg.sub_count) {
        return "";
    }
    return s_cfg.subs[index];
}

bool device_config_is_streaming(void)
{
    portENTER_CRITICAL(&s_cfg_mux);
    bool v = s_cfg.streaming;
    portEXIT_CRITICAL(&s_cfg_mux);
    return v;
}

int device_config_start_streaming(void)
{
    portENTER_CRITICAL(&s_cfg_mux);
    int rc = 0;
    if (s_cfg.streaming || s_cfg.sub_count == 0) {
        rc = ERR_STATE;
    } else {
        s_cfg.streaming = true;
    }
    portEXIT_CRITICAL(&s_cfg_mux);
    return rc;
}

int device_config_stop_streaming(void)
{
    portENTER_CRITICAL(&s_cfg_mux);
    int rc = 0;
    if (!s_cfg.streaming) {
        rc = ERR_STATE;
    } else {
        s_cfg.streaming = false;
    }
    portEXIT_CRITICAL(&s_cfg_mux);
    return rc;
}

esp_err_t device_config_register_persist_hook(esp_err_t (*save)(void), esp_err_t (*load)(void))
{
    if (s_hook_count >= MAX_PERSIST_HOOKS) {
        return ESP_ERR_NO_MEM;
    }
    s_hooks[s_hook_count].save = save;
    s_hooks[s_hook_count].load = load;
    s_hook_count++;
    return ESP_OK;
}

esp_err_t device_config_save(void)
{
    config_state_t snapshot;
    portENTER_CRITICAL(&s_cfg_mux);
    snapshot = s_cfg;
    portEXIT_CRITICAL(&s_cfg_mux);

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(h, NVS_KEY, &snapshot, sizeof(snapshot));
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);

    for (size_t i = 0; i < s_hook_count; i++) {
        esp_err_t hook_err = s_hooks[i].save();
        if (hook_err != ESP_OK && err == ESP_OK) {
            err = hook_err;
        }
    }
    return err;
}

esp_err_t device_config_load(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return err;
    }

    config_state_t loaded;
    size_t sz = sizeof(loaded);
    err = nvs_get_blob(h, NVS_KEY, &loaded, &sz);
    nvs_close(h);

    // A version mismatch means the struct layout changed since this was
    // saved. Falling back to defaults here is deliberate: a silently
    // reinterpreted blob is worse than a known-good default. See
    // docs/09-sensor-configuration.md, which records the same rule for
    // per-sensor configuration.
    if (err == ESP_OK && sz == sizeof(loaded) && loaded.version == CFG_VERSION) {
        // The blob's size and version match, but its contents are still
        // untrusted: flash can bit-rot, and a write can be interrupted
        // mid-commit. Force every string field terminated and every
        // numeric field in range before this is trusted as live
        // configuration -- treat a loaded value exactly like a value
        // arriving over the wire. See AGENTS.md, "Firmware mistakes to
        // avoid". The range checks and the termination helper are in
        // input_validate.c/.h, which has no ESP-IDF dependency and is
        // unit-tested on a host compiler -- see
        // test/host/test_input_validate.c.
        int repaired = dc_sanitize_loaded(&loaded);
        if (repaired > 0) {
            ESP_LOGW(TAG, "%d stored field(s) were out of range and were reset", repaired);
        }
        portENTER_CRITICAL(&s_cfg_mux);
        s_cfg = loaded;
        portEXIT_CRITICAL(&s_cfg_mux);
    }

    for (size_t i = 0; i < s_hook_count; i++) {
        esp_err_t hook_err = s_hooks[i].load();
        if (hook_err != ESP_OK && hook_err != ESP_ERR_NVS_NOT_FOUND && err == ESP_OK) {
            err = hook_err;
        }
    }
    return err;
}
