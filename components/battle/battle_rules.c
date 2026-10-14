#include <string.h>
#include "battle_rules.h"

/* How long after the host's clock a player's answers can still arrive. */
static const uint32_t s_slack_ms[BATTLE_PLAYERS] = { 0, BATTLE_SLACK_MS };

void battle_round_start(battle_round_t *round, uint8_t correct, uint32_t time_ms)
{
    memset(round, 0, sizeof(*round));
    round->correct = correct;
    round->time_ms = time_ms;
    round->winner = BATTLE_NOBODY;
}

bool battle_round_answer(battle_round_t *round, int player, uint8_t choice, uint32_t reaction_ms)
{
    if (player < 0 || player >= BATTLE_PLAYERS || round->decided || choice >= BATTLE_CHOICE_LATE ||
        reaction_ms > round->time_ms) {
        return false;
    }
    battle_answer_t *a = &round->answer[player];
    if (a->answered) {
        return false;
    }
    a->answered = true;
    a->choice = choice;
    a->reaction_ms = reaction_ms;
    return true;
}

static void decide(battle_round_t *round, int winner)
{
    round->decided = true;
    round->winner = winner < 0 ? BATTLE_NOBODY : (uint8_t)winner;
    round->points = winner < 0 ? 0 : battle_points(round->answer[winner].reaction_ms, round->time_ms);
    for (int p = 0; p < BATTLE_PLAYERS; p++) {
        if (!round->answer[p].answered) {
            round->answer[p].choice = winner < 0 ? BATTLE_CHOICE_NONE : BATTLE_CHOICE_LATE;
        }
    }
}

bool battle_round_update(battle_round_t *round, uint32_t now_ms)
{
    if (round->decided) {
        return true;
    }

    /* Fastest right answer so far; on an exact tie the host wins. */
    int best = -1;
    for (int p = 0; p < BATTLE_PLAYERS; p++) {
        const battle_answer_t *a = &round->answer[p];
        if (a->answered && a->choice == round->correct &&
            (best < 0 || a->reaction_ms < round->answer[best].reaction_ms)) {
            best = p;
        }
    }

    for (int p = 0; p < BATTLE_PLAYERS; p++) {
        if (round->answer[p].answered) {
            continue;
        }
        /* Without an answer yet, p can still win (beat the best right answer)
         * or still answer at all (before time runs out). */
        uint32_t until = best >= 0 ? round->answer[best].reaction_ms : round->time_ms;
        if (now_ms < until + s_slack_ms[p]) {
            return false;
        }
    }
    decide(round, best);
    return true;
}

uint16_t battle_points(uint32_t reaction_ms, uint32_t time_ms)
{
    if (time_ms == 0) {
        return BATTLE_POINTS_MAX;
    }
    if (reaction_ms > time_ms) {
        reaction_ms = time_ms;
    }
    const uint32_t range = BATTLE_POINTS_MAX - BATTLE_POINTS_MIN;
    return (uint16_t)(BATTLE_POINTS_MIN + (range * (time_ms - reaction_ms) + time_ms / 2) / time_ms);
}
