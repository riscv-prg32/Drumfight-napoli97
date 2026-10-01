/*
 * Host gameplay tests for Drumfight Napoli 97.
 *
 * The unmodified cartridge source runs against tests/host/host_prg32.h. A
 * scripted player drives the pad frame by frame (33 ms per frame, like the
 * firmware) through practice, a full match against the CPU, a pass-the-pad
 * match and a network battle with an injected peer.
 *
 *   test_game            run the tests
 *   test_game DIR        also write screenshots (PPM) to DIR
 */
#include "host/host_prg32.h"

#include "../src/drumfight.c"

static int failures;
static int checks;
static const char *shots_dir;

#define CHECK(cond)                                                        \
    do {                                                                   \
        ++checks;                                                          \
        if (!(cond)) {                                                     \
            ++failures;                                                    \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
        }                                                                  \
    } while (0)

#define UP PRG32_BTN_UP
#define DOWN PRG32_BTN_DOWN
#define LEFT PRG32_BTN_LEFT
#define RIGHT PRG32_BTN_RIGHT
#define A PRG32_BTN_A
#define B PRG32_BTN_B
#define START PRG32_BTN_START

static void frame(uint32_t input) {
    host_input = input;
    host_pixels_drawn = 0;
    drumfight_update();
    drumfight_draw();
    host_ms += 33;
}

static void frames(int n, uint32_t input) {
    while (n-- > 0) frame(input);
}

static void tap(uint32_t buttons) {
    frame(buttons);
    frame(0);
}

static void shot(const char *name) {
    char path[512];
    if (!shots_dir) return;
    /* Screens redraw only what changed; a screenshot wants a full frame. */
    g.draw |= DR_ALL;
    drumfight_draw();
    snprintf(path, sizeof(path), "%s/%s.ppm", shots_dir, name);
    host_save_ppm(path);
}

static void restart(uint32_t features) {
    host_features = features;
    host_ms = 1000;
    host_note_count = 0;
    host_score_count = 0;
    host_mp_joined = 0;
    host_mp_peer_count = 0;
    drumfight_init();
    frames(3, 0);
}

/* Run until the sequencer has just fired `step`. */
static void wait_step(int step) {
    for (int i = 0; i < 400; ++i) {
        int before = g.seq.step;
        frame(0);
        if (g.seq.step == step && before != step) return;
    }
    CHECK(!"sequencer never reached the step");
}

/* Play one pad exactly on `step`: direction and button together. */
static void hit(int step, uint32_t buttons) {
    wait_step(step);
    tap(buttons);
}

/* The same, keeping the pad down long enough for an accent. */
static void hit_hard(int step, uint32_t buttons) {
    wait_step(step);
    frames(8, buttons);
    frame(0);
}

static uint32_t pad_buttons(int voice) {
    static const uint32_t dirs[5] = {0, UP, DOWN, LEFT, RIGHT};
    return dirs[df_voice_dir(voice)] | (df_voice_button(voice) == DF_BTN_A ? A : B);
}

/* Record a whole pattern through the pads. 'x' tap, 'X' held (accent). */
static void play_rows(const char *rows[DF_VOICES]) {
    for (int v = 0; v < DF_VOICES; ++v) {
        for (int s = 0; s < DF_STEPS; ++s) {
            char c = rows[v][s];
            if (c == 'x') hit(s, pad_buttons(v));
            if (c == 'X') hit_hard(s, pad_buttons(v));
        }
    }
}

/* A joystick motion followed by its button, as a player would do it. */
static void motion(int move) {
    static const uint32_t dirs[5] = {0, UP, DOWN, LEFT, RIGHT};
    uint8_t seq[3];
    int button;
    int length = df_move_motion(move, seq, &button);
    for (int i = 0; i < length - 1; ++i) {
        frames(2, dirs[seq[i]]);
        frame(0);
    }
    frames(2, dirs[seq[length - 1]]);
    frames(2, dirs[seq[length - 1]] | (button == DF_BTN_A ? A : B));
    frame(0);
}

static int count_notes(int channel) {
    int n = 0;
    for (int i = 0; i < host_note_count; ++i) n += host_notes[i].channel == channel;
    return n;
}

static const char *ROCK[DF_VOICES] = {
    "x.......x.......", "....x.......x...", "x.x.x.x.x.x.x.x.", "................",
    "................", "................", "................", "................",
};

static const char *PIAZZA[DF_VOICES] = {
    "...x..x....x....", /* kick: QUATTRO adds the four on the floor */
    "....X.......X...", /* snare */
    "................", /* hat: already there from the rock beat */
    "................", /* open hat: CONTROTEMPO */
    "................", /* tammorra: TAMMURRIATA */
    ".x.....x.x......", /* bongo */
    "................", /* campana: TRESILLO */
    "....x.......x...", /* clap */
};

static void test_title(void) {
    restart(PRG32_FEATURE_AUDIO);
    CHECK(g.state == ST_TITLE);
    CHECK(strcmp(host_band, "DRUMFIGHT NAPOLI 97") == 0);
    frames(120, 0);
    CHECK(host_note_count > 20); /* the attract groove is playing */
    CHECK(host_fb[10][60] != host_fb[199][0]);
    shot("title");

    /* After the first full paint, the title only repaints the pads. */
    frames(10, 0);
    CHECK(host_pixels_drawn <= 8 * 16 * 7);

    tap(START);
    CHECK(host_scoreboard_shown == 1 && g.state == ST_TITLE);
    tap(DOWN);
    CHECK(g.title_item == MODE_CPU);
    tap(UP);
    tap(UP);
    CHECK(g.title_item == MODE_NET);
    tap(DOWN);
    CHECK(g.title_item == MODE_PRACTICE);
}

static void test_practice(void) {
    restart(PRG32_FEATURE_AUDIO);
    tap(A);
    CHECK(g.state == ST_COMPOSE && g.mode == MODE_PRACTICE);
    CHECK(g.seq.bpm == 112);

    /* An empty bar ticks a metronome on the campana channel. */
    host_note_count = 0;
    wait_step(0);
    wait_step(0);
    CHECK(count_notes(DF_CAMPANA) >= 4 && count_notes(DF_KICK) == 0);

    /* One live hit lands on the step that just fired and sounds once. */
    host_note_count = 0;
    hit(4, A);
    CHECK(df_get(&g.who[0].pattern, DF_KICK, 4) == DF_HIT);
    CHECK(count_notes(DF_KICK) == 1);
    /* A hit played early belongs to the coming step and is not doubled. */
    wait_step(7);
    frames(3, 0);
    host_note_count = 0;
    tap(B);
    CHECK(df_get(&g.who[0].pattern, DF_SNARE, 8) == DF_HIT);
    frames(6, 0);
    CHECK(count_notes(DF_SNARE) == 1);
    /* From the next bar on the sequencer plays it. */
    host_note_count = 0;
    wait_step(8);
    CHECK(count_notes(DF_SNARE) == 1);

    /* Drumming along with the loop leaves it alone; a held pad accents. */
    hit(4, A);
    CHECK(df_get(&g.who[0].pattern, DF_KICK, 4) == DF_HIT);
    hit_hard(4, A);
    CHECK(df_get(&g.who[0].pattern, DF_KICK, 4) == DF_ACCENT);
    hit(4, A);
    CHECK(df_get(&g.who[0].pattern, DF_KICK, 4) == DF_ACCENT);

    /* DOWN + B wipes the voice played last; DOWN + A erases under the head. */
    tap(DOWN | B);
    CHECK(g.who[0].pattern.row[DF_KICK] == 0);
    CHECK(g.who[0].pattern.row[DF_SNARE] != 0);
    hit(2, B);
    for (int i = 0; i < 80; ++i) frame(DOWN | A);
    frame(0);
    CHECK(g.who[0].pattern.row[DF_SNARE] == 0);
    CHECK(g.who[0].score.total == 0);

    /* The tempo never drifts: 8 bars at 112 BPM last 8 * 16 * 15000 / 112 ms. */
    wait_step(0);
    uint32_t start = host_ms;
    for (int bar = 0; bar < 8; ++bar) wait_step(0);
    int expected = 8 * 16 * 15000 / 112;
    int elapsed = (int)(host_ms - start);
    CHECK(elapsed > expected - 40 && elapsed < expected + 40);
}

static void test_moves_and_unlocks(void) {
    restart(PRG32_FEATURE_AUDIO);
    tap(A);
    CHECK(g.unlocked == 0);

    /* Locked: the motion is recognised but refused, and plays no pad. */
    wait_step(1);
    motion(DF_MOVE_QUATTRO);
    CHECK(g.who[0].pattern.moves == 0);
    CHECK(strncmp(g.banner, "LOCKED", 6) == 0);
    CHECK(pattern_empty(&g.who[0].pattern));

    play_rows(ROCK);
    CHECK(g.who[0].score.base >= df_move_threshold(DF_MOVE_CONTROTEMPO));
    CHECK(g.unlocked & (1u << DF_MOVE_QUATTRO));
    CHECK(g.unlocked & (1u << DF_MOVE_CONTROTEMPO));
    CHECK(!(g.unlocked & (1u << DF_MOVE_VESUVIO)));
    unsigned before = g.who[0].score.total;

    motion(DF_MOVE_QUATTRO);
    CHECK(g.who[0].pattern.moves == (1u << DF_MOVE_QUATTRO));
    CHECK(df_get(&g.who[0].pattern, DF_KICK, 4) == DF_HIT);
    CHECK(df_get(&g.who[0].pattern, DF_KICK, 12) == DF_HIT);
    CHECK(strcmp(g.banner, "QUATTRO!") == 0);
    CHECK(g.who[0].score.bonus == DF_MOVE_BONUS);
    CHECK(g.who[0].score.total > before);

    motion(DF_MOVE_CONTROTEMPO);
    CHECK(df_get(&g.who[0].pattern, DF_OPEN, 2) == DF_HIT);
    CHECK(g.who[0].score.bonus == 2 * DF_MOVE_BONUS);

    /* A motion that ends on DOWN + A must not start erasing. */
    g.unlocked = 0xff;
    hit(0, A); /* select the kick */
    uint32_t kick = g.who[0].pattern.row[DF_KICK];
    motion(DF_MOVE_VESUVIO);
    CHECK(g.who[0].pattern.moves & (1u << DF_MOVE_VESUVIO));
    CHECK((g.who[0].pattern.row[DF_KICK] & kick) == kick);

    /* Keep building until every move is open and used. */
    g.unlocked = 0;
    g.judge_dirty = 1;
    frame(0);
    play_rows(PIAZZA);
    motion(DF_MOVE_ROLLATA);
    motion(DF_MOVE_TRESILLO);
    motion(DF_MOVE_TAMMURRIATA);
    hit(4, B); /* select the snare for the echo */
    motion(DF_MOVE_ECO);
    hit(1, pad_buttons(DF_BONGO));
    motion(DF_MOVE_RITORNELLO);
    printf("practice: groove %u + moves %u = %u, unlocked %02x, used %02x\n", g.who[0].score.base,
           g.who[0].score.bonus, g.who[0].score.total, g.unlocked, g.who[0].pattern.moves);
    CHECK(g.who[0].pattern.moves == 0xff);
    CHECK(g.who[0].score.total > 800);
    wait_step(6);
    shot("compose");

    /* START opens the menu; the loop keeps playing underneath. */
    tap(START);
    CHECK(g.menu_open);
    host_note_count = 0;
    frames(40, 0);
    CHECK(host_note_count > 0);
    shot("menu");
    tap(DOWN);
    tap(RIGHT);
    CHECK(g.seq.bpm == 124);
    tap(LEFT);
    CHECK(g.seq.bpm == 112);
    tap(DOWN);
    tap(DOWN);
    CHECK(g.menu_item == 3);
    tap(A); /* EXIT submits the best groove of the session */
    CHECK(g.state == ST_TITLE);
    CHECK(host_score_count == 1 && host_score_last > 800);
}

static void finish_composing(void) {
    tap(START);
    tap(DOWN);
    CHECK(g.menu_item == 1);
    tap(A);
}

static void test_cpu_match(void) {
    restart(PRG32_FEATURE_AUDIO);
    tap(DOWN);
    tap(A);
    CHECK(g.state == ST_SETUP && g.mode == MODE_CPU);
    shot("setup");
    tap(DOWN);
    tap(A); /* FUORICORSO */
    CHECK(g.state == ST_READY && g.count == 2 && g.who[1].kind == KIND_CPU && g.who[1].level == 1);
    CHECK(strcmp(g.who[1].name, "FUORICORSO") == 0);
    shot("ready");

    int player_points = 0, cpu_points = 0;
    for (int round = 0; round < ROUNDS; ++round) {
        CHECK(g.state == ST_READY && g.round == round);
        tap(A);
        CHECK(g.state == ST_COMPOSE && g.seq.bpm == ROUND_BPM[round]);
        if (round == 0) {
            /* Round 1: let the sixteen bars run out with a simple beat. */
            play_rows(ROCK);
            for (int i = 0; i < 3000 && g.state == ST_COMPOSE; ++i) frame(0);
        } else {
            play_rows(ROCK);
            if (round == 2) motion(DF_MOVE_QUATTRO);
            finish_composing();
        }
        CHECK(g.state == ST_SHOW && g.show == 0);
        CHECK(g.who[1].pattern.moves != 0); /* the CPU has composed */
        frames(70, 0);
        CHECK(g.shown_total == g.who[0].score.total);
        if (round == 0) shot("showcase");
        for (int i = 0; i < 400 && g.state == ST_SHOW && g.show == 0; ++i) frame(0);
        CHECK(g.state == ST_SHOW && g.show == 1);
        tap(A); /* skip the CPU showcase */
        CHECK(g.state == ST_RESULT);
        if (g.who[0].score.total > g.who[1].score.total) ++player_points;
        if (g.who[1].score.total > g.who[0].score.total) ++cpu_points;
        CHECK(g.who[0].points == player_points && g.who[1].points == cpu_points);
        if (round == 0) shot("result");
        tap(A);
    }
    CHECK(g.state == ST_FINAL);
    CHECK(cpu_points == 3); /* a bare rock beat does not beat the FUORICORSO */
    CHECK(g.order[0] == 1);
    CHECK(host_score_count == 1 && host_score_last == g.who[0].best);
    shot("final");
    tap(A);
    CHECK(g.state == ST_TITLE);
}

static void test_piazza_match(void) {
    restart(PRG32_FEATURE_AUDIO);
    tap(DOWN);
    tap(DOWN);
    tap(A);
    CHECK(g.state == ST_SETUP && g.mode == MODE_PIAZZA);
    tap(DOWN);
    tap(DOWN);
    tap(A); /* 4 players */
    CHECK(g.count == 4);
    for (int round = 0; round < ROUNDS; ++round) {
        for (int player = 0; player < 4; ++player) {
            CHECK(g.state == ST_READY && g.turn == player);
            /* The second controller works too: the pad can be passed or shared. */
            host_input = A << 8;
            frame(host_input);
            frame(0);
            CHECK(g.state == ST_COMPOSE);
            for (int n = 0; n <= player; ++n) hit(4 * n, n & 1 ? B : A);
            finish_composing();
        }
        CHECK(g.state == ST_SHOW);
        for (int i = 0; i < 4; ++i) tap(A);
        CHECK(g.state == ST_RESULT);
        /* More notes, more groove here: the last player wins every round. */
        CHECK(g.order[0] == 3 && g.who[3].gained == 3 && g.who[0].gained == 0);
        tap(A);
    }
    CHECK(g.state == ST_FINAL && g.who[3].points == 9 && g.who[0].points == 0);
    CHECK(strcmp(g.who[g.order[0]].name, "PLAYER 4") == 0);
}

/* A second board, simulated: it publishes snapshots like the cartridge. */
static df_pattern_t remote_pattern;
static df_net_head_t remote_head;

static void remote_publish(void) {
    prg32_player_state_t *peer = &host_mp_peers[0];
    remote_head.voice = (uint8_t)((remote_head.voice + 1) & 7);
    peer->player_id = 0x3f2a;
    df_net_pack(&remote_pattern, &remote_head, &peer->x, &peer->y, &peer->sprite, &peer->flags);
    host_mp_peer_count = 1;
}

static void net_frames(int n, uint32_t input) {
    while (n-- > 0) {
        remote_publish();
        frame(input);
    }
}

static void test_network(void) {
    /* Without the multiplayer feature the mode explains itself. */
    restart(PRG32_FEATURE_AUDIO);
    tap(UP);
    tap(A);
    CHECK(g.state == ST_NETLOBBY && !g.net_ok && !host_mp_joined);
    tap(B);
    CHECK(g.state == ST_TITLE);

    restart(PRG32_FEATURE_AUDIO | PRG32_FEATURE_MULTIPLAYER);
    tap(UP);
    tap(A);
    CHECK(g.state == ST_NETLOBBY && g.net_ok && host_mp_joined);
    CHECK(strcmp(host_mp_signature, "drumfight-napoli97-v1") == 0);
    /* Alone in the lobby, as in QEMU: ready, but nothing starts. */
    tap(A);
    frames(30, 0);
    CHECK(g.state == ST_NETLOBBY && g.net_ready);

    uint32_t seed = 77;
    for (int round = 0; round < ROUNDS; ++round) {
        df_clear(&remote_pattern);
        remote_head.phase = DF_NET_LOBBY;
        remote_head.round = (uint8_t)round;
        remote_head.ready = 0;
        net_frames(10, 0);
        CHECK(g.state == ST_NETLOBBY); /* the peer is not ready yet */
        if (round == 0) shot("lobby");
        remote_head.ready = 1;
        net_frames(4, 0);
        CHECK(g.state == ST_COMPOSE && g.count == 2 && g.who[1].kind == KIND_NET);
        CHECK(strcmp(g.who[1].name, "P-3F2A") == 0);

        /* Both compose. The local snapshot advertises COMPOSE and our rows. */
        remote_head.phase = DF_NET_COMPOSE;
        remote_head.ready = 0;
        net_frames(8, 0);
        CHECK(((host_mp_local.sprite >> 3) & 3) == DF_NET_COMPOSE);
        CHECK(((host_mp_local.sprite >> 5) & 7) == (unsigned)round);
        net_frames(1, A);
        net_frames(30, 0);
        df_peer_t mirror;
        df_peer_reset(&mirror, 1);
        for (int i = 0; i < 30; ++i) {
            net_frames(1, 0);
            df_peer_feed(&mirror, host_mp_local.x, host_mp_local.y, host_mp_local.sprite, host_mp_local.flags);
        }
        CHECK(mirror.complete);
        CHECK(memcmp(mirror.pattern.row, g.who[0].pattern.row, sizeof(mirror.pattern.row)) == 0);

        /* The local player finishes first and waits for the peer. */
        host_input = START;
        net_frames(1, START);
        net_frames(1, 0);
        net_frames(1, DOWN);
        net_frames(1, 0);
        net_frames(1, A);
        net_frames(5, 0);
        CHECK(g.state == ST_NETWAIT);
        if (round == 0) shot("netwait");

        /* The peer composes its bar and finishes. */
        df_ai_compose(&remote_pattern, 2, &seed);
        remote_head.phase = DF_NET_DONE;
        net_frames(12, 0);
        CHECK(g.state == ST_SHOW);
        CHECK(memcmp(g.who[1].pattern.row, remote_pattern.row, sizeof(remote_pattern.row)) == 0);
        CHECK(g.who[1].pattern.moves == remote_pattern.moves);
        df_score_t expected;
        df_judge(&remote_pattern, &expected);
        net_frames(1, A);
        net_frames(1, 0);
        net_frames(1, A);
        net_frames(1, 0);
        CHECK(g.state == ST_RESULT);
        CHECK(g.who[1].score.total == expected.total && g.who[1].gained == 1);
        remote_head.phase = DF_NET_RESULT;
        net_frames(1, A);
        net_frames(2, 0);
        if (round + 1 < ROUNDS) CHECK(g.state == ST_NETLOBBY && g.net_ready && g.round == round + 1);
    }
    CHECK(g.state == ST_FINAL && g.who[1].points == 3 && g.order[0] == 1);
    tap(A);
    CHECK(g.state == ST_TITLE && !host_mp_joined);

    /* A peer that never delivers does not block the battle for ever. */
    restart(PRG32_FEATURE_AUDIO | PRG32_FEATURE_MULTIPLAYER);
    tap(UP);
    tap(A);
    df_clear(&remote_pattern);
    remote_head.phase = DF_NET_LOBBY;
    remote_head.round = 0;
    remote_head.ready = 1;
    net_frames(2, A);
    net_frames(4, 0);
    CHECK(g.state == ST_COMPOSE);
    remote_head.phase = DF_NET_COMPOSE;
    net_frames(1, START);
    net_frames(1, 0);
    net_frames(1, DOWN);
    net_frames(1, 0);
    net_frames(1, A);
    net_frames(2, 0);
    CHECK(g.state == ST_NETWAIT);
    net_frames(NET_WAIT_MS / 33 - 20, 0);
    CHECK(g.state == ST_NETWAIT);
    net_frames(40, 0);
    CHECK(g.state == ST_SHOW);
}

int main(int argc, char **argv) {
    shots_dir = argc > 1 ? argv[1] : 0;
    test_title();
    test_practice();
    test_moves_and_unlocks();
    test_cpu_match();
    test_piazza_match();
    test_network();
    CHECK(host_bad_chars == 0); /* every drawn text is portable and on screen */
    printf("test_game: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
