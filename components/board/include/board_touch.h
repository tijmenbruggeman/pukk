/* Capacitive touch controller (CST9217-compatible on the Waveshare C6 board). */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Requires board_i2c_init(). */
esp_err_t board_touch_init(void);

/* Poll the controller. Returns true with the first point, in display
 * coordinates, while the panel is touched. */
bool board_touch_read(uint16_t *x, uint16_t *y);

#ifdef __cplusplus
}
#endif
