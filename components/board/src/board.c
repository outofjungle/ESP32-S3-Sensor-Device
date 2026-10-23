#include "board.h"
#include "esp_log.h"

static const char *TAG = "board";

#if defined(BOARD_ATOMS3_LITE)
#include "board_atoms3_lite.h"
#else
#error \
    "No board profile selected. Set BOARD on the make command line, e.g. make build BOARD=atoms3_lite. See docs/16-adding-a-device.md."
#endif

const board_profile_t *board_get_profile(void)
{
    return &s_board_profile;
}

// Cached so a second call -- a second Grove sensor on the same port --
// reuses the same bus and mutex, rather than try to create a second I2C
// peripheral on the same two pins, which fails. See
// docs/16-adding-a-device.md.
static i2c_master_bus_handle_t s_bus = NULL;
static SemaphoreHandle_t s_bus_mutex = NULL;

esp_err_t board_i2c_bus_init(i2c_master_bus_handle_t *out_bus, SemaphoreHandle_t *out_mutex)
{
    if (out_bus == NULL || out_mutex == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_bus != NULL) {
        *out_bus = s_bus;
        *out_mutex = s_bus_mutex;
        return ESP_OK;
    }

    const board_profile_t *b = board_get_profile();
    if (b->i2c_sda == GPIO_NUM_NC || b->i2c_scl == GPIO_NUM_NC) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
    if (mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = -1, // let the driver pick a free port
        .sda_io_num = b->i2c_sda,
        .scl_io_num = b->i2c_scl,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed: %d", err);
        vSemaphoreDelete(mutex);
        return err;
    }

    s_bus = bus;
    s_bus_mutex = mutex;
    *out_bus = bus;
    *out_mutex = mutex;
    return ESP_OK;
}
