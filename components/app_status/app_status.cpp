#include "app_status.hpp"

#include <cmath>
#include <cstdint>
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
#define ESP_UTILS_LOG_TAG "GY87App"
#include "esp_lib_utils.h"

#define APP_NAME "GY-87"
#define CHART_POINTS 50

extern const lv_image_dsc_t icon_app_gy87_112;

using namespace esp_brookesia::gui;
using namespace esp_brookesia::systems;

namespace esp_brookesia::apps {

StatusApp *StatusApp::_instance = nullptr;

StatusApp *StatusApp::requestInstance(bool use_status_bar, bool use_navigation_bar)
{
    if (_instance == nullptr) {
        _instance = new StatusApp(use_status_bar, use_navigation_bar);
    }
    return _instance;
}

StatusApp::StatusApp(bool use_status_bar, bool use_navigation_bar)
    : App(APP_NAME, &icon_app_gy87_112, true, use_status_bar, use_navigation_bar)
{
}

StatusApp::~StatusApp()
{
}

struct GyUi {
    lv_obj_t *horizon;
    lv_obj_t *sky;
    lv_obj_t *ground;
    lv_obj_t *horizon_lbl;
    lv_obj_t *heading_arc;
    lv_obj_t *heading_lbl;
    lv_obj_t *alt_chart;
    lv_chart_series_t *alt_ser;
    lv_obj_t *acc_chart;
    lv_chart_series_t *acc_x;
    lv_chart_series_t *acc_y;
    lv_chart_series_t *acc_z;
    lv_obj_t *gyro_chart;
    lv_chart_series_t *gyro_x;
    lv_chart_series_t *gyro_y;
    lv_chart_series_t *gyro_z;
    lv_obj_t *note;
    lv_obj_t *rec_lbl;
    lv_obj_t *files_lbl;
    lv_obj_t *i2c_page;
    lv_obj_t *i2c_lbl;
    bool show_i2c;
};

static GyUi s_ui;

static lv_obj_t *make_btn(lv_obj_t *parent, const char *text, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, LV_SIZE_CONTENT, 40);
    lv_obj_set_style_pad_hor(btn, 12, 0);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    return btn;
}

static void on_zero_gyro(lv_event_t *e)
{
    (void)e;
    board_if_gy87_zero_gyro();
}

static void on_zero_level(lv_event_t *e)
{
    (void)e;
    board_if_gy87_zero_level();
}

static void on_zero_alt(lv_event_t *e)
{
    (void)e;
    board_if_gy87_zero_altitude();
}

static void on_zero_hdg(lv_event_t *e)
{
    (void)e;
    board_if_gy87_zero_heading();
}

static void sd_job_task(void *arg)
{
    int job = (int)(intptr_t)arg;
    if (job == 1) {
        board_if_storage_record_start();
    } else if (job == 2) {
        board_if_storage_record_stop();
    } else if (job == 3) {
        board_if_storage_record_stop();
        board_if_storage_unmount();
    }
    vTaskDelete(NULL);
}

static void on_rec(lv_event_t *e)
{
    (void)e;
    xTaskCreate(sd_job_task, "sd_rec", 6144, (void *)(intptr_t)1, 4, NULL);
}

static void on_stop_rec(lv_event_t *e)
{
    (void)e;
    xTaskCreate(sd_job_task, "sd_stop", 4096, (void *)(intptr_t)2, 4, NULL);
}

static void on_eject(lv_event_t *e)
{
    (void)e;
    xTaskCreate(sd_job_task, "sd_eject", 4096, (void *)(intptr_t)3, 4, NULL);
}

static void refresh_rec_status(void)
{
    board_if_storage_status_t st = {};
    board_if_storage_get_status(&st);
    if (s_ui.rec_lbl) {
        lv_label_set_text_fmt(s_ui.rec_lbl, "%s%s%s",
                              st.recording ? "REC  " : "",
                              st.status,
                              st.mounted ? "" : "");
    }
    if (s_ui.files_lbl) {
        if (!st.mounted) {
            lv_label_set_text(s_ui.files_lbl, "SD not mounted. Rec mounts FAT32 only.");
        } else if (st.file_count == 0) {
            lv_label_set_text(s_ui.files_lbl, "/sdcard/gy87  (empty)");
        } else {
            char buf[256];
            size_t n = snprintf(buf, sizeof(buf), "/sdcard/gy87  %d file(s)\n", st.file_count);
            int show = st.file_count < BOARD_IF_STORAGE_LOG_MAX ? st.file_count : BOARD_IF_STORAGE_LOG_MAX;
            for (int i = 0; i < show && n < sizeof(buf) - 40; ++i) {
                n += snprintf(buf + n, sizeof(buf) - n, "  %s\n", st.files[i]);
            }
            lv_label_set_text(s_ui.files_lbl, buf);
        }
    }
}

static void on_toggle_i2c(lv_event_t *e)
{
    (void)e;
    s_ui.show_i2c = !s_ui.show_i2c;
    if (s_ui.i2c_page) {
        if (s_ui.show_i2c) {
            lv_obj_remove_flag(s_ui.i2c_page, LV_OBJ_FLAG_HIDDEN);
            board_if_i2c_scan_result_t scan = {};
            board_if_i2c_scan(&scan);
            char buf[512];
            size_t n = 0;
            n += snprintf(buf + n, sizeof(buf) - n, "I2C GPIO7/SDA GPIO8/SCL\n");
            if (!scan.bus_ok) {
                n += snprintf(buf + n, sizeof(buf) - n, "%s\n", scan.error);
            } else if (scan.count == 0) {
                n += snprintf(buf + n, sizeof(buf) - n, "no devices\n");
            } else {
                for (int i = 0; i < scan.count && n < sizeof(buf) - 24; ++i) {
                    n += snprintf(buf + n, sizeof(buf) - n, "  0x%02X  %s\n",
                                  scan.devices[i].addr, scan.devices[i].name);
                }
            }
            lv_label_set_text(s_ui.i2c_lbl, buf);
        } else {
            lv_obj_add_flag(s_ui.i2c_page, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void refresh_instruments(void)
{
    board_if_gy87_sample_t s = {};
    board_if_gy87_read(&s);

    float roll = s.roll_deg;
    float pitch = s.pitch_deg;
    if (roll > 60.0f) {
        roll = 60.0f;
    }
    if (roll < -60.0f) {
        roll = -60.0f;
    }
    if (pitch > 40.0f) {
        pitch = 40.0f;
    }
    if (pitch < -40.0f) {
        pitch = -40.0f;
    }

    if (s_ui.horizon) {
        int h = lv_obj_get_height(s_ui.horizon);
        int shift = (int)(-pitch * (h / 80.0f));
        int xoff = (int)(roll * 1.2f);
        lv_obj_set_pos(s_ui.sky, -30 + xoff, -h / 2 + shift);
        lv_obj_set_pos(s_ui.ground, -30 + xoff, h / 2 + shift);
    }
    if (s_ui.horizon_lbl) {
        lv_label_set_text_fmt(s_ui.horizon_lbl, "R %+5.1f  P %+5.1f", s.roll_deg, s.pitch_deg);
    }
    if (s_ui.heading_arc) {
        float hdg = s.heading_deg;
        if (hdg < 0.0f) {
            hdg += 360.0f;
        }
        lv_arc_set_value(s_ui.heading_arc, (int32_t)hdg);
        lv_label_set_text_fmt(s_ui.heading_lbl, "HDG %+5.0f", s.heading_deg);
    }
    if (s_ui.alt_ser) {
        lv_chart_set_next_value(s_ui.alt_chart, s_ui.alt_ser, (int32_t)(s.altitude_m * 10.0f));
    }
    if (s_ui.acc_x) {
        lv_chart_set_next_value(s_ui.acc_chart, s_ui.acc_x, (int32_t)(s.ax * 100.0f));
        lv_chart_set_next_value(s_ui.acc_chart, s_ui.acc_y, (int32_t)(s.ay * 100.0f));
        lv_chart_set_next_value(s_ui.acc_chart, s_ui.acc_z, (int32_t)(s.az * 100.0f));
    }
    if (s_ui.gyro_x) {
        lv_chart_set_next_value(s_ui.gyro_chart, s_ui.gyro_x, (int32_t)(s.gx * 10.0f));
        lv_chart_set_next_value(s_ui.gyro_chart, s_ui.gyro_y, (int32_t)(s.gy * 10.0f));
        lv_chart_set_next_value(s_ui.gyro_chart, s_ui.gyro_z, (int32_t)(s.gz * 10.0f));
    }
    if (s_ui.note) {
        lv_label_set_text_fmt(s_ui.note,
                              "%s  acc %+4.2f %+4.2f %+4.2f g  gyro %+5.1f %+5.1f %+5.1f  alt %.1f m",
                              s.status, s.ax, s.ay, s.az, s.gx, s.gy, s.gz, s.altitude_m);
    }
    refresh_rec_status();
}

static void on_timer(lv_timer_t *t)
{
    (void)t;
    refresh_instruments();
}

static void on_screen_delete(lv_event_t *e)
{
    lv_timer_t *timer = (lv_timer_t *)lv_event_get_user_data(e);
    if (timer) {
        lv_timer_delete(timer);
    }
    memset(&s_ui, 0, sizeof(s_ui));
}

static lv_obj_t *make_chart(lv_obj_t *parent, int h)
{
    lv_obj_t *chart = lv_chart_create(parent);
    lv_obj_set_size(chart, LV_PCT(100), h);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, CHART_POINTS);
    lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_div_line_count(chart, 3, 0);
    lv_obj_set_style_bg_color(chart, lv_color_hex(0x121A22), 0);
    lv_obj_set_style_border_color(chart, lv_color_hex(0x2C3B4C), 0);
    lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);
    return chart;
}

bool StatusApp::run(void)
{
    ESP_UTILS_LOGD("Run");
    board_if_init();
    memset(&s_ui, 0, sizeof(s_ui));

    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101820), 0);
    lv_obj_set_style_pad_all(screen, 10, 0);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(screen, 6, 0);

    lv_obj_t *top = lv_obj_create(screen);
    lv_obj_set_size(top, LV_PCT(100), 168);
    lv_obj_set_style_bg_opa(top, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top, 0, 0);
    lv_obj_set_style_pad_all(top, 0, 0);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_ui.horizon = lv_obj_create(top);
    lv_obj_set_size(s_ui.horizon, 220, 150);
    lv_obj_set_style_radius(s_ui.horizon, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(s_ui.horizon, true, 0);
    lv_obj_set_style_border_color(s_ui.horizon, lv_color_hex(0xE8EEF4), 0);
    lv_obj_set_style_pad_all(s_ui.horizon, 0, 0);
    lv_obj_set_style_bg_color(s_ui.horizon, lv_color_hex(0x1A2A38), 0);
    s_ui.sky = lv_obj_create(s_ui.horizon);
    lv_obj_set_size(s_ui.sky, 280, 150);
    lv_obj_set_pos(s_ui.sky, -30, -75);
    lv_obj_set_style_bg_color(s_ui.sky, lv_color_hex(0x2E86C1), 0);
    lv_obj_set_style_border_width(s_ui.sky, 0, 0);
    lv_obj_set_style_radius(s_ui.sky, 0, 0);
    lv_obj_clear_flag(s_ui.sky, LV_OBJ_FLAG_SCROLLABLE);

    s_ui.ground = lv_obj_create(s_ui.horizon);
    lv_obj_set_size(s_ui.ground, 280, 150);
    lv_obj_set_pos(s_ui.ground, -30, 75);
    lv_obj_set_style_bg_color(s_ui.ground, lv_color_hex(0x9A7B4F), 0);
    lv_obj_set_style_border_width(s_ui.ground, 0, 0);
    lv_obj_set_style_radius(s_ui.ground, 0, 0);
    lv_obj_clear_flag(s_ui.ground, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *bar = lv_obj_create(s_ui.horizon);
    lv_obj_set_size(bar, 140, 2);
    lv_obj_center(bar);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0xF4D03F), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);

    lv_obj_t *compass = lv_obj_create(top);
    lv_obj_set_size(compass, 150, 150);
    lv_obj_set_style_bg_opa(compass, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(compass, 0, 0);
    lv_obj_set_style_pad_all(compass, 0, 0);

    s_ui.heading_arc = lv_arc_create(compass);
    lv_obj_set_size(s_ui.heading_arc, 130, 130);
    lv_obj_center(s_ui.heading_arc);
    lv_arc_set_rotation(s_ui.heading_arc, 270);
    lv_arc_set_bg_angles(s_ui.heading_arc, 0, 360);
    lv_arc_set_range(s_ui.heading_arc, 0, 360);
    lv_obj_remove_style(s_ui.heading_arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(s_ui.heading_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(s_ui.heading_arc, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_ui.heading_arc, 10, LV_PART_INDICATOR);

    s_ui.heading_lbl = lv_label_create(compass);
    lv_obj_center(s_ui.heading_lbl);
    lv_obj_set_style_text_color(s_ui.heading_lbl, lv_color_hex(0xE8EEF4), 0);

    s_ui.horizon_lbl = lv_label_create(screen);
    lv_obj_set_style_text_color(s_ui.horizon_lbl, lv_color_hex(0x3DB8FF), 0);

    lv_obj_t *alt_cap = lv_label_create(screen);
    lv_label_set_text(alt_cap, "altitude m x10");
    lv_obj_set_style_text_color(alt_cap, lv_color_hex(0x93A4B5), 0);
    s_ui.alt_chart = make_chart(screen, 70);
    lv_chart_set_axis_range(s_ui.alt_chart, LV_CHART_AXIS_PRIMARY_Y, -200, 200);
    s_ui.alt_ser = lv_chart_add_series(s_ui.alt_chart, lv_color_hex(0x58D68D), LV_CHART_AXIS_PRIMARY_Y);

    lv_obj_t *acc_cap = lv_label_create(screen);
    lv_label_set_text(acc_cap, "accel x/y/z  (0.01 g)");
    lv_obj_set_style_text_color(acc_cap, lv_color_hex(0x93A4B5), 0);
    s_ui.acc_chart = make_chart(screen, 90);
    lv_chart_set_axis_range(s_ui.acc_chart, LV_CHART_AXIS_PRIMARY_Y, -200, 200);
    s_ui.acc_x = lv_chart_add_series(s_ui.acc_chart, lv_color_hex(0xE74C3C), LV_CHART_AXIS_PRIMARY_Y);
    s_ui.acc_y = lv_chart_add_series(s_ui.acc_chart, lv_color_hex(0x58D68D), LV_CHART_AXIS_PRIMARY_Y);
    s_ui.acc_z = lv_chart_add_series(s_ui.acc_chart, lv_color_hex(0x5DADE2), LV_CHART_AXIS_PRIMARY_Y);

    lv_obj_t *gyro_cap = lv_label_create(screen);
    lv_label_set_text(gyro_cap, "gyro x/y/z  (0.1 dps)");
    lv_obj_set_style_text_color(gyro_cap, lv_color_hex(0x93A4B5), 0);
    s_ui.gyro_chart = make_chart(screen, 90);
    lv_chart_set_axis_range(s_ui.gyro_chart, LV_CHART_AXIS_PRIMARY_Y, -2500, 2500);
    s_ui.gyro_x = lv_chart_add_series(s_ui.gyro_chart, lv_color_hex(0xE74C3C), LV_CHART_AXIS_PRIMARY_Y);
    s_ui.gyro_y = lv_chart_add_series(s_ui.gyro_chart, lv_color_hex(0x58D68D), LV_CHART_AXIS_PRIMARY_Y);
    s_ui.gyro_z = lv_chart_add_series(s_ui.gyro_chart, lv_color_hex(0x5DADE2), LV_CHART_AXIS_PRIMARY_Y);

    lv_obj_t *row = lv_obj_create(screen);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(row, 6, 0);
    lv_obj_set_style_pad_column(row, 6, 0);
    make_btn(row, "Gyro 0", on_zero_gyro);
    make_btn(row, "Level 0", on_zero_level);
    make_btn(row, "Alt 0", on_zero_alt);
    make_btn(row, "Hdg 0", on_zero_hdg);
    make_btn(row, "I2C", on_toggle_i2c);
    make_btn(row, "Rec", on_rec);
    make_btn(row, "Stop", on_stop_rec);
    make_btn(row, "Eject", on_eject);

    s_ui.rec_lbl = lv_label_create(screen);
    lv_label_set_long_mode(s_ui.rec_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_ui.rec_lbl, LV_PCT(100));
    lv_obj_set_style_text_color(s_ui.rec_lbl, lv_color_hex(0xF5B041), 0);
    lv_label_set_text(s_ui.rec_lbl, "SD idle. Rec writes /sdcard/gy87 CSV.");

    s_ui.files_lbl = lv_label_create(screen);
    lv_label_set_long_mode(s_ui.files_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_ui.files_lbl, LV_PCT(100));
    lv_obj_set_style_text_color(s_ui.files_lbl, lv_color_hex(0x93A4B5), 0);
    lv_label_set_text(s_ui.files_lbl, "/sdcard/gy87");

    s_ui.note = lv_label_create(screen);
    lv_label_set_long_mode(s_ui.note, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_ui.note, LV_PCT(100));
    lv_obj_set_style_text_color(s_ui.note, lv_color_hex(0x93A4B5), 0);
    lv_label_set_text(s_ui.note, "Lay flat, then Gyro 0 / Level 0. Stop + Eject before pulling the card.");

    s_ui.i2c_page = lv_obj_create(screen);
    lv_obj_set_size(s_ui.i2c_page, LV_PCT(100), 140);
    lv_obj_set_style_bg_color(s_ui.i2c_page, lv_color_hex(0x17202A), 0);
    lv_obj_add_flag(s_ui.i2c_page, LV_OBJ_FLAG_HIDDEN);
    s_ui.i2c_lbl = lv_label_create(s_ui.i2c_page);
    lv_label_set_long_mode(s_ui.i2c_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_ui.i2c_lbl, LV_PCT(100));
    lv_obj_set_style_text_color(s_ui.i2c_lbl, lv_color_hex(0xE8EEF4), 0);

    refresh_instruments();
    lv_timer_t *timer = lv_timer_create(on_timer, 200, NULL);
    lv_timer_set_repeat_count(timer, -1);
    lv_obj_add_event_cb(screen, on_screen_delete, LV_EVENT_DELETE, timer);
    return true;
}

bool StatusApp::back(void)
{
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

} // namespace esp_brookesia::apps
