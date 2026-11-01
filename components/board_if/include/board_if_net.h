#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BOARD_IF_NET_SCAN_MAX 16

typedef struct {
    bool connected;
    int rssi;
    char ssid[33];
    char ip[16];
    char status[96];
} board_if_net_status_t;

typedef struct {
    int count;
    int rssi[BOARD_IF_NET_SCAN_MAX];
    char ssid[BOARD_IF_NET_SCAN_MAX][33];
} board_if_net_scan_t;

void board_if_net_init(void);

/** Start ESP-Hosted STA (idempotent). Does not scan or join. */
bool board_if_net_ensure_sta(void);

/** Join ssid/password. Disconnects any previous AP. Used by Drone. */
bool board_if_net_connect_sta(const char *ssid, const char *password);

/**
 * Optional: join the last NVS-stored STA. Not used at boot — Settings WLAN owns
 * start/scan/connect. Drone Connect uses board_if_net_connect_sta instead.
 */
bool board_if_net_reconnect_stored(void);

/** Stop Drone auto-retry so Settings WLAN can scan without fighting us. */
void board_if_net_release_sta(void);

bool board_if_net_scan(board_if_net_scan_t *out);
bool board_if_net_get_status(board_if_net_status_t *out);

#ifdef __cplusplus
}
#endif
