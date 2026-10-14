#include <stdio.h>
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
#include "storage.h"
#include "pukk_ble.h"

static const char *TAG = "ble";

/* 8e41xxxx-0580-4871-83bd-51273ef66407, little-endian as NimBLE expects. */
#define PUKK_UUID128(id)                                                            \
    BLE_UUID128_INIT(0x07, 0x64, 0xf6, 0x3e, 0x27, 0x51, 0xbd, 0x83, 0x71, 0x48,   \
                     0x80, 0x05, (id) & 0xff, ((id) >> 8) & 0xff, 0x41, 0x8e)

#define CTRL_MAX_LEN        244 /* ATT MTU 247 - 3 */
#define ADV_INTERVAL_MIN_MS 100
#define ADV_INTERVAL_MAX_MS 150

static const ble_uuid128_t s_svc_uuid = PUKK_UUID128(0x0001);
static const ble_uuid128_t s_ctrl_uuid = PUKK_UUID128(0x0002);
static const ble_uuid128_t s_data_uuid = PUKK_UUID128(0x0003);
static const ble_uuid128_t s_info_uuid = PUKK_UUID128(0x0004);

static uint16_t s_ctrl_handle;
static uint16_t s_conn = BLE_HS_CONN_HANDLE_NONE;
static bool s_ctrl_subscribed;
static uint8_t s_own_addr_type;
static char s_name[16];
static uint32_t s_data_writes;
static uint32_t s_data_bytes;

static void advertise(void);

static int ctrl_access_cb(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt,
                          void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    uint8_t buf[CTRL_MAX_LEN];
    uint16_t len = 0;
    if (ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof(buf), &len) != 0) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    ESP_LOGI(TAG, "CTRL write, %u bytes", len);
    ESP_LOG_BUFFER_HEXDUMP(TAG, buf, len, ESP_LOG_DEBUG);

    /* Skeleton: echo back so a client can test the write/notify round trip. */
    pukk_ble_ctrl_notify(buf, len);
    return 0;
}

static int data_access_cb(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt,
                          void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    /* Skeleton: count only. Pack transfer comes later. */
    s_data_writes++;
    s_data_bytes += OS_MBUF_PKTLEN(ctxt->om);
    return 0;
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
    int len = snprintf(json, sizeof(json), "{\"fw\":\"%s\",\"flash_total\":%u,\"flash_free\":%u}",
                       esp_app_get_description()->version, (unsigned)total, (unsigned)free);
    /* NimBLE handles offsets for long reads; always return the full value. */
    return os_mbuf_append(ctxt->om, json, len) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
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
            s_data_writes = 0;
            s_data_bytes = 0;
            ESP_LOGI(TAG, "connected (handle %u)", s_conn);
        } else {
            ESP_LOGW(TAG, "connect failed (status %d)", event->connect.status);
            advertise();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnected (reason 0x%03x), DATA received: %lu writes, %lu bytes",
                 event->disconnect.reason, (unsigned long)s_data_writes,
                 (unsigned long)s_data_bytes);
        s_conn = BLE_HS_CONN_HANDLE_NONE;
        s_ctrl_subscribed = false;
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

esp_err_t pukk_ble_init(void)
{
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

esp_err_t pukk_ble_ctrl_notify(const uint8_t *data, size_t len)
{
    if (s_conn == BLE_HS_CONN_HANDLE_NONE || !s_ctrl_subscribed) {
        return ESP_ERR_INVALID_STATE;
    }
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (!om) {
        return ESP_ERR_NO_MEM;
    }
    /* Takes ownership of om, also on failure. */
    return ble_gatts_notify_custom(s_conn, s_ctrl_handle, om) == 0 ? ESP_OK : ESP_FAIL;
}
