/*
 * Quiz packs on the "packs" LittleFS partition: /packs/<name>.pukk.
 *
 * Reading (list, check, questions) may be called from any task. The upload
 * functions are meant for a single task (the BLE host task).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "pack_format.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PACKS_NAME_MAX  32

/* Result codes. The values are the BLE error codes in docs/PACKS.md. */
typedef enum {
    PACKS_OK = 0,
    PACKS_ERR_BUSY = 1,
    PACKS_ERR_INVALID_ARG = 2,
    PACKS_ERR_NO_SPACE = 3,
    PACKS_ERR_NO_UPLOAD = 4,
    PACKS_ERR_OUT_OF_ORDER = 5,
    PACKS_ERR_MISMATCH = 6,
    PACKS_ERR_INVALID_PACK = 7,
    PACKS_ERR_IO = 8,
    PACKS_ERR_NOT_FOUND = 9,
} packs_err_t;

typedef struct {
    char name[PACKS_NAME_MAX + 1];
    char title[PACK_TITLE_MAX + 1];
    uint32_t size;
    uint16_t question_count;
} packs_entry_t;

/* Remove leftovers of an interrupted upload and log the packs found.
 * Requires storage_init(). */
esp_err_t packs_init(void);

/* 1-32 characters of a-z, 0-9, '-' and '_'. */
bool packs_name_valid(const char *name, size_t len);

/* Fill `entries` with valid packs, sorted by name. Returns the count. */
size_t packs_list(packs_entry_t *entries, size_t max);

/* Verify a pack completely (CRC, every question) and return its details. */
packs_err_t packs_check(const char *name, packs_entry_t *info);

packs_err_t packs_read_question(const char *name, uint16_t index, pack_question_t *question);

packs_err_t packs_delete(const char *name);

/* Upload: data goes to a temporary file and only replaces /packs/<name>.pukk
 * after size, CRC and format have been checked. */
packs_err_t packs_upload_begin(const char *name, uint32_t size, uint32_t crc32);
/* Data must arrive in order; repeated data is ignored. */
packs_err_t packs_upload_write(uint32_t offset, const uint8_t *data, size_t len);
packs_err_t packs_upload_finish(void);
void packs_upload_abort(void);
bool packs_upload_active(void);
uint32_t packs_upload_received(void);

const char *packs_err_name(packs_err_t err);

#ifdef __cplusplus
}
#endif
