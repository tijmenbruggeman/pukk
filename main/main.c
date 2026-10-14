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
#include "packs.h"
#include "pukk_ble.h"
#include "storage.h"
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
        ui_back();
    }
}

/* Runs in the NimBLE host task. */
static void on_ble_event(const pukk_ble_event_t *event, void *ctx)
{
    switch (event->type) {
    case PUKK_BLE_EVENT_READY:
        ui_set_device_name(pukk_ble_device_name());
        break;
    case PUKK_BLE_EVENT_UPLOAD_STARTED:
    case PUKK_BLE_EVENT_UPLOAD_PROGRESS:
        ui_upload_progress(event->name, event->received, event->total);
        break;
    case PUKK_BLE_EVENT_UPLOAD_FINISHED:
        ui_upload_finished(event->name, event->result);
        break;
    case PUKK_BLE_EVENT_PACKS_CHANGED:
        ui_packs_changed();
        break;
    default:
        break;
    }
}

static void log_status(void)
{
    board_pmu_log_status();
    TaskHandle_t ble_host = xTaskGetHandle("nimble_host");
    ESP_LOGI(TAG, "status: heap free %u, min ever %u, stack unused: main %u, lvgl %u, ble %u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)uxTaskGetStackHighWaterMark(NULL), (unsigned)ui_task_stack_free(),
             ble_host ? (unsigned)uxTaskGetStackHighWaterMark(ble_host) : 0);
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

    /* Packs must be readable before the UI shows the pack list. */
    if (ESP_ERROR_CHECK_WITHOUT_ABORT(storage_init()) == ESP_OK) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(packs_init());
    }
    log_heap("storage");

    ESP_ERROR_CHECK(ui_init());
    log_heap("lvgl");

    ui_show_home();
    log_heap("ui");

    ESP_ERROR_CHECK_WITHOUT_ABORT(board_buttons_init(on_button, NULL));
    log_heap("buttons");

    ESP_ERROR_CHECK_WITHOUT_ABORT(pukk_ble_init(on_ble_event, NULL));
    log_heap("ble");

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(STATUS_PERIOD_MS));
        log_status();
    }
}
