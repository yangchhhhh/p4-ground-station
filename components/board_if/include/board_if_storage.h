#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "board_if_gy87.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BOARD_IF_STORAGE_LOG_MAX 6

typedef struct {
    bool mounted;
    bool recording;
    uint64_t total_bytes;
    uint64_t free_bytes;
    int last_err;
    int freq_khz;
    int file_count;
    char last_file[48];
    char files[BOARD_IF_STORAGE_LOG_MAX][32];
    char status[96];
} board_if_storage_status_t;

/** Mutex only. Does not mount the card (Wi-Fi / SDMMC share the chip). */
void board_if_storage_init(void);

/** Mount MicroSD over SDMMC. High speed first, then default. Never auto-formats. */
bool board_if_storage_mount(void);
bool board_if_storage_unmount(void);
bool board_if_storage_get_status(board_if_storage_status_t *out);

/** Open /sdcard/gy87/gy87_NNN.csv and start logging from the sampler task. */
bool board_if_storage_record_start(void);
bool board_if_storage_record_stop(void);

/** Called from the GY-87 sampler. Never call from LVGL. */
void board_if_storage_record_sample(const board_if_gy87_sample_t *sample);

#define BOARD_IF_FILES_MAX  40
#define BOARD_IF_FILES_NAME 48

typedef struct {
    char name[BOARD_IF_FILES_NAME];
    bool is_dir;
    uint32_t size;
} board_if_file_ent_t;

/**
 * List a directory under /sdcard. rel is "" or a subpath without "..".
 * Does not format the card. Does not delete GY-87 logs.
 * Returns entry count, or -1 on error.
 */
int board_if_storage_list(const char *rel, board_if_file_ent_t *out, int max);

#ifdef __cplusplus
}
#endif
