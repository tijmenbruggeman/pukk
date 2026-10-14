/* Physical buttons. */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BOARD_BUTTON_KEY,   /* user key ("IO10" on the silkscreen) */
    BOARD_BUTTON_BOOT,  /* BOOT; also selects download mode when held at reset */
    BOARD_BUTTON_PWR,   /* PWR; a long press makes the PMU switch the board off */
    BOARD_BUTTON_COUNT,
} board_button_t;

/* Runs in the button task, not in an interrupt; may block briefly. */
typedef void (*board_button_cb_t)(board_button_t button, void *ctx);

/* Start watching the buttons. `cb` is called once per (debounced) press. */
esp_err_t board_buttons_init(board_button_cb_t cb, void *ctx);

const char *board_button_name(board_button_t button);

#ifdef __cplusplus
}
#endif
