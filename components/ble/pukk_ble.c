/*
 * GAP and GATT: advertising, connection handling and the Pukk service. The
 * transfer protocol on CTRL/DATA lives in protocol.c.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "packs.h"
#include "storage.h"
#include "ble_priv.h"

static const char *TAG = "ble";

/* 8e41xxxx-0580-4871-83bd-51273ef66407, little-endian as NimBLE expects. */
#define PUKK_UUID128(id)                                                            \
    BLE_UUID128_INIT(0x07, 0x64, 0xf6, 0x3e, 0x27, 0x51, 0xbd, 0x83, 0x71, 0x48,   \
                     0x80, 0x05, (id) & 0xff, ((id) >> 8) & 0xff, 0x41, 0x8e)

#define WRITE_MAX_LEN       512 /* largest ATT value */
#define PACKS_LIST_MAX      32
#define ADV_INTERVAL_MIN_MS 100
#define ADV_INTERVAL_MAX_MS 150

static const ble_uuid128_t s_svc_uuid = PUKK_UUID128(0x0001);
static const ble_uuid128_t s_ctrl_uuid = PUKK_UUID128(0x0002);
static const ble_uuid128_t s_data_uuid = PUKK_UUID128(0x0003);
static const ble_uuid128_t s_info_uuid = PUKK_UUID128(0x0004);
static const ble_uuid128_t s_packs_uuid = PUKK_UUID128(0x0005);

static uint16_t s_ctrl_handle;
static uint16_t s_conn = BLE_HS_CONN_HANDLE_NONE;
static bool s_ctrl_subscribed;
static uint8_t s_own_addr_type;
static char s_name[16];
static pukk_ble_event_cb_t s_event_cb;
static void *s_event_ctx;
/* Only touched from the host task. */
static uint8_t s_write_buf[WRITE_MAX_LEN];

static void advertise(void);

void ble_emit(const pukk_ble_event_t *event)
{
    if (s_event_cb) {
        s_event_cb(event, s_event_ctx);
    }
}

static void emit_simple(pukk_ble_event_type_t type)
{
    const pukk_ble_event_t event = { .type = type };
    ble_emit(&event);
}

void ble_ctrl_notify(const uint8_t *data, size_t len)
{
    if (s_conn == BLE_HS_CONN_HANDLE_NONE || !s_ctrl_subscribed) {
        return;
    }
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (!om) {
        ESP_LOGE(TAG, "no buffer for CTRL notification");
        return;
    }
    /* Takes ownership of om, also on failure. */
    int rc = ble_gatts_notify_custom(s_conn, s_ctrl_handle, om);
    if (rc != 0) {
        ESP_LOGE(TAG, "CTRL notification failed (%d)", rc);
    }
}

uint16_t ble_max_chunk(void)
{
    uint16_t mtu = s_conn != BLE_HS_CONN_HANDLE_NONE ? ble_att_mtu(s_conn) : 0;
    if (mtu < BLE_ATT_MTU_DFLT) {
        mtu = BLE_ATT_MTU_DFLT;
    }
    uint16_t chunk = mtu - 3 - 4; /* ATT header, offset field */
    return chunk > WRITE_MAX_LEN - 4 ? WRITE_MAX_LEN - 4 : chunk;
}

/* Copy a write into s_write_buf; returns the length or -1 if it is too long. */
static int flatten_write(struct ble_gatt_access_ctxt *ctxt)
{
    uint16_t len = 0;
    if (ble_hs_mbuf_to_flat(ctxt->om, s_write_buf, sizeof(s_write_buf), &len) != 0) {
        return -1;
    }
    return len;
}

static int ctrl_access_cb(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt,
                          void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    int len = flatten_write(ctxt);
    if (len < 0) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    protocol_ctrl_write(s_write_buf, len);
    return 0;
}

static int data_access_cb(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt,
                          void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    int len = flatten_write(ctxt);
    if (len < 0) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    protocol_data_write(s_write_buf, len);
    return 0;
}

static int append(struct os_mbuf *om, const char *text)
{
    return os_mbuf_append(om, text, strlen(text));
}

static int info_access_cb(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt,
                          void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    size_t total = 0, free = 0;
    storage_get_usage(&total, &free);

    char json[128];
    snprintf(json, sizeof(json), "{\"fw\":\"%s\",\"flash_total\":%u,\"flash_free\":%u}",
             esp_app_get_description()->version, (unsigned)total, (unsigned)free);
    /* NimBLE handles offsets for long reads; always return the full value. */
    return append(ctxt->om, json) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

/* Titles come from packs, so escape them for JSON. */
static void json_escape(char *dst, size_t size, const char *src)
{
    size_t n = 0;
    for (; *src && n + 7 < size; src++) {
        unsigned char c = (unsigned char)*src;
        if (c == '"' || c == '\\') {
            dst[n++] = '\\';
            dst[n++] = (char)c;
        } else if (c < 0x20) {
            n += snprintf(dst + n, size - n, "\\u%04x", c);
        } else {
            dst[n++] = (char)c;
        }
    }
    dst[n] = '\0';
}

static int packs_access_cb(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt,
                           void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    packs_entry_t *list = malloc(PACKS_LIST_MAX * sizeof(packs_entry_t));
    if (!list) {
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    size_t count = packs_list(list, PACKS_LIST_MAX);

    int rc = append(ctxt->om, "[");
    for (size_t i = 0; rc == 0 && i < count; i++) {
        char title[PACK_TITLE_MAX * 6 + 1];
        char item[sizeof(title) + PACKS_NAME_MAX + 64];
        json_escape(title, sizeof(title), list[i].title);
        snprintf(item, sizeof(item), "%s{\"name\":\"%s\",\"title\":\"%s\",\"questions\":%u,\"size\":%lu}",
                 i ? "," : "", list[i].name, title, list[i].question_count,
                 (unsigned long)list[i].size);
        rc = append(ctxt->om, item);
    }
    if (rc == 0) {
        rc = append(ctxt->om, "]");
    }
    free(list);
    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static const struct ble_gatt_svc_def s_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &s_ctrl_uuid.u,
                .access_cb = ctrl_access_cb,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_ctrl_handle,
            },
            {
                .uuid = &s_data_uuid.u,
                .access_cb = data_access_cb,
                .flags = BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid = &s_info_uuid.u,
                .access_cb = info_access_cb,
                .flags = BLE_GATT_CHR_F_READ,
            },
            {
                .uuid = &s_packs_uuid.u,
                .access_cb = packs_access_cb,
                .flags = BLE_GATT_CHR_F_READ,
            },
            { 0 },
        },
    },
    { 0 },
};

static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn = event->connect.conn_handle;
            ESP_LOGI(TAG, "connected (handle %u)", s_conn);
            emit_simple(PUKK_BLE_EVENT_CONNECTED);
        } else {
            ESP_LOGW(TAG, "connect failed (status %d)", event->connect.status);
            advertise();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnected (reason 0x%03x)", event->disconnect.reason);
        protocol_disconnected();
        s_conn = BLE_HS_CONN_HANDLE_NONE;
        s_ctrl_subscribed = false;
        emit_simple(PUKK_BLE_EVENT_DISCONNECTED);
        advertise();
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        advertise();
        break;

    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == s_ctrl_handle) {
            s_ctrl_subscribed = event->subscribe.cur_notify;
            ESP_LOGI(TAG, "CTRL notifications %s", s_ctrl_subscribed ? "on" : "off");
        }
        break;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU %u", event->mtu.value);
        break;

    default:
        break;
    }
    return 0;
}

static void advertise(void)
{
    /* Advertising packet: flags + service UUID (21 of 31 bytes). */
    struct ble_hs_adv_fields adv = {
        .flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP,
        .uuids128 = &s_svc_uuid,
        .num_uuids128 = 1,
        .uuids128_is_complete = 1,
    };
    int rc = ble_gap_adv_set_fields(&adv);
    if (rc != 0) {
        ESP_LOGE(TAG, "set adv data failed (%d)", rc);
        return;
    }

    /* Scan response: the name (does not fit next to a 128-bit UUID). */
    struct ble_hs_adv_fields rsp = {
        .name = (uint8_t *)s_name,
        .name_len = strlen(s_name),
        .name_is_complete = 1,
        .tx_pwr_lvl_is_present = 1,
        .tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO,
    };
    rc = ble_gap_adv_rsp_set_fields(&rsp);
    if (rc != 0) {
        ESP_LOGE(TAG, "set scan response failed (%d)", rc);
        return;
    }

    const struct ble_gap_adv_params params = {
        .conn_mode = BLE_GAP_CONN_MODE_UND,
        .disc_mode = BLE_GAP_DISC_MODE_GEN,
        .itvl_min = BLE_GAP_ADV_ITVL_MS(ADV_INTERVAL_MIN_MS),
        .itvl_max = BLE_GAP_ADV_ITVL_MS(ADV_INTERVAL_MAX_MS),
    };
    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &params, gap_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "start advertising failed (%d)", rc);
    }
}

static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc == 0) {
        rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    }
    if (rc != 0) {
        ESP_LOGE(TAG, "no usable BLE address (%d)", rc);
        return;
    }

    /* NimBLE stores the address little-endian: addr[0] is the last byte. */
    uint8_t addr[6] = { 0 };
    ble_hs_id_copy_addr(s_own_addr_type, addr, NULL);
    snprintf(s_name, sizeof(s_name), "Pukk-%02X%02X", addr[1], addr[0]);
    ble_svc_gap_device_name_set(s_name);

    ESP_LOGI(TAG, "advertising as %s (%02X:%02X:%02X:%02X:%02X:%02X)", s_name, addr[5],
             addr[4], addr[3], addr[2], addr[1], addr[0]);
    advertise();
    emit_simple(PUKK_BLE_EVENT_READY);
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "host reset (reason %d)", reason);
}

static void host_task(void *param)
{
    nimble_port_run(); /* returns only after nimble_port_stop() */
    nimble_port_freertos_deinit();
}

const char *pukk_ble_device_name(void)
{
    return s_name;
}

esp_err_t pukk_ble_init(pukk_ble_event_cb_t cb, void *ctx)
{
    s_event_cb = cb;
    s_event_ctx = ctx;
    ESP_RETURN_ON_ERROR(nimble_port_init(), TAG, "NimBLE init failed");

    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    int rc = ble_gatts_count_cfg(s_services);
    if (rc == 0) {
        rc = ble_gatts_add_svcs(s_services);
    }
    ESP_RETURN_ON_FALSE(rc == 0, ESP_FAIL, TAG, "GATT service registration failed (%d)", rc);

    nimble_port_freertos_init(host_task);
    return ESP_OK;
}
