#include <stdio.h>
#include <string.h>

#include "board_if_net.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "board_if.net";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static SemaphoreHandle_t s_lock;
static EventGroupHandle_t s_events;
static bool s_started;
static bool s_want_connect;
static int s_retry;
static char s_status[96] = "Wi-Fi idle";
static char s_ssid[33];
static char s_ip[16];
static bool s_connected;

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

static void set_status(const char *msg)
{
    snprintf(s_status, sizeof(s_status), "%s", msg);
    ESP_LOGI(TAG, "%s", msg);
}

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (s_want_connect) {
            esp_wifi_connect();
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        s_ip[0] = '\0';
        if (s_want_connect && s_retry < 6) {
            s_retry++;
            set_status("retry STA");
            esp_wifi_connect();
        } else {
            set_status("STA disconnected");
            if (s_events) {
                xEventGroupSetBits(s_events, WIFI_FAIL_BIT);
            }
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&event->ip_info.ip));
        s_connected = true;
        s_retry = 0;
        char buf[96];
        snprintf(buf, sizeof(buf), "got IP %s", s_ip);
        set_status(buf);
        if (s_events) {
            xEventGroupSetBits(s_events, WIFI_CONNECTED_BIT);
        }
    }
}

bool board_if_net_ensure_sta(void)
{
    lock_take();
    if (s_started) {
        lock_give();
        return true;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        snprintf(s_status, sizeof(s_status), "NVS %s", esp_err_to_name(err));
        lock_give();
        return false;
    }

    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        snprintf(s_status, sizeof(s_status), "netif %s", esp_err_to_name(err));
        lock_give();
        return false;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        snprintf(s_status, sizeof(s_status), "event loop %s", esp_err_to_name(err));
        lock_give();
        return false;
    }

    if (esp_netif_get_handle_from_ifkey("WIFI_STA_DEF") == NULL) {
        if (esp_netif_create_default_wifi_sta() == NULL) {
            set_status("create STA netif failed");
            lock_give();
            return false;
        }
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        snprintf(s_status, sizeof(s_status), "wifi init %s", esp_err_to_name(err));
        lock_give();
        return false;
    }

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL);

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        snprintf(s_status, sizeof(s_status), "set mode %s", esp_err_to_name(err));
        lock_give();
        return false;
    }

    s_started = true;
    set_status("C6 STA ready (2.4 GHz)");
    lock_give();
    return true;
}

bool board_if_net_connect_sta(const char *ssid, const char *password)
{
    if (ssid == NULL || ssid[0] == '\0') {
        return false;
    }
    if (!board_if_net_ensure_sta()) {
        return false;
    }

    lock_take();
    s_want_connect = false;
    s_connected = false;
    s_ip[0] = '\0';
    s_retry = 0;
    snprintf(s_ssid, sizeof(s_ssid), "%s", ssid);
    if (s_events) {
        xEventGroupClearBits(s_events, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    }
    lock_give();

    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(50));

    /* Drone AP must not overwrite the home SSID/password Settings stored in NVS. */
    esp_wifi_set_storage(WIFI_STORAGE_RAM);

    wifi_config_t wifi_config = { 0 };
    snprintf((char *)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), "%s", ssid);
    if (password) {
        snprintf((char *)wifi_config.sta.password, sizeof(wifi_config.sta.password), "%s", password);
    }
    wifi_config.sta.threshold.authmode = (password && password[0]) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        snprintf(s_status, sizeof(s_status), "set_config %s", esp_err_to_name(err));
        return false;
    }

    lock_take();
    s_want_connect = true;
    char buf[96];
    snprintf(buf, sizeof(buf), "connecting %s", ssid);
    set_status(buf);
    lock_give();

    err = esp_wifi_start();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        snprintf(s_status, sizeof(s_status), "wifi start %s", esp_err_to_name(err));
        return false;
    }
    err = esp_wifi_connect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
        snprintf(s_status, sizeof(s_status), "connect %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

static bool wifi_enable_from_nvs(void)
{
    nvs_handle_t h;
    if (nvs_open("storage", NVS_READONLY, &h) != ESP_OK) {
        return true;
    }
    int32_t en = 1;
    esp_err_t err = nvs_get_i32(h, "wifi_en", &en);
    nvs_close(h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return true;
    }
    return err == ESP_OK && en != 0;
}

bool board_if_net_reconnect_stored(void)
{
    if (!board_if_net_ensure_sta()) {
        return false;
    }
    if (!wifi_enable_from_nvs()) {
        set_status("Wi-Fi off in Settings");
        return false;
    }

    esp_wifi_set_storage(WIFI_STORAGE_FLASH);
    wifi_config_t cfg = { 0 };
    if (esp_wifi_get_config(WIFI_IF_STA, &cfg) != ESP_OK || cfg.sta.ssid[0] == '\0') {
        set_status("no saved SSID (use Settings WLAN once)");
        return false;
    }

    snprintf(s_ssid, sizeof(s_ssid), "%s", (const char *)cfg.sta.ssid);
    s_want_connect = true;
    s_retry = 0;

    esp_err_t err = esp_wifi_start();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        snprintf(s_status, sizeof(s_status), "wifi start %s", esp_err_to_name(err));
        return false;
    }
    err = esp_wifi_connect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_CONN) {
        snprintf(s_status, sizeof(s_status), "connect %s", esp_err_to_name(err));
        return false;
    }
    char buf[96];
    snprintf(buf, sizeof(buf), "joining saved %s", s_ssid);
    set_status(buf);
    return true;
}

void board_if_net_release_sta(void)
{
    s_want_connect = false;
    s_retry = 0;
    set_status("STA idle (use Settings WLAN)");
}

bool board_if_net_scan(board_if_net_scan_t *out)
{
    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    if (!board_if_net_ensure_sta()) {
        return false;
    }
    esp_wifi_start();
    wifi_scan_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    esp_err_t err = esp_wifi_scan_start(&cfg, true);
    if (err != ESP_OK) {
        snprintf(s_status, sizeof(s_status), "scan %s", esp_err_to_name(err));
        return false;
    }
    uint16_t n = BOARD_IF_NET_SCAN_MAX;
    wifi_ap_record_t rec[BOARD_IF_NET_SCAN_MAX] = { 0 };
    err = esp_wifi_scan_get_ap_records(&n, rec);
    if (err != ESP_OK) {
        snprintf(s_status, sizeof(s_status), "scan get %s", esp_err_to_name(err));
        return false;
    }
    out->count = n;
    for (uint16_t i = 0; i < n; ++i) {
        snprintf(out->ssid[i], sizeof(out->ssid[i]), "%s", (const char *)rec[i].ssid);
        out->rssi[i] = rec[i].rssi;
    }
    return true;
}

void board_if_net_init(void)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        s_events = xEventGroupCreate();
    }
}

bool board_if_net_get_status(board_if_net_status_t *out)
{
    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    lock_take();
    out->connected = s_connected;
    snprintf(out->ssid, sizeof(out->ssid), "%s", s_ssid);
    snprintf(out->ip, sizeof(out->ip), "%s", s_ip);
    snprintf(out->status, sizeof(out->status), "%s", s_status);
    lock_give();
    if (s_started && s_connected) {
        wifi_ap_record_t ap = { 0 };
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            out->rssi = ap.rssi;
        }
    }
    return s_connected;
}
