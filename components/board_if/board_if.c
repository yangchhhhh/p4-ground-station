#include "board_if.h"

void board_if_init(void)
{
    board_if_gy87_init();
    board_if_gy87_start_sampler();
    board_if_drone_link_init();
    board_if_net_init();
    board_if_storage_init(); /* mutex only; do not mount SD at boot */
    board_if_audio_init();
    board_if_humid_init(); /* GPIO22 default OFF; HTTP :8088 / MQTT after STA IP */
    board_if_place_init();
    board_if_gps_init(); /* UART1 GPIO28/29, 9600 NMEA */
}
