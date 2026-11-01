#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Existing XW-Drone Web RC. Do not invent a new protocol. */
#define BOARD_IF_DRONE_AP_SSID     "XW_Drone_WiFi"
#define BOARD_IF_DRONE_AP_PASS     "12345678"
#define BOARD_IF_DRONE_BASE_URL    "http://192.168.4.1:8080"
#define BOARD_IF_DRONE_RC_PATH     "/web_rc"
#define BOARD_IF_DRONE_HEARTBEAT   "/web_rc/heartbeat"
#define BOARD_IF_DRONE_STATUS_PATH "/web_rc/status"

typedef struct {
    float throttle; /* 0..100 percent */
    float roll;     /* degrees, ±30 at full stick */
    float pitch;
    float yaw;
    uint16_t buttons; /* bit0 arm, bit1 disarm, bit2 kill, bit6 STAB, bit7 ACRO, bit8 ALTHOLD */
} board_if_drone_rc_t;

typedef struct {
    bool connected;
    bool http_ok;
    bool armed;
    int mode;
    float voltage;
    char status[96];
} board_if_drone_status_t;

void board_if_drone_link_init(void);

/** STA to XW_Drone_WiFi, then GET /web_rc/status. Drops home/hotspot (C6 is one STA). */
bool board_if_drone_link_connect(void);

/** POST sticks as Web RC t:1 (throttle mapped -100..100, attitude ±100). */
bool board_if_drone_link_send_rc(const board_if_drone_rc_t *rc);

bool board_if_drone_link_heartbeat(void);
bool board_if_drone_link_get_status(board_if_drone_status_t *out);

#ifdef __cplusplus
}
#endif
