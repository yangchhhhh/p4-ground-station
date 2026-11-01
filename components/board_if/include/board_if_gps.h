#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * WHEELTEC G60 (ATGM336H): UART NMEA 9600 8N1, 3.3V TTL.
 * Do not plug G60 Type-C into the P4 debug Type-C (that is the flash port).
 *
 *   G60 GND  -> 40PIN GND (pin 19 or 26)
 *   G60 VIN  -> 40PIN 3.3V pin 18 (use 5V for VIN only if the module requires it)
 *   G60 TXD  -> GPIO28 (P4 UART1 RX, 40PIN pin 16)
 *   G60 RXD  -> GPIO29 (P4 UART1 TX, 40PIN pin 20)
 * PPS / REST left unconnected (macros GPIO30 / GPIO31).
 *
 * Avoid GY-87 7/8, humid 22, SDIO 14–19, backlight 26, console UART 37/38.
 */
#ifndef BOARD_IF_GPS_UART_NUM
#define BOARD_IF_GPS_UART_NUM 1
#endif
#ifndef BOARD_IF_GPS_RX_GPIO
#define BOARD_IF_GPS_RX_GPIO 28
#endif
#ifndef BOARD_IF_GPS_TX_GPIO
#define BOARD_IF_GPS_TX_GPIO 29
#endif
#ifndef BOARD_IF_GPS_BAUD
#define BOARD_IF_GPS_BAUD 9600
#endif
#ifndef BOARD_IF_GPS_PPS_GPIO
#define BOARD_IF_GPS_PPS_GPIO 30
#endif
#ifndef BOARD_IF_GPS_RST_GPIO
#define BOARD_IF_GPS_RST_GPIO 31
#endif

typedef struct {
    bool fix;
    float lat;
    float lon;
    float alt_m;
    float speed_kmh;
    float cog_deg;
    int sats;
    char utc[12];
    char status[80];
} board_if_gps_t;

void board_if_gps_init(void);
bool board_if_gps_get(board_if_gps_t *out);

#ifdef __cplusplus
}
#endif
