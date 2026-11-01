#include "app_humid.hpp"

#include <cstdio>
#include <cstring>

#include "board_if.h"
#include "esp_brookesia.hpp"
#include "lvgl.h"

#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "HumidApp"
#include "esp_lib_utils.h"

#define APP_NAME "Humid"

extern const lv_image_dsc_t icon_app_humid_64;

using namespace esp_brookesia::gui;
using namespace esp_brookesia::systems;

namespace esp_brookesia::apps {

HumidApp *HumidApp::_instance = nullptr;

HumidApp *HumidApp::requestInstance(bool use_status_bar, bool use_navigation_bar)
{
    if (_instance == nullptr) {
        _instance = new HumidApp(use_status_bar, use_navigation_bar);
    }
    return _instance;
}

static systems::phone::App::Config humid_phone_config(bool use_status_bar, bool use_navigation_bar)
{
    auto cfg = systems::phone::App::Config::SIMPLE_CONSTRUCTOR(&icon_app_humid_64, use_status_bar,
                                                               use_navigation_bar);
    if (use_navigation_bar) {
        cfg.navigation_bar_visual_mode = systems::phone::NavigationBar::VisualMode::SHOW_FIXED;
        cfg.flags.enable_navigation_gesture = 0;
    }
    return cfg;
}

HumidApp::HumidApp(bool use_status_bar, bool use_navigation_bar)
    : App(systems::base::App::Config::SIMPLE_CONSTRUCTOR(APP_NAME, &icon_app_humid_64, true),
          humid_phone_config(use_status_bar, use_navigation_bar))
{
}

HumidApp::~HumidApp()
{
}

struct HumidUi {
    lv_obj_t *state;
    lv_obj_t *url;
    lv_obj_t *mqtt_st;
    lv_obj_t *uri;
    lv_obj_t *user;
    lv_obj_t *pass;
    lv_obj_t *en;
    lv_obj_t *kb;
};

static HumidUi s_ui;

static void show_kb(lv_event_t *e)
{
    lv_obj_t *ta = (lv_obj_t *)lv_event_get_target(e);
    if (s_ui.kb) {
        lv_keyboard_set_textarea(s_ui.kb, ta);
        lv_obj_remove_flag(s_ui.kb, LV_OBJ_FLAG_HIDDEN);
    }
}

static void hide_kb(lv_event_t *e)
{
    (void)e;
    if (s_ui.kb) {
        lv_obj_add_flag(s_ui.kb, LV_OBJ_FLAG_HIDDEN);
    }
}

static void refresh_labels(void)
{
    if (s_ui.state) {
        lv_label_set_text(s_ui.state, board_if_humid_get() ? "ON" : "OFF");
        lv_obj_set_style_text_color(s_ui.state,
                                    board_if_humid_get() ? lv_color_hex(0x2AC480) : lv_color_hex(0xC45A2A), 0);
    }
    if (s_ui.url) {
        char url[48];
        if (board_if_humid_url(url, sizeof(url))) {
            lv_label_set_text_fmt(s_ui.url, "LAN  %s", url);
        } else {
            lv_label_set_text(s_ui.url, "Connect WLAN in Settings for :8088");
        }
    }
    board_if_humid_mqtt_t mq;
    memset(&mq, 0, sizeof(mq));
    board_if_humid_mqtt_get(&mq);
    if (s_ui.mqtt_st) {
        lv_label_set_text(s_ui.mqtt_st, mq.status);
        lv_obj_set_style_text_color(s_ui.mqtt_st,
                                    mq.connected ? lv_color_hex(0x2AC480) : lv_color_hex(0xF5B041), 0);
    }
}

static void on_on(lv_event_t *e)
{
    (void)e;
    board_if_humid_set(true);
    refresh_labels();
}

static void on_off(lv_event_t *e)
{
    (void)e;
    board_if_humid_set(false);
    refresh_labels();
}

static void on_apply(lv_event_t *e)
{
    (void)e;
    const char *uri = s_ui.uri ? lv_textarea_get_text(s_ui.uri) : "";
    const char *user = s_ui.user ? lv_textarea_get_text(s_ui.user) : "";
    const char *pass = s_ui.pass ? lv_textarea_get_text(s_ui.pass) : "";
    bool en = s_ui.en && lv_obj_has_state(s_ui.en, LV_STATE_CHECKED);
    board_if_humid_mqtt_set(uri, user, pass, en);
    hide_kb(NULL);
    refresh_labels();
}

static void on_timer(lv_timer_t *t)
{
    (void)t;
    refresh_labels();
}

static void on_screen_delete(lv_event_t *e)
{
    lv_timer_t *timer = (lv_timer_t *)lv_event_get_user_data(e);
    if (timer) {
        lv_timer_delete(timer);
    }
    memset(&s_ui, 0, sizeof(s_ui));
}

static lv_obj_t *make_btn(lv_obj_t *parent, const char *text, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 160, 52);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    return btn;
}

static lv_obj_t *make_ta(lv_obj_t *parent, const char *placeholder, const char *text, bool pwd)
{
    lv_obj_t *ta = lv_textarea_create(parent);
    lv_obj_set_width(ta, LV_PCT(100));
    lv_obj_set_height(ta, 42);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta, placeholder);
    lv_textarea_set_password_mode(ta, pwd);
    if (text && text[0]) {
        lv_textarea_set_text(ta, text);
    }
    lv_obj_add_event_cb(ta, show_kb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(ta, hide_kb, LV_EVENT_DEFOCUSED, NULL);
    return ta;
}

bool HumidApp::run(void)
{
    ESP_UTILS_LOGD("Run");
    board_if_init();
    memset(&s_ui, 0, sizeof(s_ui));

    board_if_humid_mqtt_t mq;
    memset(&mq, 0, sizeof(mq));
    board_if_humid_mqtt_get(&mq);

    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x102018), 0);

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
    lv_obj_set_style_pad_all(root, 10, 0);
    lv_obj_set_style_pad_row(root, 8, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *title = lv_label_create(root);
    lv_label_set_text(title, "Humid");
    lv_obj_set_style_text_color(title, lv_color_hex(0x2AC480), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);

    lv_obj_t *wire = lv_label_create(root);
    lv_label_set_long_mode(wire, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(wire, LV_PCT(100));
    lv_label_set_text(wire, "SVG 3.3V (not 5V)  GPIO22 -> S");
    lv_obj_set_style_text_color(wire, lv_color_hex(0xA8B4BE), 0);
    lv_obj_set_style_text_align(wire, LV_TEXT_ALIGN_CENTER, 0);

    s_ui.state = lv_label_create(root);
    lv_obj_set_style_text_font(s_ui.state, &lv_font_montserrat_28, 0);

    lv_obj_t *row = lv_obj_create(root);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 60);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    make_btn(row, "ON", on_on);
    make_btn(row, "OFF", on_off);

    s_ui.url = lv_label_create(root);
    lv_label_set_long_mode(s_ui.url, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_ui.url, LV_PCT(100));
    lv_obj_set_style_text_color(s_ui.url, lv_color_hex(0xE8EEF4), 0);
    lv_obj_set_style_text_align(s_ui.url, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *mqtt_title = lv_label_create(root);
    lv_label_set_text(mqtt_title, "MQTT  humid/cmd  humid/state");
    lv_obj_set_style_text_color(mqtt_title, lv_color_hex(0x2AC480), 0);

    s_ui.mqtt_st = lv_label_create(root);
    lv_label_set_long_mode(s_ui.mqtt_st, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_ui.mqtt_st, LV_PCT(100));
    lv_obj_set_style_text_align(s_ui.mqtt_st, LV_TEXT_ALIGN_CENTER, 0);

    s_ui.en = lv_switch_create(root);
    if (mq.enabled) {
        lv_obj_add_state(s_ui.en, LV_STATE_CHECKED);
    }

    s_ui.uri = make_ta(root, "mqtt://broker.example.com:1883", mq.uri, false);
    s_ui.user = make_ta(root, "user (optional)", mq.user, false);
    s_ui.pass = make_ta(root, "password (optional)", mq.pass, true);

    lv_obj_t *apply = lv_button_create(root);
    lv_obj_set_size(apply, 200, 44);
    lv_obj_t *al = lv_label_create(apply);
    lv_label_set_text(al, "Apply MQTT");
    lv_obj_center(al);
    lv_obj_add_event_cb(apply, on_apply, LV_EVENT_CLICKED, NULL);

    s_ui.kb = lv_keyboard_create(screen);
    lv_obj_set_size(s_ui.kb, vis_w, vis_h / 3);
    lv_obj_add_flag(s_ui.kb, LV_OBJ_FLAG_HIDDEN);

    refresh_labels();
    lv_timer_t *timer = lv_timer_create(on_timer, 1000, NULL);
    lv_obj_add_event_cb(screen, on_screen_delete, LV_EVENT_DELETE, timer);
    return true;
}

bool HumidApp::back(void)
{
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

} // namespace esp_brookesia::apps
