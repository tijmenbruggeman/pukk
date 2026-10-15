/*
 * Buttons: a GPIO edge interrupt wakes a small task, which then polls every
 * 10 ms until all buttons are stable and released again. No polling while idle.
 * Polling while a button is held also times long presses.
 */
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "board_buttons.h"
#include "board_config.h"

static const char *TAG = "buttons";

#define POLL_MS             10
#define STABLE_SAMPLES      3   /* 30 ms debounce */
#define TASK_STACK_SIZE     4096
#define TASK_PRIORITY       5

typedef struct {
    gpio_num_t gpio;
    bool active_high;
    const char *name;
} button_def_t;

static const button_def_t s_defs[BOARD_BUTTON_COUNT] = {
    [BOARD_BUTTON_KEY]  = { BOARD_BTN_KEY,  false, "KEY" },
    [BOARD_BUTTON_BOOT] = { BOARD_BTN_BOOT, false, "BOOT" },
    [BOARD_BUTTON_PWR]  = { BOARD_BTN_PWR,  true,  "PWR" },
};

static TaskHandle_t s_task;
static board_button_cb_t s_cb;
static void *s_cb_ctx;

static bool is_pressed(board_button_t b)
{
    return gpio_get_level(s_defs[b].gpio) == (s_defs[b].active_high ? 1 : 0);
}

static void IRAM_ATTR button_isr(void *arg)
{
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(s_task, &woken);
    portYIELD_FROM_ISR(woken);
}

static void emit(board_button_t button, board_button_event_t event)
{
    static const char *const names[] = { "pressed", "clicked", "held" };
    ESP_LOGI(TAG, "%s %s", s_defs[button].name, names[event]);
    if (s_cb) {
        s_cb(button, event, s_cb_ctx);
    }
}

static void button_task(void *arg)
{
    bool stable[BOARD_BUTTON_COUNT];
    uint8_t changed_for[BOARD_BUTTON_COUNT] = { 0 };
    TickType_t pressed_at[BOARD_BUTTON_COUNT] = { 0 };
    bool long_sent[BOARD_BUTTON_COUNT] = { false };

    /* A button held during boot (e.g. PWR to switch on) must not count as a
     * press, nor give a click or long press when it is released. */
    for (int i = 0; i < BOARD_BUTTON_COUNT; i++) {
        stable[i] = is_pressed(i);
        long_sent[i] = stable[i];
    }

    for (;;) {
        bool busy = true;
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        while (busy) {
            vTaskDelay(pdMS_TO_TICKS(POLL_MS));
            busy = false;
            for (int i = 0; i < BOARD_BUTTON_COUNT; i++) {
                bool pressed = is_pressed(i);
                if (pressed == stable[i]) {
                    changed_for[i] = 0;
                } else if (++changed_for[i] >= STABLE_SAMPLES) {
                    stable[i] = pressed;
                    changed_for[i] = 0;
                    if (pressed) {
                        pressed_at[i] = xTaskGetTickCount();
                        long_sent[i] = false;
                        emit(i, BOARD_BUTTON_PRESS);
                    } else if (!long_sent[i]) {
                        emit(i, BOARD_BUTTON_CLICK);
                    }
                }
                if (stable[i] && !long_sent[i] &&
                    xTaskGetTickCount() - pressed_at[i] >= pdMS_TO_TICKS(BOARD_BUTTON_LONG_MS)) {
                    long_sent[i] = true;
                    emit(i, BOARD_BUTTON_LONG_PRESS);
                }
                /* Keep polling while anything is held or still bouncing. */
                busy |= stable[i] || changed_for[i] > 0;
            }
        }
    }
}

esp_err_t board_buttons_init(board_button_cb_t cb, void *ctx)
{
    s_cb = cb;
    s_cb_ctx = ctx;

    ESP_RETURN_ON_FALSE(xTaskCreate(button_task, "buttons", TASK_STACK_SIZE, NULL, TASK_PRIORITY,
                                    &s_task) == pdPASS,
                        ESP_ERR_NO_MEM, TAG, "task create failed");

    esp_err_t err = gpio_install_isr_service(0);
    ESP_RETURN_ON_FALSE(err == ESP_OK || err == ESP_ERR_INVALID_STATE, err, TAG,
                        "ISR service install failed");

    for (int i = 0; i < BOARD_BUTTON_COUNT; i++) {
        const button_def_t *d = &s_defs[i];
        /* All buttons have external pull-ups; PWR is driven low by T1 when released. */
        const gpio_config_t cfg = {
            .pin_bit_mask = BIT64(d->gpio),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = d->active_high ? GPIO_PULLUP_DISABLE : GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_ANYEDGE,
        };
        ESP_RETURN_ON_ERROR(gpio_config(&cfg), TAG, "GPIO %d config failed", d->gpio);
        ESP_RETURN_ON_ERROR(gpio_isr_handler_add(d->gpio, button_isr, NULL), TAG,
                            "GPIO %d ISR failed", d->gpio);
    }
    ESP_LOGI(TAG, "buttons ready (KEY=%d, BOOT=%d, PWR=%d)", BOARD_BTN_KEY, BOARD_BTN_BOOT,
             BOARD_BTN_PWR);
    return ESP_OK;
}

const char *board_button_name(board_button_t button)
{
    return button < BOARD_BUTTON_COUNT ? s_defs[button].name : "?";
}
