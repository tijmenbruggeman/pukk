/* Persistent storage for quiz packs: LittleFS on the "packs" partition. */
#pragma once

#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STORAGE_PACKS_PATH "/packs"

/* Mount the packs filesystem at STORAGE_PACKS_PATH; formats it on first boot. */
esp_err_t storage_init(void);

/* Filesystem size and free space in bytes. */
esp_err_t storage_get_usage(size_t *total, size_t *free);

#ifdef __cplusplus
}
#endif
