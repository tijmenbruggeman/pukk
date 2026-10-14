/*
 * Battle engine (docs/BATTLE.md): lobby, pack transfer, questions and results
 * for both roles. Everything runs in one task fed by a queue, so the state
 * below needs no locking.
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "packs.h"
#include "battle.h"
#include "battle_msg.h"
#include "battle_rules.h"

static const char *TAG = "battle";

#define TASK_STACK          5120
#define TASK_PRIO           5
#define QUEUE_LEN           16
#define QUESTION_POLL_MS    10
#define HELLO_TIMEOUT_MS    5000
#define SEARCH_RETRY_MS     500
#define SEND_RETRIES        20
#define SEND_RETRY_MS       5
/* Let a goodbye message go out before the link is dropped. */
#define LEAVE_FLUSH_MS      150

/* Sending the pack to the guest: the upload protocol of docs/PACKS.md. */
#define UP_WINDOW           8192
#define UP_CHUNK_MAX        508     /* largest ATT value minus the offset */
#define UP_STALL_MS         2000
#define UP_MAX_STALLS       5
#define UP_REPLY_MS         5000
#define UP_RETRY_MS         5
#define UP_POLL_MS          100
#define UP_BEGIN            0x01
#define UP_END              0x02
#define UP_ABORT            0x03
#define UP_RSP_OK           0x80
#define UP_RSP_PROGRESS     0x81
#define UP_RSP_RESEND       0x82
#define UP_RSP_ERROR        0x8F

typedef enum {
    IN_HOST,
    IN_JOIN,
    IN_START,
    IN_ANSWER,
    IN_LEAVE,
    IN_PEER_CONNECTED,
    IN_PEER_DISCONNECTED,
    IN_PEER_BATTLE,
    IN_PEER_CTRL,
    IN_HOST_MSG,
    IN_HOST_LOST,
} input_kind_t;

typedef struct {
    uint8_t kind;
    uint8_t choice;
    uint8_t len;
    int64_t time_us;
    char name[BATTLE_PACK_NAME_MAX + 1];    /* pack (IN_HOST) or peer name */
    uint8_t data[BATTLE_MSG_MAX];
} input_t;

typedef enum {
    ST_IDLE,
    ST_SEARCHING,
    ST_HELLO,       /* host: waiting for HELLO_ACK */
    ST_TRANSFER,    /* host: sending the pack; guest: receiving it */
    ST_LOBBY,
    ST_COUNTDOWN,
    ST_QUESTION,
    ST_REVEAL,
    ST_FINISHED,
} state_t;

typedef enum {
    PHASE_BEGIN,
    PHASE_DATA,
    PHASE_END,
} up_phase_t;

static struct {
    state_t state;
    battle_role_t role;
    char pack[BATTLE_PACK_NAME_MAX + 1];
    char title[PACK_TITLE_MAX + 1];
    uint32_t pack_crc;
    uint16_t count;
    char opponent[BATTLE_DEVICE_NAME_MAX + 1];
    uint16_t index;
    pack_question_t q;
    int64_t t0;             /* when the current question appeared here */
    int64_t deadline;       /* next timed step; 0 = none */
    battle_round_t round;   /* host */
    bool answered;          /* guest: answer sent for this question */
    uint32_t score[2];      /* by role: BATTLE_HOST, BATTLE_GUEST */
    struct {
        packs_file_t *file;
        up_phase_t phase;
        uint32_t size;
        uint32_t crc;
        uint32_t sent;
        uint32_t acked;
        uint16_t chunk;
        int64_t last_reply;
        int stalls;
        bool retry;         /* out of BLE buffers: try again shortly */
    } up;
} b;

static QueueHandle_t s_queue;
static battle_event_cb_t s_cb;
static void *s_cb_ctx;
static uint8_t s_up_buf[4 + UP_CHUNK_MAX];

static int64_t now_us(void)
{
    return esp_timer_get_time();
}

static bool hosting(void)
{
    return b.state != ST_IDLE && b.role == BATTLE_ROLE_HOST;
}

static bool joining(void)
{
    return b.state != ST_IDLE && b.role == BATTLE_ROLE_GUEST;
}

/* Our index in protocol terms (score, choices). */
static int me(void)
{
    return b.role == BATTLE_ROLE_HOST ? BATTLE_HOST : BATTLE_GUEST;
}

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

/* --- events to the UI --------------------------------------------------------- */

static void init_event(battle_event_t *e, battle_event_type_t type)
{
    memset(e, 0, sizeof(*e));
    e->type = type;
    e->role = b.role;
    e->opponent = b.opponent;
    e->title = b.title[0] ? b.title : b.pack;
    e->score[BATTLE_ME] = b.score[me()];
    e->score[BATTLE_THEM] = b.score[1 - me()];
}

static void emit(const battle_event_t *e)
{
    if (s_cb) {
        s_cb(e, s_cb_ctx);
    }
}

static void emit_lobby(battle_lobby_t status, uint32_t sent, uint32_t total)
{
    battle_event_t e;
    init_event(&e, BATTLE_EVENT_LOBBY);
    e.lobby.status = status;
    e.lobby.sent = sent;
    e.lobby.total = total;
    emit(&e);
}

/* --- messages to the other Pukk ---------------------------------------------- */

static bool send(const battle_msg_t *m)
{
    uint8_t buf[BATTLE_MSG_MAX];
    size_t len = battle_msg_encode(m, buf, sizeof(buf));
    if (len == 0) {
        ESP_LOGE(TAG, "cannot encode message 0x%02x", m->type);
        return false;
    }
    for (int i = 0; i < SEND_RETRIES; i++) {
        esp_err_t err = b.role == BATTLE_ROLE_HOST ? pukk_ble_peer_write(PUKK_BLE_PEER_BATTLE, buf, len)
                                                   : pukk_ble_battle_notify(buf, len);
        if (err != ESP_ERR_NO_MEM) {
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "sending 0x%02x failed: %s", m->type, esp_err_to_name(err));
            }
            return err == ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(SEND_RETRY_MS));
    }
    ESP_LOGW(TAG, "sending 0x%02x failed: out of buffers", m->type);
    return false;
}

static void send_abort(battle_abort_reason_t reason)
{
    battle_msg_t m = { .type = BATTLE_MSG_ABORT, .abort.reason = reason };
    if (send(&m)) {
        vTaskDelay(pdMS_TO_TICKS(LEAVE_FLUSH_MS));
    }
}

/* --- ending ------------------------------------------------------------------- */

static void close_upload_file(void)
{
    packs_file_close(b.up.file);
    b.up.file = NULL;
}

static void stop(battle_stop_t reason)
{
    ESP_LOGI(TAG, "battle over (reason %d)", reason);
    if (b.role == BATTLE_ROLE_HOST) {
        pukk_ble_peer_close();
    } else {
        pukk_ble_set_joinable(false);
        pukk_ble_battle_disconnect();
    }
    close_upload_file();
    battle_event_t e;
    init_event(&e, BATTLE_EVENT_STOPPED);
    e.stopped.reason = reason;
    emit(&e);
    memset(&b, 0, sizeof(b));
}

/* Also when idle: the UI waits for STOPPED to go home. */
static void leave(void)
{
    /* Tell the other one, unless there is no link or the game is over. */
    if (b.state > ST_SEARCHING && b.state != ST_FINISHED) {
        if (b.role == BATTLE_ROLE_HOST) {
            send_abort(BATTLE_ABORT_STOPPED);
        } else {
            battle_msg_t m = { .type = BATTLE_MSG_LEAVE };
            if (send(&m)) {
                vTaskDelay(pdMS_TO_TICKS(LEAVE_FLUSH_MS));
            }
        }
    }
    stop(BATTLE_STOP_LEFT);
}

/* --- questions (both roles) ---------------------------------------------------- */

static void show_question(uint16_t index)
{
    b.index = index;
    battle_event_t e;
    init_event(&e, BATTLE_EVENT_QUESTION);
    e.question.q = &b.q;
    e.question.index = index;
    e.question.count = b.count;
    emit(&e);
    /* The UI has built the view: the question is up from now. */
    b.t0 = now_us();
    b.answered = false;
    b.state = ST_QUESTION;
    b.deadline = 0;
}

/* `winner` and `choice` in protocol terms (host, guest). */
static void show_round(uint8_t winner, const uint8_t choice[2], uint16_t points)
{
    battle_event_t e;
    init_event(&e, BATTLE_EVENT_ROUND);
    e.round.index = b.index;
    e.round.correct = b.q.correct;
    e.round.winner = winner == BATTLE_NOBODY ? BATTLE_NO_WINNER : winner == me() ? BATTLE_ME : BATTLE_THEM;
    e.round.choice[BATTLE_ME] = choice[me()];
    e.round.choice[BATTLE_THEM] = choice[1 - me()];
    e.round.points = points;
    emit(&e);
    b.state = ST_REVEAL;
}

static void show_finished(void)
{
    ESP_LOGI(TAG, "final score: host %lu, guest %lu", (unsigned long)b.score[BATTLE_HOST],
             (unsigned long)b.score[BATTLE_GUEST]);
    b.state = ST_FINISHED;
    b.deadline = 0;
    battle_event_t e;
    init_event(&e, BATTLE_EVENT_FINISHED);
    emit(&e);
}

static void show_countdown(uint8_t seconds)
{
    b.score[0] = b.score[1] = 0;
    b.state = ST_COUNTDOWN;
    battle_event_t e;
    init_event(&e, BATTLE_EVENT_COUNTDOWN);
    e.countdown.seconds = seconds;
    emit(&e);
}

static void on_answer(uint8_t choice, int64_t time_us)
{
    if (b.state != ST_QUESTION || choice >= b.q.answer_count) {
        return;
    }
    uint32_t reaction_ms = time_us > b.t0 ? (uint32_t)((time_us - b.t0) / 1000) : 0;
    if (b.role == BATTLE_ROLE_HOST) {
        if (battle_round_answer(&b.round, BATTLE_HOST, choice, reaction_ms)) {
            ESP_LOGI(TAG, "question %u: host chose %u after %lu ms", b.index + 1, choice,
                     (unsigned long)reaction_ms);
        }
    } else if (!b.answered && reaction_ms <= b.q.time_s * 1000u) {
        b.answered = true;
        battle_msg_t m = {
            .type = BATTLE_MSG_ANSWER,
            .answer = { .index = b.index, .choice = choice, .reaction_ms = (uint16_t)reaction_ms },
        };
        send(&m);
    }
}

/* --- host ----------------------------------------------------------------------- */

static void host_search(void)
{
    close_upload_file();
    b.opponent[0] = '\0';
    b.state = ST_SEARCHING;
    b.deadline = 0;
    emit_lobby(BATTLE_LOBBY_SEARCHING, 0, 0);
    pukk_ble_peer_find();
}

static void host_begin(const char *pack)
{
    if (b.state != ST_IDLE) {
        ESP_LOGW(TAG, "already in a battle");
        return;
    }
    b.role = BATTLE_ROLE_HOST;
    snprintf(b.pack, sizeof(b.pack), "%s", pack);
    packs_entry_t info;
    packs_err_t err = packs_info(pack, &info);
    if (err != PACKS_OK) {
        ESP_LOGW(TAG, "cannot host %s: %s", pack, packs_err_name(err));
        stop(BATTLE_STOP_PACK);
        return;
    }
    snprintf(b.title, sizeof(b.title), "%s", info.title);
    b.pack_crc = info.crc32;
    b.count = info.question_count;
    ESP_LOGI(TAG, "hosting %s (%u questions)", b.pack, b.count);
    host_search();
}

static void host_hello(void)
{
    battle_msg_t m = {
        .type = BATTLE_MSG_HELLO,
        .hello = { .version = BATTLE_PROTOCOL_VERSION, .pack_crc = b.pack_crc, .question_count = b.count },
    };
    snprintf(m.hello.pack, sizeof(m.hello.pack), "%s", b.pack);
    snprintf(m.hello.host, sizeof(m.hello.host), "%s", pukk_ble_device_name());
    send(&m);
    b.state = ST_HELLO;
    b.deadline = now_us() + HELLO_TIMEOUT_MS * 1000LL;
}

static void host_question(uint16_t index)
{
    packs_err_t err = packs_read_question(b.pack, index, &b.q);
    if (err != PACKS_OK) {
        ESP_LOGW(TAG, "%s question %u: %s", b.pack, index, packs_err_name(err));
        send_abort(BATTLE_ABORT_STOPPED);
        stop(BATTLE_STOP_PACK);
        return;
    }
    battle_msg_t m = { .type = BATTLE_MSG_QUESTION, .question.index = index };
    send(&m);
    show_question(index);
    battle_round_start(&b.round, b.q.correct, b.q.time_s * 1000u);
}

static void host_poll_round(void)
{
    uint32_t now_ms = (uint32_t)((now_us() - b.t0) / 1000);
    if (!battle_round_update(&b.round, now_ms)) {
        return;
    }
    const battle_round_t *r = &b.round;
    if (r->winner != BATTLE_NOBODY) {
        b.score[r->winner] += r->points;
    }
    ESP_LOGI(TAG, "question %u: winner %d, %u points (host %lu, guest %lu)", b.index + 1,
             r->winner == BATTLE_NOBODY ? -1 : r->winner, r->points,
             (unsigned long)b.score[BATTLE_HOST], (unsigned long)b.score[BATTLE_GUEST]);

    battle_msg_t m = {
        .type = BATTLE_MSG_RESULT,
        .result = {
            .index = b.index,
            .winner = r->winner,
            .points = r->points,
            .total = { b.score[BATTLE_HOST], b.score[BATTLE_GUEST] },
        },
    };
    uint8_t choice[2];
    for (int p = 0; p < 2; p++) {
        choice[p] = r->answer[p].choice;
        m.result.choice[p] = choice[p];
        m.result.reaction_ms[p] = r->answer[p].answered ? (uint16_t)r->answer[p].reaction_ms : 0;
    }
    send(&m);
    show_round(r->winner, choice, r->points);
    b.deadline = now_us() + BATTLE_REVEAL_MS * 1000LL;
}

static void host_finish(void)
{
    battle_msg_t m = {
        .type = BATTLE_MSG_END,
        .end.total = { b.score[BATTLE_HOST], b.score[BATTLE_GUEST] },
    };
    send(&m);
    show_finished();
}

static void transfer_failed(const char *why)
{
    ESP_LOGW(TAG, "sending %s failed: %s", b.pack, why);
    close_upload_file();
    const uint8_t abort_req[] = { UP_ABORT };
    pukk_ble_peer_write(PUKK_BLE_PEER_CTRL, abort_req, sizeof(abort_req));
    send_abort(BATTLE_ABORT_TRANSFER);
    stop(BATTLE_STOP_TRANSFER);
}

static void host_send_pack(void)
{
    uint32_t size;
    packs_err_t err = packs_file_open(b.pack, &b.up.file, &size);
    if (err != PACKS_OK) {
        transfer_failed(packs_err_name(err));
        return;
    }
    /* BEGIN carries the CRC of the whole file. */
    uint32_t crc = 0;
    for (uint32_t off = 0; off < size;) {
        uint32_t n = size - off < 256 ? size - off : 256;
        if (packs_file_read(b.up.file, off, s_up_buf, n) != PACKS_OK) {
            transfer_failed("read error");
            return;
        }
        crc = pack_crc32(crc, s_up_buf, n);
        off += n;
    }
    size_t name_len = strlen(b.pack);
    uint8_t req[10 + BATTLE_PACK_NAME_MAX] = { UP_BEGIN };
    put_u32(req + 1, size);
    put_u32(req + 5, crc);
    req[9] = (uint8_t)name_len;
    memcpy(req + 10, b.pack, name_len);
    if (pukk_ble_peer_write(PUKK_BLE_PEER_CTRL, req, 10 + name_len) != ESP_OK) {
        transfer_failed("BEGIN not sent");
        return;
    }
    b.up.phase = PHASE_BEGIN;
    b.up.size = size;
    b.up.crc = crc;
    b.up.sent = b.up.acked = 0;
    b.up.stalls = 0;
    b.up.last_reply = now_us();
    b.state = ST_TRANSFER;
    b.deadline = 0;
    ESP_LOGI(TAG, "%s does not have %s: sending it (%lu bytes)", b.opponent, b.pack,
             (unsigned long)size);
    emit_lobby(BATTLE_LOBBY_TRANSFER, 0, size);
}

/* Responses on the guest's CTRL characteristic. */
static void host_on_ctrl(const uint8_t *d, size_t n)
{
    if (b.state != ST_TRANSFER || n < 2) {
        return;
    }
    switch (d[0]) {
    case UP_RSP_OK:
        if (d[1] == UP_BEGIN && n >= 4 && b.up.phase == PHASE_BEGIN) {
            size_t max = pukk_ble_peer_max_write();
            max = max > 4 ? max - 4 : 0;
            if (max > UP_CHUNK_MAX) {
                max = UP_CHUNK_MAX;
            }
            uint16_t chunk = (uint16_t)(d[2] | (d[3] << 8));
            b.up.chunk = chunk > max ? (uint16_t)max : chunk;
            if (b.up.chunk == 0) {
                transfer_failed("no room for data");
                return;
            }
            b.up.phase = PHASE_DATA;
            b.up.last_reply = now_us();
        } else if (d[1] == UP_END && b.up.phase == PHASE_END) {
            ESP_LOGI(TAG, "%s sent", b.pack);
            close_upload_file();
            host_hello(); /* again: now the guest has it */
        }
        break;
    case UP_RSP_PROGRESS:
        if (n >= 5 && get_u32(d + 1) > b.up.acked && get_u32(d + 1) <= b.up.size) {
            b.up.acked = get_u32(d + 1);
            if (b.up.sent < b.up.acked) {
                b.up.sent = b.up.acked;
            }
            b.up.last_reply = now_us();
            b.up.stalls = 0;
            emit_lobby(BATTLE_LOBBY_TRANSFER, b.up.acked, b.up.size);
            if (b.up.acked == b.up.size && b.up.phase == PHASE_DATA) {
                const uint8_t end_req[] = { UP_END };
                if (pukk_ble_peer_write(PUKK_BLE_PEER_CTRL, end_req, sizeof(end_req)) != ESP_OK) {
                    transfer_failed("END not sent");
                    return;
                }
                b.up.phase = PHASE_END;
            }
        }
        break;
    case UP_RSP_RESEND:
        if (n >= 5 && get_u32(d + 1) <= b.up.size) {
            b.up.sent = get_u32(d + 1);
            b.up.last_reply = now_us();
        }
        break;
    case UP_RSP_ERROR:
        transfer_failed(n >= 3 ? packs_err_name((packs_err_t)d[2]) : "error");
        break;
    default:
        break;
    }
}

/* Keep the window of unacknowledged data full; resend after a stall. */
static void host_pump_upload(void)
{
    b.up.retry = false;
    if (b.up.phase == PHASE_DATA) {
        while (b.up.sent < b.up.size && b.up.sent < b.up.acked + UP_WINDOW) {
            uint32_t n = b.up.size - b.up.sent;
            if (n > b.up.chunk) {
                n = b.up.chunk;
            }
            put_u32(s_up_buf, b.up.sent);
            if (packs_file_read(b.up.file, b.up.sent, s_up_buf + 4, n) != PACKS_OK) {
                transfer_failed("read error");
                return;
            }
            esp_err_t err = pukk_ble_peer_write(PUKK_BLE_PEER_DATA, s_up_buf, 4 + n);
            if (err == ESP_ERR_NO_MEM) {
                b.up.retry = true;
                break;
            }
            if (err != ESP_OK) {
                transfer_failed("write failed");
                return;
            }
            b.up.sent += n;
        }
    }
    int64_t quiet_ms = (now_us() - b.up.last_reply) / 1000;
    if (quiet_ms > (b.up.phase == PHASE_DATA ? UP_STALL_MS : UP_REPLY_MS)) {
        if (b.up.phase != PHASE_DATA || ++b.up.stalls > UP_MAX_STALLS) {
            transfer_failed("no reply");
            return;
        }
        ESP_LOGW(TAG, "transfer stalled at %lu bytes, resending", (unsigned long)b.up.acked);
        b.up.sent = b.up.acked;
        b.up.last_reply = now_us();
    }
}

static void host_on_message(const battle_msg_t *m)
{
    switch (m->type) {
    case BATTLE_MSG_HELLO_ACK:
        if (b.state != ST_HELLO) {
            break;
        }
        if (m->hello_ack.version != BATTLE_PROTOCOL_VERSION) {
            ESP_LOGW(TAG, "%s speaks battle version %u", b.opponent, m->hello_ack.version);
            send_abort(BATTLE_ABORT_VERSION);
            stop(BATTLE_STOP_VERSION);
        } else if (m->hello_ack.has_pack) {
            b.state = ST_LOBBY;
            b.deadline = 0;
            emit_lobby(BATTLE_LOBBY_READY, 0, 0);
        } else {
            host_send_pack();
        }
        break;

    case BATTLE_MSG_ANSWER:
        if (b.state == ST_QUESTION && m->answer.index == b.index && m->answer.choice < b.q.answer_count &&
            battle_round_answer(&b.round, BATTLE_GUEST, m->answer.choice, m->answer.reaction_ms)) {
            ESP_LOGI(TAG, "question %u: guest chose %u after %u ms", b.index + 1, m->answer.choice,
                     m->answer.reaction_ms);
        }
        break;

    case BATTLE_MSG_LEAVE:
        ESP_LOGI(TAG, "%s left", b.opponent);
        if (b.state >= ST_COUNTDOWN && b.state < ST_FINISHED) {
            stop(BATTLE_STOP_OPPONENT);
        } else if (b.state < ST_COUNTDOWN) {
            /* Still in the lobby: wait for someone else. */
            pukk_ble_peer_close();
            host_search();
        }
        break;

    default:
        break;
    }
}

static void host_on_disconnected(void)
{
    switch (b.state) {
    case ST_SEARCHING:
        /* Scanning or connecting failed: try again in a moment. */
        b.deadline = now_us() + SEARCH_RETRY_MS * 1000LL;
        break;
    case ST_HELLO:
    case ST_TRANSFER:
    case ST_LOBBY:
        ESP_LOGI(TAG, "%s went away before the start", b.opponent);
        host_search();
        break;
    case ST_COUNTDOWN:
    case ST_QUESTION:
    case ST_REVEAL:
        stop(BATTLE_STOP_LINK);
        break;
    default:
        break;
    }
}

static void host_on_deadline(void)
{
    switch (b.state) {
    case ST_SEARCHING:
        pukk_ble_peer_find();
        break;
    case ST_HELLO:
        ESP_LOGW(TAG, "%s did not answer HELLO", b.opponent);
        pukk_ble_peer_close();
        host_search();
        break;
    case ST_COUNTDOWN:
        host_question(0);
        break;
    case ST_REVEAL:
        if (b.index + 1 < b.count) {
            host_question(b.index + 1);
        } else {
            host_finish();
        }
        break;
    default:
        break;
    }
}

static void host_start(void)
{
    if (!hosting() || b.state != ST_LOBBY) {
        return;
    }
    battle_msg_t m = { .type = BATTLE_MSG_START, .start.countdown_s = BATTLE_COUNTDOWN_S };
    send(&m);
    show_countdown(BATTLE_COUNTDOWN_S);
    b.deadline = now_us() + BATTLE_COUNTDOWN_S * 1000000LL;
}

/* --- guest ---------------------------------------------------------------------- */

static void guest_begin(void)
{
    if (b.state != ST_IDLE) {
        ESP_LOGW(TAG, "already in a battle");
        return;
    }
    b.role = BATTLE_ROLE_GUEST;
    b.state = ST_SEARCHING;
    pukk_ble_set_joinable(true);
    emit_lobby(BATTLE_LOBBY_SEARCHING, 0, 0);
}

static void guest_hello(const battle_msg_t *m)
{
    if (b.state != ST_SEARCHING && b.state != ST_TRANSFER && b.state != ST_LOBBY) {
        return;
    }
    battle_msg_t ack = { .type = BATTLE_MSG_HELLO_ACK, .hello_ack.version = BATTLE_PROTOCOL_VERSION };
    if (m->hello.version != BATTLE_PROTOCOL_VERSION) {
        ESP_LOGW(TAG, "host speaks battle version %u", m->hello.version);
        send(&ack);
        vTaskDelay(pdMS_TO_TICKS(LEAVE_FLUSH_MS));
        stop(BATTLE_STOP_VERSION);
        return;
    }
    if (!packs_name_valid(m->hello.pack, strlen(m->hello.pack)) || m->hello.question_count == 0) {
        battle_msg_t leave_msg = { .type = BATTLE_MSG_LEAVE };
        send(&leave_msg);
        vTaskDelay(pdMS_TO_TICKS(LEAVE_FLUSH_MS));
        stop(BATTLE_STOP_PACK);
        return;
    }
    pukk_ble_set_joinable(false);
    snprintf(b.pack, sizeof(b.pack), "%s", m->hello.pack);
    snprintf(b.opponent, sizeof(b.opponent), "%s", m->hello.host);
    b.pack_crc = m->hello.pack_crc;
    b.count = m->hello.question_count;

    packs_entry_t info;
    bool has_pack = packs_info(b.pack, &info) == PACKS_OK && info.crc32 == b.pack_crc &&
                    info.question_count == b.count;
    snprintf(b.title, sizeof(b.title), "%s", has_pack ? info.title : "");
    ESP_LOGI(TAG, "%s invites us to play %s (%s)", b.opponent, b.pack,
             has_pack ? "we have it" : "we need it");
    ack.hello_ack.has_pack = has_pack;
    send(&ack);
    b.state = has_pack ? ST_LOBBY : ST_TRANSFER;
    emit_lobby(has_pack ? BATTLE_LOBBY_READY : BATTLE_LOBBY_TRANSFER, 0, 0);
}

static void guest_on_message(const battle_msg_t *m)
{
    switch (m->type) {
    case BATTLE_MSG_HELLO:
        guest_hello(m);
        break;

    case BATTLE_MSG_START:
        if (b.state == ST_LOBBY) {
            show_countdown(m->start.countdown_s);
        }
        break;

    case BATTLE_MSG_QUESTION: {
        if (b.state < ST_COUNTDOWN || b.state > ST_REVEAL || m->question.index >= b.count) {
            break;
        }
        packs_err_t err = packs_read_question(b.pack, m->question.index, &b.q);
        if (err != PACKS_OK) {
            ESP_LOGW(TAG, "%s question %u: %s", b.pack, m->question.index, packs_err_name(err));
            battle_msg_t leave_msg = { .type = BATTLE_MSG_LEAVE };
            send(&leave_msg);
            vTaskDelay(pdMS_TO_TICKS(LEAVE_FLUSH_MS));
            stop(BATTLE_STOP_PACK);
            break;
        }
        show_question(m->question.index);
        break;
    }

    case BATTLE_MSG_RESULT:
        if ((b.state == ST_QUESTION || b.state == ST_REVEAL) && m->result.index == b.index) {
            b.score[BATTLE_HOST] = m->result.total[0];
            b.score[BATTLE_GUEST] = m->result.total[1];
            show_round(m->result.winner, m->result.choice, m->result.points);
        }
        break;

    case BATTLE_MSG_END:
        if (b.state >= ST_COUNTDOWN && b.state < ST_FINISHED) {
            b.score[BATTLE_HOST] = m->end.total[0];
            b.score[BATTLE_GUEST] = m->end.total[1];
            show_finished();
        }
        break;

    case BATTLE_MSG_ABORT:
        ESP_LOGI(TAG, "host stopped the battle (reason %u)", m->abort.reason);
        stop(m->abort.reason == BATTLE_ABORT_VERSION    ? BATTLE_STOP_VERSION
             : m->abort.reason == BATTLE_ABORT_TRANSFER ? BATTLE_STOP_TRANSFER
                                                        : BATTLE_STOP_OPPONENT);
        break;

    default:
        break;
    }
}

static void guest_on_host_lost(void)
{
    /* Before HELLO we keep waiting; after the game the result stays up. */
    if (b.state != ST_SEARCHING && b.state != ST_FINISHED) {
        stop(BATTLE_STOP_LINK);
    }
}

/* --- task ------------------------------------------------------------------------ */

static void handle(const input_t *in)
{
    battle_msg_t m;
    switch (in->kind) {
    case IN_HOST:
        host_begin(in->name);
        break;
    case IN_JOIN:
        guest_begin();
        break;
    case IN_START:
        host_start();
        break;
    case IN_ANSWER:
        on_answer(in->choice, in->time_us);
        break;
    case IN_LEAVE:
        leave();
        break;
    case IN_PEER_CONNECTED:
        if (hosting() && b.state == ST_SEARCHING) {
            /* Peer names are "Pukk-XXXX"; the field is sized for pack names. */
            snprintf(b.opponent, sizeof(b.opponent), "%.*s", BATTLE_DEVICE_NAME_MAX, in->name);
            emit_lobby(BATTLE_LOBBY_CONNECTED, 0, 0);
            host_hello();
        }
        break;
    case IN_PEER_DISCONNECTED:
        if (hosting()) {
            host_on_disconnected();
        }
        break;
    case IN_PEER_BATTLE:
        if (hosting() && battle_msg_decode(&m, in->data, in->len)) {
            host_on_message(&m);
        }
        break;
    case IN_PEER_CTRL:
        if (hosting()) {
            host_on_ctrl(in->data, in->len);
        }
        break;
    case IN_HOST_MSG:
        if (joining() && battle_msg_decode(&m, in->data, in->len)) {
            guest_on_message(&m);
        }
        break;
    case IN_HOST_LOST:
        if (joining()) {
            guest_on_host_lost();
        }
        break;
    default:
        break;
    }
}

static TickType_t wait_ticks(void)
{
    int64_t wait_ms = -1;
    if (b.deadline) {
        wait_ms = (b.deadline - now_us()) / 1000 + 1;
        if (wait_ms < 0) {
            wait_ms = 0;
        }
    }
    int64_t poll_ms = -1;
    if (hosting() && b.state == ST_QUESTION) {
        poll_ms = QUESTION_POLL_MS;
    } else if (hosting() && b.state == ST_TRANSFER) {
        poll_ms = b.up.retry ? UP_RETRY_MS : UP_POLL_MS;
    }
    if (poll_ms >= 0 && (wait_ms < 0 || poll_ms < wait_ms)) {
        wait_ms = poll_ms;
    }
    return wait_ms < 0 ? portMAX_DELAY : pdMS_TO_TICKS(wait_ms);
}

static void task(void *arg)
{
    input_t in;
    for (;;) {
        if (xQueueReceive(s_queue, &in, wait_ticks()) == pdTRUE) {
            handle(&in);
        }
        if (b.deadline && now_us() >= b.deadline) {
            b.deadline = 0;
            if (hosting()) {
                host_on_deadline();
            }
        }
        if (hosting() && b.state == ST_QUESTION) {
            host_poll_round();
        } else if (hosting() && b.state == ST_TRANSFER) {
            host_pump_upload();
        }
    }
}

/* --- inputs (any task) ------------------------------------------------------------- */

static void post(const input_t *in)
{
    if (!s_queue || xQueueSend(s_queue, in, 0) != pdTRUE) {
        ESP_LOGW(TAG, "input %u dropped", in->kind);
    }
}

static void post_simple(input_kind_t kind)
{
    input_t in = { .kind = kind };
    post(&in);
}

void battle_host(const char *pack_name)
{
    input_t in = { .kind = IN_HOST };
    snprintf(in.name, sizeof(in.name), "%s", pack_name);
    post(&in);
}

void battle_join(void)
{
    post_simple(IN_JOIN);
}

void battle_start(void)
{
    post_simple(IN_START);
}

void battle_answer(uint8_t choice)
{
    input_t in = { .kind = IN_ANSWER, .choice = choice, .time_us = now_us() };
    post(&in);
}

void battle_leave(void)
{
    post_simple(IN_LEAVE);
}

void battle_ble_event(const pukk_ble_event_t *event)
{
    input_t in = { 0 };
    switch (event->type) {
    case PUKK_BLE_EVENT_PEER_CONNECTED:
        in.kind = IN_PEER_CONNECTED;
        snprintf(in.name, sizeof(in.name), "%s", event->name);
        break;
    case PUKK_BLE_EVENT_PEER_DISCONNECTED:
        in.kind = IN_PEER_DISCONNECTED;
        break;
    case PUKK_BLE_EVENT_PEER_BATTLE_RX:
    case PUKK_BLE_EVENT_PEER_CTRL_RX:
    case PUKK_BLE_EVENT_BATTLE_RX:
        if (event->len > sizeof(in.data)) {
            ESP_LOGW(TAG, "message of %u bytes ignored", (unsigned)event->len);
            return;
        }
        in.kind = event->type == PUKK_BLE_EVENT_PEER_BATTLE_RX ? IN_PEER_BATTLE
                  : event->type == PUKK_BLE_EVENT_PEER_CTRL_RX ? IN_PEER_CTRL
                                                               : IN_HOST_MSG;
        in.len = (uint8_t)event->len;
        memcpy(in.data, event->data, event->len);
        break;
    case PUKK_BLE_EVENT_BATTLE_HOST_LOST:
        in.kind = IN_HOST_LOST;
        break;
    default:
        return;
    }
    post(&in);
}

esp_err_t battle_init(battle_event_cb_t cb, void *ctx)
{
    s_cb = cb;
    s_cb_ctx = ctx;
    s_queue = xQueueCreate(QUEUE_LEN, sizeof(input_t));
    ESP_RETURN_ON_FALSE(s_queue, ESP_ERR_NO_MEM, TAG, "no queue");
    BaseType_t ok = xTaskCreate(task, "battle", TASK_STACK, NULL, TASK_PRIO, NULL);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "no task");
    return ESP_OK;
}
