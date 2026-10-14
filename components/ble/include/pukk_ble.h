/*
 * BLE (NimBLE). Advertises as "Pukk-XXXX" (last 4 hex digits of the BLE MAC)
 * with one custom 128-bit GATT service:
 *
 *   service  8e410001-0580-4871-83bd-51273ef66407
 *   CTRL     8e410002-...  write, notify      requests / responses
 *   DATA     8e410003-...  write-no-response  upload data
 *   INFO     8e410004-...  read               JSON: firmware version, free flash
 *   PACKS    8e410005-...  read               JSON: packs on the device
 *   BATTLE   8e410006-...  write, notify      battle messages
 *
 * Pack transfer: docs/PACKS.md. Battles: docs/BATTLE.md; for a battle one
 * Pukk (the host) connects to another (the guest) as central, finds it by
 * the "joinable" flag in its advertisement, and uses the same service.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PUKK_BLE_EVENT_READY,           /* advertising; pukk_ble_device_name() is set */
    PUKK_BLE_EVENT_CONNECTED,       /* a client (app, tool or battle host) connected */
    PUKK_BLE_EVENT_DISCONNECTED,
    PUKK_BLE_EVENT_UPLOAD_STARTED,  /* name, total */
    PUKK_BLE_EVENT_UPLOAD_PROGRESS, /* name, received, total */
    PUKK_BLE_EVENT_UPLOAD_FINISHED, /* name, result */
    PUKK_BLE_EVENT_PACKS_CHANGED,   /* a pack was added, replaced or deleted */

    /* Guest side: a battle host wrote to our BATTLE characteristic. */
    PUKK_BLE_EVENT_BATTLE_RX,       /* data, len */
    PUKK_BLE_EVENT_BATTLE_HOST_LOST,/* that host disconnected */

    /* Host side: the Pukk we connected to as central (see pukk_ble_peer_find). */
    PUKK_BLE_EVENT_PEER_CONNECTED,  /* name: ready for writes */
    PUKK_BLE_EVENT_PEER_DISCONNECTED,
    PUKK_BLE_EVENT_PEER_BATTLE_RX,  /* data, len: BATTLE notification */
    PUKK_BLE_EVENT_PEER_CTRL_RX,    /* data, len: CTRL notification (upload responses) */
} pukk_ble_event_type_t;

#define PUKK_BLE_RESULT_DISCONNECTED (-1)
#define PUKK_BLE_RESULT_ABORTED      (-2)  /* client sent ABORT */

typedef struct {
    pukk_ble_event_type_t type;
    const char *name;   /* pack or peer name; only valid during the callback */
    uint32_t received;
    uint32_t total;
    int result;         /* 0 = ok, 1..9 = protocol error code, or PUKK_BLE_RESULT_* */
    const uint8_t *data;/* only valid during the callback */
    size_t len;
} pukk_ble_event_t;

/* Called from the NimBLE host task: keep it short and do not call back into
 * pukk_ble_* from it. */
typedef void (*pukk_ble_event_cb_t)(const pukk_ble_event_t *event, void *ctx);

/* Start the NimBLE host and begin advertising. Requires nvs_flash_init() and
 * packs_init(). */
esp_err_t pukk_ble_init(pukk_ble_event_cb_t cb, void *ctx);

/* "Pukk-XXXX", or "" until the READY event. */
const char *pukk_ble_device_name(void);

/* Any BLE link up: a client, a battle host or a battle peer. */
bool pukk_ble_connected(void);

/* --- battle guest ------------------------------------------------------------ */

/* Show in our advertisement that we want to join a battle. */
void pukk_ble_set_joinable(bool joinable);

/* Notify the battle host (the last client that wrote BATTLE). */
esp_err_t pukk_ble_battle_notify(const uint8_t *data, size_t len);

/* Drop the link with the battle host. */
void pukk_ble_battle_disconnect(void);

/* --- battle host (central) ------------------------------------------------- */

typedef enum {
    PUKK_BLE_PEER_CTRL,     /* write with response; one at a time */
    PUKK_BLE_PEER_DATA,     /* write without response */
    PUKK_BLE_PEER_BATTLE,   /* write without response */
} pukk_ble_peer_chr_t;

/* Scan for a joinable Pukk, connect to the first one found and subscribe to
 * its notifications. PEER_CONNECTED follows when it is ready; any failure
 * ends with PEER_DISCONNECTED. */
esp_err_t pukk_ble_peer_find(void);

/* Stop scanning or drop the peer link. */
void pukk_ble_peer_close(void);

/* ESP_ERR_NO_MEM when the stack is out of buffers: try again shortly. */
esp_err_t pukk_ble_peer_write(pukk_ble_peer_chr_t chr, const uint8_t *data, size_t len);

/* Largest value for pukk_ble_peer_write() on the current link. */
size_t pukk_ble_peer_max_write(void);

#ifdef __cplusplus
}
#endif
