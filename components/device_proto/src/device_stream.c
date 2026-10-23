#include "device_stream.h"
#include "device_config.h"
#include "channel_registry.h"
#include "usb_cdc.h"
#include "protocol.h"
#include "stream_logic.h"
#include "cbor.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "device_stream";
static esp_timer_handle_t s_timer;
static uint32_t s_seq = 0;

// A channel's get() callback must return a cached value here, never block
// on a sensor bus: this callback runs on esp_timer's shared dispatch task,
// and this firmware may have other periodic timers running on the same
// task (a sensor module's own poll task is separate -- see
// docs/02-board-atoms3-lite.md and docs/15-adding-a-sensor.md). A sensor
// module that needs a blocking read runs its own task and answers GET from
// a cache, exactly as sensor_core.c requires of every sensor driver.
static void send_sample(const channel_entry_t *ch)
{
    uint32_t flags = usb_cdc_take_pending_drop() ? FLAG_DROP : 0;

    uint8_t buf[PROTO_MAX_PAYLOAD];
    size_t len = 0;
    // docs/05-message-model.md: the sequence number counts up and wraps at
    // 65535. Only this one task (the esp_timer callback) touches s_seq.
    uint32_t seq = stream_seq_next(&s_seq);
    // A sample that did not encode completely is not sent: a frame with a
    // valid CRC but undecodable content is worse than a gap the host can see
    // in the sequence numbers.
    if (!stream_build_sample(buf, sizeof(buf), seq, (uint64_t)esp_timer_get_time(), ch, flags, &len)) {
        return;
    }
    usb_cdc_send_sample_frame(buf, len);
}

static void timer_cb(void *arg)
{
    (void)arg;
    if (!device_config_is_streaming()) {
        return;
    }
    size_t n = device_config_sub_count();
    for (size_t i = 0; i < n; i++) {
        const channel_entry_t *ch = channel_registry_find(device_config_sub_at(i));
        if (ch != NULL) {
            send_sample(ch);
        }
    }
}

esp_err_t device_stream_init(void)
{
    const esp_timer_create_args_t args = {
        .callback = timer_cb,
        .name = "device_stream",
    };
    esp_err_t err = esp_timer_create(&args, &s_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_create failed: %d", err);
        return err;
    }
    return esp_timer_start_periodic(s_timer, device_config_get_period_us());
}

void device_stream_apply_period(void)
{
    esp_timer_restart(s_timer, device_config_get_period_us());
}
