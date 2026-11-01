#include "app_weather.hpp"

#include <cstdio>
#include <cstring>

#include "board_if.h"
#include "cJSON.h"
#include "esp_brookesia.hpp"
#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "WeatherApp"
#include "esp_lib_utils.h"

#define APP_NAME "Weather"

extern const lv_image_dsc_t icon_app_weather_64;

using namespace esp_brookesia::gui;
using namespace esp_brookesia::systems;

namespace esp_brookesia::apps {

WeatherApp *WeatherApp::_instance = nullptr;

WeatherApp *WeatherApp::requestInstance(bool use_status_bar, bool use_navigation_bar)
{
    if (_instance == nullptr) {
        _instance = new WeatherApp(use_status_bar, use_navigation_bar);
    }
    return _instance;
}

static systems::phone::App::Config weather_phone_config(bool use_status_bar, bool use_navigation_bar)
{
    auto cfg = systems::phone::App::Config::SIMPLE_CONSTRUCTOR(&icon_app_weather_64, use_status_bar,
                                                               use_navigation_bar);
    if (use_navigation_bar) {
        cfg.navigation_bar_visual_mode = systems::phone::NavigationBar::VisualMode::SHOW_FIXED;
        cfg.flags.enable_navigation_gesture = 0;
    }
    return cfg;
}

WeatherApp::WeatherApp(bool use_status_bar, bool use_navigation_bar)
    : App(systems::base::App::Config::SIMPLE_CONSTRUCTOR(APP_NAME, &icon_app_weather_64, true),
          weather_phone_config(use_status_bar, use_navigation_bar))
{
}

WeatherApp::~WeatherApp()
{
}

struct WeatherData {
    bool ok;
    float temp_c;
    float wind_kmh;
    int code;
    char summary[48];
    char status[96];
};

struct WeatherUi {
    lv_obj_t *title;
    lv_obj_t *temp;
    lv_obj_t *desc;
    lv_obj_t *wind;
    lv_obj_t *note;
    bool fetching;
};

static WeatherUi s_ui;
static WeatherData s_data;
static volatile bool s_alive;

static void lock_no_scroll(lv_obj_t *obj)
{
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLL_CHAIN);
}

static const char *wmo_text(int code)
{
    if (code == 0) {
        return "Clear";
    }
    if (code <= 3) {
        return "Cloudy";
    }
    if (code == 45 || code == 48) {
        return "Fog";
    }
    if (code >= 51 && code <= 67) {
        return "Rain / drizzle";
    }
    if (code >= 71 && code <= 77) {
        return "Snow";
    }
    if (code >= 80 && code <= 82) {
        return "Showers";
    }
    if (code >= 95) {
        return "Thunderstorm";
    }
    return "Unknown";
}

static int http_on_data(esp_http_client_event_t *evt)
{
    if (evt->event_id != HTTP_EVENT_ON_DATA || evt->user_data == NULL) {
        return 0;
    }
    char *buf = (char *)evt->user_data;
    int used = (int)strlen(buf);
    int room = 1023 - used;
    if (room <= 0) {
        return 0;
    }
    int n = evt->data_len;
    if (n > room) {
        n = room;
    }
    memcpy(buf + used, evt->data, n);
    buf[used + n] = '\0';
    return 0;
}

static void fetch_weather(WeatherData *out)
{
    memset(out, 0, sizeof(*out));
    char url_chk[48];
    board_if_net_status_t net;
    memset(&net, 0, sizeof(net));
    board_if_net_get_status(&net);
    if (!board_if_humid_url(url_chk, sizeof(url_chk)) && !net.connected) {
        snprintf(out->status, sizeof(out->status), "Connect WLAN in Settings first");
        return;
    }

    char url[192];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
             "&current=temperature_2m,weather_code,wind_speed_10m",
             board_if_place_lat(), board_if_place_lon());

    char body[1024];
    body[0] = '\0';
    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.timeout_ms = 8000;
    cfg.event_handler = http_on_data;
    cfg.user_data = body;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.disable_auto_redirect = false;
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    if (cli == NULL) {
        snprintf(out->status, sizeof(out->status), "HTTP init failed");
        return;
    }
    esp_err_t err = esp_http_client_perform(cli);
    int status = esp_http_client_get_status_code(cli);
    esp_http_client_cleanup(cli);
    if (err != ESP_OK || status != 200) {
        snprintf(out->status, sizeof(out->status), "Fetch failed (%s / %d)", esp_err_to_name(err), status);
        return;
    }

    cJSON *root = cJSON_Parse(body);
    if (root == NULL) {
        snprintf(out->status, sizeof(out->status), "Bad JSON");
        return;
    }
    cJSON *cur = cJSON_GetObjectItem(root, "current");
    cJSON *t = cur ? cJSON_GetObjectItem(cur, "temperature_2m") : NULL;
    cJSON *c = cur ? cJSON_GetObjectItem(cur, "weather_code") : NULL;
    cJSON *w = cur ? cJSON_GetObjectItem(cur, "wind_speed_10m") : NULL;
    if (!cJSON_IsNumber(t) || !cJSON_IsNumber(c)) {
        cJSON_Delete(root);
        snprintf(out->status, sizeof(out->status), "Missing fields");
        return;
    }
    out->temp_c = (float)t->valuedouble;
    out->code = c->valueint;
    out->wind_kmh = cJSON_IsNumber(w) ? (float)w->valuedouble : 0;
    snprintf(out->summary, sizeof(out->summary), "%s", wmo_text(out->code));
    snprintf(out->status, sizeof(out->status), "Open-Meteo  %s", board_if_place_name());
    out->ok = true;
    cJSON_Delete(root);
}

static void fetch_task(void *arg)
{
    (void)arg;
    WeatherData tmp;
    fetch_weather(&tmp);
    s_data = tmp;
    s_ui.fetching = false;
    vTaskDelete(NULL);
}

static void start_fetch(void)
{
    if (s_ui.fetching) {
        return;
    }
    s_ui.fetching = true;
    snprintf(s_data.status, sizeof(s_data.status), "Fetching...");
    s_data.ok = false;
    xTaskCreate(fetch_task, "wx", 8 * 1024, NULL, 5, NULL);
}

static void on_refresh(lv_event_t *e)
{
    (void)e;
    start_fetch();
}

static void on_city(lv_event_t *e)
{
    int id = (int)(intptr_t)lv_event_get_user_data(e);
    board_if_place_set(id);
    if (s_ui.title) {
        lv_label_set_text(s_ui.title, board_if_place_name());
    }
    start_fetch();
}

static void on_timer(lv_timer_t *t)
{
    (void)t;
    if (!s_alive) {
        return;
    }
    if (s_ui.title) {
        lv_label_set_text(s_ui.title, board_if_place_name());
    }
    if (s_ui.temp) {
        if (s_data.ok) {
            lv_label_set_text_fmt(s_ui.temp, "%.0f C", s_data.temp_c);
        } else {
            lv_label_set_text(s_ui.temp, "-- C");
        }
    }
    if (s_ui.desc) {
        lv_label_set_text(s_ui.desc, s_data.ok ? s_data.summary : "No data");
    }
    if (s_ui.wind) {
        if (s_data.ok) {
            lv_label_set_text_fmt(s_ui.wind, "Wind %.0f km/h", s_data.wind_kmh);
        } else {
            lv_label_set_text(s_ui.wind, " ");
        }
    }
    if (s_ui.note) {
        lv_label_set_text(s_ui.note, s_data.status[0] ? s_data.status : " ");
    }
}

static void on_screen_delete(lv_event_t *e)
{
    lv_timer_t *timer = (lv_timer_t *)lv_event_get_user_data(e);
    if (timer) {
        lv_timer_delete(timer);
    }
    s_alive = false;
    memset(&s_ui, 0, sizeof(s_ui));
}

static lv_obj_t *make_btn(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *ud)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_height(btn, 42);
    lv_obj_set_flex_grow(btn, 1);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, ud);
    return btn;
}

bool WeatherApp::run(void)
{
    ESP_UTILS_LOGD("Run");
    board_if_init();
    memset(&s_ui, 0, sizeof(s_ui));
    s_alive = true;

    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x142033), 0);

    lv_area_t vis = getVisualArea();
    int vis_w = lv_area_get_width(&vis);
    int vis_h = lv_area_get_height(&vis);
    if (vis_w <= 0) {
        vis_w = 480;
    }
    if (vis_h <= 0) {
        vis_h = 720;
    }

    lv_obj_t *root = lv_obj_create(screen);
    lv_obj_set_size(root, vis_w, vis_h);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 12, 0);
    lv_obj_set_style_pad_row(root, 10, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lock_no_scroll(root);

    s_ui.title = lv_label_create(root);
    lv_obj_set_style_text_color(s_ui.title, lv_color_hex(0x3D8CFF), 0);
    lv_obj_set_style_text_font(s_ui.title, &lv_font_montserrat_22, 0);
    lv_label_set_text(s_ui.title, board_if_place_name());

    lv_obj_t *cities = lv_obj_create(root);
    lv_obj_set_width(cities, LV_PCT(100));
    lv_obj_set_height(cities, 50);
    lv_obj_set_style_bg_opa(cities, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cities, 0, 0);
    lv_obj_set_style_pad_all(cities, 0, 0);
    lv_obj_set_flex_flow(cities, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cities, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lock_no_scroll(cities);
    for (int i = 0; i < board_if_place_count(); i++) {
        make_btn(cities, board_if_place_at(i)->name, on_city, (void *)(intptr_t)i);
    }

    s_ui.temp = lv_label_create(root);
    lv_obj_set_style_text_color(s_ui.temp, lv_color_hex(0xE8EEF4), 0);
    lv_obj_set_style_text_font(s_ui.temp, &lv_font_montserrat_28, 0);
    lv_label_set_text(s_ui.temp, "-- C");

    s_ui.desc = lv_label_create(root);
    lv_obj_set_style_text_color(s_ui.desc, lv_color_hex(0xF5B041), 0);
    lv_label_set_text(s_ui.desc, " ");

    s_ui.wind = lv_label_create(root);
    lv_obj_set_style_text_color(s_ui.wind, lv_color_hex(0xA8B4BE), 0);
    lv_label_set_text(s_ui.wind, " ");

    lv_obj_t *btn = lv_button_create(root);
    lv_obj_set_size(btn, 180, 48);
    lv_obj_t *bl = lv_label_create(btn);
    lv_label_set_text(bl, "Refresh");
    lv_obj_center(bl);
    lv_obj_add_event_cb(btn, on_refresh, LV_EVENT_CLICKED, NULL);

    s_ui.note = lv_label_create(root);
    lv_label_set_long_mode(s_ui.note, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_ui.note, LV_PCT(100));
    lv_obj_set_style_text_color(s_ui.note, lv_color_hex(0xA8B4BE), 0);
    lv_obj_set_style_text_align(s_ui.note, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_ui.note, "Uses Settings WLAN. Does not start Wi-Fi.");

    lv_timer_t *timer = lv_timer_create(on_timer, 300, NULL);
    lv_obj_add_event_cb(screen, on_screen_delete, LV_EVENT_DELETE, timer);
    start_fetch();
    return true;
}

bool WeatherApp::back(void)
{
    s_alive = false;
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

} // namespace esp_brookesia::apps
