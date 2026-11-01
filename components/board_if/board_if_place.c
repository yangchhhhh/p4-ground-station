#include "board_if_place.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "board_if.place";

static const board_if_place_t k_places[BOARD_IF_PLACE_COUNT] = {
    {"Beijing", 39.9042f, 116.4074f},
    {"Hangzhou", 30.2741f, 120.1551f},
    {"Kunming", 25.0389f, 102.7183f},
};

static int s_id;

static void nvs_load(void)
{
    nvs_handle_t h;
    if (nvs_open("place", NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    uint8_t v = 0;
    if (nvs_get_u8(h, "id", &v) == ESP_OK && v < BOARD_IF_PLACE_COUNT) {
        s_id = v;
    }
    nvs_close(h);
}

static void nvs_save(void)
{
    nvs_handle_t h;
    if (nvs_open("place", NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    nvs_set_u8(h, "id", (uint8_t)s_id);
    nvs_commit(h);
    nvs_close(h);
}

void board_if_place_init(void)
{
    static bool once;
    if (once) {
        return;
    }
    once = true;
    nvs_flash_init();
    nvs_load();
    ESP_LOGI(TAG, "city %s", k_places[s_id].name);
}

int board_if_place_count(void)
{
    return BOARD_IF_PLACE_COUNT;
}

int board_if_place_get(void)
{
    return s_id;
}

void board_if_place_set(int id)
{
    if (id < 0 || id >= BOARD_IF_PLACE_COUNT) {
        return;
    }
    s_id = id;
    nvs_save();
}

const board_if_place_t *board_if_place_at(int id)
{
    if (id < 0 || id >= BOARD_IF_PLACE_COUNT) {
        return &k_places[0];
    }
    return &k_places[id];
}

const char *board_if_place_name(void)
{
    return k_places[s_id].name;
}

float board_if_place_lat(void)
{
    return k_places[s_id].lat;
}

float board_if_place_lon(void)
{
    return k_places[s_id].lon;
}
