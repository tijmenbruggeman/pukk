/*
 * Central role for battles (docs/BATTLE.md): the host scans for a Pukk that
 * advertises the joinable flag, connects to the first one found and sets up
 * the link: MTU exchange, service discovery and notifications on CTRL and
 * BATTLE.
 *
 * The link state belongs to the NimBLE host task; requests from other tasks
 * are handed to it through its event queue. Writes may come from any task.
 */
#include <string.h>
#include "esp_log.h"
#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "ble_priv.h"

static const char *TAG = "ble-central";

#define SCAN_ITVL_MS        60
#define SCAN_WINDOW_MS      30
#define CONNECT_TIMEOUT_MS  5000
/* Short interval, so answers reach the host quickly. */
#define LINK_ITVL_MS        15
#define LINK_TIMEOUT_MS     4000

typedef enum {
    PEER_IDLE,
    PEER_SCANNING,
    PEER_CONNECTING,
    PEER_SETUP,     /* connected: MTU, discovery, subscriptions */
    PEER_READY,
    PEER_CLOSING,   /* waiting for the disconnect */
} peer_state_t;

static struct {
    peer_state_t state;
    uint16_t conn;
    char name[16];
    uint16_t svc_start;
    uint16_t svc_end;
    uint16_t ctrl;      /* value handles */
    uint16_t data;
    uint16_t battle;
    bool find_pending;  /* find requested while closing */
} s_peer = { .conn = BLE_HS_CONN_HANDLE_NONE };

static struct ble_npl_event s_find_event;
static struct ble_npl_event s_close_event;
/* Only touched from the host task. */
static uint8_t s_rx_buf[BLE_WRITE_MAX_LEN];

static const struct ble_gap_conn_params s_conn_params = {
    .scan_itvl = BLE_GAP_SCAN_ITVL_MS(SCAN_ITVL_MS),
    .scan_window = BLE_GAP_SCAN_WIN_MS(SCAN_ITVL_MS),
    .itvl_min = BLE_GAP_CONN_ITVL_MS(LINK_ITVL_MS),
    .itvl_max = BLE_GAP_CONN_ITVL_MS(LINK_ITVL_MS),
    .latency = 0,
    .supervision_timeout = BLE_GAP_SUPERVISION_TIMEOUT_MS(LINK_TIMEOUT_MS),
};

static int gap_cb(struct ble_gap_event *event, void *arg);

static void emit(pukk_ble_event_type_t type, const uint8_t *data, size_t len)
{
    const pukk_ble_event_t event = {
        .type = type,
        .name = s_peer.name,
        .data = data,
        .len = len,
    };
    ble_emit(&event);
}

int ble_central_link_count(void)
{
    return s_peer.conn != BLE_HS_CONN_HANDLE_NONE ? 1 : 0;
}

static void start_scan(void)
{
    const struct ble_gap_disc_params params = {
        .itvl = BLE_GAP_SCAN_ITVL_MS(SCAN_ITVL_MS),
        .window = BLE_GAP_SCAN_WIN_MS(SCAN_WINDOW_MS),
        .passive = 1,   /* the joinable flag is in the advertisement itself */
        .filter_duplicates = 1,
    };
    int rc = ble_gap_disc(ble_own_addr_type(), BLE_HS_FOREVER, &params, gap_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "scan failed (%d)", rc);
        s_peer.state = PEER_IDLE;
        emit(PUKK_BLE_EVENT_PEER_DISCONNECTED, NULL, 0);
        return;
    }
    s_peer.state = PEER_SCANNING;
    ESP_LOGI(TAG, "looking for a Pukk that wants to join");
}

/* Give up on the current link; the disconnect event reports it. */
static void fail_link(const char *what, int rc)
{
    ESP_LOGW(TAG, "%s: %s failed (%d)", s_peer.name, what, rc);
    ble_gap_terminate(s_peer.conn, BLE_ERR_REM_USER_CONN_TERM);
}

static bool is_joinable(const struct ble_gap_disc_desc *desc)
{
    if (desc->event_type != BLE_HCI_ADV_RPT_EVTYPE_ADV_IND) {
        return false;
    }
    struct ble_hs_adv_fields fields;
    if (ble_hs_adv_parse_fields(&fields, desc->data, desc->length_data) != 0) {
        return false;
    }
    static const uint8_t prefix[] = { 0xFF, 0xFF, 'P', 'K' };
    return fields.mfg_data_len >= BLE_JOIN_MFG_LEN &&
           memcmp(fields.mfg_data, prefix, sizeof(prefix)) == 0 &&
           fields.mfg_data[sizeof(prefix)] == BLE_JOIN_STATE;
}

/* --- link setup: MTU > service > characteristics > subscriptions ----------- */

static int subscribe_cb(uint16_t conn, const struct ble_gatt_error *error,
                        struct ble_gatt_attr *attr, void *arg);

/* Turn on notifications. Pukks put the CCCD right after the value (see the
 * service table in pukk_ble.c), so no descriptor discovery is needed. */
static void subscribe(uint16_t value_handle, int step)
{
    static const uint8_t on[] = { 0x01, 0x00 };
    int rc = ble_gattc_write_flat(s_peer.conn, value_handle + 1, on, sizeof(on), subscribe_cb,
                                  (void *)(intptr_t)step);
    if (rc != 0) {
        fail_link("subscribe", rc);
    }
}

static int subscribe_cb(uint16_t conn, const struct ble_gatt_error *error,
                        struct ble_gatt_attr *attr, void *arg)
{
    if (error->status != 0) {
        fail_link("subscribe", error->status);
        return 0;
    }
    if ((intptr_t)arg == 0) {
        subscribe(s_peer.battle, 1);
        return 0;
    }
    s_peer.state = PEER_READY;
    ESP_LOGI(TAG, "%s ready (MTU %u)", s_peer.name, ble_att_mtu(conn));
    ble_request_fast_link(conn, false);
    emit(PUKK_BLE_EVENT_PEER_CONNECTED, NULL, 0);
    return 0;
}

static int chr_cb(uint16_t conn, const struct ble_gatt_error *error,
                  const struct ble_gatt_chr *chr, void *arg)
{
    if (error->status == 0) {
        if (ble_uuid_cmp(&chr->uuid.u, &ble_ctrl_uuid.u) == 0) {
            s_peer.ctrl = chr->val_handle;
        } else if (ble_uuid_cmp(&chr->uuid.u, &ble_data_uuid.u) == 0) {
            s_peer.data = chr->val_handle;
        } else if (ble_uuid_cmp(&chr->uuid.u, &ble_battle_uuid.u) == 0) {
            s_peer.battle = chr->val_handle;
        }
    } else if (error->status == BLE_HS_EDONE) {
        if (!s_peer.ctrl || !s_peer.data || !s_peer.battle) {
            fail_link("finding the characteristics", BLE_HS_ENOENT);
        } else {
            subscribe(s_peer.ctrl, 0);
        }
    } else {
        fail_link("characteristic discovery", error->status);
    }
    return 0;
}

static int svc_cb(uint16_t conn, const struct ble_gatt_error *error,
                  const struct ble_gatt_svc *svc, void *arg)
{
    if (error->status == 0) {
        s_peer.svc_start = svc->start_handle;
        s_peer.svc_end = svc->end_handle;
    } else if (error->status == BLE_HS_EDONE) {
        int rc = s_peer.svc_end == 0 ? BLE_HS_ENOENT
                 : ble_gattc_disc_all_chrs(conn, s_peer.svc_start, s_peer.svc_end, chr_cb, NULL);
        if (rc != 0) {
            fail_link("finding the Pukk service", rc);
        }
    } else {
        fail_link("service discovery", error->status);
    }
    return 0;
}

static int mtu_cb(uint16_t conn, const struct ble_gatt_error *error, uint16_t mtu, void *arg)
{
    /* Battle messages do not fit in the default MTU. */
    int rc = error->status;
    if (rc == 0) {
        rc = ble_gattc_disc_svc_by_uuid(conn, &ble_svc_uuid.u, svc_cb, NULL);
    }
    if (rc != 0) {
        fail_link("MTU exchange", rc);
    }
    return 0;
}

/* --- GAP events for the scan and the peer link ------------------------------ */

static void on_connect(const struct ble_gap_event *event)
{
    if (event->connect.status != 0) {
        bool closing = s_peer.state == PEER_CLOSING;
        s_peer.state = PEER_IDLE;
        if (!closing) {
            ESP_LOGW(TAG, "connecting to %s failed (%d)", s_peer.name, event->connect.status);
            emit(PUKK_BLE_EVENT_PEER_DISCONNECTED, NULL, 0);
        }
        return;
    }
    s_peer.conn = event->connect.conn_handle;
    ble_links_changed();
    if (s_peer.state == PEER_CLOSING) {
        ble_gap_terminate(s_peer.conn, BLE_ERR_REM_USER_CONN_TERM);
        return;
    }
    s_peer.state = PEER_SETUP;
    ESP_LOGI(TAG, "connected to %s (handle %u)", s_peer.name, s_peer.conn);
    int rc = ble_gattc_exchange_mtu(s_peer.conn, mtu_cb, NULL);
    if (rc != 0) {
        fail_link("MTU exchange", rc);
    }
}

static void on_disconnect(const struct ble_gap_event *event)
{
    bool closing = s_peer.state == PEER_CLOSING;
    ESP_LOGI(TAG, "%s disconnected (reason 0x%03x)", s_peer.name, event->disconnect.reason);
    s_peer.conn = BLE_HS_CONN_HANDLE_NONE;
    s_peer.state = PEER_IDLE;
    ble_links_changed();
    if (!closing) {
        emit(PUKK_BLE_EVENT_PEER_DISCONNECTED, NULL, 0);
    } else if (s_peer.find_pending) {
        s_peer.find_pending = false;
        start_scan();
    }
}

static void on_disc(const struct ble_gap_disc_desc *desc)
{
    if (s_peer.state != PEER_SCANNING || !is_joinable(desc)) {
        return;
    }
    ble_gap_disc_cancel();
    ble_pukk_name(desc->addr.val, s_peer.name, sizeof(s_peer.name));
    s_peer.svc_start = s_peer.svc_end = 0;
    s_peer.ctrl = s_peer.data = s_peer.battle = 0;
    ESP_LOGI(TAG, "found %s, connecting", s_peer.name);

    int rc = ble_gap_connect(ble_own_addr_type(), &desc->addr, CONNECT_TIMEOUT_MS, &s_conn_params,
                             gap_cb, NULL);
    if (rc != 0) {
        ESP_LOGW(TAG, "connect to %s failed (%d)", s_peer.name, rc);
        start_scan();
        return;
    }
    s_peer.state = PEER_CONNECTING;
}

static void on_notify(const struct ble_gap_event *event)
{
    uint16_t len = 0;
    if (ble_hs_mbuf_to_flat(event->notify_rx.om, s_rx_buf, sizeof(s_rx_buf), &len) != 0) {
        return;
    }
    if (event->notify_rx.attr_handle == s_peer.battle) {
        emit(PUKK_BLE_EVENT_PEER_BATTLE_RX, s_rx_buf, len);
    } else if (event->notify_rx.attr_handle == s_peer.ctrl) {
        emit(PUKK_BLE_EVENT_PEER_CTRL_RX, s_rx_buf, len);
    }
}

static int gap_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_DISC:
        on_disc(&event->disc);
        break;
    case BLE_GAP_EVENT_CONNECT:
        on_connect(event);
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        on_disconnect(event);
        break;
    case BLE_GAP_EVENT_NOTIFY_RX:
        on_notify(event);
        break;
    case BLE_GAP_EVENT_CONN_UPDATE_REQ:
    case BLE_GAP_EVENT_L2CAP_UPDATE_REQ:
        /* The guest asks for 15-30 ms, as it does with phones; keep it short. */
        event->conn_update_req.self_params->itvl_min = BLE_GAP_CONN_ITVL_MS(LINK_ITVL_MS);
        event->conn_update_req.self_params->itvl_max = BLE_GAP_CONN_ITVL_MS(LINK_ITVL_MS);
        return 0;
    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU %u", event->mtu.value);
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

/* --- requests from other tasks ----------------------------------------------- */

static void find_fn(struct ble_npl_event *ev)
{
    if (s_peer.state == PEER_CLOSING) {
        s_peer.find_pending = true;
    } else if (s_peer.state == PEER_IDLE) {
        start_scan();
    }
}

static void close_fn(struct ble_npl_event *ev)
{
    s_peer.find_pending = false;
    switch (s_peer.state) {
    case PEER_SCANNING:
        ble_gap_disc_cancel();
        s_peer.state = PEER_IDLE;
        break;
    case PEER_CONNECTING:
        /* Ends with a failed connect event, or a connect we then drop. */
        s_peer.state = PEER_CLOSING;
        ble_gap_conn_cancel();
        break;
    case PEER_SETUP:
    case PEER_READY:
        s_peer.state = PEER_CLOSING;
        ble_gap_terminate(s_peer.conn, BLE_ERR_REM_USER_CONN_TERM);
        break;
    default:
        break;
    }
}

esp_err_t pukk_ble_peer_find(void)
{
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &s_find_event);
    return ESP_OK;
}

void pukk_ble_peer_close(void)
{
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &s_close_event);
}

static int write_cb(uint16_t conn, const struct ble_gatt_error *error, struct ble_gatt_attr *attr,
                    void *arg)
{
    if (error->status != 0) {
        ESP_LOGW(TAG, "write to %s failed (%d)", s_peer.name, error->status);
    }
    return 0;
}

esp_err_t pukk_ble_peer_write(pukk_ble_peer_chr_t chr, const uint8_t *data, size_t len)
{
    if (s_peer.state != PEER_READY || len > BLE_WRITE_MAX_LEN) {
        return ESP_ERR_INVALID_STATE;
    }
    int rc;
    switch (chr) {
    case PUKK_BLE_PEER_CTRL:
        rc = ble_gattc_write_flat(s_peer.conn, s_peer.ctrl, data, len, write_cb, NULL);
        break;
    case PUKK_BLE_PEER_DATA:
        rc = ble_gattc_write_no_rsp_flat(s_peer.conn, s_peer.data, data, len);
        break;
    case PUKK_BLE_PEER_BATTLE:
        rc = ble_gattc_write_no_rsp_flat(s_peer.conn, s_peer.battle, data, len);
        break;
    default:
        return ESP_ERR_INVALID_ARG;
    }
    if (rc == BLE_HS_ENOMEM) {
        return ESP_ERR_NO_MEM;
    }
    return rc == 0 ? ESP_OK : ESP_FAIL;
}

size_t pukk_ble_peer_max_write(void)
{
    uint16_t mtu = s_peer.conn != BLE_HS_CONN_HANDLE_NONE ? ble_att_mtu(s_peer.conn) : 0;
    return mtu > 3 ? mtu - 3 : 0;
}

void ble_central_init(void)
{
    ble_npl_event_init(&s_find_event, find_fn, NULL);
    ble_npl_event_init(&s_close_event, close_fn, NULL);
}
