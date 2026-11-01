#include <stdio.h>
#include <string.h>
#include "board_if_audio.h"

void board_if_audio_init(void)
{
}

bool board_if_audio_start(void)
{
    /* TODO: ES7210 mic + ES8311 codec, see official example 06_I2SCodec. */
    return false;
}

bool board_if_audio_get_status(board_if_audio_status_t *out)
{
    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    snprintf(out->status, sizeof(out->status), "stub: ES7210/ES8311 not started");
    return false;
}
