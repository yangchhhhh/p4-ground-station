#include "app_game_2048.hpp"

#include <cstdio>
#include <cstring>

#include "esp_brookesia.hpp"
#include "esp_random.h"
#include "lvgl.h"

#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "Game2048"
#include "esp_lib_utils.h"

#define APP_NAME "2048"
#define TILE 4

extern const lv_image_dsc_t icon_app_2048_64;

using namespace esp_brookesia::gui;
using namespace esp_brookesia::systems;

namespace esp_brookesia::apps {

Game2048App *Game2048App::_instance = nullptr;

Game2048App *Game2048App::requestInstance(bool use_status_bar, bool use_navigation_bar)
{
    if (_instance == nullptr) {
        _instance = new Game2048App(use_status_bar, use_navigation_bar);
    }
    return _instance;
}

static systems::phone::App::Config game_phone_config(bool use_status_bar, bool use_navigation_bar)
{
    auto cfg = systems::phone::App::Config::SIMPLE_CONSTRUCTOR(&icon_app_2048_64, use_status_bar,
                                                               use_navigation_bar);
    if (use_navigation_bar) {
        cfg.navigation_bar_visual_mode = systems::phone::NavigationBar::VisualMode::SHOW_FIXED;
        cfg.flags.enable_navigation_gesture = 0;
    }
    return cfg;
}

Game2048App::Game2048App(bool use_status_bar, bool use_navigation_bar)
    : App(systems::base::App::Config::SIMPLE_CONSTRUCTOR(APP_NAME, &icon_app_2048_64, true),
          game_phone_config(use_status_bar, use_navigation_bar))
{
}

Game2048App::~Game2048App()
{
}

struct GameUi {
    uint16_t cell[TILE][TILE];
    uint32_t score;
    lv_obj_t *score_lbl;
    lv_obj_t *note;
    lv_obj_t *tile[TILE][TILE];
    lv_point_t press;
    bool down;
};

static GameUi s_ui;

static void lock_no_scroll(lv_obj_t *obj)
{
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLL_CHAIN);
}

static lv_color_t tile_color(uint16_t v)
{
    switch (v) {
    case 0:    return lv_color_hex(0x3A3228);
    case 2:    return lv_color_hex(0xEEE4DA);
    case 4:    return lv_color_hex(0xEDE0C8);
    case 8:    return lv_color_hex(0xF2B179);
    case 16:   return lv_color_hex(0xF59563);
    case 32:   return lv_color_hex(0xF67C5F);
    case 64:   return lv_color_hex(0xF65E3B);
    case 128:  return lv_color_hex(0xEDCF72);
    case 256:  return lv_color_hex(0xEDCC61);
    case 512:  return lv_color_hex(0xEDC850);
    case 1024: return lv_color_hex(0xEDC53F);
    default:   return lv_color_hex(0xEDC22E);
    }
}

static lv_color_t tile_fg(uint16_t v)
{
    return (v <= 4) ? lv_color_hex(0x776E65) : lv_color_white();
}

static void spawn(void)
{
    int empty[TILE * TILE][2];
    int n = 0;
    for (int r = 0; r < TILE; r++) {
        for (int c = 0; c < TILE; c++) {
            if (s_ui.cell[r][c] == 0) {
                empty[n][0] = r;
                empty[n][1] = c;
                n++;
            }
        }
    }
    if (n == 0) {
        return;
    }
    int i = (int)(esp_random() % (uint32_t)n);
    s_ui.cell[empty[i][0]][empty[i][1]] = (esp_random() % 10u) == 0 ? 4 : 2;
}

static bool slide_line(uint16_t *line)
{
    uint16_t packed[TILE] = {0};
    int w = 0;
    for (int i = 0; i < TILE; i++) {
        if (line[i]) {
            packed[w++] = line[i];
        }
    }
    uint16_t out[TILE] = {0};
    int o = 0;
    for (int i = 0; i < w; ) {
        if (i + 1 < w && packed[i] == packed[i + 1]) {
            out[o] = (uint16_t)(packed[i] * 2);
            s_ui.score += out[o];
            i += 2;
        } else {
            out[o] = packed[i];
            i++;
        }
        o++;
    }
    bool moved = false;
    for (int i = 0; i < TILE; i++) {
        if (line[i] != out[i]) {
            moved = true;
        }
        line[i] = out[i];
    }
    return moved;
}

static bool move_dir(int dir)
{
    bool moved = false;
    if (dir == 0 || dir == 1) { /* left / right */
        for (int r = 0; r < TILE; r++) {
            uint16_t line[TILE];
            for (int c = 0; c < TILE; c++) {
                line[c] = (dir == 0) ? s_ui.cell[r][c] : s_ui.cell[r][TILE - 1 - c];
            }
            if (slide_line(line)) {
                moved = true;
            }
            for (int c = 0; c < TILE; c++) {
                if (dir == 0) {
                    s_ui.cell[r][c] = line[c];
                } else {
                    s_ui.cell[r][TILE - 1 - c] = line[c];
                }
            }
        }
    } else {
        for (int c = 0; c < TILE; c++) {
            uint16_t line[TILE];
            for (int r = 0; r < TILE; r++) {
                line[r] = (dir == 2) ? s_ui.cell[r][c] : s_ui.cell[TILE - 1 - r][c];
            }
            if (slide_line(line)) {
                moved = true;
            }
            for (int r = 0; r < TILE; r++) {
                if (dir == 2) {
                    s_ui.cell[r][c] = line[r];
                } else {
                    s_ui.cell[TILE - 1 - r][c] = line[r];
                }
            }
        }
    }
    return moved;
}

static bool has_moves(void)
{
    for (int r = 0; r < TILE; r++) {
        for (int c = 0; c < TILE; c++) {
            if (s_ui.cell[r][c] == 0) {
                return true;
            }
            if (c + 1 < TILE && s_ui.cell[r][c] == s_ui.cell[r][c + 1]) {
                return true;
            }
            if (r + 1 < TILE && s_ui.cell[r][c] == s_ui.cell[r + 1][c]) {
                return true;
            }
        }
    }
    return false;
}

static void paint(void)
{
    bool won = false;
    for (int r = 0; r < TILE; r++) {
        for (int c = 0; c < TILE; c++) {
            uint16_t v = s_ui.cell[r][c];
            if (v >= 2048) {
                won = true;
            }
            lv_obj_t *btn = s_ui.tile[r][c];
            if (!btn) {
                continue;
            }
            lv_obj_set_style_bg_color(btn, tile_color(v), 0);
            lv_obj_t *lbl = lv_obj_get_child(btn, 0);
            if (lbl) {
                if (v == 0) {
                    lv_label_set_text(lbl, "");
                } else {
                    lv_label_set_text_fmt(lbl, "%u", (unsigned)v);
                }
                lv_obj_set_style_text_color(lbl, tile_fg(v), 0);
            }
        }
    }
    if (s_ui.score_lbl) {
        lv_label_set_text_fmt(s_ui.score_lbl, "Score  %u", (unsigned)s_ui.score);
    }
    if (s_ui.note) {
        if (!has_moves()) {
            lv_label_set_text(s_ui.note, "Game over");
        } else if (won) {
            lv_label_set_text(s_ui.note, "2048!");
        } else {
            lv_label_set_text(s_ui.note, "Swipe or use arrows");
        }
    }
}

static void new_game(void)
{
    memset(s_ui.cell, 0, sizeof(s_ui.cell));
    s_ui.score = 0;
    spawn();
    spawn();
    paint();
}

static void do_move(int dir)
{
    if (move_dir(dir)) {
        spawn();
        paint();
    }
}

static void on_new(lv_event_t *e)
{
    (void)e;
    new_game();
}

static void on_arrow(lv_event_t *e)
{
    int dir = (int)(intptr_t)lv_event_get_user_data(e);
    do_move(dir);
}

static void on_board(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_active();
    if (indev == NULL) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    if (code == LV_EVENT_PRESSED) {
        s_ui.press = p;
        s_ui.down = true;
    } else if (code == LV_EVENT_RELEASED && s_ui.down) {
        s_ui.down = false;
        int dx = p.x - s_ui.press.x;
        int dy = p.y - s_ui.press.y;
        if (dx * dx + dy * dy < 24 * 24) {
            return;
        }
        if (dx * dx > dy * dy) {
            do_move(dx > 0 ? 1 : 0);
        } else {
            do_move(dy > 0 ? 3 : 2);
        }
    }
}

static void on_screen_delete(lv_event_t *e)
{
    (void)e;
    memset(&s_ui, 0, sizeof(s_ui));
}

static lv_obj_t *make_btn(lv_obj_t *parent, const char *text, lv_event_cb_t cb, void *ud)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_height(btn, 40);
    lv_obj_set_flex_grow(btn, 1);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, ud);
    return btn;
}

bool Game2048App::run(void)
{
    ESP_UTILS_LOGD("Run");
    memset(&s_ui, 0, sizeof(s_ui));

    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x1A1612), 0);

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
    lv_obj_set_style_pad_row(root, 8, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lock_no_scroll(root);

    lv_obj_t *title = lv_label_create(root);
    lv_label_set_text(title, "2048");
    lv_obj_set_style_text_color(title, lv_color_hex(0xEDC22E), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_22, 0);

    s_ui.score_lbl = lv_label_create(root);
    lv_obj_set_style_text_color(s_ui.score_lbl, lv_color_hex(0xE8EEF4), 0);

    int cell = (vis_w - 40) / TILE;
    if (cell > 96) {
        cell = 96;
    }
    lv_obj_t *board = lv_obj_create(root);
    lv_obj_set_size(board, cell * TILE + 16, cell * TILE + 16);
    lv_obj_set_style_bg_color(board, lv_color_hex(0x2B2218), 0);
    lv_obj_set_style_pad_all(board, 6, 0);
    lv_obj_set_style_pad_row(board, 4, 0);
    lv_obj_set_style_pad_column(board, 4, 0);
    lv_obj_set_style_border_width(board, 0, 0);
    lv_obj_set_flex_flow(board, LV_FLEX_FLOW_ROW_WRAP);
    lock_no_scroll(board);
    lv_obj_add_event_cb(board, on_board, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(board, on_board, LV_EVENT_RELEASED, NULL);

    for (int r = 0; r < TILE; r++) {
        for (int c = 0; c < TILE; c++) {
            lv_obj_t *t = lv_obj_create(board);
            lv_obj_set_size(t, cell - 2, cell - 2);
            lv_obj_set_style_radius(t, 8, 0);
            lv_obj_set_style_border_width(t, 0, 0);
            lv_obj_set_style_pad_all(t, 0, 0);
            lock_no_scroll(t);
            lv_obj_remove_flag(t, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_t *lbl = lv_label_create(t);
            lv_obj_center(lbl);
            s_ui.tile[r][c] = t;
        }
    }

    lv_obj_t *arrows = lv_obj_create(root);
    lv_obj_set_width(arrows, LV_PCT(100));
    lv_obj_set_height(arrows, 48);
    lv_obj_set_style_bg_opa(arrows, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(arrows, 0, 0);
    lv_obj_set_style_pad_all(arrows, 0, 0);
    lv_obj_set_flex_flow(arrows, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(arrows, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lock_no_scroll(arrows);
    make_btn(arrows, "<", on_arrow, (void *)(intptr_t)0);
    make_btn(arrows, "^", on_arrow, (void *)(intptr_t)2);
    make_btn(arrows, "v", on_arrow, (void *)(intptr_t)3);
    make_btn(arrows, ">", on_arrow, (void *)(intptr_t)1);

    lv_obj_t *ng = lv_button_create(root);
    lv_obj_set_size(ng, 160, 42);
    lv_obj_t *ngl = lv_label_create(ng);
    lv_label_set_text(ngl, "New game");
    lv_obj_center(ngl);
    lv_obj_add_event_cb(ng, on_new, LV_EVENT_CLICKED, NULL);

    s_ui.note = lv_label_create(root);
    lv_obj_set_style_text_color(s_ui.note, lv_color_hex(0xA8B4BE), 0);

    lv_obj_add_event_cb(screen, on_screen_delete, LV_EVENT_DELETE, NULL);
    new_game();
    return true;
}

bool Game2048App::back(void)
{
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

} // namespace esp_brookesia::apps
