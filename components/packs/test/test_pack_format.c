/*
 * Host test for pack_format.c. Run tools/test_host.sh; it builds the sample
 * pack with tools/pukk.py and compiles this with address/UB sanitizers.
 *
 * Checks that a pack built by the Python tool reads back correctly, and that
 * corrupted packs (including ones with a recomputed, valid CRC) are rejected
 * without out-of-bounds access.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pack_format.h"

typedef struct {
    uint8_t *data;
    uint32_t size;
} blob_t;

static int failures;

#define CHECK(cond, ...)                                    \
    do {                                                    \
        if (!(cond)) {                                      \
            failures++;                                     \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
            fprintf(stderr, __VA_ARGS__);                   \
            fprintf(stderr, "\n");                          \
        }                                                   \
    } while (0)

static int blob_read(void *ctx, uint32_t offset, void *buf, uint32_t len)
{
    const blob_t *b = ctx;
    if ((uint64_t)offset + len > b->size) {
        return -1;
    }
    memcpy(buf, b->data + offset, len);
    return 0;
}

static void fix_crc(blob_t *b)
{
    memset(b->data + 12, 0, 4);
    uint32_t crc = pack_crc32(0, b->data, b->size);
    for (int i = 0; i < 4; i++) {
        b->data[12 + i] = (uint8_t)(crc >> (8 * i));
    }
}

/* Open + verify + read everything; returns the first error. */
static pack_err_t read_all(blob_t *b)
{
    pack_t pack;
    pack_err_t err = pack_open(&pack, blob_read, b, b->size);
    if (err != PACK_OK) {
        return err;
    }
    /* Lazy reads must be safe even before pack_verify(). */
    char title[PACK_TITLE_MAX + 1];
    pack_question_t q;
    pack_read_title(&pack, title, sizeof(title));
    for (uint32_t i = 0; i <= pack.question_count; i++) {
        pack_read_question(&pack, (uint16_t)i, &q);
    }
    return pack_verify(&pack);
}

static blob_t load(const char *path)
{
    blob_t b = { 0 };
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        exit(2);
    }
    fseek(f, 0, SEEK_END);
    b.size = (uint32_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    b.data = malloc(b.size);
    if (fread(b.data, 1, b.size, f) != b.size) {
        perror("read");
        exit(2);
    }
    fclose(f);
    return b;
}

static void test_crc(void)
{
    CHECK(pack_crc32(0, "123456789", 9) == 0xCBF43926u, "CRC-32 check value");
    uint32_t crc = pack_crc32(0, "1234", 4);
    CHECK(pack_crc32(crc, "56789", 5) == 0xCBF43926u, "incremental CRC-32");
}

static void test_sample(blob_t *b)
{
    pack_t pack;
    CHECK(pack_open(&pack, blob_read, b, b->size) == PACK_OK, "open sample");
    CHECK(pack_verify(&pack) == PACK_OK, "verify sample");
    CHECK(pack.question_count == 8, "question count %u", pack.question_count);

    char title[PACK_TITLE_MAX + 1];
    CHECK(pack_read_title(&pack, title, sizeof(title)) == PACK_OK, "title");
    CHECK(strcmp(title, "Voorbeeld") == 0, "title is '%s'", title);

    pack_question_t q;
    CHECK(pack_read_question(&pack, 0, &q) == PACK_OK, "question 0");
    CHECK(strcmp(q.text, "Welke planeet staat het dichtst bij de zon?") == 0, "q0 text");
    CHECK(q.answer_count == 4 && q.correct == 1 && q.time_s == 15, "q0 fields");
    CHECK(strcmp(q.answers[1], "Mercurius") == 0, "q0 answer");

    CHECK(pack_read_question(&pack, 2, &q) == PACK_OK, "question 2");
    CHECK(strcmp(q.text, "Wat is de hoofdstad van Belgie?") == 0, "accent stripped");

    CHECK(pack_read_question(&pack, 3, &q) == PACK_OK, "question 3");
    CHECK(q.answer_count == 2 && q.time_s == 10, "two answers, 10 s");

    CHECK(pack_read_question(&pack, 8, &q) == PACK_ERR_RANGE, "index past end");
}

static void test_corruption(const blob_t *orig)
{
    blob_t b = { malloc(orig->size), orig->size };

    /* Any single flipped byte must be caught (by the CRC at the latest). */
    for (uint32_t pos = 0; pos < orig->size; pos++) {
        memcpy(b.data, orig->data, orig->size);
        b.data[pos] ^= 0xFF;
        CHECK(read_all(&b) != PACK_OK, "flipped byte %u accepted", pos);
    }

    /* Truncated files. */
    for (uint32_t size = 0; size < orig->size; size++) {
        blob_t t = { orig->data, size };
        CHECK(read_all(&t) != PACK_OK, "truncated to %u accepted", size);
    }

    /* Random damage with a valid CRC, so the structural checks get exercised.
     * Pass/fail is not checked here: the sanitizers catch bad memory access. */
    srand(1234);
    int accepted = 0;
    for (int i = 0; i < 20000; i++) {
        memcpy(b.data, orig->data, orig->size);
        int edits = 1 + rand() % 4;
        for (int e = 0; e < edits; e++) {
            uint32_t pos = (uint32_t)rand() % orig->size;
            b.data[pos] = (rand() % 3 == 0) ? 0xFF : (uint8_t)rand();
        }
        fix_crc(&b);
        accepted += read_all(&b) == PACK_OK;
    }
    printf("random damage with valid CRC: %d of 20000 still valid packs\n", accepted);
    free(b.data);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s sample.pukk\n", argv[0]);
        return 2;
    }
    blob_t sample = load(argv[1]);
    test_crc();
    test_sample(&sample);
    test_corruption(&sample);
    free(sample.data);

    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all pack format tests passed\n");
    return 0;
}
