#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool mic_ready;
    bool speaker_ready;
    char status[80];
} board_if_audio_status_t;

void board_if_audio_init(void);

/** ES7210 mic / ES8311 codec. Not started in this revision. */
bool board_if_audio_start(void);
bool board_if_audio_get_status(board_if_audio_status_t *out);

#ifdef __cplusplus
}
#endif
