#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board_if_drone_link.h"
#include "board_if_net.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "board_if.drone";

static bool s_sta_ok;
static bool s_http_ok;
static bool s_armed;
static int s_mode;
static float s_voltage;
static char s_status[96] = "waiting for drone AP";
static uint16_t s_last_buttons;

static void set_status(const char *msg)
{
    snprintf(s_status, sizeof(s_status), "%s", msg);
    ESP_LOGI(TAG, "%s", msg);
}

static int http_exchange(const char *url, const char *post, char *resp, size_t resp_len)
{
    esp_http_client_config_t cfg = {
        .url = url,
        .method = post ? HTTP_METHOD_POST : HTTP_METHOD_GET,
        .timeout_ms = 1200,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        return -1;
    }
    if (post) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, post, (int)strlen(post));
    }
    esp_err_t err = esp_http_client_perform(client);
    int code = -1;
    if (err == ESP_OK) {
        code = esp_http_client_get_status_code(client);
        if (resp && resp_len > 0) {
            int n = esp_http_client_read_response(client, resp, (int)resp_len - 1);
            if (n < 0) {
                n = 0;
            }
            resp[n] = '\0';
        }
    } else if (resp && resp_len > 0) {
        snprintf(resp, resp_len, "%s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
    return code;
}

static void parse_status_json(const char *json)
{
    if (json == NULL) {
        return;
    }
    const char *v = strstr(json, "\"voltage\"");
    if (v) {
        v = strchr(v, ':');
        if (v) {
            s_voltage = strtof(v + 1, NULL);
        }
    }
    if (strstr(json, "\"arm\":1") || strstr(json, "\"armed\":true")) {
        s_armed = true;
    } else if (strstr(json, "\"arm\":0") || strstr(json, "\"armed\":false")) {
        s_armed = false;
    }
    const char *m = strstr(json, "\"m\":");
    if (m) {
        s_mode = atoi(m + 4);
    }
}

static bool post_json(const char *path, const char *body)
{
    char url[96];
    snprintf(url, sizeof(url), "%s%s", BOARD_IF_DRONE_BASE_URL, path);
    char resp[256] = { 0 };
    int code = http_exchange(url, body, resp, sizeof(resp));
    if (code == 200) {
        s_http_ok = true;
        parse_status_json(resp);
        return true;
    }
    s_http_ok = false;
    snprintf(s_status, sizeof(s_status), "HTTP %s -> %d", path, code);
    return false;
}

void board_if_drone_link_init(void)
{
}

bool board_if_drone_link_connect(void)
{
    set_status("joining XW_Drone_WiFi (drops home/hotspot)");
    s_sta_ok = false;
    s_http_ok = false;
    s_last_buttons = 0;
    if (!board_if_net_connect_sta(BOARD_IF_DRONE_AP_SSID, BOARD_IF_DRONE_AP_PASS)) {
        set_status("waiting for drone AP");
        return false;
    }
    s_sta_ok = true;

    char url[96];
    snprintf(url, sizeof(url), "%s%s", BOARD_IF_DRONE_BASE_URL, BOARD_IF_DRONE_STATUS_PATH);
    char resp[384] = { 0 };
    int code = http_exchange(url, NULL, resp, sizeof(resp));
    if (code != 200) {
        set_status("AP up, waiting HTTP :8080");
        return false;
    }
    parse_status_json(resp);
    s_http_ok = true;
    set_status("Web RC ready");
    return true;
}

bool board_if_drone_link_send_rc(const board_if_drone_rc_t *rc)
{
    if (rc == NULL || !s_sta_ok) {
        return false;
    }
    float th = rc->throttle;
    if (th < 0.0f) {
        th = 0.0f;
    }
    if (th > 100.0f) {
        th = 100.0f;
    }
    /* Frontend stick Y: -100 = 0%, +100 = 100%. */
    int th_raw = (int)(th * 2.0f - 100.0f);
    int r = (int)(rc->roll / 30.0f * 100.0f);
    int p = (int)(rc->pitch / 30.0f * 100.0f);
    int y = (int)(rc->yaw / 30.0f * 100.0f);

    char body[160];
    snprintf(body, sizeof(body), "{\"t\":1,\"th\":%d,\"r\":%d,\"p\":%d,\"y\":%d}",
             th_raw, r, p, y);
    if (!post_json(BOARD_IF_DRONE_RC_PATH, body)) {
        return false;
    }

    uint16_t prev = s_last_buttons;
    uint16_t now = rc->buttons;
    for (int i = 0; i < 16; ++i) {
        int was = (prev >> i) & 1;
        int is = (now >> i) & 1;
        if (was != is) {
            char bjson[64];
            snprintf(bjson, sizeof(bjson), "{\"t\":2,\"b\":%d,\"s\":%d}", i, is);
            post_json(BOARD_IF_DRONE_RC_PATH, bjson);
        }
    }
    s_last_buttons = now;
    return true;
}

bool board_if_drone_link_heartbeat(void)
{
    if (!s_sta_ok) {
        return false;
    }
    return post_json(BOARD_IF_DRONE_HEARTBEAT, "{\"t\":4}");
}

bool board_if_drone_link_get_status(board_if_drone_status_t *out)
{
    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->connected = s_sta_ok;
    out->http_ok = s_http_ok;
    out->armed = s_armed;
    out->mode = s_mode;
    out->voltage = s_voltage;
    snprintf(out->status, sizeof(out->status), "%s", s_status);

    if (s_sta_ok) {
        char url[96];
        snprintf(url, sizeof(url), "%s%s", BOARD_IF_DRONE_BASE_URL, BOARD_IF_DRONE_STATUS_PATH);
        char resp[384] = { 0 };
        int code = http_exchange(url, NULL, resp, sizeof(resp));
        if (code == 200) {
            parse_status_json(resp);
            s_http_ok = true;
            out->http_ok = true;
            out->armed = s_armed;
            out->mode = s_mode;
            out->voltage = s_voltage;
            snprintf(s_status, sizeof(s_status), "Web RC %.2f V", s_voltage);
            snprintf(out->status, sizeof(out->status), "%s", s_status);
            return true;
        }
        s_http_ok = false;
        snprintf(out->status, sizeof(out->status), "status HTTP %d", code);
    }
    return s_sta_ok && s_http_ok;
}
