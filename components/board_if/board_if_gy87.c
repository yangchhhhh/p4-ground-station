#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "board_if_gy87.h"
#include "board_if_storage.h"
#include "bsp/esp-bsp.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "board_if.gy87";

#define MPU_ADDR   0x68
#define HMC_ADDR   0x1E
#define QMC_ADDR   0x0D
#define BMP_ADDR   0x77
#define I2C_TO_MS  50
#define SCL_HZ     100000

#define MPU_PWR_MGMT_1  0x6B
#define MPU_USER_CTRL   0x6A
#define MPU_INT_PIN_CFG 0x37
#define MPU_ACCEL_XOUT  0x3B

#define HMC_CFG_A  0x00
#define HMC_CFG_B  0x01
#define HMC_MODE   0x02
#define HMC_DATA   0x03

#define QMC_DATA   0x00
#define QMC_CTRL2  0x0B
#define QMC_CTRL1  0x09

#define BMP_CALIB  0xAA
#define BMP_CTRL   0xF4
#define BMP_DATA   0xF6
#define BMP_CMD_TEMP 0x2E
#define BMP_CMD_PRES 0x34 /* oss = 0 */

static i2c_master_dev_handle_t s_mpu;
static i2c_master_dev_handle_t s_hmc;
static i2c_master_dev_handle_t s_qmc;
static i2c_master_dev_handle_t s_bmp;
static bool s_ready;
static bool s_bmp_cal_ok;
static bool s_have_p0;
static float s_p0_pa;
static float s_gx0, s_gy0, s_gz0;
static float s_roll0, s_pitch0, s_heading0;
static SemaphoreHandle_t s_cache_lock;
static board_if_gy87_sample_t s_cache;
static bool s_sampler;

static int16_t ac1, ac2, ac3, b1, b2, mb, mc, md;
static uint16_t ac4, ac5, ac6;

static esp_err_t add_dev(i2c_master_bus_handle_t bus, uint8_t addr, i2c_master_dev_handle_t *out)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = SCL_HZ,
    };
    return i2c_master_bus_add_device(bus, &cfg, out);
}

static esp_err_t wr(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(dev, buf, 2, I2C_TO_MS);
}

static esp_err_t rd(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(dev, &reg, 1, data, len, I2C_TO_MS);
}

static int16_t be16(const uint8_t *p)
{
    return (int16_t)((p[0] << 8) | p[1]);
}

static uint16_t be16u(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

static bool load_bmp_cal(void)
{
    uint8_t raw[22];
    if (rd(s_bmp, BMP_CALIB, raw, sizeof(raw)) != ESP_OK) {
        return false;
    }
    ac1 = be16(raw + 0);
    ac2 = be16(raw + 2);
    ac3 = be16(raw + 4);
    ac4 = be16u(raw + 6);
    ac5 = be16u(raw + 8);
    ac6 = be16u(raw + 10);
    b1 = be16(raw + 12);
    b2 = be16(raw + 14);
    mb = be16(raw + 16);
    mc = be16(raw + 18);
    md = be16(raw + 20);
    (void)mb;
    return ac1 != 0 && ac1 != -1;
}

void board_if_gy87_init(void)
{
    if (s_ready) {
        return;
    }

    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (bus == NULL) {
        ESP_LOGW(TAG, "I2C bus not ready");
        return;
    }

    if (add_dev(bus, MPU_ADDR, &s_mpu) != ESP_OK) {
        ESP_LOGW(TAG, "MPU6050 add failed");
        return;
    }

    if (wr(s_mpu, MPU_PWR_MGMT_1, 0x00) != ESP_OK) {
        ESP_LOGW(TAG, "MPU6050 wake failed");
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    wr(s_mpu, MPU_USER_CTRL, 0x00);
    wr(s_mpu, MPU_INT_PIN_CFG, 0x02);
    vTaskDelay(pdMS_TO_TICKS(10));

    if (add_dev(bus, HMC_ADDR, &s_hmc) == ESP_OK) {
        wr(s_hmc, HMC_CFG_A, 0x70);
        wr(s_hmc, HMC_CFG_B, 0x20);
        wr(s_hmc, HMC_MODE, 0x00);
    } else {
        s_hmc = NULL;
        ESP_LOGW(TAG, "HMC5883L not on bus (bypass?)");
    }

    /* Many GY-87 boards ship QMC5883L at 0x0D instead of HMC5883L. */
    if (add_dev(bus, QMC_ADDR, &s_qmc) == ESP_OK) {
        wr(s_qmc, QMC_CTRL2, 0x01);
        wr(s_qmc, QMC_CTRL1, 0x1D);
    } else {
        s_qmc = NULL;
    }

    if (add_dev(bus, BMP_ADDR, &s_bmp) == ESP_OK) {
        s_bmp_cal_ok = load_bmp_cal();
    } else {
        s_bmp = NULL;
        ESP_LOGW(TAG, "BMP180 add failed");
    }

    s_ready = true;
}

static bool read_mpu(board_if_gy87_sample_t *out)
{
    uint8_t raw[14];
    if (s_mpu == NULL || rd(s_mpu, MPU_ACCEL_XOUT, raw, sizeof(raw)) != ESP_OK) {
        return false;
    }
    out->present_mpu6050 = true;
    out->ax = be16(raw + 0) / 16384.0f;
    out->ay = be16(raw + 2) / 16384.0f;
    out->az = be16(raw + 4) / 16384.0f;
    out->temperature_c = (be16(raw + 6) / 340.0f) + 36.53f;
    out->gx = be16(raw + 8) / 131.0f;
    out->gy = be16(raw + 10) / 131.0f;
    out->gz = be16(raw + 12) / 131.0f;
    return true;
}

static int16_t le16(const uint8_t *p)
{
    return (int16_t)((p[1] << 8) | p[0]);
}

static bool read_hmc(board_if_gy87_sample_t *out)
{
    uint8_t raw[6];
    if (s_hmc == NULL || rd(s_hmc, HMC_DATA, raw, sizeof(raw)) != ESP_OK) {
        return false;
    }
    out->present_hmc5883l = true;
    /* Gain 1.3 Ga → 0.92 mG/LSB; 1 mG = 0.1 uT */
    const float lsb_ut = 0.092f;
    out->mx = be16(raw + 0) * lsb_ut;
    out->mz = be16(raw + 2) * lsb_ut;
    out->my = be16(raw + 4) * lsb_ut;
    return true;
}

static bool read_qmc(board_if_gy87_sample_t *out)
{
    uint8_t raw[6];
    if (s_qmc == NULL || rd(s_qmc, QMC_DATA, raw, sizeof(raw)) != ESP_OK) {
        return false;
    }
    out->present_hmc5883l = true;
    /* CTRL1 0x1D: 8 Gauss range → 3000 LSB/G; 1 G = 100 uT */
    const float lsb_ut = 100.0f / 3000.0f;
    out->mx = le16(raw + 0) * lsb_ut;
    out->my = le16(raw + 2) * lsb_ut;
    out->mz = le16(raw + 4) * lsb_ut;
    return true;
}

static bool read_bmp(board_if_gy87_sample_t *out)
{
    if (s_bmp == NULL || !s_bmp_cal_ok) {
        return false;
    }

    if (wr(s_bmp, BMP_CTRL, BMP_CMD_TEMP) != ESP_OK) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
    uint8_t traw[2];
    if (rd(s_bmp, BMP_DATA, traw, 2) != ESP_OK) {
        return false;
    }
    long ut = be16u(traw);

    if (wr(s_bmp, BMP_CTRL, BMP_CMD_PRES) != ESP_OK) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
    uint8_t praw[3] = {0};
    if (rd(s_bmp, BMP_DATA, praw, 3) != ESP_OK) {
        return false;
    }
    long up = ((long)praw[0] << 16 | (long)praw[1] << 8 | praw[2]) >> 8;

    long x1 = ((ut - (long)ac6) * (long)ac5) >> 15;
    long x2 = ((long)mc << 11) / (x1 + md);
    long b5 = x1 + x2;
    out->temperature_c = (b5 + 8) / 160.0f;

    long b6 = b5 - 4000;
    x1 = (b2 * ((b6 * b6) >> 12)) >> 11;
    x2 = (ac2 * b6) >> 11;
    long x3 = x1 + x2;
    long b3 = (((ac1 * 4 + x3) << 0) + 2) >> 2;
    x1 = (ac3 * b6) >> 13;
    x2 = (b1 * ((b6 * b6) >> 12)) >> 16;
    x3 = ((x1 + x2) + 2) >> 2;
    unsigned long b4 = ((unsigned long)ac4 * (unsigned long)(x3 + 32768)) >> 15;
    unsigned long b7 = ((unsigned long)up - (unsigned long)b3) * 50000UL;
    long p;
    if (b7 < 0x80000000UL) {
        p = (long)((b7 << 1) / b4);
    } else {
        p = (long)((b7 / b4) << 1);
    }
    x1 = (p >> 8) * (p >> 8);
    x1 = (x1 * 3038) >> 16;
    x2 = (-7357 * p) >> 16;
    p = p + ((x1 + x2 + 3791) >> 4);

    out->present_bmp180 = true;
    out->pressure_pa = (float)p;
    if (!s_have_p0 && p > 0) {
        s_p0_pa = (float)p;
        s_have_p0 = true;
    }
    if (s_have_p0 && s_p0_pa > 0.0f) {
        out->altitude_m = 44330.0f * (1.0f - powf(out->pressure_pa / s_p0_pa, 0.19029495f));
    }
    return true;
}

static bool gy87_sample_now(board_if_gy87_sample_t *out)
{
    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    board_if_gy87_init();
    if (!s_ready) {
        snprintf(out->status, sizeof(out->status), "GY-87 I2C not ready");
        return false;
    }

    bool imu = read_mpu(out);
    bool mag = read_hmc(out) || read_qmc(out);
    bool baro = read_bmp(out);
    out->data_valid = imu || mag || baro;

    if (!out->data_valid) {
        snprintf(out->status, sizeof(out->status), "no GY-87 data (0x68/0x77/0x0D)");
        return false;
    }

    out->gx -= s_gx0;
    out->gy -= s_gy0;
    out->gz -= s_gz0;

    if (imu) {
        float roll = atan2f(out->ay, out->az) * (180.0f / (float)M_PI);
        float den = sqrtf(out->ay * out->ay + out->az * out->az);
        float pitch = atan2f(-out->ax, den > 0.05f ? den : 0.05f) * (180.0f / (float)M_PI);
        out->roll_deg = roll - s_roll0;
        out->pitch_deg = pitch - s_pitch0;
    }
    if (mag) {
        float hdg = atan2f(out->my, out->mx) * (180.0f / (float)M_PI);
        hdg -= s_heading0;
        while (hdg > 180.0f) {
            hdg -= 360.0f;
        }
        while (hdg < -180.0f) {
            hdg += 360.0f;
        }
        out->heading_deg = hdg;
    }

    snprintf(out->status, sizeof(out->status),
             "MPU=%d MAG=%d BMP=%d",
             out->present_mpu6050, out->present_hmc5883l, out->present_bmp180);
    return true;
}

static void sampler_task(void *arg)
{
    (void)arg;
    board_if_gy87_init();
    for (;;) {
        board_if_gy87_sample_t tmp = {};
        gy87_sample_now(&tmp);
        if (s_cache_lock) {
            xSemaphoreTake(s_cache_lock, portMAX_DELAY);
            s_cache = tmp;
            xSemaphoreGive(s_cache_lock);
        } else {
            s_cache = tmp;
        }
        board_if_storage_record_sample(&tmp);
        vTaskDelay(pdMS_TO_TICKS(120));
    }
}

void board_if_gy87_start_sampler(void)
{
    if (s_cache_lock == NULL) {
        s_cache_lock = xSemaphoreCreateMutex();
    }
    if (!s_sampler) {
        s_sampler = true;
        xTaskCreate(sampler_task, "gy87", 6144, NULL, 4, NULL);
    }
}

bool board_if_gy87_read(board_if_gy87_sample_t *out)
{
    if (out == NULL) {
        return false;
    }
    board_if_gy87_start_sampler();
    if (s_cache_lock) {
        xSemaphoreTake(s_cache_lock, portMAX_DELAY);
        *out = s_cache;
        xSemaphoreGive(s_cache_lock);
    } else {
        *out = s_cache;
    }
    return out->data_valid;
}

static bool snapshot(board_if_gy87_sample_t *tmp)
{
    memset(tmp, 0, sizeof(*tmp));
    bool imu = read_mpu(tmp);
    (void)read_hmc(tmp);
    (void)read_qmc(tmp);
    (void)read_bmp(tmp);
    return imu;
}

void board_if_gy87_zero_gyro(void)
{
    board_if_gy87_init();
    float sx = 0, sy = 0, sz = 0;
    int n = 0;
    for (int i = 0; i < 12; ++i) {
        board_if_gy87_sample_t s = {};
        if (read_mpu(&s)) {
            sx += s.gx;
            sy += s.gy;
            sz += s.gz;
            n++;
        }
        vTaskDelay(pdMS_TO_TICKS(12));
    }
    if (n > 0) {
        s_gx0 = sx / n;
        s_gy0 = sy / n;
        s_gz0 = sz / n;
        ESP_LOGI(TAG, "gyro zero %.2f %.2f %.2f", s_gx0, s_gy0, s_gz0);
    }
}

void board_if_gy87_zero_level(void)
{
    board_if_gy87_init();
    board_if_gy87_sample_t s = {};
    if (!snapshot(&s)) {
        return;
    }
    s_roll0 = atan2f(s.ay, s.az) * (180.0f / (float)M_PI);
    float den = sqrtf(s.ay * s.ay + s.az * s.az);
    s_pitch0 = atan2f(-s.ax, den > 0.05f ? den : 0.05f) * (180.0f / (float)M_PI);
    ESP_LOGI(TAG, "level zero roll=%.1f pitch=%.1f", s_roll0, s_pitch0);
}

void board_if_gy87_zero_altitude(void)
{
    s_have_p0 = false;
    s_p0_pa = 0;
    board_if_gy87_sample_t s = {};
    (void)read_bmp(&s);
}

void board_if_gy87_zero_heading(void)
{
    board_if_gy87_init();
    board_if_gy87_sample_t s = {};
    snapshot(&s);
    if (s.present_hmc5883l) {
        s_heading0 = atan2f(s.my, s.mx) * (180.0f / (float)M_PI);
        ESP_LOGI(TAG, "heading zero %.1f", s_heading0);
    }
}
