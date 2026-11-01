#include <stdio.h>
#include <string.h>
#include "board_if_i2c_scan.h"
#include "bsp/esp-bsp.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

static const char *TAG = "board_if.i2c";

static const char *name_for_addr(uint8_t addr)
{
    switch (addr) {
    case 0x0D:
        return "QMC5883L mag";
    case 0x14:
    case 0x5D:
        return "GT911 touch";
    case 0x18:
        return "ES8311?";
    case 0x1E:
        return "HMC5883L mag";
    case 0x40:
        return "ES7210?";
    case 0x68:
        return "MPU6050 (GY-87)";
    case 0x77:
        return "BMP180 (GY-87)";
    default:
        return "unknown";
    }
}

void board_if_i2c_scan(board_if_i2c_scan_result_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));

    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (bus == NULL) {
        snprintf(out->error, sizeof(out->error), "I2C bus not ready");
        ESP_LOGW(TAG, "%s", out->error);
        return;
    }

    out->bus_ok = true;
    for (int addr = 0x08; addr <= 0x77 && out->count < BOARD_IF_I2C_MAX_DEVICES; ++addr) {
        esp_err_t err = i2c_master_probe(bus, (uint16_t)addr, 20);
        if (err == ESP_OK) {
            out->devices[out->count].addr = (uint8_t)addr;
            out->devices[out->count].name = name_for_addr((uint8_t)addr);
            ESP_LOGI(TAG, "found 0x%02X (%s)", addr, out->devices[out->count].name);
            out->count++;
        }
    }
}
