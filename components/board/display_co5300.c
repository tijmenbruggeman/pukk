/*
 * CO5300 AMOLED panel over QSPI, driven through Espressif's SH8601 driver
 * (command compatible), exactly like the Waveshare 09_LVGL_V9_Test demo.
 * The panel has its own GRAM, so we only send the areas that changed.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_sh8601.h"
#include "esp_log.h"
#include "board_display.h"
#include "board_config.h"
#include "board_priv.h"

static const char *TAG = "display";

#define LCD_OPCODE_WRITE_CMD    0x02
#define LCD_CMD_BRIGHTNESS      0x51

/* Init sequence from Waveshare 09_LVGL_V9_Test (display_bsp.cpp), 480x480.
 * The SH8601 driver warns that 0x3A/0x36 override its defaults; that is expected. */
static const sh8601_lcd_init_cmd_t s_init_cmds[] = {
    {0x11, (uint8_t[]){0x00}, 0, 600},                      /* sleep out */
    {0xFE, (uint8_t[]){0x20}, 1, 0},                        /* vendor page 0x20 */
    {0x19, (uint8_t[]){0x10}, 1, 0},
    {0x1C, (uint8_t[]){0xA0}, 1, 0},
    {0xFE, (uint8_t[]){0x00}, 1, 0},                        /* back to user page */
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},                        /* RGB565 */
    {0x35, (uint8_t[]){0x00}, 1, 0},                        /* TE output on (LCD_TE only goes to test point TP20) */
    {0x36, (uint8_t[]){0x30}, 1, 0},                        /* MADCTL */
    {0x53, (uint8_t[]){0x20}, 1, 0},                        /* brightness control on */
    {0x51, (uint8_t[]){0xFF}, 1, 0},                        /* brightness max */
    {0x63, (uint8_t[]){0xFF}, 1, 0},                        /* HBM brightness */
    {0x2A, (uint8_t[]){0x00, 0x00, 0x01, 0xDF}, 4, 0},      /* columns 0..479 */
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xDF}, 4, 0},      /* rows 0..479 */
    {0x29, (uint8_t[]){0x00}, 0, 100},                      /* display on */
};

static const board_display_info_t s_info = {
    .width = BOARD_LCD_H_RES,
    .height = BOARD_LCD_V_RES,
    .align = BOARD_LCD_ALIGN,
    .swap_bytes = true,
    .max_transfer_bytes = BOARD_LCD_H_RES * BOARD_LCD_MAX_TRANSFER_LINES * 2,
};

static esp_lcd_panel_io_handle_t s_io;
static esp_lcd_panel_handle_t s_panel;
static board_display_done_cb_t s_done_cb;
static void *s_done_ctx;

static bool IRAM_ATTR on_color_trans_done(esp_lcd_panel_io_handle_t io,
                                          esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    return s_done_cb ? s_done_cb(s_done_ctx) : false;
}

/* There is no reset GPIO: LCD_RESET is pulled up to ALDO3, so toggling ALDO3
 * resets the panel (same timing as the Waveshare demo). */
static esp_err_t reset_panel(void)
{
    ESP_RETURN_ON_ERROR(board_pmu_set_aldo3(true), TAG, "ALDO3 on failed");
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_RETURN_ON_ERROR(board_pmu_set_aldo3(false), TAG, "ALDO3 off failed");
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_RETURN_ON_ERROR(board_pmu_set_aldo3(true), TAG, "ALDO3 on failed");
    vTaskDelay(pdMS_TO_TICKS(100));
    return ESP_OK;
}

esp_err_t board_display_init(void)
{
    const spi_bus_config_t bus_cfg = {
        .sclk_io_num = BOARD_LCD_PCLK,
        .data0_io_num = BOARD_LCD_D0,
        .data1_io_num = BOARD_LCD_D1,
        .data2_io_num = BOARD_LCD_D2,
        .data3_io_num = BOARD_LCD_D3,
        .data4_io_num = GPIO_NUM_NC,
        .data5_io_num = GPIO_NUM_NC,
        .data6_io_num = GPIO_NUM_NC,
        .data7_io_num = GPIO_NUM_NC,
        .max_transfer_sz = s_info.max_transfer_bytes,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(BOARD_LCD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO),
                        TAG, "SPI bus init failed");

    /* Panel IO settings as in the Waveshare demo. */
    const esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = BOARD_LCD_CS,
        .dc_gpio_num = GPIO_NUM_NC,
        .spi_mode = 0,
        .pclk_hz = BOARD_LCD_PCLK_HZ,
        .trans_queue_depth = 1,
        .on_color_trans_done = on_color_trans_done,
        .lcd_cmd_bits = 32,
        .lcd_param_bits = 8,
        .flags.quad_mode = true,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BOARD_LCD_SPI_HOST,
                                                 &io_cfg, &s_io),
                        TAG, "panel IO init failed");

    const sh8601_vendor_config_t vendor_cfg = {
        .init_cmds = s_init_cmds,
        .init_cmds_size = sizeof(s_init_cmds) / sizeof(s_init_cmds[0]),
        .flags.use_qspi_interface = 1,
    };
    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = GPIO_NUM_NC,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = (void *)&vendor_cfg,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_sh8601(s_io, &panel_cfg, &s_panel),
                        TAG, "panel driver init failed");
    ESP_RETURN_ON_ERROR(reset_panel(), TAG, "panel reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "panel init sequence failed");

    ESP_LOGI(TAG, "CO5300 %ux%u ready (QSPI %d MHz)", s_info.width, s_info.height,
             BOARD_LCD_PCLK_HZ / 1000000);
    return ESP_OK;
}

const board_display_info_t *board_display_info(void)
{
    return &s_info;
}

esp_err_t board_display_set_done_cb(board_display_done_cb_t cb, void *ctx)
{
    s_done_ctx = ctx;
    s_done_cb = cb;
    return ESP_OK;
}

esp_err_t board_display_draw(int x1, int y1, int x2, int y2, const void *pixels)
{
    /* esp_lcd uses exclusive end coordinates. */
    return esp_lcd_panel_draw_bitmap(s_panel, x1, y1, x2 + 1, y2 + 1, pixels);
}

esp_err_t board_display_set_brightness(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    uint8_t level = (uint8_t)(percent * 255 / 100);
    int cmd = (LCD_OPCODE_WRITE_CMD << 24) | (LCD_CMD_BRIGHTNESS << 8);
    return esp_lcd_panel_io_tx_param(s_io, cmd, &level, 1);
}
