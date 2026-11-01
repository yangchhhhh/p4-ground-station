#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BOARD_IF_I2C_MAX_DEVICES 16

typedef struct {
    uint8_t addr;
    const char *name;
} board_if_i2c_device_t;

typedef struct {
    int count;
    bool bus_ok;
    char error[64];
    board_if_i2c_device_t devices[BOARD_IF_I2C_MAX_DEVICES];
} board_if_i2c_scan_result_t;

/** Scan the board I2C (GPIO7 SDA / GPIO8 SCL, shared with GT911 and GY-87). */
void board_if_i2c_scan(board_if_i2c_scan_result_t *out);

#ifdef __cplusplus
}
#endif
