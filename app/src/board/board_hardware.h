#ifndef BOARD_HARDWARE_H
#define BOARD_HARDWARE_H

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif
void set_pinmux();
void board_backlight_set(uint8_t enabled);
void board_backlight_set_level(uint8_t brightness);
void PowerDownCustom(void);
void check_poweron_reason(void);
#ifdef __cplusplus
}
#endif

#endif // BOARD_HARDWARE_H
