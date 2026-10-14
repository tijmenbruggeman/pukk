/*
 * Reader for the binary .pukk pack format (docs/PACKS.md).
 *
 * Plain C without ESP-IDF dependencies, so it can be unit-tested on the host
 * (components/packs/test). All access goes through a read callback; nothing
 * is trusted until checked, since packs arrive over BLE.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PACK_HEADER_SIZE        32
#define PACK_QUESTION_SIZE      24
#define PACK_VERSION            1
#define PACK_MAX_FILE_SIZE      (1024 * 1024)
#define PACK_MAX_QUESTIONS      1000
#define PACK_MIN_ANSWERS        2
#define PACK_MAX_ANSWERS        4
#define PACK_TITLE_MAX          40  /* bytes of UTF-8, excluding the terminating 0 */
#define PACK_QUESTION_MAX       120
#define PACK_ANSWER_MAX         32
#define PACK_DEFAULT_TIME_S     15
#define PACK_MIN_TIME_S         5
#define PACK_MAX_TIME_S         60

typedef enum {
    PACK_OK = 0,
    PACK_ERR_IO,        /* read callback failed */
    PACK_ERR_FORMAT,    /* bad magic, version, size or offsets */
    PACK_ERR_CRC,       /* checksum mismatch */
    PACK_ERR_RANGE,     /* index, field value or string length out of range */
} pack_err_t;

/* Read `len` bytes at `offset` into `buf`. Return 0 on success. */
typedef int (*pack_read_fn)(void *ctx, uint32_t offset, void *buf, uint32_t len);

typedef struct {
    pack_read_fn read;
    void *ctx;
    uint32_t file_size;
    uint32_t crc32;
    uint32_t index_offset;
    uint32_t strings_offset;
    uint32_t strings_size;
    uint32_t title;
    uint16_t question_count;
} pack_t;

typedef struct {
    char text[PACK_QUESTION_MAX + 1];
    char answers[PACK_MAX_ANSWERS][PACK_ANSWER_MAX + 1];
    uint8_t answer_count;
    uint8_t correct;
    uint8_t time_s;     /* resolved: never 0 */
} pack_question_t;

/* Read and check the header and table layout. Cheap: reads 33 bytes. */
pack_err_t pack_open(pack_t *pack, pack_read_fn read, void *ctx, uint32_t file_size);

pack_err_t pack_read_title(const pack_t *pack, char *buf, size_t size);
pack_err_t pack_read_question(const pack_t *pack, uint16_t index, pack_question_t *question);

/* Check the CRC and every question and string. Reads the whole file. */
pack_err_t pack_verify(const pack_t *pack);

/* CRC-32 (IEEE 802.3), same as zlib.crc32(). Pass 0 to start. */
uint32_t pack_crc32(uint32_t crc, const void *data, size_t len);

const char *pack_err_name(pack_err_t err);

#ifdef __cplusplus
}
#endif
