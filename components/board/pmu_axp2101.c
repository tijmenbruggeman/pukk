/*
 * Minimal AXP2101 driver: only what Pukk needs.
 *
 * The init settings mirror Custom_PmicPortInit()/Custom_PmicRegisterInit() in
 * the Waveshare demos, which use XPowersLib. Register addresses and field
 * encodings come from XPowersLib's AXP2101 implementation.
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "board.h"
#include "board_config.h"
#include "board_priv.h"

static const char *TAG = "pmu";

#define AXP2101_CHIP_ID         0x4A

#define REG_STATUS1             0x00 /* bit5 VBUS good, bit3 battery present */
#define REG_STATUS2             0x01 /* bits 6:5 current direction, bits 2:0 charger state */
#define REG_IC_TYPE             0x03
#define REG_VBUS_ILIM           0x16 /* bits 2:0 VBUS input current limit */
#define REG_GAUGE_WDT_CTRL      0x18 /* bit3 fuel gauge enable */
#define REG_ADC_CTRL            0x30 /* bit0 VBAT, bit1 TS pin, bit2 VBUS, bit3 VSYS */
#define REG_ADC_VBAT_H          0x34 /* 0x34/0x35, 13 bits, mV */
#define REG_ADC_VBUS_H          0x38 /* 0x38/0x39, 14 bits, mV */
#define REG_ADC_VSYS_H          0x3A /* 0x3A/0x3B, 14 bits, mV */
#define REG_IPRECHG             0x61 /* precharge current, 25 mA/step */
#define REG_ICC                 0x62 /* constant charge current */
#define REG_ITERM               0x63 /* termination current, 25 mA/step */
#define REG_BAT_DET             0x68 /* bit0 battery detection enable */
#define REG_DC_ONOFF            0x80
#define REG_DC1_VOLTAGE         0x82
#define REG_LDO_ONOFF0          0x90 /* bits 0..3 = ALDO1..ALDO4 */
#define REG_ALDO1_VOLTAGE       0x92 /* ALDO1..ALDO4 at 0x92..0x95 */
#define REG_BAT_PERCENT         0xA4

#define VBUS_ILIM_2000MA        5
#define ICC_500MA               11  /* 0..8: 0..200 mA in 25 mA steps, then 100 mA steps */
#define MV_TO_DC1(mv)           (((mv) - 1500) / 100)
#define MV_TO_ALDO(mv)          (((mv) - 500) / 100)

#define I2C_TIMEOUT_MS          100
#define I2C_ATTEMPTS            3

typedef struct {
    uint8_t reg;
    uint8_t mask;
    uint8_t value;
} reg_setting_t;

static const reg_setting_t s_init_settings[] = {
    /* As XPowersLib begin(): there is no NTC on the battery, so ignore the TS pin. */
    { REG_ADC_CTRL,          BIT(1), 0 },

    /* Waveshare demo settings (Custom_PmicRegisterInit). */
    { REG_VBUS_ILIM,         0x07, VBUS_ILIM_2000MA },
    { REG_DC1_VOLTAGE,       0x1F, MV_TO_DC1(3300) },  /* DCDC1 = VCC3V3, powers the C6 and the panel */
    { REG_ALDO1_VOLTAGE + 0, 0x1F, MV_TO_ALDO(3300) }, /* ALDO1 = A3V3, audio analog supply */
    { REG_ALDO1_VOLTAGE + 1, 0x1F, MV_TO_ALDO(3300) }, /* ALDO2 = speaker amplifier control */
    { REG_ALDO1_VOLTAGE + 2, 0x1F, MV_TO_ALDO(3300) }, /* ALDO3 = LCD_RESET pull-up */
    { REG_ALDO1_VOLTAGE + 3, 0x1F, MV_TO_ALDO(3300) }, /* ALDO4 = not connected */
    { REG_IPRECHG,           0x0F, 2 },                /* precharge 50 mA */
    { REG_ICC,               0x1F, ICC_500MA },        /* constant charge current 500 mA */
    { REG_ITERM,             0x0F, 2 },                /* termination current 50 mA */

    /* Pukk: enable the measurements we log. */
    { REG_BAT_DET,           BIT(0), BIT(0) },
    { REG_ADC_CTRL,          BIT(0) | BIT(2) | BIT(3), BIT(0) | BIT(2) | BIT(3) },
    { REG_GAUGE_WDT_CTRL,    BIT(3), BIT(3) },
};

static i2c_master_dev_handle_t s_dev;

static esp_err_t reg_read(uint8_t reg, uint8_t *val)
{
    esp_err_t err = ESP_FAIL;
    for (int i = 0; i < I2C_ATTEMPTS; i++) {
        err = i2c_master_transmit_receive(s_dev, &reg, 1, val, 1, I2C_TIMEOUT_MS);
        if (err == ESP_OK) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return err;
}

static esp_err_t reg_write(uint8_t reg, uint8_t val)
{
    const uint8_t buf[2] = { reg, val };
    esp_err_t err = ESP_FAIL;
    for (int i = 0; i < I2C_ATTEMPTS; i++) {
        err = i2c_master_transmit(s_dev, buf, sizeof(buf), I2C_TIMEOUT_MS);
        if (err == ESP_OK) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return err;
}

/* Read-modify-write; skips the write when the field already has the value. */
static esp_err_t reg_update(uint8_t reg, uint8_t mask, uint8_t value)
{
    uint8_t old;
    ESP_RETURN_ON_ERROR(reg_read(reg, &old), TAG, "read 0x%02x failed", reg);
    uint8_t new = (old & ~mask) | (value & mask);
    if (new == old) {
        return ESP_OK;
    }
    ESP_LOGD(TAG, "reg 0x%02x: 0x%02x -> 0x%02x", reg, old, new);
    return reg_write(reg, new);
}

/* ADC results are split over two registers: high bits first, then the low byte. */
static uint16_t read_adc(uint8_t reg_high, uint8_t high_mask)
{
    uint8_t hi, lo;
    if (reg_read(reg_high, &hi) != ESP_OK || reg_read(reg_high + 1, &lo) != ESP_OK) {
        return 0;
    }
    return ((uint16_t)(hi & high_mask) << 8) | lo;
}

esp_err_t board_pmu_init(void)
{
    if (!s_dev) {
        const i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = BOARD_PMU_I2C_ADDR,
            .scl_speed_hz = BOARD_PMU_I2C_HZ,
        };
        ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(board_i2c_bus(), &dev_cfg, &s_dev),
                            TAG, "add I2C device failed");
    }

    uint8_t id;
    ESP_RETURN_ON_ERROR(reg_read(REG_IC_TYPE, &id), TAG,
                        "no response from AXP2101 at 0x%02x", BOARD_PMU_I2C_ADDR);
    if (id != AXP2101_CHIP_ID) {
        ESP_LOGE(TAG, "unexpected chip id 0x%02x (want 0x%02x)", id, AXP2101_CHIP_ID);
        return ESP_ERR_NOT_FOUND;
    }

    for (size_t i = 0; i < sizeof(s_init_settings) / sizeof(s_init_settings[0]); i++) {
        const reg_setting_t *s = &s_init_settings[i];
        ESP_RETURN_ON_ERROR(reg_update(s->reg, s->mask, s->value), TAG,
                            "write 0x%02x failed", s->reg);
    }

    uint8_t dc_onoff = 0, ldo_onoff = 0;
    reg_read(REG_DC_ONOFF, &dc_onoff);
    reg_read(REG_LDO_ONOFF0, &ldo_onoff);
    ESP_LOGI(TAG, "AXP2101 ready (DC on/off 0x%02x, LDO on/off 0x%02x)", dc_onoff, ldo_onoff);
    return ESP_OK;
}

esp_err_t board_pmu_set_aldo3(bool on)
{
    return reg_update(REG_LDO_ONOFF0, BIT(2), on ? BIT(2) : 0);
}

esp_err_t board_pmu_get_status(board_power_status_t *out)
{
    uint8_t st1, st2;
    ESP_RETURN_ON_ERROR(reg_read(REG_STATUS1, &st1), TAG, "status read failed");
    ESP_RETURN_ON_ERROR(reg_read(REG_STATUS2, &st2), TAG, "status read failed");

    out->vbus_present = st1 & BIT(5);
    out->battery_present = st1 & BIT(3);
    out->charging = ((st2 >> 5) & 0x03) == 0x01;
    uint8_t state = st2 & 0x07;
    out->charge_state = state <= BOARD_CHARGE_NOT_CHARGING ? (board_charge_state_t)state
                                                            : BOARD_CHARGE_NOT_CHARGING;

    out->battery_mv = out->battery_present ? read_adc(REG_ADC_VBAT_H, 0x1F) : 0;
    out->vbus_mv = out->vbus_present ? read_adc(REG_ADC_VBUS_H, 0x3F) : 0;
    out->system_mv = read_adc(REG_ADC_VSYS_H, 0x3F);

    uint8_t percent = 0xFF;
    if (out->battery_present) {
        reg_read(REG_BAT_PERCENT, &percent);
    }
    out->battery_percent = percent <= 100 ? (int8_t)percent : -1;
    return ESP_OK;
}

const char *board_charge_state_name(board_charge_state_t state)
{
    switch (state) {
    case BOARD_CHARGE_TRICKLE:           return "trickle";
    case BOARD_CHARGE_PRECHARGE:         return "precharge";
    case BOARD_CHARGE_CONSTANT_CURRENT:  return "constant current";
    case BOARD_CHARGE_CONSTANT_VOLTAGE:  return "constant voltage";
    case BOARD_CHARGE_DONE:              return "done";
    case BOARD_CHARGE_NOT_CHARGING:      return "not charging";
    }
    return "?";
}

void board_pmu_log_status(void)
{
    board_power_status_t st;
    if (board_pmu_get_status(&st) != ESP_OK) {
        return;
    }
    char battery[48];
    if (st.battery_present) {
        snprintf(battery, sizeof(battery), "%u mV (%d%%)", st.battery_mv, st.battery_percent);
    } else {
        snprintf(battery, sizeof(battery), "not detected");
    }
    ESP_LOGI(TAG, "battery %s, charger: %s%s, USB %s (%u mV), system %u mV",
             battery, board_charge_state_name(st.charge_state), st.charging ? " (charging)" : "",
             st.vbus_present ? "connected" : "not connected", st.vbus_mv, st.system_mv);
}
