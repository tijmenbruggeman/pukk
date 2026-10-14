/*
 * Touch controller. The board has a Hynitron CST92xx; Waveshare drives it with
 * their CST9217 driver (same register protocol), so we do too.
 */
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch_cst9217.h"
#include "esp_log.h"
#include "board.h"
#include "board_config.h"

static const char *TAG = "touch";

static esp_lcd_touch_handle_t s_touch;

esp_err_t board_touch_init(void)
{
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_CST9217_CONFIG();
    io_cfg.scl_speed_hz = BOARD_TOUCH_I2C_HZ;
    esp_lcd_panel_io_handle_t io;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(board_i2c_bus(), &io_cfg, &io),
                        TAG, "touch IO init failed");

    const esp_lcd_touch_config_t cfg = {
        .x_max = BOARD_LCD_H_RES - 1,
        .y_max = BOARD_LCD_V_RES - 1,
        .rst_gpio_num = BOARD_TOUCH_RST,
        .int_gpio_num = BOARD_TOUCH_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = BOARD_TOUCH_SWAP_XY,
            .mirror_x = BOARD_TOUCH_MIRROR_X,
            .mirror_y = BOARD_TOUCH_MIRROR_Y,
        },
    };
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_cst9217(io, &cfg, &s_touch),
                        TAG, "touch controller init failed");
    ESP_LOGI(TAG, "touch ready (INT=%d, RST=%d)", BOARD_TOUCH_INT, BOARD_TOUCH_RST);
    return ESP_OK;
}

bool board_touch_read(uint16_t *x, uint16_t *y)
{
    /* A failed read (including "no valid frame") counts as released; the
     * driver keeps the previous points in that case. */
    if (!s_touch || esp_lcd_touch_read_data(s_touch) != ESP_OK) {
        return false;
    }
    esp_lcd_touch_point_data_t point;
    uint8_t count = 0;
    if (esp_lcd_touch_get_data(s_touch, &point, &count, 1) != ESP_OK || count == 0) {
        return false;
    }
    *x = point.x;
    *y = point.y;
    return true;
}
