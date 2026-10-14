/* Internal interface between the GAP/GATT server (pukk_ble.c), the transfer
 * protocol (protocol.c) and the central role (ble_central.c). Everything here
 * runs in the NimBLE host task unless noted. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "host/ble_uuid.h"
#include "pukk_ble.h"

/* 8e41xxxx-0580-4871-83bd-51273ef66407, little-endian as NimBLE expects. */
#define PUKK_UUID128(id)                                                            \
    BLE_UUID128_INIT(0x07, 0x64, 0xf6, 0x3e, 0x27, 0x51, 0xbd, 0x83, 0x71, 0x48,   \
                     0x80, 0x05, (id) & 0xff, ((id) >> 8) & 0xff, 0x41, 0x8e)

extern const ble_uuid128_t ble_svc_uuid;
extern const ble_uuid128_t ble_ctrl_uuid;
extern const ble_uuid128_t ble_data_uuid;
extern const ble_uuid128_t ble_battle_uuid;

/* Joinable flag in the advertisement: manufacturer data with the company ID
 * for testing (0xFFFF), "PK" and a state byte. */
#define BLE_JOIN_MFG_LEN    5
#define BLE_JOIN_STATE      1

/* Largest ATT value. */
#define BLE_WRITE_MAX_LEN   512

uint8_t ble_own_addr_type(void);

/* "Pukk-XXXX" for a Pukk's BLE address. */
void ble_pukk_name(const uint8_t addr[6], char *out, size_t size);

/* Ask for Data Length Extension and 2M PHY on a link; with `params` also for
 * a short connection interval (only useful as peripheral). */
void ble_request_fast_link(uint16_t conn, bool params);

/* The central link went up or down: refresh advertising. */
void ble_links_changed(void);

void ble_central_init(void);
/* 1 while connected (or connecting) to a peer as central. */
int ble_central_link_count(void);

/* Send a notification on CTRL to a client; dropped if it did not subscribe. */
void ble_ctrl_notify(uint16_t conn, const uint8_t *data, size_t len);

/* Largest DATA payload (excluding the 4-byte offset) for a client's link. */
uint16_t ble_max_chunk(uint16_t conn);

void ble_emit(const pukk_ble_event_t *event);

void protocol_ctrl_write(uint16_t conn, const uint8_t *data, size_t len);
void protocol_data_write(uint16_t conn, const uint8_t *data, size_t len);
void protocol_disconnected(uint16_t conn);
