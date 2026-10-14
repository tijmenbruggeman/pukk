/* Internal helpers shared between the board drivers. */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

/* AXP2101 ALDO3 also drives the panel's LCD_RESET line (see board_config.h). */
esp_err_t board_pmu_set_aldo3(bool on);
