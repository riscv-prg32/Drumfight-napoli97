/*
 * Host unit tests for the Drumfight Napoli 97 logic core (src/df_core.h).
 * Run through tests/run_tests.sh; no PRG32 checkout or toolchain is needed.
 */
#include <stdio.h>
#include <string.h>

#include "df_core.h"

static int failures;
static int checks;

#define CHECK(cond)                                                        \
    do {                                                                   \
        ++checks;                                                          \
        if (!(cond)) {                                                     \
            ++failures;                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
        }                                                                  \
    } while (0)

/* Build a pattern from eight 16-character rows: '.' off, 'g' ghost,
 * 'x' hit, 'X' accent. Row order is the voice order of df_core.h. */
static void from_text(df_pattern_t *p, const char *rows[DF_VOICES]) {
    df_clear(p);
    for (int v = 0; v < DF_VOICES; ++v) {
        for (int s = 0; s < DF_STEPS; ++s) {
            char c = rows[v][s];
            df_set(p, v, s, c == 'X' ? DF_ACCENT : c == 'x' ? DF_HIT : c == 'g' ? DF_GHOST : DF_OFF);
        }
    }
}

static const char *ROCK[DF_VOICES] = {
    "x.......x.......", /* kick */
    "....x.......x...", /* snare */
    "x.x.x.x.x.x.x.x.", /* hat */
    "................", "................", "................", "................", "................",
};

static const char *PIAZZA[DF_VOICES] = {
    "X..x..x.x..x....", /* kick */
    "....X..g....X.gx", /* snare */
    "x.x.x.x.x.x.x.x.", /* hat */
    "..x...x...x...x.", /* open hat */
    "X..x..xgX..x..x.", /* tammorra */
    ".x.....x.x......", /* bongo */
    "X..x..x.X..x..x.", /* campana */
    "....x.......x...", /* clap */
};

static void test_model(void) {
    df_pattern_t p;
    df_clear(&p);
    CHECK(df_get(&p, 3, 7) == DF_OFF);
    df_set(&p, 3, 7, DF_ACCENT);
    CHECK(df_get(&p, 3, 7) == DF_ACCENT);
    CHECK(df_get(&p, 3, 6) == DF_OFF && df_get(&p, 3, 8) == DF_OFF);
    CHECK(df_get(&p, 3, 23) == DF_ACCENT); /* steps wrap */
    df_set(&p, 3, 7, DF_OFF);
    CHECK(p.row[3] == 0);

    df_live_hit(&p, DF_KICK, 0);
    CHECK(df_get(&p, DF_KICK, 0) == DF_HIT);
    df_live_hit(&p, DF_KICK, 0);
    CHECK(df_get(&p, DF_KICK, 0) == DF_HIT); /* drumming along changes nothing */
    df_live_accent(&p, DF_KICK, 0);
    CHECK(df_get(&p, DF_KICK, 0) == DF_ACCENT);
    df_live_hit(&p, DF_KICK, 0);
    CHECK(df_get(&p, DF_KICK, 0) == DF_ACCENT);
    df_set(&p, DF_KICK, 1, DF_GHOST);
    df_live_hit(&p, DF_KICK, 1);
    CHECK(df_get(&p, DF_KICK, 1) == DF_HIT);

    df_raise(&p, DF_KICK, 0, DF_HIT);
    CHECK(df_get(&p, DF_KICK, 0) == DF_ACCENT); /* raise never lowers */
}

static void test_pads(void) {
    int seen = 0;
    for (int dir = DF_DIR_NONE; dir <= DF_DIR_RIGHT; ++dir) {
        for (int button = 0; button < 2; ++button) {
            int voice = df_pad_voice(dir, button);
            if (dir == DF_DIR_DOWN) {
                CHECK(voice == -1);
                continue;
            }
            CHECK(voice >= 0 && voice < DF_VOICES);
            CHECK(df_voice_dir(voice) == dir);
            CHECK(df_voice_button(voice) == button);
            seen |= 1 << voice;
        }
    }
    CHECK(seen == 0xff); /* every voice has exactly one pad */
}

static void test_judge(void) {
    df_pattern_t p;
    df_score_t empty, rock, piazza, noise;

    df_clear(&p);
    p.moves = 0xff;
    df_judge(&p, &empty);
    CHECK(empty.total == 0 && empty.base == 0 && empty.bonus == 0);

    from_text(&p, ROCK);
    df_judge(&p, &rock);
    CHECK(rock.crit[DF_CRIT_PULSE] == 100);
    CHECK(rock.base > 120 && rock.base < 320);

    from_text(&p, PIAZZA);
    df_judge(&p, &piazza);
    CHECK(piazza.base > rock.base + 150);
    CHECK(piazza.crit[DF_CRIT_VARIETY] == 100);
    CHECK(piazza.total == piazza.base);
    p.moves = 0x07;
    df_judge(&p, &piazza);
    CHECK(piazza.bonus == 3 * DF_MOVE_BONUS);
    CHECK(piazza.total == piazza.base + 3 * DF_MOVE_BONUS);

    /* Every step of every voice: noise, not groove. */
    for (int v = 0; v < DF_VOICES; ++v) p.row[v] = 0xaaaaaaaau;
    p.moves = 0;
    df_judge(&p, &noise);
    CHECK(noise.crit[DF_CRIT_BALANCE] == 0);
    CHECK(noise.base < rock.base);

    for (int c = 0; c < DF_CRITERIA; ++c) {
        CHECK(rock.crit[c] <= 100 && piazza.crit[c] <= 100 && noise.crit[c] <= 100);
    }
    printf("judge: rock %u, piazza %u (+%u), noise %u\n", rock.base, piazza.base, piazza.bonus, noise.base);
    printf("piazza criteria:");
    for (int c = 0; c < DF_CRITERIA; ++c) printf(" %u", piazza.crit[c]);
    printf("\n");
}

static void test_syncopation(void) {
    df_pattern_t p;
    df_clear(&p);
    df_set(&p, DF_KICK, 0, DF_HIT);
    CHECK(df_voice_syncopation(&p, DF_KICK) == 0); /* downbeat only */
    df_set(&p, DF_KICK, 3, DF_HIT);                /* weak step, rest on beat 2 */
    CHECK(df_voice_syncopation(&p, DF_KICK) == 2);
    df_set(&p, DF_KICK, 4, DF_HIT);                /* beat 2 played: resolved */
    CHECK(df_voice_syncopation(&p, DF_KICK) == 1); /* only beat 2 against beat 3 */
    df_set(&p, DF_KICK, 5, DF_GHOST);              /* ghosts never syncopate */
    CHECK(df_voice_syncopation(&p, DF_KICK) == 1);
}

static void test_moves(void) {
    uint8_t dirs[3];
    int button;
    /* Every move has a distinct motion that starts with DOWN. */
    for (int a = 0; a < DF_MOVES; ++a) {
        int length = df_move_motion(a, dirs, &button);
        CHECK(length == 2 || length == 3);
        CHECK(dirs[0] == DF_DIR_DOWN);
        CHECK(df_move_match(dirs, length, button) == a);
        if (length == 2) CHECK(df_move_match(dirs, 1, button) == -1);
        if (a > 0) CHECK(df_move_threshold(a) > df_move_threshold(a - 1));
    }
    CHECK(df_move_threshold(DF_MOVES - 1) < DF_BASE_MAX);

    /* A long motion is not mistaken for the short one it ends with. */
    uint8_t vesuvio[3] = {DF_DIR_DOWN, DF_DIR_UP, DF_DIR_DOWN};
    CHECK(df_move_match(vesuvio, 3, DF_BTN_A) == DF_MOVE_VESUVIO);
    CHECK(df_move_match(vesuvio, 3, DF_BTN_B) == DF_MOVE_RITORNELLO);
    uint8_t pads[2] = {DF_DIR_LEFT, DF_DIR_RIGHT};
    CHECK(df_move_match(pads, 2, DF_BTN_A) == -1);

    df_pattern_t p;
    df_clear(&p);
    df_move_apply(&p, DF_MOVE_QUATTRO, 0);
    CHECK(df_get(&p, DF_KICK, 0) == DF_ACCENT && df_get(&p, DF_KICK, 4) == DF_HIT);
    CHECK(df_get(&p, DF_KICK, 8) == DF_HIT && df_get(&p, DF_KICK, 12) == DF_HIT);
    CHECK(p.moves == 1u << DF_MOVE_QUATTRO);

    df_move_apply(&p, DF_MOVE_ECO, DF_KICK);
    CHECK(df_get(&p, DF_KICK, 3) == DF_GHOST && df_get(&p, DF_KICK, 15) == DF_GHOST);
    CHECK(df_get(&p, DF_KICK, 4) == DF_HIT); /* echoes never overwrite notes */

    df_clear(&p);
    df_set(&p, DF_BONGO, 1, DF_HIT);
    df_set(&p, DF_BONGO, 6, DF_ACCENT);
    df_set(&p, DF_BONGO, 12, DF_HIT);
    df_move_apply(&p, DF_MOVE_RITORNELLO, DF_BONGO);
    CHECK(df_get(&p, DF_BONGO, 9) == DF_HIT && df_get(&p, DF_BONGO, 14) == DF_ACCENT);
    CHECK(df_get(&p, DF_BONGO, 12) == DF_OFF);

    df_clear(&p);
    for (int m = 0; m < DF_MOVES; ++m) df_move_apply(&p, m, DF_SNARE);
    CHECK(p.moves == 0xff);
    df_score_t all;
    df_judge(&p, &all);
    CHECK(all.bonus == DF_MOVES * DF_MOVE_BONUS);
    CHECK(all.total <= DF_TOTAL_MAX);
    printf("moves: all eight on an empty bar score %u (base %u)\n", all.total, all.base);
}

static void test_ai(void) {
    int previous_mean = 0;
    for (int level = 0; level < DF_AI_LEVELS; ++level) {
        int sum = 0, lo = 9999, hi = 0;
        for (uint32_t seed = 1; seed <= 200; ++seed) {
            uint32_t rng = seed * 2654435761u;
            df_pattern_t p;
            df_score_t s;
            df_ai_compose(&p, level, &rng);
            df_judge(&p, &s);
            sum += s.total;
            if (s.total < lo) lo = s.total;
            if (s.total > hi) hi = s.total;
            CHECK(p.moves != 0);
        }
        int mean = sum / 200;
        printf("ai level %d: mean %d, range %d..%d (target %d)\n", level, mean, lo, hi, df_ai_target(level));
        CHECK(mean > previous_mean + 100);     /* levels are clearly apart */
        CHECK(lo >= df_ai_target(level) - 120); /* never embarrassingly weak */
        CHECK(hi <= df_ai_target(level) + 120); /* never out of its league */
        previous_mean = mean;
    }
    /* Same seed, same pattern: battles can be replayed in tests. */
    uint32_t a = 1234, b = 1234;
    df_pattern_t pa, pb;
    df_ai_compose(&pa, 2, &a);
    df_ai_compose(&pb, 2, &b);
    CHECK(memcmp(pa.row, pb.row, sizeof(pa.row)) == 0 && pa.moves == pb.moves);
}

static void test_network(void) {
    df_pattern_t mine;
    from_text(&mine, PIAZZA);
    mine.moves = 0x25;
    df_peer_t peer;
    df_peer_reset(&peer, 42);

    /* A full cycle of eight snapshots rebuilds the pattern. */
    for (int v = 0; v < DF_VOICES; ++v) {
        df_net_head_t head = {(uint8_t)v, DF_NET_COMPOSE, 1, 0};
        int16_t x, y;
        uint16_t sprite, flags;
        df_net_pack(&mine, &head, &x, &y, &sprite, &flags);
        CHECK(!peer.complete);
        df_peer_feed(&peer, x, y, sprite, flags);
    }
    CHECK(peer.complete);
    CHECK(memcmp(peer.pattern.row, mine.row, sizeof(mine.row)) == 0);
    CHECK(peer.pattern.moves == 0x25);
    CHECK(peer.phase == DF_NET_COMPOSE && peer.round == 1 && !peer.ready);
    CHECK(!df_peer_done(&peer, 1)); /* still composing */
    CHECK(df_peer_ready(&peer, 1) && !df_peer_ready(&peer, 2));

    /* The pattern changes: stale rows must not be trusted. */
    df_live_hit(&mine, DF_CLAP, 2);
    CHECK(df_get(&mine, DF_CLAP, 2) == DF_HIT);
    {
        df_net_head_t head = {DF_KICK, DF_NET_DONE, 1, 0};
        int16_t x, y;
        uint16_t sprite, flags;
        df_net_pack(&mine, &head, &x, &y, &sprite, &flags);
        df_peer_feed(&peer, x, y, sprite, flags);
        CHECK(!peer.complete && !df_peer_done(&peer, 1));
    }
    /* Snapshots may be lost, repeated or reordered. */
    static const int order[] = {7, 7, 3, 1, 6, 2, 5, 3, 4, 0};
    for (unsigned i = 0; i < sizeof(order) / sizeof(order[0]); ++i) {
        df_net_head_t head = {(uint8_t)order[i], DF_NET_DONE, 1, 0};
        int16_t x, y;
        uint16_t sprite, flags;
        df_net_pack(&mine, &head, &x, &y, &sprite, &flags);
        df_peer_feed(&peer, x, y, sprite, flags);
    }
    CHECK(peer.complete && df_peer_done(&peer, 1));
    CHECK(df_get(&peer.pattern, DF_CLAP, 2) == DF_HIT);

    /* Both boards compute the same groove from the same pattern. */
    df_score_t local, remote;
    df_judge(&mine, &local);
    df_judge(&peer.pattern, &remote);
    CHECK(local.total == remote.total);

    /* A peer that already waits in the next lobby still counts as done. */
    {
        df_net_head_t head = {DF_KICK, DF_NET_LOBBY, 2, 1};
        int16_t x, y;
        uint16_t sprite, flags;
        df_net_pack(&mine, &head, &x, &y, &sprite, &flags);
        df_peer_feed(&peer, x, y, sprite, flags);
        CHECK(df_peer_done(&peer, 1) && df_peer_ready(&peer, 2));
    }
}

static void test_ranking(void) {
    uint16_t totals[4] = {500, 720, 500, 310};
    CHECK(df_round_points(totals, 4, 1) == 3);
    CHECK(df_round_points(totals, 4, 0) == 1 && df_round_points(totals, 4, 2) == 1);
    CHECK(df_round_points(totals, 4, 3) == 0);
    uint8_t order[4];
    df_rank(totals, 4, order);
    CHECK(order[0] == 1 && order[1] == 0 && order[2] == 2 && order[3] == 3);
    uint16_t duel[2] = {640, 641};
    CHECK(df_round_points(duel, 2, 1) == 1 && df_round_points(duel, 2, 0) == 0);
}

int main(void) {
    test_model();
    test_pads();
    test_judge();
    test_syncopation();
    test_moves();
    test_ai();
    test_network();
    test_ranking();
    printf("test_core: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
