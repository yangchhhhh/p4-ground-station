#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool present_mpu6050;   /* 0x68 on the main I2C bus */
    bool present_bmp180;    /* 0x77 on the main I2C bus */
    bool present_hmc5883l;  /* 0x1E HMC or 0x0D QMC after MPU bypass */
    bool data_valid;
    float ax, ay, az;       /* g, after level offset */
    float gx, gy, gz;       /* deg/s, after gyro bias */
    float mx, my, mz;       /* uT */
    float pressure_pa;
    float temperature_c;
    float altitude_m;       /* relative to p0 */
    float roll_deg;         /* from accel, after level zero */
    float pitch_deg;
    float heading_deg;      /* mag yaw, after heading zero */
    char status[96];
} board_if_gy87_sample_t;

void board_if_gy87_init(void);

/** Background sampler so the LVGL thread never blocks on I2C. */
void board_if_gy87_start_sampler(void);

/** Copy the last sampler result. Never delays. */
bool board_if_gy87_read(board_if_gy87_sample_t *out);

/** Average gyro at rest. Hold the board still. */
void board_if_gy87_zero_gyro(void);
/** Treat current pose as level (0 roll / 0 pitch). */
void board_if_gy87_zero_level(void);
/** Current pressure becomes 0 m. */
void board_if_gy87_zero_altitude(void);
/** Current mag heading becomes 0. */
void board_if_gy87_zero_heading(void);

#ifdef __cplusplus
}
#endif
