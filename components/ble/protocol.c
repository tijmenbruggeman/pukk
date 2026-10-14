/*
 * Pack transfer protocol on CTRL/DATA (docs/PACKS.md).
 */
#include <string.h>
#include "esp_log.h"
#include "packs.h"
#include "ble_priv.h"

static const char *TAG = "ble-proto";

enum {
    OP_BEGIN = 0x01,
    OP_END = 0x02,
    OP_ABORT = 0x03,
    OP_DELETE = 0x04,
    OP_DATA = 0x10,     /* only used in ERROR responses */
    RSP_OK = 0x80,
    RSP_PROGRESS = 0x81,
    RSP_RESEND = 0x82,
    RSP_ERROR = 0x8F,
};

#define PROGRESS_STEP   4096

static char s_name[PACKS_NAME_MAX + 1];
/* The client doing the upload; other clients get BUSY meanwhile. */
static uint16_t s_client;
static uint32_t s_total;
static uint32_t s_next_progress;
/* Position of the last RESEND, so a burst of out-of-order writes asks once. */
static uint32_t s_resend_at;

static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void put_u32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) {
        p[i] = (uint8_t)(v >> (8 * i));
    }
}

static void send_ok(uint16_t conn, uint8_t op)
{
    const uint8_t rsp[] = { RSP_OK, op };
    ble_ctrl_notify(conn, rsp, sizeof(rsp));
}

static void send_error(uint16_t conn, uint8_t op, packs_err_t err)
{
    const uint8_t rsp[] = { RSP_ERROR, op, (uint8_t)err };
    ESP_LOGW(TAG, "request 0x%02x failed: %s", op, packs_err_name(err));
    ble_ctrl_notify(conn, rsp, sizeof(rsp));
}

/* An upload is running for another client. */
static bool busy_for(uint16_t conn)
{
    return packs_upload_active() && conn != s_client;
}

static void emit(pukk_ble_event_type_t type, uint32_t received, int result)
{
    const pukk_ble_event_t event = {
        .type = type,
        .name = s_name,
        .received = received,
        .total = s_total,
        .result = result,
    };
    ble_emit(&event);
}

/* Parse "u8 len, name" at p (n bytes available, must be used exactly). */
static bool parse_name(const uint8_t *p, size_t n)
{
    if (n < 1 || n != 1u + p[0] || !packs_name_valid((const char *)p + 1, p[0])) {
        return false;
    }
    memcpy(s_name, p + 1, p[0]);
    s_name[p[0]] = '\0';
    return true;
}

static void abort_upload(int result)
{
    uint32_t received = packs_upload_received();
    packs_upload_abort();
    emit(PUKK_BLE_EVENT_UPLOAD_FINISHED, received, result);
}

static void handle_begin(uint16_t conn, const uint8_t *p, size_t n)
{
    if (packs_upload_active()) {
        send_error(conn, OP_BEGIN, PACKS_ERR_BUSY);
        return;
    }
    if (n < 9 || !parse_name(p + 8, n - 8)) {
        send_error(conn, OP_BEGIN, PACKS_ERR_INVALID_ARG);
        return;
    }
    uint32_t size = get_u32(p);
    packs_err_t err = packs_upload_begin(s_name, size, get_u32(p + 4));
    if (err != PACKS_OK) {
        send_error(conn, OP_BEGIN, err);
        return;
    }
    s_client = conn;
    s_total = size;
    s_next_progress = PROGRESS_STEP;
    s_resend_at = UINT32_MAX;

    uint16_t chunk = ble_max_chunk(conn);
    const uint8_t rsp[] = { RSP_OK, OP_BEGIN, (uint8_t)chunk, (uint8_t)(chunk >> 8) };
    ble_ctrl_notify(conn, rsp, sizeof(rsp));
    emit(PUKK_BLE_EVENT_UPLOAD_STARTED, 0, 0);
}

static void handle_end(uint16_t conn)
{
    if (busy_for(conn)) {
        send_error(conn, OP_END, PACKS_ERR_BUSY);
        return;
    }
    uint32_t received = packs_upload_received();
    packs_err_t err = packs_upload_finish();
    if (err == PACKS_ERR_NO_UPLOAD) {
        send_error(conn, OP_END, err);
        return;
    }
    if (err == PACKS_OK) {
        send_ok(conn, OP_END);
    } else {
        send_error(conn, OP_END, err);
    }
    emit(PUKK_BLE_EVENT_UPLOAD_FINISHED, received, err);
    if (err == PACKS_OK) {
        emit(PUKK_BLE_EVENT_PACKS_CHANGED, received, 0);
    }
}

static void handle_delete(uint16_t conn, const uint8_t *p, size_t n)
{
    /* parse_name() overwrites the name of a running upload. */
    if (packs_upload_active()) {
        send_error(conn, OP_DELETE, PACKS_ERR_BUSY);
        return;
    }
    if (!parse_name(p, n)) {
        send_error(conn, OP_DELETE, PACKS_ERR_INVALID_ARG);
        return;
    }
    packs_err_t err = packs_delete(s_name);
    if (err != PACKS_OK) {
        send_error(conn, OP_DELETE, err);
        return;
    }
    send_ok(conn, OP_DELETE);
    emit(PUKK_BLE_EVENT_PACKS_CHANGED, 0, 0);
}

void protocol_ctrl_write(uint16_t conn, const uint8_t *data, size_t len)
{
    if (len == 0) {
        return;
    }
    const uint8_t op = data[0];
    const uint8_t *p = data + 1;
    const size_t n = len - 1;

    switch (op) {
    case OP_BEGIN:
        handle_begin(conn, p, n);
        break;
    case OP_END:
        handle_end(conn);
        break;
    case OP_ABORT:
        if (busy_for(conn)) {
            send_error(conn, OP_ABORT, PACKS_ERR_BUSY);
            break;
        }
        if (packs_upload_active()) {
            abort_upload(PUKK_BLE_RESULT_ABORTED);
        }
        send_ok(conn, OP_ABORT);
        break;
    case OP_DELETE:
        handle_delete(conn, p, n);
        break;
    default:
        send_error(conn, op, PACKS_ERR_INVALID_ARG);
        break;
    }
}

void protocol_data_write(uint16_t conn, const uint8_t *data, size_t len)
{
    if (len < 4) {
        send_error(conn, OP_DATA, PACKS_ERR_INVALID_ARG);
        return;
    }
    if (!packs_upload_active()) {
        send_error(conn, OP_DATA, PACKS_ERR_NO_UPLOAD);
        return;
    }
    if (busy_for(conn)) {
        send_error(conn, OP_DATA, PACKS_ERR_BUSY);
        return;
    }
    packs_err_t err = packs_upload_write(get_u32(data), data + 4, len - 4);
    uint32_t received = packs_upload_received();
    if (err == PACKS_ERR_OUT_OF_ORDER) {
        /* A write got lost on the way: ask the client to continue from here. */
        if (s_resend_at != received) {
            s_resend_at = received;
            ESP_LOGW(TAG, "gap at %lu, asking for resend", (unsigned long)received);
            uint8_t rsp[5] = { RSP_RESEND };
            put_u32(rsp + 1, received);
            ble_ctrl_notify(conn, rsp, sizeof(rsp));
        }
        return;
    }
    if (err != PACKS_OK) {
        send_error(conn, OP_DATA, err);
        abort_upload(err);
        return;
    }

    if (received >= s_next_progress || received == s_total) {
        while (s_next_progress <= received) {
            s_next_progress += PROGRESS_STEP;
        }
        uint8_t rsp[5] = { RSP_PROGRESS };
        put_u32(rsp + 1, received);
        ble_ctrl_notify(conn, rsp, sizeof(rsp));
        emit(PUKK_BLE_EVENT_UPLOAD_PROGRESS, received, 0);
    }
}

void protocol_disconnected(uint16_t conn)
{
    if (packs_upload_active() && conn == s_client) {
        abort_upload(PUKK_BLE_RESULT_DISCONNECTED);
    }
}
