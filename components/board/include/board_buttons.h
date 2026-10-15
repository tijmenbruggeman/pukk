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

/* Held this long: BOARD_BUTTON_LONG_PRESS. */
#define BOARD_BUTTON_LONG_MS    1500

typedef enum {
    BOARD_BUTTON_PRESS,         /* pressed (debounced) */
    BOARD_BUTTON_CLICK,         /* released before BOARD_BUTTON_LONG_MS */
    BOARD_BUTTON_LONG_PRESS,    /* held for BOARD_BUTTON_LONG_MS; no CLICK follows */
} board_button_event_t;

/* Runs in the button task, not in an interrupt; may block briefly. */
typedef void (*board_button_cb_t)(board_button_t button, board_button_event_t event, void *ctx);

/* Start watching the buttons. Every press gives PRESS, then either CLICK or
 * LONG_PRESS. */
esp_err_t board_buttons_init(board_button_cb_t cb, void *ctx);

const char *board_button_name(board_button_t button);

#ifdef __cplusplus
}
#endif
