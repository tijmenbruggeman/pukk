/*
 * Battle rules (docs/BATTLE.md): who wins a question and what it is worth.
 *
 * Plain C without ESP-IDF dependencies, so it can be unit-tested on the host
 * (components/battle/test). Only the host runs it.
 *
 * Times are milliseconds. Reaction times are measured by each Pukk from the
 * moment the question appears on its own screen; `now_ms` is the host's time
 * since its question appeared. The guest's question appears a little later
 * and its answers take a little while to arrive, so the host waits
 * BATTLE_SLACK_MS longer for the guest before it decides.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BATTLE_PLAYERS      2
#define BATTLE_HOST         0
#define BATTLE_GUEST        1
#define BATTLE_NOBODY       0xFF

#define BATTLE_SLACK_MS     200
#define BATTLE_POINTS_MIN   500
#define BATTLE_POINTS_MAX   1000

/* Choices besides answer 0-3, once the round is decided. */
#define BATTLE_CHOICE_LATE  0xFE    /* no answer: the other player was right first */
#define BATTLE_CHOICE_NONE  0xFF    /* no answer: time ran out */

typedef struct {
    bool answered;
    uint8_t choice;
    uint32_t reaction_ms;
} battle_answer_t;

typedef struct {
    uint8_t correct;
    uint32_t time_ms;
    battle_answer_t answer[BATTLE_PLAYERS];
    /* Set once battle_round_update() has decided the round. */
    bool decided;
    uint8_t winner;     /* BATTLE_HOST, BATTLE_GUEST or BATTLE_NOBODY */
    uint16_t points;    /* for the winner */
} battle_round_t;

void battle_round_start(battle_round_t *round, uint8_t correct, uint32_t time_ms);

/* Record a player's answer. Ignored (returns false) when the player already
 * answered, the round is decided, or the reaction time exceeds the limit. */
bool battle_round_answer(battle_round_t *round, int player, uint8_t choice, uint32_t reaction_ms);

/* Decide the round if nothing can change the outcome any more. Returns true
 * once decided; from then on every answer[].choice is final (an answer,
 * BATTLE_CHOICE_LATE or BATTLE_CHOICE_NONE). */
bool battle_round_update(battle_round_t *round, uint32_t now_ms);

/* 1000 for an instant answer down to 500 at the last moment. */
uint16_t battle_points(uint32_t reaction_ms, uint32_t time_ms);

#ifdef __cplusplus
}
#endif
