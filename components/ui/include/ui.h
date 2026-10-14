/*
 * UI layer: LVGL 9 port (own task + lock) and the screens.
 *
 * LVGL is not thread-safe. Every lv_* call made outside the LVGL task must be
 * wrapped in ui_lock()/ui_unlock(). LVGL callbacks (events, timers,
 * animations) already run inside the LVGL task with the lock held. The lock is
 * recursive, so the ui_* functions below may also be called from callbacks.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Create the LVGL display and touch input on top of the board drivers and
 * start the LVGL task. Requires board_display_init() and board_touch_init(). */
esp_err_t ui_init(void);

/* timeout_ms < 0 waits forever. Returns true when the lock was taken. */
bool ui_lock(int timeout_ms);
void ui_unlock(void);

/* Smallest amount of LVGL task stack that has stayed unused, in bytes. */
size_t ui_task_stack_free(void);

/* Phase 0 test screen: one question, four answers, 15 s timer bar. */
void ui_quiz_show(void);
void ui_quiz_restart(void);

#ifdef __cplusplus
}
#endif
