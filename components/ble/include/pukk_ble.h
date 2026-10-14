/*
 * BLE peripheral (NimBLE). Advertises as "Pukk-XXXX" (last 4 hex digits of
 * the BLE MAC) with one custom 128-bit GATT service:
 *
 *   service  8e410001-0580-4871-83bd-51273ef66407
 *   CTRL     8e410002-...  write, notify      commands / replies
 *   DATA     8e410003-...  write-no-response  bulk data (pack upload, later)
 *   INFO     8e410004-...  read               JSON: firmware version, free flash
 *
 * Phase 0 is a skeleton: CTRL writes are echoed back as a notification and
 * DATA writes are only counted.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Start the NimBLE host and begin advertising. Requires nvs_flash_init(). */
esp_err_t pukk_ble_init(void);

/* Notify the connected central on CTRL, if it has subscribed. */
esp_err_t pukk_ble_ctrl_notify(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif
