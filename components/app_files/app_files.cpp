#include "app_files.hpp"

#include <cstdio>
#include <cstring>

#include "board_if.h"
#include "esp_brookesia.hpp"
#include "lvgl.h"

#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "FilesApp"
#include "esp_lib_utils.h"

#define APP_NAME "Files"

extern const lv_image_dsc_t icon_app_files_64;

using namespace esp_brookesia::gui;
using namespace esp_brookesia::systems;

namespace esp_brookesia::apps {

FilesApp *FilesApp::_instance = nullptr;

FilesApp *FilesApp::requestInstance(bool use_status_bar, bool use_navigation_bar)
{
    if (_instance == nullptr) {
        _instance = new FilesApp(use_status_bar, use_navigation_bar);
    }
    return _instance;
}

static systems::phone::App::Config files_phone_config(bool use_status_bar, bool use_navigation_bar)
{
    auto cfg = systems::phone::App::Config::SIMPLE_CONSTRUCTOR(&icon_app_files_64, use_status_bar,
                                                               use_navigation_bar);
    if (use_navigation_bar) {
        cfg.navigation_bar_visual_mode = systems::phone::NavigationBar::VisualMode::SHOW_FIXED;
        cfg.flags.enable_navigation_gesture = 0;
    }
    return cfg;
}

FilesApp::FilesApp(bool use_status_bar, bool use_navigation_bar)
    : App(systems::base::App::Config::SIMPLE_CONSTRUCTOR(APP_NAME, &icon_app_files_64, true),
          files_phone_config(use_status_bar, use_navigation_bar))
{
}

FilesApp::~FilesApp()
{
}

struct FilesUi {
    lv_obj_t *path;
    lv_obj_t *list;
    lv_obj_t *note;
    char rel[160];
};

static FilesUi s_ui;

static void go_parent(void)
{
    char *slash = strrchr(s_ui.rel, '/');
    if (slash) {
        *slash = '\0';
    } else {
        s_ui.rel[0] = '\0';
    }
}

static void refresh_list(void);

static void on_item(lv_event_t *e)
{
    const char *ud = (const char *)lv_event_get_user_data(e);
    if (ud == NULL || ud[0] == '\0' || ud[1] != ':') {
        return;
    }
    bool is_dir = (ud[0] == 'D');
    const char *name = ud + 2;
    if (!is_dir) {
        if (s_ui.note) {
            lv_label_set_text_fmt(s_ui.note, "%s", name);
        }
        return;
    }
    if (strcmp(name, "..") == 0) {
        go_parent();
        refresh_list();
        return;
    }
    char next[160];
    if (s_ui.rel[0] == '\0') {
        snprintf(next, sizeof(next), "%.159s", name);
    } else {
        snprintf(next, sizeof(next), "%.80s/%.70s", s_ui.rel, name);
    }
    snprintf(s_ui.rel, sizeof(s_ui.rel), "%s", next);
    refresh_list();
}

static void add_row(lv_obj_t *list, const char *label, const char *name, bool is_dir)
{
    lv_obj_t *btn = lv_list_add_button(list, is_dir ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE, label);
    char hold_txt[52];
    snprintf(hold_txt, sizeof(hold_txt), "%c:%.47s", is_dir ? 'D' : 'F', name);
    lv_obj_t *hold = lv_label_create(btn);
    lv_label_set_text(hold, hold_txt);
    lv_obj_add_flag(hold, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(btn, on_item, LV_EVENT_CLICKED, (void *)lv_label_get_text(hold));
}

static void refresh_list(void)
{
    if (!s_ui.list || !s_ui.path) {
        return;
    }
    lv_obj_clean(s_ui.list);
    lv_label_set_text_fmt(s_ui.path, "/sdcard%s%s", s_ui.rel[0] ? "/" : "", s_ui.rel);

    board_if_file_ent_t ents[BOARD_IF_FILES_MAX];
    int n = board_if_storage_list(s_ui.rel, ents, BOARD_IF_FILES_MAX);
    if (n < 0) {
        if (s_ui.note) {
            lv_label_set_text(s_ui.note, "Mount failed. Insert FAT32 MicroSD.");
        }
        return;
    }
    if (s_ui.rel[0]) {
        add_row(s_ui.list, "..  (up)", "..", true);
    }
    for (int i = 0; i < n; i++) {
        char line[80];
        if (ents[i].is_dir) {
            snprintf(line, sizeof(line), "%.47s/", ents[i].name);
        } else {
            snprintf(line, sizeof(line), "%.40s  %u B", ents[i].name, (unsigned)ents[i].size);
        }
        add_row(s_ui.list, line, ents[i].name, ents[i].is_dir);
    }
    if (s_ui.note) {
        lv_label_set_text_fmt(s_ui.note, "%d items  (read-only, GY-87 logs kept)", n);
    }
}

static void on_up(lv_event_t *e)
{
    (void)e;
    go_parent();
    refresh_list();
}

static void on_screen_delete(lv_event_t *e)
{
    (void)e;
    memset(&s_ui, 0, sizeof(s_ui));
}

bool FilesApp::run(void)
{
    ESP_UTILS_LOGD("Run");
    board_if_init();
    memset(&s_ui, 0, sizeof(s_ui));

    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x1A1810), 0);

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

    lv_obj_t *title = lv_label_create(root);
    lv_label_set_text(title, "Files");
    lv_obj_set_style_text_color(title, lv_color_hex(0xF5B041), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);

    s_ui.path = lv_label_create(root);
    lv_label_set_long_mode(s_ui.path, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_ui.path, LV_PCT(100));
    lv_obj_set_style_text_color(s_ui.path, lv_color_hex(0xE8EEF4), 0);

    lv_obj_t *up = lv_button_create(root);
    lv_obj_set_size(up, 120, 40);
    lv_obj_t *ul = lv_label_create(up);
    lv_label_set_text(ul, "Up");
    lv_obj_center(ul);
    lv_obj_add_event_cb(up, on_up, LV_EVENT_CLICKED, NULL);

    s_ui.list = lv_list_create(root);
    lv_obj_set_width(s_ui.list, LV_PCT(100));
    lv_obj_set_flex_grow(s_ui.list, 1);

    s_ui.note = lv_label_create(root);
    lv_label_set_long_mode(s_ui.note, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_ui.note, LV_PCT(100));
    lv_obj_set_style_text_color(s_ui.note, lv_color_hex(0xA8B4BE), 0);
    lv_obj_set_style_text_align(s_ui.note, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_add_event_cb(screen, on_screen_delete, LV_EVENT_DELETE, NULL);
    refresh_list();
    return true;
}

bool FilesApp::back(void)
{
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

} // namespace esp_brookesia::apps
