#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "esp_err.h"

// An in-memory stand-in for one NVS blob, with switches to force a failure.
// Only what device_config.c uses.
typedef int nvs_handle_t;
#define NVS_READONLY  0
#define NVS_READWRITE 1

static uint8_t g_nvs_blob[512];
static size_t g_nvs_blob_len = 0;
static int g_nvs_has_blob = 0;
static int g_nvs_fail_open = 0;
static int g_nvs_fail_commit = 0;

static inline esp_err_t nvs_open(const char *ns, int mode, nvs_handle_t *h)
{
    (void)ns;
    (void)mode;
    *h = 1;
    return g_nvs_fail_open ? ESP_FAIL : ESP_OK;
}

static inline esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *len)
{
    (void)h;
    (void)key;
    if (!g_nvs_has_blob)
        return ESP_ERR_NVS_NOT_FOUND;
    if (*len < g_nvs_blob_len) {
        *len = g_nvs_blob_len;
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(out, g_nvs_blob, g_nvs_blob_len);
    *len = g_nvs_blob_len;
    return ESP_OK;
}

static inline esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *data, size_t len)
{
    (void)h;
    (void)key;
    if (len > sizeof(g_nvs_blob))
        return ESP_ERR_INVALID_SIZE;
    memcpy(g_nvs_blob, data, len);
    g_nvs_blob_len = len;
    g_nvs_has_blob = 1;
    return ESP_OK;
}

static inline esp_err_t nvs_commit(nvs_handle_t h)
{
    (void)h;
    return g_nvs_fail_commit ? ESP_FAIL : ESP_OK;
}
static inline void nvs_close(nvs_handle_t h)
{
    (void)h;
}
