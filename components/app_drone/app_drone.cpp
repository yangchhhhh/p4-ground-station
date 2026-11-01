#include "app_drone.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

#include "board_if.h"
#include "esp_brookesia.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "DroneApp"
#include "esp_lib_utils.h"

#define APP_NAME "Drone"
#define STICK_SIZE 200
#define KNOB_SIZE  44

extern const lv_image_dsc_t icon_app_drone_112;

using namespace esp_brookesia::gui;
using namespace esp_brookesia::systems;

namespace esp_brookesia::apps {

DroneApp *DroneApp::_instance = nullptr;

DroneApp *DroneApp::requestInstance(bool use_status_bar, bool use_navigation_bar)
{
    if (_instance == nullptr) {
        _instance = new DroneApp(use_status_bar, use_navigation_bar);
    }
    return _instance;
}

static systems::phone::App::Config drone_phone_config(bool use_status_bar, bool use_navigation_bar)
{
    auto cfg = systems::phone::App::Config::SIMPLE_CONSTRUCTOR(&icon_app_drone_112, use_status_bar,
                                                               use_navigation_bar);
    if (use_navigation_bar) {
        cfg.navigation_bar_visual_mode = systems::phone::NavigationBar::VisualMode::SHOW_FIXED;
        cfg.flags.enable_navigation_gesture = 0;
    }
    return cfg;
}

DroneApp::DroneApp(bool use_status_bar, bool use_navigation_bar)
    : App(systems::base::App::Config::SIMPLE_CONSTRUCTOR(APP_NAME, &icon_app_drone_112, true),
          drone_phone_config(use_status_bar, use_navigation_bar))
{
}

DroneApp::~DroneApp()
{
}

struct Stick {
    lv_obj_t *base;
    lv_obj_t *knob;
    float x; /* -1..1 */
    float y; /* -1..1, up positive */
};

struct DroneUi {
    Stick left;
    Stick right;
    lv_obj_t *status;
    lv_obj_t *volt;
    uint16_t buttons;
    bool connecting;
    bool live;
};

static DroneUi s_ui;
static board_if_drone_rc_t s_rc;
static board_if_drone_status_t s_st;
static volatile bool s_io_run;
static TaskHandle_t s_io;

static void lock_no_scroll(lv_obj_t *obj)
{
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLL_CHAIN);
}

static void clamp_stick(float *x, float *y)
{
    float n = sqrtf((*x) * (*x) + (*y) * (*y));
    if (n > 1.0f) {
        *x /= n;
        *y /= n;
    }
}

static void place_knob(Stick *st)
{
    if (!st->base || !st->knob) {
        return;
    }
    int w = lv_obj_get_width(st->base);
    int k = lv_obj_get_width(st->knob);
    if (w <= 0) {
        w = STICK_SIZE;
    }
    if (k <= 0) {
        k = KNOB_SIZE;
    }
    int r = w / 2 - k / 2;
    lv_obj_set_pos(st->knob, (int)(w / 2 - k / 2 + st->x * r), (int)(w / 2 - k / 2 - st->y * r));
}

static void stick_event(lv_event_t *e)
{
    Stick *st = (Stick *)lv_event_get_user_data(e);
    lv_indev_t *indev = lv_indev_active();
    if (st == NULL || indev == NULL) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    lv_area_t a;
    lv_obj_get_coords(st->base, &a);
    float cx = (a.x1 + a.x2) / 2.0f;
    float cy = (a.y1 + a.y2) / 2.0f;
    float w = (float)(a.x2 - a.x1);
    st->x = (p.x - cx) / (w / 2.0f - 8.0f);
    st->y = (cy - p.y) / (w / 2.0f - 8.0f);
    clamp_stick(&st->x, &st->y);
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        if (st == &s_ui.right) {
            st->x = 0;
            st->y = 0;
        } else {
            st->x = 0; /* yaw springs back; throttle stays */
        }
    }
    place_knob(st);
}

static lv_obj_t *make_stick(lv_obj_t *parent, Stick *st, const char *cap)
{
    lv_obj_t *col = lv_obj_create(parent);
    lv_obj_set_size(col, STICK_SIZE + 12, STICK_SIZE + 36);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(col, 0, 0);
    lv_obj_set_style_pad_all(col, 0, 0);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lock_no_scroll(col);

    lv_obj_t *cap_l = lv_label_create(col);
    lv_label_set_text(cap_l, cap);
    lv_obj_set_style_text_color(cap_l, lv_color_hex(0x93A4B5), 0);

    st->base = lv_obj_create(col);
    lv_obj_set_size(st->base, STICK_SIZE, STICK_SIZE);
    lv_obj_set_style_radius(st->base, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(st->base, lv_color_hex(0x17202A), 0);
    lv_obj_set_style_border_color(st->base, lv_color_hex(0x3DB8FF), 0);
    lv_obj_set_style_pad_all(st->base, 0, 0);
    lv_obj_add_flag(st->base, LV_OBJ_FLAG_CLICKABLE);
    lock_no_scroll(st->base);
    lv_obj_add_event_cb(st->base, stick_event, LV_EVENT_PRESSING, st);
    lv_obj_add_event_cb(st->base, stick_event, LV_EVENT_RELEASED, st);

    st->knob = lv_obj_create(st->base);
    lv_obj_set_size(st->knob, KNOB_SIZE, KNOB_SIZE);
    lv_obj_set_style_radius(st->knob, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(st->knob, lv_color_hex(0x3DB8FF), 0);
    lv_obj_set_style_border_width(st->knob, 0, 0);
    lv_obj_remove_flag(st->knob, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(st->knob, LV_OBJ_FLAG_EVENT_BUBBLE);
    lock_no_scroll(st->knob);
    st->x = 0;
    st->y = (st == &s_ui.left) ? -1.0f : 0.0f; /* throttle starts at bottom */
    place_knob(st);
    return col;
}

static lv_obj_t *make_btn(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *ud)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_height(btn, 36);
    lv_obj_set_flex_grow(btn, 1);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, ud);
    return btn;
}

static lv_obj_t *make_btn_row(lv_obj_t *parent)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 42);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lock_no_scroll(row);
    return row;
}

static void on_connect_task(void *arg)
{
    (void)arg;
    s_ui.connecting = true;
    board_if_drone_link_connect();
    s_ui.connecting = false;
    vTaskDelete(NULL);
}

static void on_connect(lv_event_t *e)
{
    (void)e;
    if (s_ui.connecting) {
        return;
    }
    xTaskCreate(on_connect_task, "drone_ap", 6 * 1024, NULL, 5, NULL);
}

static void pulse_bit(int bit)
{
    s_ui.buttons |= (1u << bit);
}

static void on_arm(lv_event_t *e)
{
    (void)e;
    pulse_bit(0);
}
static void on_disarm(lv_event_t *e)
{
    (void)e;
    pulse_bit(1);
}
static void on_kill(lv_event_t *e)
{
    (void)e;
    pulse_bit(2);
}
static void on_stab(lv_event_t *e)
{
    (void)e;
    s_ui.buttons = (uint16_t)((s_ui.buttons & ~((1u << 6) | (1u << 7) | (1u << 8))) | (1u << 6));
}
static void on_acro(lv_event_t *e)
{
    (void)e;
    s_ui.buttons = (uint16_t)((s_ui.buttons & ~((1u << 6) | (1u << 7) | (1u << 8))) | (1u << 7));
}
static void on_alt(lv_event_t *e)
{
    (void)e;
    s_ui.buttons = (uint16_t)((s_ui.buttons & ~((1u << 6) | (1u << 7) | (1u << 8))) | (1u << 8));
}

static void io_task(void *arg)
{
    (void)arg;
    int tick = 0;
    while (s_io_run) {
        tick++;
        if ((tick % 6) == 1) {
            board_if_drone_link_get_status(&s_st);
        }
        if (s_st.connected && s_st.http_ok) {
            board_if_drone_link_send_rc(&s_rc);
            board_if_drone_link_heartbeat();
            s_rc.buttons &= (uint16_t) ~((1u << 0) | (1u << 1) | (1u << 2));
        }
        vTaskDelay(pdMS_TO_TICKS(80));
    }
    s_io = NULL;
    vTaskDelete(NULL);
}

static void on_timer(lv_timer_t *t)
{
    (void)t;
    s_rc.throttle = (s_ui.left.y + 1.0f) * 50.0f;
    s_rc.yaw = s_ui.left.x * 30.0f;
    s_rc.roll = s_ui.right.x * 30.0f;
    s_rc.pitch = s_ui.right.y * 30.0f;
    s_rc.buttons = (uint16_t)(s_rc.buttons | s_ui.buttons);
    s_ui.buttons &= (uint16_t) ~((1u << 0) | (1u << 1) | (1u << 2));
    s_ui.live = s_st.connected && s_st.http_ok;
    if (s_ui.status) {
        lv_label_set_text(s_ui.status, s_st.status[0] ? s_st.status : "waiting for drone AP");
    }
    if (s_ui.volt) {
        lv_label_set_text_fmt(s_ui.volt, "Vbat %.2f V  %s", s_st.voltage,
                              s_ui.live ? (s_st.armed ? "ARMED" : "SAFE") : "WAIT AP");
    }
}

static void on_screen_delete(lv_event_t *e)
{
    lv_timer_t *timer = (lv_timer_t *)lv_event_get_user_data(e);
    if (timer) {
        lv_timer_delete(timer);
    }
    s_io_run = false;
    memset(&s_ui, 0, sizeof(s_ui));
    board_if_net_release_sta();
}

bool DroneApp::run(void)
{
    ESP_UTILS_LOGD("Run");
    board_if_init();
    memset(&s_ui, 0, sizeof(s_ui));
    memset(&s_rc, 0, sizeof(s_rc));
    memset(&s_st, 0, sizeof(s_st));
    s_ui.left.y = -1.0f;
    if (!s_io_run) {
        s_io_run = true;
        xTaskCreate(io_task, "drone_io", 6144, NULL, 5, &s_io);
    }

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
    lv_obj_set_style_pad_all(root, 8, 0);
    lv_obj_set_style_pad_row(root, 6, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lock_no_scroll(root);

    lv_obj_t *title = lv_label_create(root);
    lv_label_set_text(title, "Drone");
    lv_obj_set_style_text_color(title, lv_color_hex(0x3DB8FF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);

    s_ui.status = lv_label_create(root);
    lv_label_set_long_mode(s_ui.status, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_ui.status, LV_PCT(100));
    lv_label_set_text(s_ui.status, "waiting for drone AP");
    lv_obj_set_style_text_color(s_ui.status, lv_color_hex(0xF5B041), 0);
    lv_obj_set_style_text_align(s_ui.status, LV_TEXT_ALIGN_CENTER, 0);

    s_ui.volt = lv_label_create(root);
    lv_obj_set_style_text_color(s_ui.volt, lv_color_hex(0xE8EEF4), 0);
    lv_label_set_text(s_ui.volt, "WAIT AP");

    lv_obj_t *connect_row = make_btn_row(root);
    make_btn(connect_row, "Connect", on_connect, NULL);

    lv_obj_t *sticks = lv_obj_create(root);
    lv_obj_set_width(sticks, LV_PCT(100));
    lv_obj_set_height(sticks, STICK_SIZE + 40);
    lv_obj_set_style_bg_opa(sticks, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sticks, 0, 0);
    lv_obj_set_style_pad_all(sticks, 0, 0);
    lv_obj_set_flex_flow(sticks, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(sticks, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lock_no_scroll(sticks);
    make_stick(sticks, &s_ui.left, "THR / YAW");
    make_stick(sticks, &s_ui.right, "ROLL / PITCH");

    lv_obj_t *row1 = make_btn_row(root);
    make_btn(row1, "ARM", on_arm, NULL);
    make_btn(row1, "DISARM", on_disarm, NULL);
    make_btn(row1, "KILL", on_kill, NULL);

    lv_obj_t *row2 = make_btn_row(root);
    make_btn(row2, "STAB", on_stab, NULL);
    make_btn(row2, "ACRO", on_acro, NULL);
    make_btn(row2, "ALT", on_alt, NULL);

    lv_obj_update_layout(root);
    place_knob(&s_ui.left);
    place_knob(&s_ui.right);

    lv_timer_t *timer = lv_timer_create(on_timer, 80, NULL);
    lv_obj_add_event_cb(screen, on_screen_delete, LV_EVENT_DELETE, timer);
    return true;
}

bool DroneApp::back(void)
{
    board_if_net_release_sta();
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

} // namespace esp_brookesia::apps
