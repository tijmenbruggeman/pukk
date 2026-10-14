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
#include <stdint.h>
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

/* Wordmark while the rest of the device starts. Call right after ui_init(). */
void ui_show_splash(void);

/* Show the pack list (after the splash has been up for a moment). */
void ui_show_home(void);

/* KEY button: leave a game and return to the pack list. */
void ui_back(void);

/* Status bar: BLE name (so you know where to upload to), battery, link. */
void ui_set_device_name(const char *name);
/* percent < 0: no battery. */
void ui_set_battery(int percent, bool charging);
void ui_set_ble_connected(bool connected);

/* Show or hide the FPS/CPU and memory overlays (hidden at start). */
void ui_toggle_debug(void);

/* Reload the pack list after an upload or delete. */
void ui_packs_changed(void);

/* Upload pop-up. `result` is 0 on success, else a pukk_ble result code. */
void ui_upload_progress(const char *name, uint32_t received, uint32_t total);
void ui_upload_finished(const char *name, int result);

#ifdef __cplusplus
}
#endif
