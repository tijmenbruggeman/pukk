#include <string.h>
#include "pack_format.h"

#define NO_STRING       0xFFFFFFFFu
#define CRC_FIELD       12  /* offset of the crc32 header field */

static const uint8_t s_magic[4] = { 'P', 'U', 'K', 'K' };

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint32_t pack_crc32(uint32_t crc, const void *data, size_t len)
{
    const uint8_t *p = data;
    crc = ~crc;
    while (len--) {
        crc ^= *p++;
        for (int i = 0; i < 8; i++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

pack_err_t pack_open(pack_t *pack, pack_read_fn read, void *ctx, uint32_t file_size)
{
    uint8_t h[PACK_HEADER_SIZE];
    memset(pack, 0, sizeof(*pack));
    pack->read = read;
    pack->ctx = ctx;

    if (file_size < PACK_HEADER_SIZE + 1 || file_size > PACK_MAX_FILE_SIZE) {
        return PACK_ERR_FORMAT;
    }
    if (read(ctx, 0, h, sizeof(h)) != 0) {
        return PACK_ERR_IO;
    }
    if (memcmp(h, s_magic, sizeof(s_magic)) != 0 || h[4] != PACK_VERSION) {
        return PACK_ERR_FORMAT;
    }

    pack->flags = h[5];
    pack->question_count = get_u16(h + 6);
    pack->file_size = get_u32(h + 8);
    pack->crc32 = get_u32(h + 12);
    pack->index_offset = get_u32(h + 16);
    pack->strings_offset = get_u32(h + 20);
    pack->strings_size = get_u32(h + 24);
    pack->title = get_u32(h + 28);

    /* 64-bit sums: no overflow tricks with crafted offsets. */
    uint64_t index_end = (uint64_t)pack->index_offset +
                         (uint64_t)pack->question_count * PACK_QUESTION_SIZE;
    if (pack->file_size != file_size ||
        pack->question_count == 0 || pack->question_count > PACK_MAX_QUESTIONS ||
        pack->index_offset < PACK_HEADER_SIZE ||
        index_end > pack->strings_offset ||
        pack->strings_size == 0 ||
        (uint64_t)pack->strings_offset + pack->strings_size != file_size ||
        pack->title >= pack->strings_size) {
        return PACK_ERR_FORMAT;
    }

    /* Packs from before the extension have their questions right after the header. */
    if (pack->index_offset >= PACK_HEADER_SIZE + PACK_HEADER_EXT_SIZE) {
        uint8_t ext[PACK_HEADER_EXT_SIZE];
        if (read(ctx, PACK_HEADER_SIZE, ext, sizeof(ext)) != 0) {
            return PACK_ERR_IO;
        }
        pack->per_game = get_u16(ext);
    }

    /* The string table must end with 0, so every string is terminated. */
    uint8_t last;
    if (read(ctx, file_size - 1, &last, 1) != 0) {
        return PACK_ERR_IO;
    }
    return last == 0 ? PACK_OK : PACK_ERR_FORMAT;
}

uint16_t pack_game_order(const pack_t *pack, uint32_t (*random)(void), uint16_t *order)
{
    const uint16_t n = pack->question_count;
    const uint16_t k = pack->per_game && pack->per_game < n ? pack->per_game : n;
    const bool shuffle = pack->flags & PACK_FLAG_RANDOM_ORDER;
    for (uint16_t i = 0; i < n; i++) {
        order[i] = i;
    }
    if (k == n && !shuffle) {
        return n;
    }
    /* Partial Fisher-Yates: order[0..k) becomes a random pick. */
    for (uint16_t i = 0; i < k; i++) {
        uint16_t j = (uint16_t)(i + random() % (uint32_t)(n - i));
        uint16_t t = order[i];
        order[i] = order[j];
        order[j] = t;
    }
    if (!shuffle) {
        /* Back in pack order (insertion sort; at most 1000). */
        for (uint16_t i = 1; i < k; i++) {
            uint16_t v = order[i];
            uint16_t j = i;
            for (; j > 0 && order[j - 1] > v; j--) {
                order[j] = order[j - 1];
            }
            order[j] = v;
        }
    }
    return k;
}

/* Copy the string at `offset` into buf; fails if it does not fit in `max` bytes. */
static pack_err_t read_string(const pack_t *pack, uint32_t offset, char *buf, size_t max)
{
    if (offset >= pack->strings_size) {
        return PACK_ERR_FORMAT;
    }
    uint32_t avail = pack->strings_size - offset;
    uint32_t len = avail < max + 1 ? avail : (uint32_t)(max + 1);
    if (pack->read(pack->ctx, pack->strings_offset + offset, buf, len) != 0) {
        return PACK_ERR_IO;
    }
    if (memchr(buf, 0, len) == NULL) {
        buf[0] = '\0';
        return PACK_ERR_RANGE;
    }
    return PACK_OK;
}

pack_err_t pack_read_title(const pack_t *pack, char *buf, size_t size)
{
    char title[PACK_TITLE_MAX + 1];
    pack_err_t err = read_string(pack, pack->title, title, PACK_TITLE_MAX);
    if (err == PACK_OK && size > 0) {
        strncpy(buf, title, size - 1);
        buf[size - 1] = '\0';
    }
    return err;
}

pack_err_t pack_read_question(const pack_t *pack, uint16_t index, pack_question_t *q)
{
    uint8_t e[PACK_QUESTION_SIZE];
    memset(q, 0, sizeof(*q));
    if (index >= pack->question_count) {
        return PACK_ERR_RANGE;
    }
    if (pack->read(pack->ctx, pack->index_offset + (uint32_t)index * PACK_QUESTION_SIZE, e,
                   sizeof(e)) != 0) {
        return PACK_ERR_IO;
    }

    q->answer_count = e[20];
    q->correct = e[21];
    q->time_s = e[22] ? e[22] : PACK_DEFAULT_TIME_S;
    if (q->answer_count < PACK_MIN_ANSWERS || q->answer_count > PACK_MAX_ANSWERS ||
        q->correct >= q->answer_count ||
        q->time_s < PACK_MIN_TIME_S || q->time_s > PACK_MAX_TIME_S) {
        return PACK_ERR_RANGE;
    }

    pack_err_t err = read_string(pack, get_u32(e), q->text, PACK_QUESTION_MAX);
    for (int i = 0; err == PACK_OK && i < PACK_MAX_ANSWERS; i++) {
        uint32_t offset = get_u32(e + 4 + 4 * i);
        if (i < q->answer_count) {
            err = read_string(pack, offset, q->answers[i], PACK_ANSWER_MAX);
        } else if (offset != NO_STRING) {
            err = PACK_ERR_FORMAT;
        }
    }
    return err;
}

pack_err_t pack_verify(const pack_t *pack)
{
    uint8_t buf[256];
    uint32_t crc = 0;
    for (uint32_t pos = 0; pos < pack->file_size; ) {
        uint32_t n = pack->file_size - pos < sizeof(buf) ? pack->file_size - pos : sizeof(buf);
        if (pack->read(pack->ctx, pos, buf, n) != 0) {
            return PACK_ERR_IO;
        }
        /* The crc32 field counts as zero. */
        for (uint32_t i = 0; i < n; i++) {
            if (pos + i >= CRC_FIELD && pos + i < CRC_FIELD + 4) {
                buf[i] = 0;
            }
        }
        crc = pack_crc32(crc, buf, n);
        pos += n;
    }
    if (crc != pack->crc32) {
        return PACK_ERR_CRC;
    }

    char title[PACK_TITLE_MAX + 1];
    pack_err_t err = read_string(pack, pack->title, title, PACK_TITLE_MAX);
    pack_question_t q;
    for (uint16_t i = 0; err == PACK_OK && i < pack->question_count; i++) {
        err = pack_read_question(pack, i, &q);
    }
    return err;
}

const char *pack_err_name(pack_err_t err)
{
    switch (err) {
    case PACK_OK:           return "ok";
    case PACK_ERR_IO:       return "read error";
    case PACK_ERR_FORMAT:   return "invalid format";
    case PACK_ERR_CRC:      return "CRC mismatch";
    case PACK_ERR_RANGE:    return "value out of range";
    }
    return "?";
}
