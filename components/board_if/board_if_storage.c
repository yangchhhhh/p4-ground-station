#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "board_if_storage.h"
#include "bsp/esp-bsp.h"
#include "driver/sdmmc_host.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#include "sdmmc_cmd.h"

static const char *TAG = "board_if.sd";
#define SD_MOUNT_POINT BSP_SD_MOUNT_POINT
#define GY87_DIR       SD_MOUNT_POINT "/gy87"

static bool s_mounted;
static bool s_recording;
static int s_last_err;
static int s_freq_khz;
static int s_flush_n;
static int s_file_index;
static sdmmc_card_t *s_card;
static sd_pwr_ctrl_handle_t s_pwr;
static FILE *s_fp;
static SemaphoreHandle_t s_lock;
static char s_last_file[48];
static char s_status[96] = "SD idle";
static int s_file_count;
static char s_files[BOARD_IF_STORAGE_LOG_MAX][32];

static void lock_take(void)
{
    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
}

static void lock_give(void)
{
    if (s_lock) {
        xSemaphoreGive(s_lock);
    }
}

static void refresh_file_list_locked(void)
{
    s_file_count = 0;
    memset(s_files, 0, sizeof(s_files));
    DIR *dir = opendir(GY87_DIR);
    if (dir == NULL) {
        return;
    }
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') {
            continue;
        }
        if (strstr(ent->d_name, "gy87_") != ent->d_name) {
            continue;
        }
        if (s_file_count < BOARD_IF_STORAGE_LOG_MAX) {
            snprintf(s_files[s_file_count], sizeof(s_files[0]), "%.31s", ent->d_name);
        }
        s_file_count++;
    }
    closedir(dir);
}

static int next_file_index(void)
{
    int max_n = 0;
    DIR *dir = opendir(GY87_DIR);
    if (dir == NULL) {
        return 1;
    }
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        int n = 0;
        if (sscanf(ent->d_name, "gy87_%d.csv", &n) == 1 && n > max_n) {
            max_n = n;
        }
    }
    closedir(dir);
    return max_n + 1;
}

static esp_err_t mount_at_freq(int freq_khz)
{
    const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 8,
        .allocation_unit_size = 64 * 1024,
    };

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
    host.max_freq_khz = freq_khz;

    if (s_pwr == NULL) {
        sd_pwr_ctrl_ldo_config_t ldo_config = {
            .ldo_chan_id = 4,
        };
        esp_err_t err = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &s_pwr);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "SD LDO failed: %s", esp_err_to_name(err));
            return err;
        }
    }
    host.pwr_ctrl_handle = s_pwr;

    const sdmmc_slot_config_t slot_config = {
        .cd = SDMMC_SLOT_NO_CD,
        .wp = SDMMC_SLOT_NO_WP,
        .width = 4,
        .flags = 0,
    };

    return esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot_config, &mount_config, &s_card);
}

static void record_stop_locked(void)
{
    if (s_fp) {
        fflush(s_fp);
        fclose(s_fp);
        s_fp = NULL;
    }
    s_recording = false;
    s_flush_n = 0;
}

void board_if_storage_init(void)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
    }
}

bool board_if_storage_mount(void)
{
    lock_take();
    if (s_mounted) {
        lock_give();
        return true;
    }

    esp_err_t err = bsp_sdcard_mount();
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        s_mounted = true;
        s_last_err = (int)ESP_OK;
        s_freq_khz = SDMMC_FREQ_HIGHSPEED;
        snprintf(s_status, sizeof(s_status), "SD mounted");
        refresh_file_list_locked();
        ESP_LOGI(TAG, "SD mounted via BSP");
        lock_give();
        return true;
    }
    ESP_LOGW(TAG, "BSP high-speed mount failed: %s, retry default", esp_err_to_name(err));
    (void)bsp_sdcard_unmount();

    err = mount_at_freq(SDMMC_FREQ_DEFAULT);
    s_freq_khz = SDMMC_FREQ_DEFAULT;
    s_last_err = (int)err;
    s_mounted = (err == ESP_OK);
    if (!s_mounted) {
        snprintf(s_status, sizeof(s_status), "FAIL %s — format FAT32", esp_err_to_name(err));
        ESP_LOGE(TAG, "SD mount failed: %s (need FAT32, not exFAT)", esp_err_to_name(err));
    } else {
        snprintf(s_status, sizeof(s_status), "SD mounted %d kHz", s_freq_khz);
        refresh_file_list_locked();
    }
    lock_give();
    return s_mounted;
}

bool board_if_storage_unmount(void)
{
    lock_take();
    record_stop_locked();
    if (!s_mounted) {
        snprintf(s_status, sizeof(s_status), "SD already ejected");
        lock_give();
        return true;
    }
    esp_err_t err = bsp_sdcard_unmount();
    if (err != ESP_OK && s_card) {
        err = esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, s_card);
        s_card = NULL;
    }
    s_mounted = false;
    s_last_err = (int)err;
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        s_file_count = 0;
        memset(s_files, 0, sizeof(s_files));
        snprintf(s_status, sizeof(s_status), "SD ejected, pull the card");
        lock_give();
        return true;
    }
    snprintf(s_status, sizeof(s_status), "eject %s", esp_err_to_name(err));
    lock_give();
    return false;
}

bool board_if_storage_record_start(void)
{
    if (!board_if_storage_mount()) {
        return false;
    }
    lock_take();
    if (s_recording) {
        lock_give();
        return true;
    }
    if (mkdir(GY87_DIR, 0775) != 0 && errno != EEXIST) {
        snprintf(s_status, sizeof(s_status), "mkdir gy87 failed");
        lock_give();
        return false;
    }
    s_file_index = next_file_index();
    char path[96];
    snprintf(path, sizeof(path), GY87_DIR "/gy87_%03d.csv", s_file_index);
    s_fp = fopen(path, "w");
    if (s_fp == NULL) {
        snprintf(s_status, sizeof(s_status), "open CSV failed");
        lock_give();
        return false;
    }
    fprintf(s_fp, "ms,ax,ay,az,gx,gy,gz,mx,my,mz,roll,pitch,heading,alt_m,pressure_pa\n");
    fflush(s_fp);
    snprintf(s_last_file, sizeof(s_last_file), "gy87_%03d.csv", s_file_index);
    s_recording = true;
    s_flush_n = 0;
    snprintf(s_status, sizeof(s_status), "REC %s", s_last_file);
    refresh_file_list_locked();
    lock_give();
    return true;
}

bool board_if_storage_record_stop(void)
{
    lock_take();
    if (!s_recording) {
        snprintf(s_status, sizeof(s_status), "not recording");
        lock_give();
        return true;
    }
    record_stop_locked();
    refresh_file_list_locked();
    snprintf(s_status, sizeof(s_status), "saved %s  Eject before pull", s_last_file);
    lock_give();
    return true;
}

void board_if_storage_record_sample(const board_if_gy87_sample_t *sample)
{
    if (sample == NULL || !s_recording || s_fp == NULL) {
        return;
    }
    lock_take();
    if (!s_recording || s_fp == NULL) {
        lock_give();
        return;
    }
    int64_t ms = esp_timer_get_time() / 1000;
    fprintf(s_fp, "%lld,%.4f,%.4f,%.4f,%.3f,%.3f,%.3f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.3f,%.1f\n",
            (long long)ms,
            sample->ax, sample->ay, sample->az,
            sample->gx, sample->gy, sample->gz,
            sample->mx, sample->my, sample->mz,
            sample->roll_deg, sample->pitch_deg, sample->heading_deg,
            sample->altitude_m, sample->pressure_pa);
    if ((++s_flush_n % 8) == 0) {
        fflush(s_fp);
    }
    lock_give();
}

bool board_if_storage_get_status(board_if_storage_status_t *out)
{
    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    lock_take();
    out->mounted = s_mounted;
    out->recording = s_recording;
    out->last_err = s_last_err;
    out->freq_khz = s_freq_khz;
    snprintf(out->last_file, sizeof(out->last_file), "%s", s_last_file);
    snprintf(out->status, sizeof(out->status), "%s", s_status);
    out->file_count = s_file_count;
    memcpy(out->files, s_files, sizeof(out->files));
    lock_give();
    return s_mounted;
}

static bool path_ok(const char *rel)
{
    if (rel == NULL) {
        return false;
    }
    if (rel[0] == '/' || rel[0] == '\\') {
        return false;
    }
    if (strstr(rel, "..") != NULL) {
        return false;
    }
    return true;
}

int board_if_storage_list(const char *rel, board_if_file_ent_t *out, int max)
{
    if (out == NULL || max <= 0 || !path_ok(rel)) {
        return -1;
    }
    if (!board_if_storage_mount()) {
        return -1;
    }

    char full[192];
    if (rel[0] == '\0') {
        snprintf(full, sizeof(full), "%s", SD_MOUNT_POINT);
    } else {
        snprintf(full, sizeof(full), "%s/%s", SD_MOUNT_POINT, rel);
    }

    DIR *dir = opendir(full);
    if (dir == NULL) {
        ESP_LOGW(TAG, "opendir %s failed", full);
        return -1;
    }

    int n = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && n < max) {
        if (ent->d_name[0] == '.') {
            continue;
        }
        memset(&out[n], 0, sizeof(out[n]));
        snprintf(out[n].name, sizeof(out[n].name), "%.47s", ent->d_name);
        char child[256];
        snprintf(child, sizeof(child), "%.191s/%.63s", full, ent->d_name);
        struct stat st;
        if (stat(child, &st) == 0) {
            out[n].is_dir = S_ISDIR(st.st_mode);
            out[n].size = (uint32_t)st.st_size;
        } else {
            out[n].is_dir = (ent->d_type == DT_DIR);
        }
        n++;
    }
    closedir(dir);
    return n;
}
