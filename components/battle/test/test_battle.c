/*
 * Host test for battle_rules.c and battle_msg.c. Run tools/test_host.sh; it
 * compiles this with address/UB sanitizers.
 */
#include <stdio.h>
#include <string.h>
#include "battle_msg.h"
#include "battle_rules.h"

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

enum { H = BATTLE_HOST, G = BATTLE_GUEST };

/* A 10 s question with answer 2 as the right one. */
static battle_round_t round10(void)
{
    battle_round_t r;
    battle_round_start(&r, 2, 10000);
    return r;
}

static void test_points(void)
{
    CHECK(battle_points(0, 10000) == 1000, "instant answer");
    CHECK(battle_points(10000, 10000) == 500, "last moment");
    CHECK(battle_points(5000, 10000) == 750, "halfway");
    CHECK(battle_points(20000, 10000) == 500, "past the limit");
    CHECK(battle_points(1, 15000) == 1000, "rounds to nearest");
    CHECK(battle_points(16, 15000) == 999, "rounds to nearest");
    CHECK(battle_points(0, 0) == 1000, "no time limit");
}

static void test_host_first(void)
{
    battle_round_t r = round10();
    CHECK(battle_round_answer(&r, H, 2, 1000), "host answer accepted");
    /* The guest may still have been faster until 1000 + slack. */
    CHECK(!battle_round_update(&r, 1100), "waits for the guest");
    CHECK(!battle_round_update(&r, 1000 + BATTLE_SLACK_MS - 1), "waits for the guest");
    CHECK(battle_round_update(&r, 1000 + BATTLE_SLACK_MS), "decided after the slack");
    CHECK(r.winner == H, "host wins");
    CHECK(r.points == battle_points(1000, 10000), "points %u", r.points);
    CHECK(r.answer[G].choice == BATTLE_CHOICE_LATE, "guest too late");
    CHECK(!battle_round_answer(&r, G, 2, 900), "answers after the decision are ignored");
}

static void test_guest_faster_but_later(void)
{
    /* The host taps first in real time, but the guest's reaction was faster. */
    battle_round_t r = round10();
    battle_round_answer(&r, H, 2, 1000);
    CHECK(!battle_round_update(&r, 1050), "waits");
    battle_round_answer(&r, G, 2, 980);
    CHECK(battle_round_update(&r, 1120), "both answered: decided");
    CHECK(r.winner == G, "guest wins on reaction time");
    CHECK(r.answer[H].choice == 2, "host's own (slower) choice is kept");
}

static void test_guest_slower(void)
{
    battle_round_t r = round10();
    battle_round_answer(&r, H, 2, 1000);
    battle_round_answer(&r, G, 2, 1010);
    CHECK(battle_round_update(&r, 1100), "decided");
    CHECK(r.winner == H, "host wins");
    CHECK(r.answer[G].choice == 2, "guest's choice kept");
}

static void test_guest_first(void)
{
    /* A right guest answer: the host cannot beat it once its own clock is past it. */
    battle_round_t r = round10();
    battle_round_answer(&r, G, 2, 3000);
    CHECK(!battle_round_update(&r, 2990), "host could still be faster");
    CHECK(battle_round_update(&r, 3040), "decided");
    CHECK(r.winner == G, "guest wins");
    CHECK(r.answer[H].choice == BATTLE_CHOICE_LATE, "host too late");
    CHECK(r.points == battle_points(3000, 10000), "points");
}

static void test_wrong_answers(void)
{
    battle_round_t r = round10();
    battle_round_answer(&r, H, 1, 500);
    CHECK(!battle_round_update(&r, 600), "the guest can still answer");
    CHECK(!battle_round_answer(&r, H, 2, 700), "a second answer is ignored");
    battle_round_answer(&r, G, 2, 4000);
    CHECK(battle_round_update(&r, 4100), "decided");
    CHECK(r.winner == G && r.answer[H].choice == 1, "guest wins, host was wrong");

    r = round10();
    battle_round_answer(&r, G, 0, 500);
    battle_round_answer(&r, H, 3, 800);
    CHECK(battle_round_update(&r, 800), "both wrong: decided at once");
    CHECK(r.winner == BATTLE_NOBODY && r.points == 0, "nobody wins");
    CHECK(r.answer[G].choice == 0 && r.answer[H].choice == 3, "choices kept");
}

static void test_timeout(void)
{
    battle_round_t r = round10();
    CHECK(!battle_round_update(&r, 9999), "running");
    CHECK(!battle_round_update(&r, 10000), "the guest's time ends later");
    CHECK(!battle_round_update(&r, 10000 + BATTLE_SLACK_MS - 1), "the guest's time ends later");
    CHECK(battle_round_update(&r, 10000 + BATTLE_SLACK_MS), "decided");
    CHECK(r.winner == BATTLE_NOBODY, "nobody wins");
    CHECK(r.answer[H].choice == BATTLE_CHOICE_NONE && r.answer[G].choice == BATTLE_CHOICE_NONE, "no answers");

    /* A last-moment guest answer that arrives during the slack still counts. */
    r = round10();
    battle_round_answer(&r, H, 0, 2000);
    CHECK(!battle_round_update(&r, 10100), "waiting for the guest");
    battle_round_answer(&r, G, 2, 9990);
    CHECK(battle_round_update(&r, 10150) && r.winner == G, "late guest answer wins");
    CHECK(r.points == battle_points(9990, 10000), "points");
}

static void test_invalid_answers(void)
{
    battle_round_t r = round10();
    CHECK(!battle_round_answer(&r, 2, 2, 100), "unknown player");
    CHECK(!battle_round_answer(&r, G, 2, 10001), "beyond the time limit");
    CHECK(!battle_round_answer(&r, G, BATTLE_CHOICE_LATE, 100), "not an answer");
    CHECK(battle_round_answer(&r, G, 2, 10000), "at the limit");
}

static void test_tie(void)
{
    battle_round_t r = round10();
    battle_round_answer(&r, G, 2, 1500);
    battle_round_answer(&r, H, 2, 1500);
    CHECK(battle_round_update(&r, 1600) && r.winner == H, "a tie goes to the host");
}

/* --- messages ---------------------------------------------------------------- */

static bool roundtrip(const battle_msg_t *in, battle_msg_t *out, size_t *len)
{
    uint8_t buf[BATTLE_MSG_MAX];
    *len = battle_msg_encode(in, buf, sizeof(buf));
    return *len > 0 && battle_msg_decode(out, buf, *len);
}

static void test_messages(void)
{
    battle_msg_t in, out;
    size_t len;

    memset(&in, 0, sizeof(in));
    in.type = BATTLE_MSG_HELLO;
    in.hello.version = BATTLE_PROTOCOL_VERSION;
    strcpy(in.hello.pack, "abcdefghijklmnopqrstuvwxyz-01234");  /* 32: the longest name */
    in.hello.pack_crc = 0xDEADBEEF;
    in.hello.question_count = 1000;
    strcpy(in.hello.host, "Pukk-EA66");
    CHECK(roundtrip(&in, &out, &len), "HELLO roundtrip");
    CHECK(len == 1 + 1 + 33 + 4 + 2 + 10, "HELLO length %zu", len);
    CHECK(strcmp(out.hello.pack, in.hello.pack) == 0 && strcmp(out.hello.host, "Pukk-EA66") == 0 &&
          out.hello.pack_crc == 0xDEADBEEF && out.hello.question_count == 1000, "HELLO fields");

    uint8_t buf[BATTLE_MSG_MAX];
    strcpy(in.hello.pack, "abcdefghijklmnopqrstuvwxyz-012345");    /* 33 */
    CHECK(battle_msg_encode(&in, buf, sizeof(buf)) == 0, "name too long");
    strcpy(in.hello.pack, "tafels");
    CHECK(battle_msg_encode(&in, buf, 10) == 0, "buffer too small");

    /* Another version's HELLO: only the version can be read. */
    const uint8_t other[] = { BATTLE_MSG_HELLO, BATTLE_PROTOCOL_VERSION + 1, 0xAA, 0xBB };
    CHECK(battle_msg_decode(&out, other, sizeof(other)) && out.hello.version == BATTLE_PROTOCOL_VERSION + 1,
          "other version's HELLO");

    memset(&in, 0, sizeof(in));
    in.type = BATTLE_MSG_RESULT;
    in.result.round = 29;
    in.result.winner = BATTLE_GUEST;
    in.result.choice[0] = BATTLE_CHOICE_LATE;
    in.result.choice[1] = 3;
    in.result.reaction_ms[0] = 0;
    in.result.reaction_ms[1] = 60000;
    in.result.points = 512;
    in.result.total[0] = 12345;
    in.result.total[1] = 0x01020304;
    CHECK(roundtrip(&in, &out, &len) && len == 20, "RESULT roundtrip (%zu bytes)", len);
    CHECK(memcmp(&in.result, &out.result, sizeof(in.result)) == 0, "RESULT fields");

    memset(&in, 0, sizeof(in));
    in.type = BATTLE_MSG_ANSWER;
    in.answer.round = 7;
    in.answer.choice = 1;
    in.answer.reaction_ms = 2345;
    CHECK(roundtrip(&in, &out, &len) && len == 6, "ANSWER roundtrip");
    CHECK(out.answer.round == 7 && out.answer.choice == 1 && out.answer.reaction_ms == 2345, "ANSWER fields");

    memset(&in, 0, sizeof(in));
    in.type = BATTLE_MSG_START;
    in.start.countdown_s = 3;
    in.start.rounds = 20;
    CHECK(roundtrip(&in, &out, &len) && len == 4, "START roundtrip");
    CHECK(out.start.countdown_s == 3 && out.start.rounds == 20, "START fields");

    memset(&in, 0, sizeof(in));
    in.type = BATTLE_MSG_QUESTION;
    in.question.round = 19;
    in.question.question = 999;
    CHECK(roundtrip(&in, &out, &len) && len == 5, "QUESTION roundtrip");
    CHECK(out.question.round == 19 && out.question.question == 999, "QUESTION fields");

    memset(&in, 0, sizeof(in));
    in.type = BATTLE_MSG_HELLO_ACK;
    in.hello_ack.version = 1;
    in.hello_ack.has_pack = true;
    CHECK(roundtrip(&in, &out, &len) && len == 3 && out.hello_ack.has_pack, "HELLO_ACK roundtrip");

    const uint8_t types[] = { BATTLE_MSG_START, BATTLE_MSG_QUESTION, BATTLE_MSG_END, BATTLE_MSG_ABORT,
                              BATTLE_MSG_LEAVE };
    for (size_t i = 0; i < sizeof(types); i++) {
        memset(&in, 0, sizeof(in));
        in.type = types[i];
        CHECK(roundtrip(&in, &out, &len) && out.type == types[i], "type 0x%02x roundtrip", types[i]);
    }

    /* Malformed input. */
    const uint8_t unknown[] = { 0x42 };
    CHECK(!battle_msg_decode(&out, unknown, sizeof(unknown)), "unknown type");
    CHECK(!battle_msg_decode(&out, unknown, 0), "empty");
    const uint8_t short_answer[] = { BATTLE_MSG_ANSWER, 1, 0, 2, 0x10 };
    CHECK(!battle_msg_decode(&out, short_answer, sizeof(short_answer)), "truncated");
    const uint8_t long_question[] = { BATTLE_MSG_QUESTION, 1, 0, 2, 0, 0 };
    CHECK(!battle_msg_decode(&out, long_question, sizeof(long_question)), "trailing byte");
    const uint8_t bad_name[] = { BATTLE_MSG_HELLO, BATTLE_PROTOCOL_VERSION, 3, 'a', 0, 'b', 0, 0, 0, 0, 1, 0, 1, 'X' };
    CHECK(!battle_msg_decode(&out, bad_name, sizeof(bad_name)), "NUL in name");
    const uint8_t name_past_end[] = { BATTLE_MSG_HELLO, BATTLE_PROTOCOL_VERSION, 40, 'a', 'b' };
    CHECK(!battle_msg_decode(&out, name_past_end, sizeof(name_past_end)), "name past the end");
}

int main(void)
{
    test_points();
    test_host_first();
    test_guest_faster_but_later();
    test_guest_slower();
    test_guest_first();
    test_wrong_answers();
    test_timeout();
    test_invalid_answers();
    test_tie();
    test_messages();
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("battle: all checks passed\n");
    return 0;
}
