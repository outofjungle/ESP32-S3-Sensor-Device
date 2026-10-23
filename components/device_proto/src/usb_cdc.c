#include "usb_cdc.h"
#include "protocol.h"
#include "proto_frame.h"
#include "proto_cmd.h"
#include "rx_accum.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_cdc_acm.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/ringbuf.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static const char *TAG = "usb_cdc";

#define LOG_RINGBUF_SIZE  2048
#define LOG_LINE_MAX      160
#define PORT_LABEL_MAXLEN 48

static char s_serial[13];
static char s_cdc0_name[PORT_LABEL_MAXLEN];
static char s_cdc1_name[PORT_LABEL_MAXLEN];
static const char s_lang_id[2] = { 0x09, 0x04 };
// Index: 0=language, 1=manufacturer, 2=product, 3=serial, 4=CDC0 interface
// name, 5=CDC1 interface name. Index 5 is only used when the debug port is
// enabled. See s_config_desc_2cdc / s_config_desc_1cdc below.
static const char *s_string_desc[6];

// Interface and endpoint numbers for a two-CDC-ACM composite device. These
// match what esp_tinyusb's own default descriptor builder assigns when
// CONFIG_TINYUSB_CDC_COUNT=2. A custom descriptor is built here only so
// each CDC interface can carry its own name (STRID_CDC0_INTERFACE vs
// STRID_CDC1_INTERFACE) instead of sharing one, which is all esp_tinyusb's
// built-in default descriptor supports.
#define ITF_NUM_CDC0      0
#define ITF_NUM_CDC0_DATA 1
#define ITF_NUM_CDC1      2
#define ITF_NUM_CDC1_DATA 3
#define ITF_NUM_TOTAL     4

#define EPNUM_CDC0_NOTIF 1
#define EPNUM_CDC0_DATA  2
#define EPNUM_CDC1_NOTIF 3
#define EPNUM_CDC1_DATA  4

#define STRID_CDC0_INTERFACE 4
#define STRID_CDC1_INTERFACE 5

#define USB_CONFIG_TOTAL_LEN_2CDC (TUD_CONFIG_DESC_LEN + 2 * TUD_CDC_DESC_LEN)
#define USB_CONFIG_TOTAL_LEN_1CDC (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN)

// Both tables are complete, independently valid configuration descriptors
// -- not one truncated at runtime. The debug port (CDC1) is off by
// default; holding the board's button through boot selects the 2-CDC
// table instead. See docs/02-board-atoms3-lite.md.
static const uint8_t s_config_desc_2cdc[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, USB_CONFIG_TOTAL_LEN_2CDC, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC0, STRID_CDC0_INTERFACE, 0x80 | EPNUM_CDC0_NOTIF, 8, EPNUM_CDC0_DATA,
                       0x80 | EPNUM_CDC0_DATA, 64),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC1, STRID_CDC1_INTERFACE, 0x80 | EPNUM_CDC1_NOTIF, 8, EPNUM_CDC1_DATA,
                       0x80 | EPNUM_CDC1_DATA, 64),
};

static const uint8_t s_config_desc_1cdc[] = {
    TUD_CONFIG_DESCRIPTOR(1, 2, 0, USB_CONFIG_TOTAL_LEN_1CDC, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC0, STRID_CDC0_INTERFACE, 0x80 | EPNUM_CDC0_NOTIF, 8, EPNUM_CDC0_DATA,
                       0x80 | EPNUM_CDC0_DATA, 64),
};

static portMUX_TYPE s_stats_mux = portMUX_INITIALIZER_UNLOCKED;
static usb_cdc_stats_t s_stats;
static volatile bool s_pending_drop = false;

static RingbufHandle_t s_log_rb;
static SemaphoreHandle_t s_rx_sem;

// ---------------------------------------------------------------------------
// Serial number and string descriptors
// ---------------------------------------------------------------------------

static void build_serial(void)
{
    // Zero-initialized so a failure here (exceedingly rare on real
    // hardware) still yields a deterministic serial number rather than one
    // built from uninitialized stack memory.
    uint8_t mac[6] = { 0 };
    esp_err_t err = esp_efuse_mac_get_default(mac);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_efuse_mac_get_default failed: %d", err);
    }
    snprintf(s_serial, sizeof(s_serial), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

const char *usb_cdc_get_serial(void)
{
    return s_serial;
}

// ---------------------------------------------------------------------------
// Log output (docs/12-flow-control-and-backpressure.md, rules 1-4)
// ---------------------------------------------------------------------------

static int log_vprintf(const char *fmt, va_list args)
{
    char line[LOG_LINE_MAX];
    int n = vsnprintf(line, sizeof(line), fmt, args);
    if (n <= 0) {
        return n;
    }
    size_t len = (size_t)n;
    if (len >= sizeof(line)) {
        len = sizeof(line) - 1;
    }
    // A zero timeout never blocks. A logging call must never wait for USB.
    if (xRingbufferSend(s_log_rb, line, len, 0) != pdTRUE) {
        portENTER_CRITICAL(&s_stats_mux);
        s_stats.log_lines_dropped++;
        portEXIT_CRITICAL(&s_stats_mux);
    }
    return n;
}

static void log_drain_task(void *arg)
{
    (void)arg;
    for (;;) {
        size_t item_len = 0;
        void *item = xRingbufferReceive(s_log_rb, &item_len, pdMS_TO_TICKS(200));
        if (item == NULL) {
            continue;
        }
        if (tud_cdc_n_connected(TINYUSB_CDC_ACM_1)) {
            size_t queued = tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_1, (const uint8_t *)item, item_len);
            if (queued > 0) {
                tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_1, 0);
            }
        }
        vRingbufferReturnItem(s_log_rb, item);
    }
}

// ---------------------------------------------------------------------------
// Command and sample transmit (CDC0)
// ---------------------------------------------------------------------------

bool usb_cdc_send_response_frame(const uint8_t *cbor_payload, size_t len)
{
    uint8_t wire[PROTO_DECODE_CAP];
    size_t wire_len = 0;
    if (!proto_frame_encode(cbor_payload, len, wire, sizeof(wire), &wire_len)) {
        return false;
    }
    // Check before queuing: TinyUSB auto-flushes once the FIFO holds one
    // max-packet-size's worth, so a queue call that cannot accept the
    // whole frame would already put a truncated, undelimited prefix on
    // the wire before a check on tinyusb_cdcacm_write_queue()'s return
    // value could catch it -- the host's framer then concatenates the
    // next frame onto that prefix and loses it too. This does not fully
    // close the race against the sample sender on the other task (the
    // check and the queue call below are not atomic together), but it
    // closes the common case: backpressure from a slow host. See
    // AGENTS.md, "Firmware mistakes to avoid".
    if (tud_cdc_n_write_available(TINYUSB_CDC_ACM_0) < wire_len) {
        return false;
    }
    size_t queued = tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, wire, wire_len);
    // A command response is synchronous: the host is waiting for it. A
    // short blocking flush is acceptable here, unlike the logging path.
    tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, pdMS_TO_TICKS(50));

    portENTER_CRITICAL(&s_stats_mux);
    s_stats.frames_tx++;
    portEXIT_CRITICAL(&s_stats_mux);

    return queued == wire_len;
}

void usb_cdc_send_sample_frame(const uint8_t *cbor_payload, size_t len)
{
    uint8_t wire[PROTO_DECODE_CAP];
    size_t wire_len = 0;
    if (!proto_frame_encode(cbor_payload, len, wire, sizeof(wire), &wire_len)) {
        return;
    }

    // See usb_cdc_send_response_frame's comment: check before queuing.
    if (tud_cdc_n_write_available(TINYUSB_CDC_ACM_0) < wire_len) {
        portENTER_CRITICAL(&s_stats_mux);
        s_stats.samples_dropped++;
        portEXIT_CRITICAL(&s_stats_mux);
        s_pending_drop = true;
        return;
    }

    size_t queued = tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, wire, wire_len);
    if (queued != wire_len) {
        // The rare remaining case: the response sender's task consumed
        // the space between the check above and this call. Flag the next
        // frame, rather than trust what just reached the wire. See
        // docs/12-flow-control-and-backpressure.md, Rule 5.
        portENTER_CRITICAL(&s_stats_mux);
        s_stats.samples_dropped++;
        portEXIT_CRITICAL(&s_stats_mux);
        s_pending_drop = true;
        return;
    }
    // Non-blocking: a sample frame must never stall the sampler.
    tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, 0);

    portENTER_CRITICAL(&s_stats_mux);
    s_stats.frames_tx++;
    portEXIT_CRITICAL(&s_stats_mux);
}

bool usb_cdc_take_pending_drop(void)
{
    bool v = s_pending_drop;
    s_pending_drop = false;
    return v;
}

void usb_cdc_note_crc_error(void)
{
    portENTER_CRITICAL(&s_stats_mux);
    s_stats.crc_errors++;
    portEXIT_CRITICAL(&s_stats_mux);
}

void usb_cdc_get_stats(usb_cdc_stats_t *out)
{
    portENTER_CRITICAL(&s_stats_mux);
    *out = s_stats;
    portEXIT_CRITICAL(&s_stats_mux);
}

// ---------------------------------------------------------------------------
// Receive (CDC0)
// ---------------------------------------------------------------------------

static void cdc0_rx_callback(int itf, cdcacm_event_t *event)
{
    (void)itf;
    (void)event;
    xSemaphoreGive(s_rx_sem);
}

static void on_rx_frame(void *ctx, const uint8_t *wire, size_t len)
{
    (void)ctx;
    portENTER_CRITICAL(&s_stats_mux);
    s_stats.frames_rx++;
    portEXIT_CRITICAL(&s_stats_mux);

    uint8_t payload[PROTO_DECODE_CAP];
    size_t payload_len = 0;
    if (proto_frame_decode(wire, len, payload, sizeof(payload), &payload_len)) {
        proto_cmd_dispatch(payload, payload_len);
    } else {
        usb_cdc_note_crc_error();
    }
}

static void on_rx_oversize(void *ctx)
{
    (void)ctx;
    // Longer than the receive buffer: count it as a frame and an error, and
    // do not try to decode the part that fit.
    portENTER_CRITICAL(&s_stats_mux);
    s_stats.frames_rx++;
    portEXIT_CRITICAL(&s_stats_mux);
    usb_cdc_note_crc_error();
}

static void rx_task(void *arg)
{
    (void)arg;
    static rx_accum_t acc;
    static const rx_accum_cb_t cb = { on_rx_frame, on_rx_oversize };
    rx_accum_init(&acc);

    for (;;) {
        xSemaphoreTake(s_rx_sem, portMAX_DELAY);
        bool more = true;
        while (more) {
            // Drain in bounded batches and yield between them, rather than
            // running until the FIFO empties with no pause. usb_rx runs at
            // the highest application priority: a host that streams bytes
            // continuously (by accident or not) must not be able to
            // starve every lower-priority task -- the status LED, the log
            // drain, sensor polling -- for as long as it keeps writing.
            // See AGENTS.md, "Firmware mistakes to avoid".
            for (int chunk_num = 0; chunk_num < 64 && more; chunk_num++) {
                uint8_t chunk[64];
                size_t got = 0;
                esp_err_t err = tinyusb_cdcacm_read(TINYUSB_CDC_ACM_0, chunk, sizeof(chunk), &got);
                if (err != ESP_OK || got == 0) {
                    more = false;
                    break;
                }
                rx_accum_feed(&acc, chunk, got, &cb, NULL);
            }
            if (more) {
                taskYIELD();
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

static void fill_string_descriptors(const char *port_label)
{
    build_serial();
    snprintf(s_cdc0_name, sizeof(s_cdc0_name), "%s Data", port_label);
    snprintf(s_cdc1_name, sizeof(s_cdc1_name), "%s Debug", port_label);

    s_string_desc[0] = s_lang_id;
    s_string_desc[1] = CONFIG_TINYUSB_DESC_MANUFACTURER_STRING;
    s_string_desc[2] = CONFIG_TINYUSB_DESC_PRODUCT_STRING;
    s_string_desc[3] = s_serial;
    s_string_desc[4] = s_cdc0_name;
    s_string_desc[5] = s_cdc1_name;
}

// CDC1, the log ring buffer, and the task that is the only writer of CDC1.
static esp_err_t start_debug_port(void)
{
    const tinyusb_config_cdcacm_t cdc1_cfg = {
        .cdc_port = TINYUSB_CDC_ACM_1,
    };
    esp_err_t err = tinyusb_cdcacm_init(&cdc1_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "tinyusb_cdcacm_init CDC1 failed: %d", err);
        return err;
    }

    s_log_rb = xRingbufferCreate(LOG_RINGBUF_SIZE, RINGBUF_TYPE_NOSPLIT);
    if (s_log_rb == NULL) {
        ESP_LOGE(TAG, "xRingbufferCreate failed");
        return ESP_ERR_NO_MEM;
    }
    xTaskCreate(log_drain_task, "log_drain", 4096, NULL, 3, NULL);

    // Route ESP_LOG away from CDC1 directly: the drain task above owns every
    // write to CDC1. This call takes effect for every log line after this
    // point, including from other tasks and from ISRs that call ESP_LOG. Left
    // at its default (discarded) when the debug port is off, so nothing tries
    // to use a port that does not exist.
    esp_log_set_vprintf(log_vprintf);
    return ESP_OK;
}

esp_err_t usb_cdc_init(const char *port_label, bool enable_debug_port)
{
    fill_string_descriptors(port_label);

    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
    tusb_cfg.descriptor.string = s_string_desc;
    tusb_cfg.descriptor.string_count = enable_debug_port ? 6 : 5;
    tusb_cfg.descriptor.full_speed_config = enable_debug_port ? s_config_desc_2cdc : s_config_desc_1cdc;

    esp_err_t err = tinyusb_driver_install(&tusb_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "tinyusb_driver_install failed: %d", err);
        return err;
    }

    // Created before tinyusb_cdcacm_init() registers cdc0_rx_callback, which
    // calls xSemaphoreGive(s_rx_sem). If the host sent any byte to CDC0 before
    // this semaphore existed, that call would give a NULL handle and crash.
    s_rx_sem = xSemaphoreCreateBinary();

    const tinyusb_config_cdcacm_t cdc0_cfg = {
        .cdc_port = TINYUSB_CDC_ACM_0,
        .callback_rx = cdc0_rx_callback,
    };
    err = tinyusb_cdcacm_init(&cdc0_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "tinyusb_cdcacm_init CDC0 failed: %d", err);
        return err;
    }

    if (enable_debug_port) {
        err = start_debug_port();
        if (err != ESP_OK) {
            return err;
        }
    }

    xTaskCreate(rx_task, "usb_rx", 4096, NULL, 5, NULL);
    return ESP_OK;
}
