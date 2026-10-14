/* Fake pack store for the host preview: three packs with fixed questions. */
#include <stdio.h>
#include <string.h>
#include "packs.h"

typedef struct {
    const char *text;
    const char *answers[4];
    uint8_t count;
    uint8_t correct;
    uint8_t time_s;
} fake_question_t;

static const fake_question_t s_questions[] = {
    { "Welke planeet staat het dichtst bij de zon?", { "Venus", "Mercurius", "Mars", "Aarde" }, 4, 1, 15 },
    { "Is een tomaat botanisch gezien een vrucht?", { "Ja", "Nee" }, 2, 0, 10 },
    { "Wat is de hoofdstad van België?", { "Antwerpen", "Brussel", "Luik" }, 3, 1, 15 },
    { "Hoeveel poten heeft een spin?", { "6", "8", "10", "12" }, 4, 1, 10 },
};

#define QUESTION_COUNT (sizeof(s_questions) / sizeof(s_questions[0]))

static const packs_entry_t s_packs[] = {
    { "algemeen", "Algemene kennis", 4120, QUESTION_COUNT },
    { "dieren", "Dieren in de Ardèche", 2210, 12 },
    { "sport", "Sport & spel", 1980, 20 },
};

/* The preview switches this to show the empty state. */
size_t fake_pack_count = sizeof(s_packs) / sizeof(s_packs[0]);

size_t packs_list(packs_entry_t *entries, size_t max)
{
    size_t n = fake_pack_count < max ? fake_pack_count : max;
    memcpy(entries, s_packs, n * sizeof(entries[0]));
    return n;
}

packs_err_t packs_info(const char *name, packs_entry_t *info)
{
    for (size_t i = 0; i < fake_pack_count; i++) {
        if (strcmp(s_packs[i].name, name) == 0) {
            *info = s_packs[i];
            if (i == 0) {
                info->question_count = QUESTION_COUNT;
            }
            return PACKS_OK;
        }
    }
    return PACKS_ERR_NOT_FOUND;
}

packs_err_t packs_read_question(const char *name, uint16_t index, pack_question_t *q)
{
    if (index >= QUESTION_COUNT) {
        return PACKS_ERR_NOT_FOUND;
    }
    const fake_question_t *f = &s_questions[index];
    memset(q, 0, sizeof(*q));
    snprintf(q->text, sizeof(q->text), "%s", f->text);
    for (int i = 0; i < f->count; i++) {
        snprintf(q->answers[i], sizeof(q->answers[i]), "%s", f->answers[i]);
    }
    q->answer_count = f->count;
    q->correct = f->correct;
    q->time_s = f->time_s;
    return PACKS_OK;
}

const char *packs_err_name(packs_err_t err)
{
    return err == PACKS_OK ? "ok" : "error";
}
