#include "app_map.hpp"

#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <setjmp.h>
#include <sys/stat.h>

#include "board_if.h"
#include "esp_brookesia.hpp"
#include "esp_heap_caps.h"
#include "lvgl.h"
#include "png.h"

#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "MapApp"
#include "esp_lib_utils.h"

#define APP_NAME "Map"
#define TILE_Z 16
#define TILE_PX 256
#define GRID 3

extern const lv_image_dsc_t icon_app_map_64;

using namespace esp_brookesia::gui;
using namespace esp_brookesia::systems;

namespace esp_brookesia::apps {

MapApp *MapApp::_instance = nullptr;

MapApp *MapApp::requestInstance(bool use_status_bar, bool use_navigation_bar)
{
    if (_instance == nullptr) {
        _instance = new MapApp(use_status_bar, use_navigation_bar);
    }
    return _instance;
}

static systems::phone::App::Config map_phone_config(bool use_status_bar, bool use_navigation_bar)
{
    auto cfg = systems::phone::App::Config::SIMPLE_CONSTRUCTOR(&icon_app_map_64, use_status_bar,
                                                               use_navigation_bar);
    if (use_navigation_bar) {
        cfg.navigation_bar_visual_mode = systems::phone::NavigationBar::VisualMode::SHOW_FIXED;
        cfg.flags.enable_navigation_gesture = 0;
    }
    return cfg;
}

MapApp::MapApp(bool use_status_bar, bool use_navigation_bar)
    : App(systems::base::App::Config::SIMPLE_CONSTRUCTOR(APP_NAME, &icon_app_map_64, true),
          map_phone_config(use_status_bar, use_navigation_bar))
{
}

MapApp::~MapApp()
{
}

struct MapTile {
    lv_obj_t *img;
    lv_image_dsc_t dsc;
    uint8_t *pixels;
};

struct MapUi {
    lv_obj_t *wrap;
    lv_obj_t *dot;
    lv_obj_t *note;
    MapTile tile[GRID][GRID];
    int origin_x;
    int origin_y;
    float fx;
    float fy;
    int vis_w;
    int vis_h;
    int pan_x;
    int pan_y;
    lv_point_t press;
    bool down;
    bool follow;
    char ext[8];
    int last_loaded;
    char last_path[80];
};

static MapUi s_ui;

static void latlon_to_tile(float lat, float lon, int *tx, int *ty, float *fx, float *fy)
{
    const double n = (double)(1 << TILE_Z);
    double x = (lon + 180.0) / 360.0 * n;
    double lat_rad = (double)lat * 3.141592653589793 / 180.0;
    double y = (1.0 - log(tan(lat_rad) + 1.0 / cos(lat_rad)) / 3.141592653589793) / 2.0 * n;
    *tx = (int)floor(x);
    *ty = (int)floor(y);
    *fx = (float)(x - *tx);
    *fy = (float)(y - *ty);
}

static bool stat_try(char *path, int path_len, int col, int row, const char *ext)
{
    snprintf(path, path_len, "/sdcard/tiles/L16/R%08x/C%08x%s", row, col, ext);
    struct stat st;
    return stat(path, &st) == 0;
}

static bool tile_path(char *path, int path_len, int col, int row)
{
    static const char *k_ext[] = {".png", ".jpg", ".jpeg", ".PNG", ".JPG"};
    if (s_ui.ext[0]) {
        return stat_try(path, path_len, col, row, s_ui.ext);
    }
    for (size_t i = 0; i < sizeof(k_ext) / sizeof(k_ext[0]); i++) {
        if (stat_try(path, path_len, col, row, k_ext[i])) {
            snprintf(s_ui.ext, sizeof(s_ui.ext), "%s", k_ext[i]);
            return true;
        }
    }
    snprintf(path, path_len, "/sdcard/tiles/L16/R%08x/C%08x.png", row, col);
    return false;
}

static bool load_png_bgra(const char *path, uint8_t **out, int *w, int *h)
{
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return false;
    }
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop info = png ? png_create_info_struct(png) : NULL;
    if (!png || !info || setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(png ? &png : NULL, info ? &info : NULL, NULL);
        fclose(fp);
        return false;
    }
    png_init_io(png, fp);
    png_read_info(png, info);
    int width = (int)png_get_image_width(png, info);
    int height = (int)png_get_image_height(png, info);
    png_byte color = png_get_color_type(png, info);
    png_byte depth = png_get_bit_depth(png, info);
    if (depth == 16) {
        png_set_strip_16(png);
    }
    if (color == PNG_COLOR_TYPE_PALETTE) {
        png_set_palette_to_rgb(png);
    }
    if (color == PNG_COLOR_TYPE_GRAY && depth < 8) {
        png_set_expand_gray_1_2_4_to_8(png);
    }
    if (png_get_valid(png, info, PNG_INFO_tRNS)) {
        png_set_tRNS_to_alpha(png);
    }
    if (color == PNG_COLOR_TYPE_RGB || color == PNG_COLOR_TYPE_GRAY || color == PNG_COLOR_TYPE_PALETTE) {
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    }
    if (color == PNG_COLOR_TYPE_GRAY || color == PNG_COLOR_TYPE_GRAY_ALPHA) {
        png_set_gray_to_rgb(png);
    }
    png_read_update_info(png, info);
    size_t need = (size_t)width * (size_t)height * 4;
    uint8_t *buf = (uint8_t *)heap_caps_malloc(need, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buf == NULL) {
        buf = (uint8_t *)malloc(need);
    }
    if (buf == NULL) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return false;
    }
    png_bytep *rows = (png_bytep *)malloc((size_t)height * sizeof(png_bytep));
    if (rows == NULL) {
        free(buf);
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return false;
    }
    for (int y = 0; y < height; y++) {
        rows[y] = buf + (size_t)y * (size_t)width * 4;
    }
    png_read_image(png, rows);
    png_read_end(png, NULL);
    free(rows);
    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);
    for (size_t i = 0; i < need; i += 4) {
        uint8_t r = buf[i];
        uint8_t b = buf[i + 2];
        buf[i] = b;
        buf[i + 2] = r;
    }
    *out = buf;
    *w = width;
    *h = height;
    return true;
}

static void free_tile(MapTile *t)
{
    if (t->pixels) {
        heap_caps_free(t->pixels);
        t->pixels = NULL;
    }
    memset(&t->dsc, 0, sizeof(t->dsc));
}

static void place_wrap(void)
{
    if (!s_ui.wrap) {
        return;
    }
    int ox = s_ui.vis_w / 2 - (int)((1.0f + s_ui.fx) * TILE_PX) + s_ui.pan_x;
    int oy = s_ui.vis_h / 2 - 40 - (int)((1.0f + s_ui.fy) * TILE_PX) + s_ui.pan_y;
    lv_obj_set_pos(s_ui.wrap, ox, oy);
}

static void update_note(int center_x, int center_y, int loaded)
{
    if (!s_ui.note) {
        return;
    }
    const char *shown = s_ui.last_path;
    const char *prefix = "/sdcard/tiles/L16/";
    if (strncmp(shown, prefix, 18) == 0) {
        shown += 18;
    }
    if (shown[0] == '\0') {
        const char *ext = s_ui.ext[0] ? s_ui.ext : ".png";
        lv_label_set_text_fmt(s_ui.note, "L16 x=%d y=%d %d/9\nR%08x/C%08x%.7s",
                              center_x, center_y, loaded, center_y, center_x, ext);
        return;
    }
    lv_label_set_text_fmt(s_ui.note, "L16 x=%d y=%d %d/9\n%.47s", center_x, center_y, loaded, shown);
}

static int load_grid(int center_x, int center_y)
{
    s_ui.origin_x = center_x - 1;
    s_ui.origin_y = center_y - 1;
    board_if_storage_mount();
    int loaded = 0;
    s_ui.last_path[0] = '\0';
    for (int r = 0; r < GRID; r++) {
        for (int c = 0; c < GRID; c++) {
            MapTile *t = &s_ui.tile[r][c];
            free_tile(t);
            char path[128];
            int col = s_ui.origin_x + c;
            int row = s_ui.origin_y + r;
            bool found = tile_path(path, sizeof(path), col, row);
            if (r == 1 && c == 1) {
                snprintf(s_ui.last_path, sizeof(s_ui.last_path), "%.79s", path);
            }
            if (!found ||
                (strstr(s_ui.ext, "png") == NULL && strstr(s_ui.ext, "PNG") == NULL)) {
                if (t->img) {
                    lv_obj_add_flag(t->img, LV_OBJ_FLAG_HIDDEN);
                }
                continue;
            }
            uint8_t *pix = NULL;
            int w = 0;
            int h = 0;
            if (!load_png_bgra(path, &pix, &w, &h) || w <= 0 || h <= 0) {
                if (t->img) {
                    lv_obj_add_flag(t->img, LV_OBJ_FLAG_HIDDEN);
                }
                continue;
            }
            t->pixels = pix;
            t->dsc.header.cf = LV_COLOR_FORMAT_ARGB8888;
            t->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
            t->dsc.header.w = (uint32_t)w;
            t->dsc.header.h = (uint32_t)h;
            t->dsc.data_size = (uint32_t)w * (uint32_t)h * 4;
            t->dsc.data = pix;
            if (t->img) {
                lv_image_set_src(t->img, &t->dsc);
                lv_obj_remove_flag(t->img, LV_OBJ_FLAG_HIDDEN);
            }
            loaded++;
        }
    }
    s_ui.last_loaded = loaded;
    update_note(center_x, center_y, loaded);
    return loaded;
}

static bool scan_l16_coverage(int *cx, int *cy)
{
    board_if_storage_mount();
    DIR *dir = opendir("/sdcard/tiles/L16");
    if (dir == NULL) {
        return false;
    }
    int minx = INT_MAX;
    int maxx = INT_MIN;
    int miny = INT_MAX;
    int maxy = INT_MIN;
    int n = 0;
    int rows_seen = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && rows_seen < 80) {
        if (ent->d_name[0] != 'R' && ent->d_name[0] != 'r') {
            continue;
        }
        char *end = NULL;
        int row = (int)strtoul(ent->d_name + 1, &end, 16);
        if (end == ent->d_name + 1) {
            continue;
        }
        rows_seen++;
        char sub[96];
        snprintf(sub, sizeof(sub), "/sdcard/tiles/L16/%.40s", ent->d_name);
        DIR *inner = opendir(sub);
        if (inner == NULL) {
            continue;
        }
        struct dirent *f;
        int files = 0;
        while ((f = readdir(inner)) != NULL && files < 48) {
            if (f->d_name[0] != 'C' && f->d_name[0] != 'c') {
                continue;
            }
            files++;
            char full[160];
            snprintf(full, sizeof(full), "%.90s/%.47s", sub, f->d_name);
            struct stat st;
            if (stat(full, &st) != 0 || st.st_size < 512) {
                continue;
            }
            end = NULL;
            int col = (int)strtoul(f->d_name + 1, &end, 16);
            if (end == f->d_name + 1) {
                continue;
            }
            if (col < minx) {
                minx = col;
            }
            if (col > maxx) {
                maxx = col;
            }
            if (row < miny) {
                miny = row;
            }
            if (row > maxy) {
                maxy = row;
            }
            n++;
        }
        closedir(inner);
    }
    closedir(dir);
    if (n == 0 || cx == NULL || cy == NULL) {
        return false;
    }
    *cx = (minx + maxx) / 2;
    *cy = (miny + maxy) / 2;
    return true;
}

static void center_on_xy(int tx, int ty)
{
    s_ui.fx = 0.5f;
    s_ui.fy = 0.5f;
    s_ui.pan_x = 0;
    s_ui.pan_y = 0;
    load_grid(tx, ty);
    place_wrap();
}

static void center_on(float lat, float lon)
{
    int tx = 0;
    int ty = 0;
    latlon_to_tile(lat, lon, &tx, &ty, &s_ui.fx, &s_ui.fy);
    s_ui.pan_x = 0;
    s_ui.pan_y = 0;
    if (tx != s_ui.origin_x + 1 || ty != s_ui.origin_y + 1 || s_ui.tile[1][1].pixels == NULL) {
        load_grid(tx, ty);
    }
    place_wrap();
}

static void show_map(bool prefer_gps)
{
    board_if_gps_t g;
    memset(&g, 0, sizeof(g));
    board_if_gps_get(&g);
    if (prefer_gps && g.fix) {
        center_on(g.lat, g.lon);
        return;
    }
    center_on(board_if_place_lat(), board_if_place_lon());
    if (s_ui.last_loaded > 0) {
        return;
    }
    int cx = 0;
    int cy = 0;
    if (scan_l16_coverage(&cx, &cy)) {
        center_on_xy(cx, cy);
    }
}

static void on_drag(lv_event_t *e)
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
        s_ui.follow = false;
    } else if (code == LV_EVENT_PRESSING && s_ui.down) {
        s_ui.pan_x += p.x - s_ui.press.x;
        s_ui.pan_y += p.y - s_ui.press.y;
        s_ui.press = p;
        place_wrap();
    } else if (code == LV_EVENT_RELEASED) {
        s_ui.down = false;
        int shift_x = 0;
        int shift_y = 0;
        while (s_ui.pan_x <= -TILE_PX) {
            s_ui.pan_x += TILE_PX;
            shift_x++;
        }
        while (s_ui.pan_x >= TILE_PX) {
            s_ui.pan_x -= TILE_PX;
            shift_x--;
        }
        while (s_ui.pan_y <= -TILE_PX) {
            s_ui.pan_y += TILE_PX;
            shift_y++;
        }
        while (s_ui.pan_y >= TILE_PX) {
            s_ui.pan_y -= TILE_PX;
            shift_y--;
        }
        if (shift_x || shift_y) {
            load_grid(s_ui.origin_x + 1 + shift_x, s_ui.origin_y + 1 + shift_y);
        }
        place_wrap();
    }
}

static void on_follow(lv_event_t *e)
{
    (void)e;
    s_ui.follow = true;
    show_map(true);
}

static void on_timer(lv_timer_t *t)
{
    (void)t;
    if (!s_ui.follow) {
        return;
    }
    board_if_gps_t g;
    memset(&g, 0, sizeof(g));
    board_if_gps_get(&g);
    if (g.fix) {
        center_on(g.lat, g.lon);
    }
}

static void on_screen_delete(lv_event_t *e)
{
    lv_timer_t *timer = (lv_timer_t *)lv_event_get_user_data(e);
    if (timer) {
        lv_timer_delete(timer);
    }
    for (int r = 0; r < GRID; r++) {
        for (int c = 0; c < GRID; c++) {
            free_tile(&s_ui.tile[r][c]);
        }
    }
    memset(&s_ui, 0, sizeof(s_ui));
}

bool MapApp::run(void)
{
    ESP_UTILS_LOGD("Run");
    board_if_init();
    memset(&s_ui, 0, sizeof(s_ui));
    s_ui.follow = true;
    s_ui.origin_x = -9999;
    s_ui.origin_y = -9999;

    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x1A1810), 0);

    lv_area_t vis = getVisualArea();
    s_ui.vis_w = lv_area_get_width(&vis);
    s_ui.vis_h = lv_area_get_height(&vis);
    if (s_ui.vis_w <= 0) {
        s_ui.vis_w = 480;
    }
    if (s_ui.vis_h <= 0) {
        s_ui.vis_h = 720;
    }

    lv_obj_t *root = lv_obj_create(screen);
    lv_obj_set_size(root, s_ui.vis_w, s_ui.vis_h);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    s_ui.wrap = lv_obj_create(root);
    lv_obj_set_size(s_ui.wrap, TILE_PX * GRID, TILE_PX * GRID);
    lv_obj_set_style_bg_color(s_ui.wrap, lv_color_hex(0x2A2418), 0);
    lv_obj_set_style_border_width(s_ui.wrap, 0, 0);
    lv_obj_set_style_pad_all(s_ui.wrap, 0, 0);
    lv_obj_set_style_radius(s_ui.wrap, 0, 0);
    lv_obj_remove_flag(s_ui.wrap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_ui.wrap, on_drag, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_ui.wrap, on_drag, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(s_ui.wrap, on_drag, LV_EVENT_RELEASED, NULL);

    for (int r = 0; r < GRID; r++) {
        for (int c = 0; c < GRID; c++) {
            lv_obj_t *img = lv_image_create(s_ui.wrap);
            lv_obj_set_pos(img, c * TILE_PX, r * TILE_PX);
            lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
            s_ui.tile[r][c].img = img;
        }
    }

    s_ui.dot = lv_obj_create(root);
    lv_obj_set_size(s_ui.dot, 16, 16);
    lv_obj_set_style_radius(s_ui.dot, 8, 0);
    lv_obj_set_style_bg_color(s_ui.dot, lv_color_hex(0xE74C3C), 0);
    lv_obj_set_style_border_width(s_ui.dot, 2, 0);
    lv_obj_set_style_border_color(s_ui.dot, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(s_ui.dot, LV_ALIGN_CENTER, 0, -20);
    lv_obj_remove_flag(s_ui.dot, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *bar = lv_obj_create(root);
    lv_obj_set_size(bar, s_ui.vis_w, 88);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x101010), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_70, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *title = lv_label_create(bar);
    lv_label_set_text(title, "Map  L16");
    lv_obj_set_style_text_color(title, lv_color_hex(0xF5B041), 0);

    s_ui.note = lv_label_create(bar);
    lv_obj_set_style_text_color(s_ui.note, lv_color_hex(0xE8EEF4), 0);
    lv_label_set_long_mode(s_ui.note, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_ui.note, s_ui.vis_w - 16);

    lv_obj_t *btn = lv_button_create(root);
    lv_obj_set_size(btn, 120, 36);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_t *bl = lv_label_create(btn);
    lv_label_set_text(bl, "Follow");
    lv_obj_center(bl);
    lv_obj_add_event_cb(btn, on_follow, LV_EVENT_CLICKED, NULL);

    show_map(true);

    lv_timer_t *timer = lv_timer_create(on_timer, 800, NULL);
    lv_obj_add_event_cb(screen, on_screen_delete, LV_EVENT_DELETE, timer);
    return true;
}

bool MapApp::back(void)
{
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

} // namespace esp_brookesia::apps
