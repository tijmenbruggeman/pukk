/*
 * LVGL 9 port: partial rendering into two DMA-capable draw buffers in internal
 * RAM, flushed asynchronously to the panel's own GRAM.
 *
 * While buffer A is being sent over QSPI, LVGL renders into buffer B. LVGL
 * waits for a transfer by blocking on a semaphore given from the transfer-done
 * interrupt, so the CPU is free for other tasks (BLE) in the meantime.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "board.h"
#include "ui.h"

static const char *TAG = "lvgl";

#define DRAW_BUF_LINES      48  /* 480 x 48 px = 1/10 of the screen per buffer */
#define TASK_STACK_SIZE     (8 * 1024)
#define TASK_PRIORITY       4   /* below the NimBLE host task */
#define TASK_MIN_DELAY_MS   1
#define TASK_MAX_DELAY_MS   500
#define FLUSH_TIMEOUT_MS    1000

static SemaphoreHandle_t s_lock;
static SemaphoreHandle_t s_flush_done;
static TaskHandle_t s_task;
static lv_display_t *s_display;

static uint32_t tick_get_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/*
 * Whole-system idle percentage for LVGL's performance monitor. LVGL's default
 * (LV_OS_NONE) only measures its own timer handler; this uses the FreeRTOS
 * idle task run time instead, so BLE and everything else is included.
 * Hooked in via LV_SYSMON_GET_IDLE (see CMakeLists.txt).
 */
uint32_t ui_cpu_idle_percent(void)
{
    static configRUN_TIME_COUNTER_TYPE last_idle, last_total;
    configRUN_TIME_COUNTER_TYPE idle = ulTaskGetIdleRunTimeCounter();
    configRUN_TIME_COUNTER_TYPE total = (configRUN_TIME_COUNTER_TYPE)esp_timer_get_time();
    configRUN_TIME_COUNTER_TYPE d_idle = idle - last_idle;
    configRUN_TIME_COUNTER_TYPE d_total = total - last_total;
    last_idle = idle;
    last_total = total;
    if (d_total == 0) {
        return 100;
    }
    uint32_t percent = (uint32_t)((uint64_t)d_idle * 100 / d_total);
    return percent > 100 ? 100 : percent;
}

/* The CO5300 wants areas that start on, and span, a multiple of `align` pixels. */
static void rounder_cb(lv_event_t *e)
{
    lv_area_t *area = lv_event_get_param(e);
    const int32_t align = board_display_info()->align;
    area->x1 -= area->x1 % align;
    area->y1 -= area->y1 % align;
    area->x2 += align - 1 - area->x2 % align;
    area->y2 += align - 1 - area->y2 % align;
}

static bool IRAM_ATTR flush_done_isr(void *ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_flush_done, &woken);
    return woken == pdTRUE;
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    if (board_display_info()->swap_bytes) {
        lv_draw_sw_rgb565_swap(px_map, lv_area_get_size(area));
    }
    if (board_display_draw(area->x1, area->y1, area->x2, area->y2, px_map) != ESP_OK) {
        ESP_LOGE(TAG, "display transfer failed");
        /* No transfer started, so nothing will signal completion. */
        lv_display_flush_ready(disp);
    }
}

static void flush_wait_cb(lv_display_t *disp)
{
    if (xSemaphoreTake(s_flush_done, pdMS_TO_TICKS(FLUSH_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "display transfer timed out");
    }
}

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    static uint16_t last_x, last_y;
    if (board_touch_read(&last_x, &last_y)) {
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
    data->point.x = last_x;
    data->point.y = last_y;
}

static void lvgl_task(void *arg)
{
    ESP_LOGI(TAG, "LVGL task started");
    for (;;) {
        uint32_t delay_ms = TASK_MAX_DELAY_MS;
        if (ui_lock(-1)) {
            delay_ms = lv_timer_handler();
            ui_unlock();
        }
        if (delay_ms < TASK_MIN_DELAY_MS) {
            delay_ms = TASK_MIN_DELAY_MS;
        } else if (delay_ms > TASK_MAX_DELAY_MS) {
            delay_ms = TASK_MAX_DELAY_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

bool ui_lock(int timeout_ms)
{
    const TickType_t ticks = timeout_ms < 0 ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(s_lock, ticks) == pdTRUE;
}

void ui_unlock(void)
{
    xSemaphoreGiveRecursive(s_lock);
}

size_t ui_task_stack_free(void)
{
    return s_task ? uxTaskGetStackHighWaterMark(s_task) : 0;
}

esp_err_t ui_init(void)
{
    s_lock = xSemaphoreCreateRecursiveMutex();
    s_flush_done = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_lock && s_flush_done, ESP_ERR_NO_MEM, TAG, "no memory for semaphores");

    lv_init();
    lv_tick_set_cb(tick_get_ms);

    const board_display_info_t *info = board_display_info();
    const size_t buf_bytes = info->width * DRAW_BUF_LINES * sizeof(uint16_t);
    ESP_RETURN_ON_FALSE(buf_bytes <= info->max_transfer_bytes, ESP_ERR_INVALID_SIZE, TAG,
                        "draw buffer larger than the display can transfer");

    /* Internal RAM only (there is no PSRAM) and DMA-capable for the SPI master. */
    void *buf1 = heap_caps_malloc(buf_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    void *buf2 = heap_caps_malloc(buf_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!buf1 || !buf2) {
        heap_caps_free(buf1);
        heap_caps_free(buf2);
        ESP_LOGE(TAG, "no memory for 2 x %u B draw buffers", (unsigned)buf_bytes);
        return ESP_ERR_NO_MEM;
    }

    s_display = lv_display_create(info->width, info->height);
    lv_display_set_color_format(s_display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(s_display, buf1, buf2, buf_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(s_display, flush_cb);
    lv_display_set_flush_wait_cb(s_display, flush_wait_cb);
    lv_display_add_event_cb(s_display, rounder_cb, LV_EVENT_INVALIDATE_AREA, NULL);
    ESP_RETURN_ON_ERROR(board_display_set_done_cb(flush_done_isr, NULL), TAG, "done cb failed");

    lv_indev_t *touch = lv_indev_create();
    lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch, touch_read_cb);
    lv_indev_set_display(touch, s_display);

    ESP_RETURN_ON_FALSE(xTaskCreate(lvgl_task, "lvgl", TASK_STACK_SIZE, NULL, TASK_PRIORITY,
                                    &s_task) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "LVGL task create failed");

    ESP_LOGI(TAG, "LVGL %d.%d.%d, 2 x %u B draw buffers (%ux%u px), %d kB LVGL heap",
             LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH, (unsigned)buf_bytes,
             info->width, DRAW_BUF_LINES, CONFIG_LV_MEM_SIZE_KILOBYTES);
    return ESP_OK;
}
