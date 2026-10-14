#include "board.h"
#include "board_config.h"
#include "esp_check.h"

static const char *TAG = "board";

static i2c_master_bus_handle_t s_bus;

const char *board_name(void)
{
    return BOARD_NAME;
}

esp_err_t board_i2c_init(void)
{
    if (s_bus) {
        return ESP_OK;
    }
    const i2c_master_bus_config_t cfg = {
        .i2c_port = BOARD_I2C_PORT,
        .scl_io_num = BOARD_I2C_SCL,
        .sda_io_num = BOARD_I2C_SDA,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        /* The board has external 2.2k pull-ups; the internal ones do no harm. */
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&cfg, &s_bus), TAG, "I2C bus init failed");
    ESP_LOGI(TAG, "I2C bus ready (SCL=%d, SDA=%d)", BOARD_I2C_SCL, BOARD_I2C_SDA);
    return ESP_OK;
}

i2c_master_bus_handle_t board_i2c_bus(void)
{
    return s_bus;
}
