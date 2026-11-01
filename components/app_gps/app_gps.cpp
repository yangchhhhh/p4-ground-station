#include "app_gps.hpp"

#include <cstdio>
#include <cstring>

#include "board_if.h"
#include "esp_brookesia.hpp"
#include "lvgl.h"

#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "GpsApp"
#include "esp_lib_utils.h"

#define APP_NAME "GPS"

extern const lv_image_dsc_t icon_app_gps_64;

using namespace esp_brookesia::gui;
using namespace esp_brookesia::systems;

namespace esp_brookesia::apps {

GpsApp *GpsApp::_instance = nullptr;

GpsApp *GpsApp::requestInstance(bool use_status_bar, bool use_navigation_bar)
{
    if (_instance == nullptr) {
        _instance = new GpsApp(use_status_bar, use_navigation_bar);
    }
    return _instance;
}

static systems::phone::App::Config gps_phone_config(bool use_status_bar, bool use_navigation_bar)
{
    auto cfg = systems::phone::App::Config::SIMPLE_CONSTRUCTOR(&icon_app_gps_64, use_status_bar,
                                                               use_navigation_bar);
    if (use_navigation_bar) {
        cfg.navigation_bar_visual_mode = systems::phone::NavigationBar::VisualMode::SHOW_FIXED;
        cfg.flags.enable_navigation_gesture = 0;
    }
    return cfg;
}

GpsApp::GpsApp(bool use_status_bar, bool use_navigation_bar)
    : App(systems::base::App::Config::SIMPLE_CONSTRUCTOR(APP_NAME, &icon_app_gps_64, true),
          gps_phone_config(use_status_bar, use_navigation_bar))
{
}

GpsApp::~GpsApp()
{
}

struct GpsUi {
    lv_obj_t *status;
    lv_obj_t *lat;
    lv_obj_t *lon;
    lv_obj_t *alt;
    lv_obj_t *sats;
    lv_obj_t *spd;
    lv_obj_t *cog;
    lv_obj_t *utc;
};

static GpsUi s_ui;

static void lock_no_scroll(lv_obj_t *obj)
{
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLL_CHAIN);
}

static lv_obj_t *line(lv_obj_t *parent)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_width(lbl, LV_PCT(100));
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xE8EEF4), 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    return lbl;
}

static void on_timer(lv_timer_t *t)
{
    (void)t;
    board_if_gps_t g;
    memset(&g, 0, sizeof(g));
    board_if_gps_get(&g);
    if (s_ui.status) {
        lv_label_set_text(s_ui.status, g.status);
        lv_obj_set_style_text_color(s_ui.status,
                                    g.fix ? lv_color_hex(0x2AC480) : lv_color_hex(0xF5B041), 0);
    }
    if (s_ui.lat) {
        lv_label_set_text_fmt(s_ui.lat, "Lat  %.6f", g.lat);
    }
    if (s_ui.lon) {
        lv_label_set_text_fmt(s_ui.lon, "Lon  %.6f", g.lon);
    }
    if (s_ui.alt) {
        lv_label_set_text_fmt(s_ui.alt, "Alt  %.1f m", g.alt_m);
    }
    if (s_ui.sats) {
        lv_label_set_text_fmt(s_ui.sats, "Sats  %d", g.sats);
    }
    if (s_ui.spd) {
        lv_label_set_text_fmt(s_ui.spd, "Speed  %.1f km/h", g.speed_kmh);
    }
    if (s_ui.cog) {
        lv_label_set_text_fmt(s_ui.cog, "Course  %.1f deg", g.cog_deg);
    }
    if (s_ui.utc) {
        lv_label_set_text_fmt(s_ui.utc, "UTC  %s", g.utc[0] ? g.utc : "--");
    }
}

static void on_screen_delete(lv_event_t *e)
{
    lv_timer_t *timer = (lv_timer_t *)lv_event_get_user_data(e);
    if (timer) {
        lv_timer_delete(timer);
    }
    memset(&s_ui, 0, sizeof(s_ui));
}

bool GpsApp::run(void)
{
    ESP_UTILS_LOGD("Run");
    board_if_init();
    memset(&s_ui, 0, sizeof(s_ui));

    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101820), 0);

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
    lv_obj_set_style_pad_row(root, 8, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lock_no_scroll(root);

    lv_obj_t *title = lv_label_create(root);
    lv_label_set_text(title, "G60 GPS");
    lv_obj_set_style_text_color(title, lv_color_hex(0x3DB8FF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);

    lv_obj_t *wire = lv_label_create(root);
    lv_label_set_long_mode(wire, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(wire, LV_PCT(100));
    lv_label_set_text(wire, "TXD->GPIO28 RX  RXD->GPIO29 TX\nVIN 3.3V TTL  not debug Type-C");
    lv_obj_set_style_text_color(wire, lv_color_hex(0xA8B4BE), 0);
    lv_obj_set_style_text_align(wire, LV_TEXT_ALIGN_CENTER, 0);

    s_ui.status = line(root);
    s_ui.lat = line(root);
    s_ui.lon = line(root);
    s_ui.alt = line(root);
    s_ui.sats = line(root);
    s_ui.spd = line(root);
    s_ui.cog = line(root);
    s_ui.utc = line(root);

    lv_timer_t *timer = lv_timer_create(on_timer, 400, NULL);
    lv_obj_add_event_cb(screen, on_screen_delete, LV_EVENT_DELETE, timer);
    on_timer(timer);
    return true;
}

bool GpsApp::back(void)
{
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

} // namespace esp_brookesia::apps
