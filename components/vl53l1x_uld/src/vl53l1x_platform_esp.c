#include "vl53l1x_platform_esp.h"
#include "vl53l1_platform.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "vl53l1x_platform";

// Every VL53L1X register is addressed by a 16-bit, big-endian index. This
// is fixed by the chip, not a choice made here -- see the register map in
// include/VL53L1X_api.h, whose addresses run past 0x0100.
#define I2C_TIMEOUT_MS 50

#define MAX_DEVICES 4

typedef struct {
    bool in_use;
    uint16_t dev_addr;
    i2c_master_dev_handle_t handle;
} attached_device_t;

static attached_device_t s_devices[MAX_DEVICES];

static i2c_master_dev_handle_t find_handle(uint16_t dev_addr)
{
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (s_devices[i].in_use && s_devices[i].dev_addr == dev_addr) {
            return s_devices[i].handle;
        }
    }
    return NULL;
}

esp_err_t vl53l1x_platform_attach(i2c_master_bus_handle_t bus, uint16_t dev_addr)
{
    if (find_handle(dev_addr) != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    int slot = -1;
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (!s_devices[i].in_use) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        return ESP_ERR_NO_MEM;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = dev_addr,
        .scl_speed_hz = 400000,
    };
    i2c_master_dev_handle_t handle;
    esp_err_t err = i2c_master_bus_add_device(bus, &dev_cfg, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_bus_add_device(0x%02X) failed: %d", dev_addr, err);
        return err;
    }

    s_devices[slot].in_use = true;
    s_devices[slot].dev_addr = dev_addr;
    s_devices[slot].handle = handle;
    return ESP_OK;
}

esp_err_t vl53l1x_platform_detach(uint16_t dev_addr)
{
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (s_devices[i].in_use && s_devices[i].dev_addr == dev_addr) {
            esp_err_t err = i2c_master_bus_rm_device(s_devices[i].handle);
            s_devices[i].in_use = false;
            return err;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

// ---------------------------------------------------------------------------
// The nine platform functions declared in vl53l1_platform.h.
// ---------------------------------------------------------------------------

static int8_t do_write(uint16_t dev, uint16_t index, const uint8_t *data, size_t data_len)
{
    i2c_master_dev_handle_t handle = find_handle(dev);
    if (handle == NULL) {
        return (int8_t)-1;
    }
    uint8_t buf[2 + 4]; // 2-byte register index + at most a dword of data
    if (data_len > 4) {
        return (int8_t)-1;
    }
    buf[0] = (uint8_t)(index >> 8);
    buf[1] = (uint8_t)(index & 0xFF);
    memcpy(&buf[2], data, data_len);

    esp_err_t err = i2c_master_transmit(handle, buf, 2 + data_len, I2C_TIMEOUT_MS);
    return (err == ESP_OK) ? (int8_t)0 : (int8_t)-1;
}

static int8_t do_read(uint16_t dev, uint16_t index, uint8_t *data, size_t data_len)
{
    i2c_master_dev_handle_t handle = find_handle(dev);
    if (handle == NULL) {
        return (int8_t)-1;
    }
    uint8_t addr_buf[2] = { (uint8_t)(index >> 8), (uint8_t)(index & 0xFF) };
    esp_err_t err = i2c_master_transmit_receive(handle, addr_buf, sizeof(addr_buf), data, data_len, I2C_TIMEOUT_MS);
    return (err == ESP_OK) ? (int8_t)0 : (int8_t)-1;
}

// cppcheck-suppress constParameterPointer ; the signature is fixed by ST's platform interface
int8_t VL53L1_WriteMulti(uint16_t dev, uint16_t index, uint8_t *pdata, uint32_t count)
{
    i2c_master_dev_handle_t handle = find_handle(dev);
    if (handle == NULL) {
        return (int8_t)-1;
    }
    // A general-purpose multi-byte write (used by VL53L1X_SensorInit to
    // push its default configuration table) needs a buffer sized for the
    // register index plus the whole payload, not do_write()'s 4-byte cap.
    uint8_t stack_buf[32];
    uint8_t *buf = stack_buf;
    uint8_t *heap_buf = NULL;
    if (count + 2 > sizeof(stack_buf)) {
        heap_buf = malloc(count + 2);
        if (heap_buf == NULL) {
            return (int8_t)-1;
        }
        buf = heap_buf;
    }
    buf[0] = (uint8_t)(index >> 8);
    buf[1] = (uint8_t)(index & 0xFF);
    memcpy(&buf[2], pdata, count);

    esp_err_t err = i2c_master_transmit(handle, buf, count + 2, I2C_TIMEOUT_MS);
    free(heap_buf);
    return (err == ESP_OK) ? (int8_t)0 : (int8_t)-1;
}

int8_t VL53L1_ReadMulti(uint16_t dev, uint16_t index, uint8_t *pdata, uint32_t count)
{
    return do_read(dev, index, pdata, count);
}

int8_t VL53L1_WrByte(uint16_t dev, uint16_t index, uint8_t data)
{
    return do_write(dev, index, &data, 1);
}

int8_t VL53L1_WrWord(uint16_t dev, uint16_t index, uint16_t data)
{
    uint8_t buf[2] = { (uint8_t)(data >> 8), (uint8_t)(data & 0xFF) };
    return do_write(dev, index, buf, sizeof(buf));
}

int8_t VL53L1_WrDWord(uint16_t dev, uint16_t index, uint32_t data)
{
    uint8_t buf[4] = {
        (uint8_t)(data >> 24),
        (uint8_t)(data >> 16),
        (uint8_t)(data >> 8),
        (uint8_t)(data & 0xFF),
    };
    return do_write(dev, index, buf, sizeof(buf));
}

int8_t VL53L1_RdByte(uint16_t dev, uint16_t index, uint8_t *pdata)
{
    return do_read(dev, index, pdata, 1);
}

int8_t VL53L1_RdWord(uint16_t dev, uint16_t index, uint16_t *pdata)
{
    uint8_t buf[2];
    int8_t rc = do_read(dev, index, buf, sizeof(buf));
    if (rc == 0) {
        *pdata = ((uint16_t)buf[0] << 8) | buf[1];
    }
    return rc;
}

int8_t VL53L1_RdDWord(uint16_t dev, uint16_t index, uint32_t *pdata)
{
    uint8_t buf[4];
    int8_t rc = do_read(dev, index, buf, sizeof(buf));
    if (rc == 0) {
        *pdata = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) | ((uint32_t)buf[2] << 8) | buf[3];
    }
    return rc;
}

int8_t VL53L1_WaitMs(uint16_t dev, int32_t wait_ms)
{
    (void)dev;
    vTaskDelay(pdMS_TO_TICKS(wait_ms));
    return (int8_t)0;
}
