/* Power management (AXP2101 on the Waveshare C6 board). */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BOARD_CHARGE_TRICKLE = 0,
    BOARD_CHARGE_PRECHARGE,
    BOARD_CHARGE_CONSTANT_CURRENT,
    BOARD_CHARGE_CONSTANT_VOLTAGE,
    BOARD_CHARGE_DONE,
    BOARD_CHARGE_NOT_CHARGING,
} board_charge_state_t;

typedef struct {
    bool battery_present;
    bool vbus_present;
    bool charging;
    board_charge_state_t charge_state;
    uint16_t battery_mv;    /* 0 when no battery is detected */
    int8_t battery_percent; /* fuel gauge estimate, -1 when unknown */
    uint16_t vbus_mv;       /* 0 when USB is not connected */
    uint16_t system_mv;
} board_power_status_t;

/* Configure rails and charger. Must run before the display and touch are
 * initialised, because the PMU powers/resets them. */
esp_err_t board_pmu_init(void);

esp_err_t board_pmu_get_status(board_power_status_t *out);
const char *board_charge_state_name(board_charge_state_t state);

/* Read the status and print it on one log line. */
void board_pmu_log_status(void);

#ifdef __cplusplus
}
#endif
