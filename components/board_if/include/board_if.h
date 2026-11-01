#pragma once

#include "board_if_gy87.h"
#include "board_if_drone_link.h"
#include "board_if_net.h"
#include "board_if_storage.h"
#include "board_if_audio.h"
#include "board_if_humid.h"
#include "board_if_place.h"
#include "board_if_gps.h"
#include "board_if_i2c_scan.h"

#ifdef __cplusplus
extern "C" {
#endif

/** One-shot init for every stub / helper. Safe to call more than once. */
void board_if_init(void);

#ifdef __cplusplus
}
#endif
