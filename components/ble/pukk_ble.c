/*
 * GAP and GATT server: advertising, client connections and the Pukk service.
 * The transfer protocol on CTRL/DATA lives in protocol.c, the central role
 * (battle host) in ble_central.c.
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

#define MAX_LINKS           CONFIG_BT_NIMBLE_MAX_CONNECTIONS
#define PACKS_LIST_MAX      32
#define ADV_INTERVAL_MIN_MS 100
#define ADV_INTERVAL_MAX_MS 150
/* Connection parameters within Apple's accessory guidelines. */
#define CONN_ITVL_MIN_MS    15
#define CONN_ITVL_MAX_MS    30
#define CONN_TIMEOUT_MS     4000
/* Largest link-layer payload and the airtime it needs on 1M PHY. */
#define LL_MAX_OCTETS       251
#define LL_MAX_TIME_US      2120

const ble_uuid128_t ble_svc_uuid = PUKK_UUID128(0x0001);
const ble_uuid128_t ble_ctrl_uuid = PUKK_UUID128(0x0002);
const ble_uuid128_t ble_data_uuid = PUKK_UUID128(0x0003);
static const ble_uuid128_t s_info_uuid = PUKK_UUID128(0x0004);
static const ble_uuid128_t s_packs_uuid = PUKK_UUID128(0x0005);
const ble_uuid128_t ble_battle_uuid = PUKK_UUID128(0x0006);

/* A client connected to our GATT server. */
typedef struct {
    uint16_t conn;          /* BLE_HS_CONN_HANDLE_NONE: slot free */
    bool ctrl_subscribed;
    bool battle_subscribed;
} client_t;

static client_t s_clients[MAX_LINKS];
static uint16_t s_ctrl_handle;
static uint16_t s_battle_handle;
/* The client that last wrote BATTLE: our battle host. */
static uint16_t s_battle_conn = BLE_HS_CONN_HANDLE_NONE;
static bool s_joinable;
static bool s_synced;
static struct ble_npl_event s_adv_event;
static uint8_t s_own_addr_type;
static char s_name[16];
static pukk_ble_event_cb_t s_event_cb;
static void *s_event_ctx;
/* Only touched from the host task. */
static uint8_t s_write_buf[BLE_WRITE_MAX_LEN];

static void advertise(void);

uint8_t ble_own_addr_type(void)
{
    return s_own_addr_type;
}

/* NimBLE stores addresses little-endian: addr[0] is the last byte. */
void ble_pukk_name(const uint8_t addr[6], char *out, size_t size)
{
    snprintf(out, size, "Pukk-%02X%02X", addr[1], addr[0]);
}

static client_t *find_client(uint16_t conn)
{
    for (int i = 0; i < MAX_LINKS; i++) {
        if (s_clients[i].conn == conn && conn != BLE_HS_CONN_HANDLE_NONE) {
            return &s_clients[i];
        }
    }
    return NULL;
}

static int client_count(void)
{
    int n = 0;
    for (int i = 0; i < MAX_LINKS; i++) {
        n += s_clients[i].conn != BLE_HS_CONN_HANDLE_NONE;
    }
    return n;
}

bool pukk_ble_connected(void)
{
    return client_count() + ble_central_link_count() > 0;
}

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

static int notify(uint16_t conn, uint16_t handle, const uint8_t *data, size_t len)
{
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (!om) {
        return BLE_HS_ENOMEM;
    }
    /* Takes ownership of om, also on failure. */
    return ble_gatts_notify_custom(conn, handle, om);
}

void ble_ctrl_notify(uint16_t conn, const uint8_t *data, size_t len)
{
    const client_t *c = find_client(conn);
    if (!c || !c->ctrl_subscribed) {
        return;
    }
    int rc = notify(conn, s_ctrl_handle, data, len);
    if (rc != 0) {
        ESP_LOGE(TAG, "CTRL notification failed (%d)", rc);
    }
}

uint16_t ble_max_chunk(uint16_t conn)
{
    uint16_t mtu = ble_att_mtu(conn);
    if (mtu < BLE_ATT_MTU_DFLT) {
        mtu = BLE_ATT_MTU_DFLT;
    }
    uint16_t chunk = mtu - 3 - 4; /* ATT header, offset field */
    return chunk > BLE_WRITE_MAX_LEN - 4 ? BLE_WRITE_MAX_LEN - 4 : chunk;
}

esp_err_t pukk_ble_battle_notify(const uint8_t *data, size_t len)
{
    uint16_t conn = s_battle_conn;
    const client_t *c = find_client(conn);
    if (!c || !c->battle_subscribed) {
        return ESP_ERR_INVALID_STATE;
    }
    int rc = notify(conn, s_battle_handle, data, len);
    if (rc == BLE_HS_ENOMEM) {
        return ESP_ERR_NO_MEM;
    }
    if (rc != 0) {
        ESP_LOGW(TAG, "BATTLE notification failed (%d)", rc);
        return ESP_FAIL;
    }
    return ESP_OK;
}

void pukk_ble_battle_disconnect(void)
{
    uint16_t conn = s_battle_conn;
    if (conn != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
    }
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
    protocol_ctrl_write(conn, s_write_buf, len);
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
    protocol_data_write(conn, s_write_buf, len);
    return 0;
}

static int battle_access_cb(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt,
                            void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    int len = flatten_write(ctxt);
    if (len < 0) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    s_battle_conn = conn;
    const pukk_ble_event_t event = {
        .type = PUKK_BLE_EVENT_BATTLE_RX,
        .data = s_write_buf,
        .len = (size_t)len,
    };
    ble_emit(&event);
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
        .uuid = &ble_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &ble_ctrl_uuid.u,
                .access_cb = ctrl_access_cb,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_ctrl_handle,
            },
            {
                .uuid = &ble_data_uuid.u,
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
            {
                /* The host writes without response; ble_central.c relies on
                 * the CCCD directly following the value. */
                .uuid = &ble_battle_uuid.u,
                .access_cb = battle_access_cb,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_battle_handle,
            },
            { 0 },
        },
    },
    { 0 },
};

static void log_conn_params(uint16_t conn, const char *what)
{
    struct ble_gap_conn_desc desc;
    if (ble_gap_conn_find(conn, &desc) == 0) {
        ESP_LOGI(TAG, "%s: interval %u.%02u ms, latency %u, timeout %u ms", what,
                 desc.conn_itvl * 125 / 100, desc.conn_itvl * 125 % 100, desc.conn_latency,
                 desc.supervision_timeout * 10);
    }
}

/*
 * Ask for a fast link for uploads. Without Data Length Extension every
 * 244-byte write is split into ten 27-byte link-layer packets; 2M PHY halves
 * the airtime and a short interval gives more transmit opportunities. The
 * central decides; the results are logged from the GAP events.
 */
void ble_request_fast_link(uint16_t conn, bool params)
{
    int rc = ble_gap_set_data_len(conn, LL_MAX_OCTETS, LL_MAX_TIME_US);
    if (rc != 0) {
        ESP_LOGW(TAG, "data length request failed (%d)", rc);
    }
    rc = ble_gap_set_prefered_le_phy(conn, BLE_GAP_LE_PHY_2M_MASK, BLE_GAP_LE_PHY_2M_MASK,
                                     BLE_GAP_LE_PHY_CODED_ANY);
    if (rc != 0) {
        ESP_LOGW(TAG, "PHY request failed (%d)", rc);
    }
    if (!params) {
        return;
    }
    const struct ble_gap_upd_params upd = {
        .itvl_min = BLE_GAP_CONN_ITVL_MS(CONN_ITVL_MIN_MS),
        .itvl_max = BLE_GAP_CONN_ITVL_MS(CONN_ITVL_MAX_MS),
        .latency = 0,
        .supervision_timeout = BLE_GAP_SUPERVISION_TIMEOUT_MS(CONN_TIMEOUT_MS),
    };
    rc = ble_gap_update_params(conn, &upd);
    if (rc != 0) {
        ESP_LOGW(TAG, "connection parameter request failed (%d)", rc);
    }
}

static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            uint16_t conn = event->connect.conn_handle;
            client_t *c = NULL;
            for (int i = 0; !c && i < MAX_LINKS; i++) {
                if (s_clients[i].conn == BLE_HS_CONN_HANDLE_NONE) {
                    c = &s_clients[i];
                }
            }
            if (!c) {
                ESP_LOGW(TAG, "no room for connection %u", conn);
                ble_gap_terminate(conn, BLE_ERR_CONN_LIMIT);
                break;
            }
            *c = (client_t) { .conn = conn };
            ESP_LOGI(TAG, "client connected (handle %u)", conn);
            log_conn_params(conn, "connection");
            ble_request_fast_link(conn, true);
            emit_simple(PUKK_BLE_EVENT_CONNECTED);
        } else {
            ESP_LOGW(TAG, "connect failed (status %d)", event->connect.status);
        }
        advertise();
        break;

    case BLE_GAP_EVENT_DISCONNECT: {
        uint16_t conn = event->disconnect.conn.conn_handle;
        ESP_LOGI(TAG, "client %u disconnected (reason 0x%03x)", conn, event->disconnect.reason);
        protocol_disconnected(conn);
        client_t *c = find_client(conn);
        if (c) {
            c->conn = BLE_HS_CONN_HANDLE_NONE;
        }
        emit_simple(PUKK_BLE_EVENT_DISCONNECTED);
        if (conn == s_battle_conn) {
            s_battle_conn = BLE_HS_CONN_HANDLE_NONE;
            emit_simple(PUKK_BLE_EVENT_BATTLE_HOST_LOST);
        }
        advertise();
        break;
    }

    case BLE_GAP_EVENT_ADV_COMPLETE:
        advertise();
        break;

    case BLE_GAP_EVENT_SUBSCRIBE: {
        client_t *c = find_client(event->subscribe.conn_handle);
        if (!c) {
            break;
        }
        if (event->subscribe.attr_handle == s_ctrl_handle) {
            c->ctrl_subscribed = event->subscribe.cur_notify;
            ESP_LOGI(TAG, "CTRL notifications %s", c->ctrl_subscribed ? "on" : "off");
        } else if (event->subscribe.attr_handle == s_battle_handle) {
            c->battle_subscribed = event->subscribe.cur_notify;
            ESP_LOGI(TAG, "BATTLE notifications %s", c->battle_subscribed ? "on" : "off");
        }
        break;
    }

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU %u", event->mtu.value);
        break;

    case BLE_GAP_EVENT_CONN_UPDATE:
        log_conn_params(event->conn_update.conn_handle, "connection updated");
        break;

    case BLE_GAP_EVENT_PHY_UPDATE_COMPLETE:
        ESP_LOGI(TAG, "PHY: tx %uM, rx %uM (status %d)", event->phy_updated.tx_phy,
                 event->phy_updated.rx_phy, event->phy_updated.status);
        break;

    case BLE_GAP_EVENT_DATA_LEN_CHG:
        ESP_LOGI(TAG, "data length: tx %u, rx %u bytes", event->data_len_chg.max_tx_octets,
                 event->data_len_chg.max_rx_octets);
        break;

    default:
        break;
    }
    return 0;
}

/* (Re)start advertising with the current joinable flag. With a link up we
 * only advertise to let a battle host in. */
static void advertise(void)
{
    if (!s_synced) {
        return;
    }
    if (ble_gap_adv_active()) {
        ble_gap_adv_stop();
    }
    int links = client_count() + ble_central_link_count();
    if (links >= MAX_LINKS || (links > 0 && !s_joinable)) {
        return;
    }

    /* Advertising packet: flags + service UUID (21 of 31 bytes), and when
     * joinable the manufacturer data (7 more). */
    static const uint8_t join_mfg[BLE_JOIN_MFG_LEN] = { 0xFF, 0xFF, 'P', 'K', BLE_JOIN_STATE };
    struct ble_hs_adv_fields adv = {
        .flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP,
        .uuids128 = &ble_svc_uuid,
        .num_uuids128 = 1,
        .uuids128_is_complete = 1,
        .mfg_data = s_joinable ? join_mfg : NULL,
        .mfg_data_len = s_joinable ? sizeof(join_mfg) : 0,
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

void ble_links_changed(void)
{
    advertise();
}

static void adv_event_fn(struct ble_npl_event *ev)
{
    advertise();
}

void pukk_ble_set_joinable(bool joinable)
{
    if (joinable == s_joinable) {
        return;
    }
    s_joinable = joinable;
    ESP_LOGI(TAG, "%s", joinable ? "joinable: waiting for a battle host" : "not joinable");
    /* Advertising state belongs to the host task. */
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &s_adv_event);
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

    uint8_t addr[6] = { 0 };
    ble_hs_id_copy_addr(s_own_addr_type, addr, NULL);
    ble_pukk_name(addr, s_name, sizeof(s_name));
    ble_svc_gap_device_name_set(s_name);
    s_synced = true;

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
    for (int i = 0; i < MAX_LINKS; i++) {
        s_clients[i].conn = BLE_HS_CONN_HANDLE_NONE;
    }
    s_event_cb = cb;
    s_event_ctx = ctx;
    ESP_RETURN_ON_ERROR(nimble_port_init(), TAG, "NimBLE init failed");
    ble_npl_event_init(&s_adv_event, adv_event_fn, NULL);
    ble_central_init();

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
