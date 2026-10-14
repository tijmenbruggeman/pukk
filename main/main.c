/*
 * Pukk - handheld quiz device.
 *
 * app_main only wires the components together in the right order and logs
 * the free heap after every step, so we can see what each subsystem costs.
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "board.h"
#include "ui.h"

static const char *TAG = "pukk";

#define STATUS_PERIOD_MS (30 * 1000)

/* Log free internal and DMA-capable heap, plus the change since the previous step. */
static void log_heap(const char *step)
{
    static size_t prev_internal, prev_dma;
    size_t internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t dma = heap_caps_get_free_size(MALLOC_CAP_DMA);
    size_t largest_dma = heap_caps_get_largest_free_block(MALLOC_CAP_DMA);

    if (prev_internal == 0) {
        prev_internal = internal;
        prev_dma = dma;
    }
    ESP_LOGI(TAG, "heap after %-8s internal %6u (%+7d)  dma %6u (%+7d)  largest dma block %6u",
             step, (unsigned)internal, (int)internal - (int)prev_internal,
             (unsigned)dma, (int)dma - (int)prev_dma, (unsigned)largest_dma);
    prev_internal = internal;
    prev_dma = dma;
}

static void init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition is full or outdated, erasing");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

static void on_button(board_button_t button, void *ctx)
{
    if (button == BOARD_BUTTON_KEY) {
        ui_quiz_restart();
    }
}

static void log_status(void)
{
    board_pmu_log_status();
    ESP_LOGI(TAG, "status: heap free %u, min ever %u, lvgl stack unused %u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)ui_task_stack_free());
}

void app_main(void)
{
    const esp_app_desc_t *app = esp_app_get_description();
    ESP_LOGI(TAG, "Pukk %s (ESP-IDF %s)", app->version, app->idf_ver);
    log_heap("boot");

    init_nvs();
    log_heap("nvs");

    /* PMU and display are essential; the steps after them log an error and
     * carry on, so a failing peripheral does not cause a reboot loop. */

    /* The PMU must come first: it powers and resets the display. */
    ESP_LOGI(TAG, "board: %s", board_name());
    ESP_ERROR_CHECK(board_i2c_init());
    ESP_ERROR_CHECK(board_pmu_init());
    board_pmu_log_status();
    log_heap("pmu");

    ESP_ERROR_CHECK(board_display_init());
    log_heap("display");

    ESP_ERROR_CHECK_WITHOUT_ABORT(board_touch_init());
    log_heap("touch");

    ESP_ERROR_CHECK(ui_init());
    log_heap("lvgl");

    ui_quiz_show();
    log_heap("ui");

    ESP_ERROR_CHECK_WITHOUT_ABORT(board_buttons_init(on_button, NULL));
    log_heap("buttons");

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(STATUS_PERIOD_MS));
        log_status();
    }
}
