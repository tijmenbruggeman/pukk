/*
 * BLE peripheral (NimBLE). Advertises as "Pukk-XXXX" (last 4 hex digits of
 * the BLE MAC) with one custom 128-bit GATT service for pack transfer:
 *
 *   service  8e410001-0580-4871-83bd-51273ef66407
 *   CTRL     8e410002-...  write, notify      requests / responses
 *   DATA     8e410003-...  write-no-response  upload data
 *   INFO     8e410004-...  read               JSON: firmware version, free flash
 *   PACKS    8e410005-...  read               JSON: packs on the device
 *
 * Protocol: docs/PACKS.md.
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PUKK_BLE_EVENT_READY,           /* advertising; pukk_ble_device_name() is set */
    PUKK_BLE_EVENT_CONNECTED,
    PUKK_BLE_EVENT_DISCONNECTED,
    PUKK_BLE_EVENT_UPLOAD_STARTED,  /* name, total */
    PUKK_BLE_EVENT_UPLOAD_PROGRESS, /* name, received, total */
    PUKK_BLE_EVENT_UPLOAD_FINISHED, /* name, result */
    PUKK_BLE_EVENT_PACKS_CHANGED,   /* a pack was added, replaced or deleted */
} pukk_ble_event_type_t;

#define PUKK_BLE_RESULT_DISCONNECTED (-1)
#define PUKK_BLE_RESULT_ABORTED      (-2)  /* client sent ABORT */

typedef struct {
    pukk_ble_event_type_t type;
    const char *name;   /* pack name; only valid during the callback */
    uint32_t received;
    uint32_t total;
    int result;         /* 0 = ok, 1..9 = protocol error code, or PUKK_BLE_RESULT_* */
} pukk_ble_event_t;

/* Called from the NimBLE host task: keep it short and do not call back into
 * pukk_ble_* from it. */
typedef void (*pukk_ble_event_cb_t)(const pukk_ble_event_t *event, void *ctx);

/* Start the NimBLE host and begin advertising. Requires nvs_flash_init() and
 * packs_init(). */
esp_err_t pukk_ble_init(pukk_ble_event_cb_t cb, void *ctx);

/* "Pukk-XXXX", or "" until the READY event. */
const char *pukk_ble_device_name(void);

#ifdef __cplusplus
}
#endif
