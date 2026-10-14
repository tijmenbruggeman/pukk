/* Display panel (CO5300 AMOLED over QSPI on the Waveshare C6 board). */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t width;
    uint16_t height;
    uint8_t align;             /* flushed areas must start at and span multiples of this */
    bool swap_bytes;           /* RGB565 pixels must be sent high byte first */
    size_t max_transfer_bytes; /* largest pixel buffer board_display_draw() accepts */
} board_display_info_t;

/* Called from interrupt context when a board_display_draw() transfer has
 * finished. Must be IRAM-safe. Return true if a higher priority task was woken. */
typedef bool (*board_display_done_cb_t)(void *ctx);

/* Power-cycle/reset the panel, run its init sequence and switch it on.
 * Requires board_pmu_init(). */
esp_err_t board_display_init(void);

const board_display_info_t *board_display_info(void);

esp_err_t board_display_set_done_cb(board_display_done_cb_t cb, void *ctx);

/* Start an asynchronous transfer of RGB565 pixels to the inclusive area
 * (x1,y1)-(x2,y2). `pixels` must be DMA-capable and stay untouched until the
 * done callback fires. */
esp_err_t board_display_draw(int x1, int y1, int x2, int y2, const void *pixels);

/* 0..100. Not thread-safe against board_display_draw(): call it from the
 * task that draws (i.e. with the UI lock held). */
esp_err_t board_display_set_brightness(uint8_t percent);

#ifdef __cplusplus
}
#endif
