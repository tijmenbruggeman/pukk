#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "storage.h"
#include "packs.h"

static const char *TAG = "packs";

#define EXT                 ".pukk"
#define EXT_LEN             (sizeof(EXT) - 1)
#define TEMP_PATH           STORAGE_PACKS_PATH "/.upload"
#define PATH_LEN            (sizeof(STORAGE_PACKS_PATH) + 1 + PACKS_NAME_MAX + EXT_LEN + 1)
#define LOG_LIST_MAX        16
/* Old and new version of a pack coexist until the rename, plus fs overhead. */
#define SPACE_MARGIN        (16 * 1024)

static struct {
    bool active;
    int fd;
    char name[PACKS_NAME_MAX + 1];
    uint32_t size;
    uint32_t expected_crc;
    uint32_t crc;
    uint32_t received;
} s_upload = { .fd = -1 };

bool packs_name_valid(const char *name, size_t len)
{
    if (len == 0 || len > PACKS_NAME_MAX) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) {
            return false;
        }
    }
    return true;
}

static void pack_path(char *buf, const char *name)
{
    snprintf(buf, PATH_LEN, "%s/%s%s", STORAGE_PACKS_PATH, name, EXT);
}

/*
 * Reading a pack means many small reads at two places at once: the question
 * table and the string table. Two cached 512-byte windows (least recently used
 * is replaced) turn most of them into memcpy instead of a filesystem call.
 *
 * Plain read() rather than stdio: newlib's fread on these files costs a VFS
 * call per byte when unbuffered, or per 128 bytes with its default buffer,
 * which made verifying a 71 KB pack take 4-15 s.
 */
#define CACHE_SLOT_SIZE 512

typedef struct {
    uint32_t base;
    uint32_t len;
    uint8_t data[CACHE_SLOT_SIZE];
} cache_slot_t;

typedef struct {
    int fd;
    cache_slot_t slot[2];
    uint8_t lru;
} reader_t;

static bool read_at(int fd, uint32_t offset, void *buf, uint32_t len, uint32_t *got)
{
    if (lseek(fd, (off_t)offset, SEEK_SET) != (off_t)offset) {
        return false;
    }
    ssize_t n = read(fd, buf, len);
    if (n < 0) {
        return false;
    }
    *got = (uint32_t)n;
    return true;
}

static int file_read(void *ctx, uint32_t offset, void *buf, uint32_t len)
{
    reader_t *r = ctx;
    for (int i = 0; i < 2; i++) {
        cache_slot_t *s = &r->slot[i];
        if (s->len && offset >= s->base && (uint64_t)offset + len <= (uint64_t)s->base + s->len) {
            memcpy(buf, s->data + (offset - s->base), len);
            r->lru = (uint8_t)(i ^ 1);
            return 0;
        }
    }
    uint32_t got;
    if (len > CACHE_SLOT_SIZE) {
        return read_at(r->fd, offset, buf, len, &got) && got == len ? 0 : -1;
    }
    cache_slot_t *s = &r->slot[r->lru];
    s->base = offset;
    if (!read_at(r->fd, offset, s->data, CACHE_SLOT_SIZE, &s->len) || s->len < len) {
        s->len = 0;
        return -1;
    }
    memcpy(buf, s->data, len);
    r->lru ^= 1;
    return 0;
}

static packs_err_t map_pack_err(pack_err_t err)
{
    if (err == PACK_OK) {
        return PACKS_OK;
    }
    return err == PACK_ERR_IO ? PACKS_ERR_IO : PACKS_ERR_INVALID_PACK;
}

static void close_pack(reader_t *r)
{
    close(r->fd);
    free(r);
}

/* Open the file at `path` and check its header. On success call close_pack(). */
static packs_err_t open_pack(const char *path, reader_t **reader, pack_t *pack)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        return PACKS_ERR_NOT_FOUND;
    }
    if (st.st_size > PACK_MAX_FILE_SIZE) {
        return PACKS_ERR_INVALID_PACK;
    }
    reader_t *r = calloc(1, sizeof(*r));
    if (!r) {
        return PACKS_ERR_IO;
    }
    r->fd = open(path, O_RDONLY);
    if (r->fd < 0) {
        free(r);
        return PACKS_ERR_IO;
    }
    packs_err_t err = map_pack_err(pack_open(pack, file_read, r, (uint32_t)st.st_size));
    if (err != PACKS_OK) {
        close_pack(r);
        return err;
    }
    *reader = r;
    return PACKS_OK;
}

/* Fill in title, size and question count; with `verify` also check everything. */
static packs_err_t read_info(const char *path, packs_entry_t *info, bool verify)
{
    reader_t *r;
    pack_t pack;
    packs_err_t err = open_pack(path, &r, &pack);
    if (err != PACKS_OK) {
        return err;
    }
    info->size = pack.file_size;
    info->question_count = pack.question_count;
    err = map_pack_err(pack_read_title(&pack, info->title, sizeof(info->title)));
    if (err == PACKS_OK && verify) {
        err = map_pack_err(pack_verify(&pack));
    }
    close_pack(r);
    return err;
}

static int compare_names(const void *a, const void *b)
{
    return strcmp(((const packs_entry_t *)a)->name, ((const packs_entry_t *)b)->name);
}

size_t packs_list(packs_entry_t *entries, size_t max)
{
    DIR *dir = opendir(STORAGE_PACKS_PATH);
    if (!dir) {
        return 0;
    }
    size_t count = 0;
    struct dirent *de;
    while (count < max && (de = readdir(dir)) != NULL) {
        size_t len = strlen(de->d_name);
        if (len <= EXT_LEN || strcmp(de->d_name + len - EXT_LEN, EXT) != 0 ||
            !packs_name_valid(de->d_name, len - EXT_LEN)) {
            continue;
        }
        packs_entry_t *e = &entries[count];
        memcpy(e->name, de->d_name, len - EXT_LEN);
        e->name[len - EXT_LEN] = '\0';

        char path[PATH_LEN];
        pack_path(path, e->name);
        if (read_info(path, e, false) == PACKS_OK) {
            count++;
        } else {
            ESP_LOGW(TAG, "skipping invalid pack %s", de->d_name);
        }
    }
    closedir(dir);
    qsort(entries, count, sizeof(entries[0]), compare_names);
    return count;
}

packs_err_t packs_info(const char *name, packs_entry_t *info)
{
    if (!packs_name_valid(name, strlen(name))) {
        return PACKS_ERR_INVALID_ARG;
    }
    char path[PATH_LEN];
    pack_path(path, name);
    strcpy(info->name, name);
    return read_info(path, info, false);
}

packs_err_t packs_read_question(const char *name, uint16_t index, pack_question_t *question)
{
    if (!packs_name_valid(name, strlen(name))) {
        return PACKS_ERR_INVALID_ARG;
    }
    char path[PATH_LEN];
    pack_path(path, name);
    reader_t *r;
    pack_t pack;
    packs_err_t err = open_pack(path, &r, &pack);
    if (err != PACKS_OK) {
        return err;
    }
    pack_err_t perr = pack_read_question(&pack, index, question);
    close_pack(r);
    return perr == PACK_ERR_RANGE && index >= pack.question_count ? PACKS_ERR_NOT_FOUND
                                                                    : map_pack_err(perr);
}

packs_err_t packs_delete(const char *name)
{
    if (!packs_name_valid(name, strlen(name))) {
        return PACKS_ERR_INVALID_ARG;
    }
    char path[PATH_LEN];
    pack_path(path, name);
    if (unlink(path) != 0) {
        return errno == ENOENT ? PACKS_ERR_NOT_FOUND : PACKS_ERR_IO;
    }
    ESP_LOGI(TAG, "deleted %s", name);
    return PACKS_OK;
}

packs_err_t packs_upload_begin(const char *name, uint32_t size, uint32_t crc32)
{
    if (s_upload.active) {
        return PACKS_ERR_BUSY;
    }
    if (!packs_name_valid(name, strlen(name)) || size <= PACK_HEADER_SIZE ||
        size > PACK_MAX_FILE_SIZE) {
        return PACKS_ERR_INVALID_ARG;
    }
    size_t total = 0, free = 0;
    if (storage_get_usage(&total, &free) != ESP_OK) {
        return PACKS_ERR_IO;
    }
    if (free < (size_t)size + SPACE_MARGIN) {
        return PACKS_ERR_NO_SPACE;
    }
    int fd = open(TEMP_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        return PACKS_ERR_IO;
    }
    s_upload.active = true;
    s_upload.fd = fd;
    strcpy(s_upload.name, name);
    s_upload.size = size;
    s_upload.expected_crc = crc32;
    s_upload.crc = 0;
    s_upload.received = 0;
    ESP_LOGI(TAG, "upload %s started (%lu bytes)", name, (unsigned long)size);
    return PACKS_OK;
}

packs_err_t packs_upload_write(uint32_t offset, const uint8_t *data, size_t len)
{
    if (!s_upload.active) {
        return PACKS_ERR_NO_UPLOAD;
    }
    if (offset > s_upload.received) {
        return PACKS_ERR_OUT_OF_ORDER;
    }
    /* Skip anything we already have (a client retrying a write). */
    uint32_t skip = s_upload.received - offset;
    if (skip >= len) {
        return PACKS_OK;
    }
    data += skip;
    len -= skip;
    if ((uint64_t)s_upload.received + len > s_upload.size) {
        return PACKS_ERR_MISMATCH;
    }
    if (write(s_upload.fd, data, len) != (ssize_t)len) {
        return PACKS_ERR_IO;
    }
    s_upload.crc = pack_crc32(s_upload.crc, data, len);
    s_upload.received += len;
    return PACKS_OK;
}

void packs_upload_abort(void)
{
    if (!s_upload.active) {
        return;
    }
    close(s_upload.fd);
    s_upload.fd = -1;
    unlink(TEMP_PATH);
    s_upload.active = false;
    ESP_LOGW(TAG, "upload %s aborted at %lu of %lu bytes", s_upload.name,
             (unsigned long)s_upload.received, (unsigned long)s_upload.size);
}

packs_err_t packs_upload_finish(void)
{
    if (!s_upload.active) {
        return PACKS_ERR_NO_UPLOAD;
    }
    packs_err_t err = PACKS_OK;
    if (close(s_upload.fd) != 0) {
        err = PACKS_ERR_IO;
    }
    s_upload.fd = -1;
    if (err == PACKS_OK &&
        (s_upload.received != s_upload.size || s_upload.crc != s_upload.expected_crc)) {
        ESP_LOGW(TAG, "received %lu of %lu bytes, crc %08lx (expected %08lx)",
                 (unsigned long)s_upload.received, (unsigned long)s_upload.size,
                 (unsigned long)s_upload.crc, (unsigned long)s_upload.expected_crc);
        err = PACKS_ERR_MISMATCH;
    }
    if (err == PACKS_OK) {
        int64_t start = esp_timer_get_time();
        packs_entry_t info;
        err = read_info(TEMP_PATH, &info, true);
        if (err == PACKS_ERR_NOT_FOUND) {
            err = PACKS_ERR_IO;
        }
        ESP_LOGI(TAG, "verified in %lld ms", (esp_timer_get_time() - start) / 1000);
    }
    if (err == PACKS_OK) {
        char path[PATH_LEN];
        pack_path(path, s_upload.name);
        unlink(path); /* replace an existing pack with the same name */
        if (rename(TEMP_PATH, path) != 0) {
            err = PACKS_ERR_IO;
        }
    }

    if (err == PACKS_OK) {
        ESP_LOGI(TAG, "upload %s complete", s_upload.name);
    } else {
        ESP_LOGW(TAG, "upload %s rejected: %s", s_upload.name, packs_err_name(err));
        unlink(TEMP_PATH);
    }
    s_upload.active = false;
    return err;
}

bool packs_upload_active(void)
{
    return s_upload.active;
}

uint32_t packs_upload_received(void)
{
    return s_upload.received;
}

esp_err_t packs_init(void)
{
    unlink(TEMP_PATH); /* left over if the device reset during an upload */

    packs_entry_t *list = calloc(LOG_LIST_MAX, sizeof(packs_entry_t));
    if (!list) {
        return ESP_ERR_NO_MEM;
    }
    size_t count = packs_list(list, LOG_LIST_MAX);
    ESP_LOGI(TAG, "%u pack(s) on %s", (unsigned)count, STORAGE_PACKS_PATH);
    for (size_t i = 0; i < count; i++) {
        ESP_LOGI(TAG, "  %s: \"%s\", %u questions, %lu bytes", list[i].name, list[i].title,
                 list[i].question_count, (unsigned long)list[i].size);
    }
    free(list);
    return ESP_OK;
}

const char *packs_err_name(packs_err_t err)
{
    switch (err) {
    case PACKS_OK:                  return "ok";
    case PACKS_ERR_BUSY:            return "busy";
    case PACKS_ERR_INVALID_ARG:     return "invalid request";
    case PACKS_ERR_NO_SPACE:        return "not enough space";
    case PACKS_ERR_NO_UPLOAD:       return "no upload active";
    case PACKS_ERR_OUT_OF_ORDER:    return "data out of order";
    case PACKS_ERR_MISMATCH:        return "size or CRC mismatch";
    case PACKS_ERR_INVALID_PACK:    return "invalid pack";
    case PACKS_ERR_IO:              return "I/O error";
    case PACKS_ERR_NOT_FOUND:       return "not found";
    }
    return "?";
}
