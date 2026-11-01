#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board_if_gps.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "board_if.gps";

static SemaphoreHandle_t s_lock;
static board_if_gps_t s_fix;
static bool s_started;

static void lock_take(void)
{
    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
}

static void lock_give(void)
{
    if (s_lock) {
        xSemaphoreGive(s_lock);
    }
}

static float nmea_deg(const char *ddmm, char hemi)
{
    if (ddmm == NULL || ddmm[0] == '\0') {
        return 0;
    }
    double v = atof(ddmm);
    int d = (int)(v / 100.0);
    double m = v - (d * 100.0);
    double deg = d + m / 60.0;
    if (hemi == 'S' || hemi == 'W') {
        deg = -deg;
    }
    return (float)deg;
}

static int split_fields(char *line, char **f, int max)
{
    int n = 0;
    f[n++] = line;
    for (char *p = line; *p && n < max; p++) {
        if (*p == '*') {
            *p = '\0';
            break;
        }
        if (*p == ',') {
            *p = '\0';
            f[n++] = p + 1;
        }
    }
    return n;
}

static void parse_gga(char *line)
{
    char *f[20];
    int n = split_fields(line, f, 20);
    if (n < 10) {
        return;
    }
    int quality = atoi(f[6]);
    lock_take();
    snprintf(s_fix.utc, sizeof(s_fix.utc), "%.8s", f[1]);
    s_fix.sats = atoi(f[7]);
    s_fix.alt_m = (float)atof(f[9]);
    if (quality > 0 && f[2][0] && f[4][0]) {
        s_fix.lat = nmea_deg(f[2], f[3][0]);
        s_fix.lon = nmea_deg(f[4], f[5][0]);
        s_fix.fix = true;
        snprintf(s_fix.status, sizeof(s_fix.status), "fix  %d sats", s_fix.sats);
    } else {
        s_fix.fix = false;
        snprintf(s_fix.status, sizeof(s_fix.status), "no fix  %d sats", s_fix.sats);
    }
    lock_give();
}

static void parse_vtg(char *line)
{
    char *f[16];
    int n = split_fields(line, f, 16);
    if (n < 8) {
        return;
    }
    lock_take();
    s_fix.cog_deg = (float)atof(f[1]);
    s_fix.speed_kmh = (float)atof(f[7]);
    lock_give();
}

static void gps_task(void *arg)
{
    (void)arg;
    uint8_t byte;
    char line[128];
    int n = 0;
    while (1) {
        int got = uart_read_bytes(BOARD_IF_GPS_UART_NUM, &byte, 1, pdMS_TO_TICKS(200));
        if (got <= 0) {
            continue;
        }
        if (byte == '\r') {
            continue;
        }
        if (byte == '\n' || n >= (int)sizeof(line) - 1) {
            line[n] = '\0';
            if (n > 6 && line[0] == '$') {
                if (strstr(line, "GGA")) {
                    parse_gga(line);
                } else if (strstr(line, "VTG")) {
                    parse_vtg(line);
                }
            }
            n = 0;
            continue;
        }
        line[n++] = (char)byte;
    }
}

void board_if_gps_init(void)
{
    if (s_started) {
        return;
    }
    s_lock = xSemaphoreCreateMutex();
    memset(&s_fix, 0, sizeof(s_fix));
    snprintf(s_fix.status, sizeof(s_fix.status), "UART1 GPIO%d RX @ %d",
             BOARD_IF_GPS_RX_GPIO, BOARD_IF_GPS_BAUD);

    uart_config_t cfg = {
        .baud_rate = BOARD_IF_GPS_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
        .flags = {0},
    };
    if (uart_driver_install(BOARD_IF_GPS_UART_NUM, 2048, 0, 0, NULL, 0) != ESP_OK) {
        ESP_LOGW(TAG, "uart install failed");
        snprintf(s_fix.status, sizeof(s_fix.status), "UART install failed");
        return;
    }
    uart_param_config(BOARD_IF_GPS_UART_NUM, &cfg);
    uart_set_pin(BOARD_IF_GPS_UART_NUM, BOARD_IF_GPS_TX_GPIO, BOARD_IF_GPS_RX_GPIO,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    s_started = true;
    xTaskCreate(gps_task, "g60", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "G60 UART1 RX GPIO%d TX GPIO%d 9600 (3.3V TTL)",
             BOARD_IF_GPS_RX_GPIO, BOARD_IF_GPS_TX_GPIO);
}

bool board_if_gps_get(board_if_gps_t *out)
{
    if (out == NULL) {
        return false;
    }
    lock_take();
    *out = s_fix;
    lock_give();
    return out->fix;
}
