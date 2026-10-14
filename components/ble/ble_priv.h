/* Internal interface between the GATT layer (pukk_ble.c) and the transfer
 * protocol (protocol.c). Everything here runs in the NimBLE host task. */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "pukk_ble.h"

/* Send a notification on CTRL; dropped when no client has subscribed. */
void ble_ctrl_notify(const uint8_t *data, size_t len);

/* Largest DATA payload (excluding the 4-byte offset) for the current link. */
uint16_t ble_max_chunk(void);

void ble_emit(const pukk_ble_event_t *event);

void protocol_ctrl_write(const uint8_t *data, size_t len);
void protocol_data_write(const uint8_t *data, size_t len);
void protocol_disconnected(void);
