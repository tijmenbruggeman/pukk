#include <string.h>
#include "battle_msg.h"

typedef struct {
    uint8_t *buf;
    size_t size;
    size_t len;
    bool ok;
} writer_t;

static void put(writer_t *w, uint32_t v, int bytes)
{
    if (w->len + (size_t)bytes > w->size) {
        w->ok = false;
        return;
    }
    for (int i = 0; i < bytes; i++) {
        w->buf[w->len++] = (uint8_t)(v >> (8 * i));
    }
}

/* "u8 len, bytes" */
static void put_name(writer_t *w, const char *name, size_t max)
{
    size_t n = 0;
    while (n <= max && name[n]) {
        n++;
    }
    if (n > max || w->len + 1 + n > w->size) {
        w->ok = false;
        return;
    }
    w->buf[w->len++] = (uint8_t)n;
    memcpy(w->buf + w->len, name, n);
    w->len += n;
}

size_t battle_msg_encode(const battle_msg_t *m, uint8_t *buf, size_t size)
{
    writer_t w = { .buf = buf, .size = size, .ok = true };
    put(&w, m->type, 1);
    switch (m->type) {
    case BATTLE_MSG_HELLO:
        put(&w, m->hello.version, 1);
        put_name(&w, m->hello.pack, BATTLE_PACK_NAME_MAX);
        put(&w, m->hello.pack_crc, 4);
        put(&w, m->hello.question_count, 2);
        put_name(&w, m->hello.host, BATTLE_DEVICE_NAME_MAX);
        break;
    case BATTLE_MSG_START:
        put(&w, m->start.countdown_s, 1);
        break;
    case BATTLE_MSG_QUESTION:
        put(&w, m->question.index, 2);
        break;
    case BATTLE_MSG_RESULT:
        put(&w, m->result.index, 2);
        put(&w, m->result.winner, 1);
        for (int p = 0; p < 2; p++) {
            put(&w, m->result.choice[p], 1);
            put(&w, m->result.reaction_ms[p], 2);
        }
        put(&w, m->result.points, 2);
        put(&w, m->result.total[0], 4);
        put(&w, m->result.total[1], 4);
        break;
    case BATTLE_MSG_END:
        put(&w, m->end.total[0], 4);
        put(&w, m->end.total[1], 4);
        break;
    case BATTLE_MSG_ABORT:
        put(&w, m->abort.reason, 1);
        break;
    case BATTLE_MSG_HELLO_ACK:
        put(&w, m->hello_ack.version, 1);
        put(&w, m->hello_ack.has_pack ? 1 : 0, 1);
        break;
    case BATTLE_MSG_ANSWER:
        put(&w, m->answer.index, 2);
        put(&w, m->answer.choice, 1);
        put(&w, m->answer.reaction_ms, 2);
        break;
    case BATTLE_MSG_LEAVE:
        break;
    default:
        return 0;
    }
    return w.ok ? w.len : 0;
}

typedef struct {
    const uint8_t *buf;
    size_t len;
    size_t pos;
    bool ok;
} reader_t;

static uint32_t get(reader_t *r, int bytes)
{
    if (r->pos + (size_t)bytes > r->len) {
        r->ok = false;
        return 0;
    }
    uint32_t v = 0;
    for (int i = 0; i < bytes; i++) {
        v |= (uint32_t)r->buf[r->pos++] << (8 * i);
    }
    return v;
}

static void get_name(reader_t *r, char *out, size_t max)
{
    size_t n = get(r, 1);
    if (!r->ok || n > max || r->pos + n > r->len || memchr(r->buf + r->pos, 0, n)) {
        r->ok = false;
        out[0] = '\0';
        return;
    }
    memcpy(out, r->buf + r->pos, n);
    out[n] = '\0';
    r->pos += n;
}

bool battle_msg_decode(battle_msg_t *m, const uint8_t *buf, size_t len)
{
    reader_t r = { .buf = buf, .len = len, .ok = true };
    memset(m, 0, sizeof(*m));
    m->type = (uint8_t)get(&r, 1);
    switch (m->type) {
    case BATTLE_MSG_HELLO:
        m->hello.version = (uint8_t)get(&r, 1);
        if (r.ok && m->hello.version != BATTLE_PROTOCOL_VERSION) {
            return true;
        }
        get_name(&r, m->hello.pack, BATTLE_PACK_NAME_MAX);
        m->hello.pack_crc = get(&r, 4);
        m->hello.question_count = (uint16_t)get(&r, 2);
        get_name(&r, m->hello.host, BATTLE_DEVICE_NAME_MAX);
        break;
    case BATTLE_MSG_START:
        m->start.countdown_s = (uint8_t)get(&r, 1);
        break;
    case BATTLE_MSG_QUESTION:
        m->question.index = (uint16_t)get(&r, 2);
        break;
    case BATTLE_MSG_RESULT:
        m->result.index = (uint16_t)get(&r, 2);
        m->result.winner = (uint8_t)get(&r, 1);
        for (int p = 0; p < 2; p++) {
            m->result.choice[p] = (uint8_t)get(&r, 1);
            m->result.reaction_ms[p] = (uint16_t)get(&r, 2);
        }
        m->result.points = (uint16_t)get(&r, 2);
        m->result.total[0] = get(&r, 4);
        m->result.total[1] = get(&r, 4);
        break;
    case BATTLE_MSG_END:
        m->end.total[0] = get(&r, 4);
        m->end.total[1] = get(&r, 4);
        break;
    case BATTLE_MSG_ABORT:
        m->abort.reason = (uint8_t)get(&r, 1);
        break;
    case BATTLE_MSG_HELLO_ACK:
        m->hello_ack.version = (uint8_t)get(&r, 1);
        m->hello_ack.has_pack = get(&r, 1) != 0;
        break;
    case BATTLE_MSG_ANSWER:
        m->answer.index = (uint16_t)get(&r, 2);
        m->answer.choice = (uint8_t)get(&r, 1);
        m->answer.reaction_ms = (uint16_t)get(&r, 2);
        break;
    case BATTLE_MSG_LEAVE:
        break;
    default:
        return false;
    }
    return r.ok && r.pos == len;
}
