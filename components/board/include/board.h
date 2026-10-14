/*
 * Thin hardware layer for Pukk. Everything board-specific (pins, chips,
 * power-up order) stays behind these functions so another board variant can
 * be added without touching ui/ or ble/.
 */
#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "board_pmu.h"
#include "board_display.h"
#include "board_touch.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Human readable board name for logs. */
const char *board_name(void);

/* Create the shared I2C master bus. Call once, before any other board_* init. */
esp_err_t board_i2c_init(void);
i2c_master_bus_handle_t board_i2c_bus(void);

#ifdef __cplusplus
}
#endif
