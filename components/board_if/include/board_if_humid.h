#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * SVG humidifier (3–4.5 V, NOT 5 V). Do not use the 40PIN 5V rail.
 *
 *   P4 40PIN 3.3V  -> SVG V  (and GV V if that header is used)
 *   P4 GND         -> SVG G  and GV G
 *   GPIO22         -> SVG S  (default: high = ON, low = OFF)
 *
 * LAN: http://<sta-ip>:8088  /on /off /status
 * WAN: MQTT after STA has an IP. Topics humid/cmd and humid/state.
 * Empty broker URI disables MQTT. Does not start Wi-Fi.
 */
#ifndef BOARD_IF_HUMID_GPIO
#define BOARD_IF_HUMID_GPIO 22
#endif
#ifndef BOARD_IF_HUMID_ACTIVE_HIGH
#define BOARD_IF_HUMID_ACTIVE_HIGH 1
#endif
#define BOARD_IF_HUMID_HTTP_PORT 8088
#define BOARD_IF_HUMID_MQTT_CMD   "humid/cmd"
#define BOARD_IF_HUMID_MQTT_STATE "humid/state"

typedef struct {
    bool enabled;
    bool connected;
    char uri[96];
    char user[32];
    char pass[32];
    char status[80];
} board_if_humid_mqtt_t;

void board_if_humid_init(void);
void board_if_humid_set(bool on);
bool board_if_humid_get(void);
bool board_if_humid_url(char *buf, int buflen);

void board_if_humid_mqtt_get(board_if_humid_mqtt_t *out);
void board_if_humid_mqtt_set(const char *uri, const char *user, const char *pass, bool enable);

#ifdef __cplusplus
}
#endif
