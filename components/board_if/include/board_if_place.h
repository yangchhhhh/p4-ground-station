#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define BOARD_IF_PLACE_COUNT 3

typedef struct {
    const char *name;
    float lat;
    float lon;
} board_if_place_t;

void board_if_place_init(void);
int board_if_place_count(void);
int board_if_place_get(void);
void board_if_place_set(int id);
const board_if_place_t *board_if_place_at(int id);
const char *board_if_place_name(void);
float board_if_place_lat(void);
float board_if_place_lon(void);

#ifdef __cplusplus
}
#endif
