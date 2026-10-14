/*
 * Battle mode (docs/BATTLE.md): two Pukks play the same pack against each
 * other. One hosts (picks the pack, decides every question), the other joins.
 *
 * The engine runs in its own task. The UI drives it with battle_host(),
 * battle_join(), battle_start(), battle_answer() and battle_leave(); main
 * feeds it the BLE events. What to show comes back through the event
 * callback, always from the engine task and always from this Pukk's point
 * of view: score[BATTLE_ME] is ours, score[BATTLE_THEM] the opponent's.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "pack_format.h"
#include "pukk_ble.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BATTLE_ME           0
#define BATTLE_THEM         1
#define BATTLE_NO_WINNER    0xFF

/* Choices besides 0-3 in a round result (same values as in battle_rules.h). */
#define BATTLE_LATE         0xFE    /* no answer: the other one was right first */
#define BATTLE_NO_ANSWER    0xFF    /* no answer: time ran out */

#define BATTLE_COUNTDOWN_S  3
#define BATTLE_REVEAL_MS    2500

typedef enum {
    BATTLE_ROLE_HOST,
    BATTLE_ROLE_GUEST,
} battle_role_t;

typedef enum {
    BATTLE_EVENT_LOBBY,     /* lobby status changed */
    BATTLE_EVENT_COUNTDOWN, /* the game starts in `countdown_s` */
    BATTLE_EVENT_QUESTION,  /* show a question; its time starts now */
    BATTLE_EVENT_ROUND,     /* the question is decided */
    BATTLE_EVENT_FINISHED,  /* all questions done: final score */
    BATTLE_EVENT_STOPPED,   /* the battle ended early (or was left) */
} battle_event_type_t;

typedef enum {
    BATTLE_LOBBY_SEARCHING, /* host: looking for an opponent; guest: waiting for a host */
    BATTLE_LOBBY_CONNECTED, /* checking whether the guest has the pack */
    BATTLE_LOBBY_TRANSFER,  /* the pack is being sent to the guest */
    BATTLE_LOBBY_READY,     /* host: can start; guest: waiting for the host to start */
} battle_lobby_t;

typedef enum {
    BATTLE_STOP_LEFT,       /* we left */
    BATTLE_STOP_OPPONENT,   /* the opponent left */
    BATTLE_STOP_LINK,       /* connection lost */
    BATTLE_STOP_VERSION,    /* the other Pukk has incompatible firmware */
    BATTLE_STOP_TRANSFER,   /* the pack could not be sent */
    BATTLE_STOP_PACK,       /* the pack cannot be read */
} battle_stop_t;

typedef struct {
    battle_event_type_t type;
    battle_role_t role;
    const char *opponent;   /* "Pukk-XXXX", or "" while searching */
    const char *title;      /* pack title (the name until the guest has it) */
    uint32_t score[2];      /* BATTLE_ME, BATTLE_THEM */
    union {
        struct {
            battle_lobby_t status;
            uint32_t sent;      /* TRANSFER: bytes so far, of total */
            uint32_t total;
        } lobby;
        struct {
            uint8_t seconds;
        } countdown;
        struct {
            const pack_question_t *q;
            uint16_t index;
            uint16_t count;
        } question;
        struct {
            uint16_t index;
            uint8_t correct;
            uint8_t winner;     /* BATTLE_ME, BATTLE_THEM or BATTLE_NO_WINNER */
            uint8_t choice[2];  /* 0-3, BATTLE_LATE or BATTLE_NO_ANSWER */
            uint16_t points;
        } round;
        struct {
            battle_stop_t reason;
        } stopped;
    };
} battle_event_t;

/* Called from the engine task; pointers are only valid during the call. */
typedef void (*battle_event_cb_t)(const battle_event_t *event, void *ctx);

esp_err_t battle_init(battle_event_cb_t cb, void *ctx);

/* Host a battle with this pack: look for an opponent. */
void battle_host(const char *pack_name);
/* Wait for a host to pick us. */
void battle_join(void);
/* Host, lobby ready: start the countdown. */
void battle_start(void);
/* Answer the current question; the reaction time is taken now. */
void battle_answer(uint8_t choice);
/* Stop the battle (or stop searching) on both Pukks. */
void battle_leave(void);

/* Forward BLE events here (from the NimBLE host task). */
void battle_ble_event(const pukk_ble_event_t *event);

#ifdef __cplusplus
}
#endif
