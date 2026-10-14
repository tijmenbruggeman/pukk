/*
 * Pin and bus assignments per board variant (private to the board component).
 *
 * Sources: Waveshare ESP-IDF demos (01_AXP2101_Test, 09_LVGL_V9_Test), the
 * Arduino and XiaoZhi ports for this board, and
 * docs/ESP32-C6-Touch-AMOLED-2.16-Schematic.pdf. See docs/NOTES.md for the
 * discrepancies found while cross-checking.
 */
#pragma once

#include "sdkconfig.h"
#include "driver/gpio.h"

#if CONFIG_PUKK_BOARD_WS_C6_AMOLED_216

#define BOARD_NAME              "Waveshare ESP32-C6-Touch-AMOLED-2.16"

/* Shared I2C bus: AXP2101, CST9217 touch, PCF85063, QMI8658, ES8311/ES7210.
 * Schematic: ESP32_SCL = MTDO = GPIO7, ESP32_SDA = GPIO8 (2.2k pull-ups R42/R43). */
#define BOARD_I2C_PORT          I2C_NUM_0
#define BOARD_I2C_SCL           GPIO_NUM_7
#define BOARD_I2C_SDA           GPIO_NUM_8

#define BOARD_PMU_I2C_ADDR      0x34
#define BOARD_PMU_I2C_HZ        100000

/* CO5300 AMOLED over QSPI (connector J4). No reset GPIO: LCD_RESET is pulled
 * up to AXP2101 ALDO3 through R16, so cycling ALDO3 resets the panel. */
#define BOARD_LCD_SPI_HOST      SPI2_HOST
#define BOARD_LCD_CS            GPIO_NUM_15
#define BOARD_LCD_PCLK          GPIO_NUM_0
#define BOARD_LCD_D0            GPIO_NUM_1
#define BOARD_LCD_D1            GPIO_NUM_2
#define BOARD_LCD_D2            GPIO_NUM_3
#define BOARD_LCD_D3            GPIO_NUM_4
#define BOARD_LCD_PCLK_HZ       (40 * 1000 * 1000)
#define BOARD_LCD_H_RES         480
#define BOARD_LCD_V_RES         480

/* Touch controller (J4 pins 17/18). */
#define BOARD_TOUCH_INT         GPIO_NUM_5
#define BOARD_TOUCH_RST         GPIO_NUM_11
#define BOARD_TOUCH_I2C_HZ      400000

/* Buttons (silkscreen names). IO10 and BOOT are active low with 10k pull-ups.
 * PWR goes to the AXP2101 PWRON pin; it is mirrored on GPIO18 through
 * transistor T1, which inverts it: GPIO18 reads high while PWR is pressed. */
#define BOARD_BTN_KEY           GPIO_NUM_10
#define BOARD_BTN_BOOT          GPIO_NUM_9
#define BOARD_BTN_PWR           GPIO_NUM_18

#else
#error "No Pukk board variant selected (menuconfig > Pukk board)"
#endif
