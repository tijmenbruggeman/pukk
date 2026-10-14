/*
 * Messages on the BATTLE characteristic (docs/BATTLE.md): host -> guest as
 * writes, guest -> host as notifications. All numbers little-endian.
 *
 * Plain C without ESP-IDF dependencies, unit-tested on the host. Decoding
 * checks lengths exactly; nothing from the other Pukk is trusted.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BATTLE_PROTOCOL_VERSION 1
#define BATTLE_MSG_MAX          80  /* longest encoded message */
#define BATTLE_PACK_NAME_MAX    32
#define BATTLE_DEVICE_NAME_MAX  23

typedef enum {
    /* host -> guest */
    BATTLE_MSG_HELLO = 0x01,
    BATTLE_MSG_START = 0x03,
    BATTLE_MSG_QUESTION = 0x04,
    BATTLE_MSG_RESULT = 0x06,
    BATTLE_MSG_END = 0x07,
    BATTLE_MSG_ABORT = 0x08,
    /* guest -> host */
    BATTLE_MSG_HELLO_ACK = 0x81,
    BATTLE_MSG_ANSWER = 0x84,
    BATTLE_MSG_LEAVE = 0x88,
} battle_msg_type_t;

typedef enum {
    BATTLE_ABORT_STOPPED = 1,   /* KEY pressed */
    BATTLE_ABORT_VERSION = 2,   /* other protocol version */
    BATTLE_ABORT_TRANSFER = 3,  /* pack could not be sent */
} battle_abort_reason_t;

typedef struct {
    uint8_t type;   /* battle_msg_type_t */
    union {
        struct {
            /* Only `version` is filled in when it is not ours: the rest of a
             * HELLO from another protocol version cannot be read. */
            uint8_t version;
            char pack[BATTLE_PACK_NAME_MAX + 1];
            uint32_t pack_crc;
            uint16_t question_count;
            char host[BATTLE_DEVICE_NAME_MAX + 1];
        } hello;
        struct {
            uint8_t countdown_s;
        } start;
        struct {
            uint16_t index;
        } question;
        struct {
            uint16_t index;
            uint8_t winner;
            uint8_t choice[2];          /* host, guest */
            uint16_t reaction_ms[2];
            uint16_t points;
            uint32_t total[2];
        } result;
        struct {
            uint32_t total[2];
        } end;
        struct {
            uint8_t reason;
        } abort;
        struct {
            uint8_t version;
            bool has_pack;
        } hello_ack;
        struct {
            uint16_t index;
            uint8_t choice;
            uint16_t reaction_ms;
        } answer;
    };
} battle_msg_t;

/* Returns the encoded length, or 0 if `size` is too small or a name too long. */
size_t battle_msg_encode(const battle_msg_t *msg, uint8_t *buf, size_t size);

/* Returns false for unknown types and malformed messages. */
bool battle_msg_decode(battle_msg_t *msg, const uint8_t *buf, size_t len);

#ifdef __cplusplus
}
#endif
