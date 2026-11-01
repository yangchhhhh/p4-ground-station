#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "board_if_humid.h"
#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "lwip/inet.h"
#include "mqtt_client.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "board_if.humid";

static bool s_on;
static bool s_gpio_ready;
static httpd_handle_t s_httpd;
static esp_mqtt_client_handle_t s_mqtt;
static bool s_mqtt_connected;
static board_if_humid_mqtt_t s_mq = {
    .status = "MQTT off",
};

static int drive_level(bool on)
{
#if BOARD_IF_HUMID_ACTIVE_HIGH
    return on ? 1 : 0;
#else
    return on ? 0 : 1;
#endif
}

static bool sta_ip(char *buf, int buflen)
{
    if (buf == NULL || buflen < 8) {
        return false;
    }
    buf[0] = '\0';
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == NULL) {
        return false;
    }
    esp_netif_ip_info_t info;
    if (esp_netif_get_ip_info(netif, &info) != ESP_OK || info.ip.addr == 0) {
        return false;
    }
    snprintf(buf, buflen, IPSTR, IP2STR(&info.ip));
    return true;
}

static void mqtt_save(void)
{
    nvs_handle_t h;
    if (nvs_open("humid", NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_str(h, "uri", s_mq.uri);
    nvs_set_str(h, "user", s_mq.user);
    nvs_set_str(h, "pass", s_mq.pass);
    nvs_set_u8(h, "en", s_mq.enabled ? 1 : 0);
    nvs_commit(h);
    nvs_close(h);
}

static void mqtt_load(void)
{
    nvs_handle_t h;
    if (nvs_open("humid", NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    size_t n = sizeof(s_mq.uri);
    nvs_get_str(h, "uri", s_mq.uri, &n);
    n = sizeof(s_mq.user);
    nvs_get_str(h, "user", s_mq.user, &n);
    n = sizeof(s_mq.pass);
    nvs_get_str(h, "pass", s_mq.pass, &n);
    uint8_t en = 0;
    if (nvs_get_u8(h, "en", &en) == ESP_OK) {
        s_mq.enabled = en != 0;
    }
    nvs_close(h);
}

static void mqtt_publish_state(void)
{
    if (!s_mqtt || !s_mqtt_connected) {
        return;
    }
    char json[24];
    snprintf(json, sizeof(json), "{\"on\":%s}", s_on ? "true" : "false");
    esp_mqtt_client_publish(s_mqtt, BOARD_IF_HUMID_MQTT_STATE, json, 0, 1, 1);
}

static void mqtt_apply_cmd(const char *data, int len)
{
    char buf[16];
    int n = len;
    if (n >= (int)sizeof(buf)) {
        n = sizeof(buf) - 1;
    }
    memcpy(buf, data, n);
    buf[n] = '\0';
    if (strcasecmp(buf, "on") == 0 || strcmp(buf, "1") == 0) {
        board_if_humid_set(true);
    } else if (strcasecmp(buf, "off") == 0 || strcmp(buf, "0") == 0) {
        board_if_humid_set(false);
    }
}

static void mqtt_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    esp_mqtt_event_handle_t ev = data;
    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
        s_mqtt_connected = true;
        snprintf(s_mq.status, sizeof(s_mq.status), "MQTT connected");
        esp_mqtt_client_subscribe(s_mqtt, BOARD_IF_HUMID_MQTT_CMD, 1);
        mqtt_publish_state();
        break;
    case MQTT_EVENT_DISCONNECTED:
        s_mqtt_connected = false;
        snprintf(s_mq.status, sizeof(s_mq.status), "MQTT disconnected");
        break;
    case MQTT_EVENT_DATA:
        if (ev && ev->data && ev->data_len > 0) {
            mqtt_apply_cmd(ev->data, ev->data_len);
        }
        break;
    case MQTT_EVENT_ERROR:
        snprintf(s_mq.status, sizeof(s_mq.status), "MQTT error");
        break;
    default:
        break;
    }
}

static void mqtt_stop(void)
{
    s_mqtt_connected = false;
    if (s_mqtt) {
        esp_mqtt_client_stop(s_mqtt);
        esp_mqtt_client_destroy(s_mqtt);
        s_mqtt = NULL;
    }
}

static void mqtt_start(void)
{
    if (!s_mq.enabled || s_mq.uri[0] == '\0') {
        snprintf(s_mq.status, sizeof(s_mq.status), "MQTT off");
        return;
    }
    char ip[16];
    if (!sta_ip(ip, sizeof(ip))) {
        snprintf(s_mq.status, sizeof(s_mq.status), "MQTT waits for WLAN");
        return;
    }
    mqtt_stop();
    esp_mqtt_client_config_t cfg = {0};
    cfg.broker.address.uri = s_mq.uri;
    if (s_mq.user[0]) {
        cfg.credentials.username = s_mq.user;
    }
    if (s_mq.pass[0]) {
        cfg.credentials.authentication.password = s_mq.pass;
    }
    s_mqtt = esp_mqtt_client_init(&cfg);
    if (s_mqtt == NULL) {
        snprintf(s_mq.status, sizeof(s_mq.status), "MQTT init failed");
        return;
    }
    esp_mqtt_client_register_event(s_mqtt, ESP_EVENT_ANY_ID, mqtt_event, NULL);
    if (esp_mqtt_client_start(s_mqtt) != ESP_OK) {
        snprintf(s_mq.status, sizeof(s_mq.status), "MQTT start failed");
        mqtt_stop();
        return;
    }
    snprintf(s_mq.status, sizeof(s_mq.status), "MQTT connecting");
}

static esp_err_t send_page(httpd_req_t *req)
{
    char ip[16] = {0};
    sta_ip(ip, sizeof(ip));
    char page[900];
    snprintf(page, sizeof(page),
             "<!doctype html><html><head><meta name=viewport "
             "content=\"width=device-width,initial-scale=1\">"
             "<title>Humid</title>"
             "<style>body{font-family:sans-serif;text-align:center;"
             "background:#102018;color:#e8eef4;margin:24px}"
             "a{display:inline-block;margin:12px;padding:18px 36px;"
             "background:#2ac480;color:#102018;text-decoration:none;"
             "border-radius:12px;font-size:24px;font-weight:700}"
             ".off{background:#c45a2a;color:#fff}</style></head><body>"
             "<h1>Humidifier</h1>"
             "<p>SVG S on GPIO%d &mdash; 3.3V only, not 5V</p>"
             "<p style=font-size:28px>%s</p>"
             "<p><a href=/on>ON</a> <a class=off href=/off>OFF</a></p>"
             "<p>http://%s:%d</p>"
             "<p>MQTT %s / %s</p></body></html>",
             BOARD_IF_HUMID_GPIO,
             s_on ? "ON" : "OFF",
             ip[0] ? ip : "0.0.0.0",
             BOARD_IF_HUMID_HTTP_PORT,
             s_mq.enabled ? "on" : "off",
             s_mq.status);
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t handle_root(httpd_req_t *req)
{
    return send_page(req);
}

static esp_err_t handle_on(httpd_req_t *req)
{
    board_if_humid_set(true);
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t handle_off(httpd_req_t *req)
{
    board_if_humid_set(false);
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t handle_status(httpd_req_t *req)
{
    char json[48];
    snprintf(json, sizeof(json), "{\"on\":%s,\"mqtt\":%s}",
             s_on ? "true" : "false",
             s_mqtt_connected ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static void http_stop(void)
{
    if (s_httpd) {
        httpd_stop(s_httpd);
        s_httpd = NULL;
        ESP_LOGI(TAG, "HTTP :%d stopped", BOARD_IF_HUMID_HTTP_PORT);
    }
}

static void http_start(void)
{
    if (s_httpd) {
        return;
    }
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = BOARD_IF_HUMID_HTTP_PORT;
    cfg.lru_purge_enable = true;
    cfg.stack_size = 6 * 1024;
    if (httpd_start(&s_httpd, &cfg) != ESP_OK) {
        ESP_LOGW(TAG, "HTTP :%d start failed", BOARD_IF_HUMID_HTTP_PORT);
        s_httpd = NULL;
        return;
    }
    const httpd_uri_t uris[] = {
        {.uri = "/", .method = HTTP_GET, .handler = handle_root},
        {.uri = "/on", .method = HTTP_GET, .handler = handle_on},
        {.uri = "/off", .method = HTTP_GET, .handler = handle_off},
        {.uri = "/status", .method = HTTP_GET, .handler = handle_status},
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        httpd_register_uri_handler(s_httpd, &uris[i]);
    }
    char ip[16] = {0};
    sta_ip(ip, sizeof(ip));
    ESP_LOGI(TAG, "HTTP http://%s:%d  (SVG GPIO%d, 3.3V not 5V)",
             ip[0] ? ip : "0.0.0.0", BOARD_IF_HUMID_HTTP_PORT, BOARD_IF_HUMID_GPIO);
}

static void on_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        http_start();
        mqtt_start();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP) {
        mqtt_stop();
        http_stop();
    } else if (base == WIFI_EVENT &&
               (id == WIFI_EVENT_STA_DISCONNECTED || id == WIFI_EVENT_STA_STOP)) {
        mqtt_stop();
        http_stop();
    }
}

void board_if_humid_init(void)
{
    if (!s_gpio_ready) {
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << BOARD_IF_HUMID_GPIO,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_ENABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io);
        s_on = false;
        gpio_set_level(BOARD_IF_HUMID_GPIO, drive_level(false));
        s_gpio_ready = true;
        nvs_flash_init();
        mqtt_load();
        ESP_LOGI(TAG, "GPIO%d SVG S default OFF (3.3V module, not 5V)", BOARD_IF_HUMID_GPIO);
    }

    static bool events;
    if (!events) {
        events = true;
        esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &on_ip, NULL);
        esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_ip, NULL);
    }

    char ip[16];
    if (sta_ip(ip, sizeof(ip))) {
        http_start();
        mqtt_start();
    }
}

void board_if_humid_set(bool on)
{
    if (!s_gpio_ready) {
        board_if_humid_init();
    }
    s_on = on;
    gpio_set_level(BOARD_IF_HUMID_GPIO, drive_level(on));
    mqtt_publish_state();
    ESP_LOGI(TAG, "humidifier %s", on ? "ON" : "OFF");
}

bool board_if_humid_get(void)
{
    return s_on;
}

bool board_if_humid_url(char *buf, int buflen)
{
    if (buf == NULL || buflen < 16) {
        return false;
    }
    buf[0] = '\0';
    char ip[16];
    if (!sta_ip(ip, sizeof(ip))) {
        return false;
    }
    snprintf(buf, buflen, "http://%s:%d", ip, BOARD_IF_HUMID_HTTP_PORT);
    return true;
}

void board_if_humid_mqtt_get(board_if_humid_mqtt_t *out)
{
    if (out == NULL) {
        return;
    }
    *out = s_mq;
    out->connected = s_mqtt_connected;
}

void board_if_humid_mqtt_set(const char *uri, const char *user, const char *pass, bool enable)
{
    if (uri) {
        snprintf(s_mq.uri, sizeof(s_mq.uri), "%.95s", uri);
    }
    if (user) {
        snprintf(s_mq.user, sizeof(s_mq.user), "%.31s", user);
    }
    if (pass) {
        snprintf(s_mq.pass, sizeof(s_mq.pass), "%.31s", pass);
    }
    s_mq.enabled = enable;
    mqtt_save();
    if (enable) {
        mqtt_start();
    } else {
        mqtt_stop();
        snprintf(s_mq.status, sizeof(s_mq.status), "MQTT off");
    }
}
