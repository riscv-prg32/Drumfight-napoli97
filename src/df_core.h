/*
 * Drumfight Napoli 97 - game logic core.
 *
 * Everything in this header is pure integer C99 with no PRG32 calls, so the
 * same code runs inside the cartridge and inside the host unit tests
 * (tests/test_core.c). It contains:
 *
 *   - the pattern model (8 voices x 16 steps, 2 bits per step);
 *   - the groove judge (six criteria, 0..100 each);
 *   - the eight special drum moves and their joystick motions;
 *   - the CPU opponent (hill climbing on the judge);
 *   - the network snapshot packing used by the multiplayer battle.
 *
 * Portable PRG32 cartridges carry no relocation records, so initialised
 * static data must not contain pointers: tables hold plain numbers and
 * fixed-size character arrays only.
 */
#ifndef DF_CORE_H
#define DF_CORE_H

#include <stdint.h>

#define DF_VOICES 8
#define DF_STEPS 16

/* Voice order = synth channel = instrument index in audio/audio.json. */
#define DF_KICK 0
#define DF_SNARE 1
#define DF_HAT 2
#define DF_OPEN 3
#define DF_TAMMORRA 4
#define DF_BONGO 5
#define DF_CAMPANA 6
#define DF_CLAP 7

/* Step levels. */
#define DF_OFF 0
#define DF_GHOST 1
#define DF_HIT 2
#define DF_ACCENT 3

/* Joystick directions used by pads and motions. */
#define DF_DIR_NONE 0
#define DF_DIR_UP 1
#define DF_DIR_DOWN 2
#define DF_DIR_LEFT 3
#define DF_DIR_RIGHT 4

#define DF_BTN_A 0
#define DF_BTN_B 1

/* Special drum moves. */
#define DF_MOVES 8
#define DF_MOVE_QUATTRO 0
#define DF_MOVE_CONTROTEMPO 1
#define DF_MOVE_ROLLATA 2
#define DF_MOVE_TRESILLO 3
#define DF_MOVE_TAMMURRIATA 4
#define DF_MOVE_ECO 5
#define DF_MOVE_RITORNELLO 6
#define DF_MOVE_VESUVIO 7
#define DF_MOVE_BONUS 25

/* Judge criteria. */
#define DF_CRITERIA 6
#define DF_CRIT_PULSE 0
#define DF_CRIT_VARIETY 1
#define DF_CRIT_BALANCE 2
#define DF_CRIT_SYNCOPATION 3
#define DF_CRIT_DYNAMICS 4
#define DF_CRIT_STRUCTURE 5
#define DF_BASE_MAX 800
#define DF_TOTAL_MAX 999

/* CPU opponents. */
#define DF_AI_LEVELS 3

typedef struct {
    uint32_t row[DF_VOICES]; /* step s uses bits 2s+1..2s */
    uint8_t moves;           /* bit m set = special move m was performed */
} df_pattern_t;

typedef struct {
    uint8_t crit[DF_CRITERIA]; /* 0..100 each */
    uint16_t base;             /* weighted criteria, 0..DF_BASE_MAX */
    uint16_t bonus;            /* DF_MOVE_BONUS per performed move */
    uint16_t total;            /* base + bonus, capped at DF_TOTAL_MAX */
} df_score_t;

/* ------------------------------------------------------------------ */
/* Pattern model                                                       */
/* ------------------------------------------------------------------ */

static inline int df_get(const df_pattern_t *p, int voice, int step) {
    return (int)((p->row[voice] >> ((step & 15) * 2)) & 3u);
}

static inline void df_set(df_pattern_t *p, int voice, int step, int level) {
    int shift = (step & 15) * 2;
    p->row[voice] = (p->row[voice] & ~(3u << shift)) |
                    (((uint32_t)level & 3u) << shift);
}

static inline void df_clear(df_pattern_t *p) {
    for (int v = 0; v < DF_VOICES; ++v) p->row[v] = 0;
    p->moves = 0;
}

static inline void df_copy(df_pattern_t *dst, const df_pattern_t *src) {
    for (int v = 0; v < DF_VOICES; ++v) dst->row[v] = src->row[v];
    dst->moves = src->moves;
}

/* A live pad hit: an empty or ghost step becomes a hit. Hitting a step
 * that already sounds changes nothing, so a player can keep drumming along
 * with the loop, bar after bar, without altering it. */
static inline void df_live_hit(df_pattern_t *p, int voice, int step) {
    if (df_get(p, voice, step) < DF_HIT) df_set(p, voice, step, DF_HIT);
}

/* A pad that is held down turns its hit into an accent. */
static inline void df_live_accent(df_pattern_t *p, int voice, int step) {
    df_set(p, voice, step, DF_ACCENT);
}

/* Never lowers a level: used by the moves, so a move cannot erase accents
 * the player already played. */
static inline void df_raise(df_pattern_t *p, int voice, int step, int level) {
    if (df_get(p, voice, step) < level) df_set(p, voice, step, level);
}

static inline int df_popcount8(unsigned value) {
    int n = 0;
    for (value &= 0xffu; value; value >>= 1) n += (int)(value & 1u);
    return n;
}

/* Deterministic generator (xorshift32); the state must never be zero. */
static inline uint32_t df_rand(uint32_t *state) {
    uint32_t x = *state ? *state : 0x9e3779b9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static inline int df_rand_below(uint32_t *state, int n) {
    return n > 0 ? (int)((df_rand(state) >> 8) % (uint32_t)n) : 0;
}

/* ------------------------------------------------------------------ */
/* Pads: joystick direction + button -> voice                          */
/* ------------------------------------------------------------------ */

/* DOWN is reserved for editing (erase / clear) and for starting motions. */
static inline int df_pad_voice(int dir, int button) {
    if (dir == DF_DIR_NONE) return button == DF_BTN_A ? DF_KICK : DF_SNARE;
    if (dir == DF_DIR_UP) return button == DF_BTN_A ? DF_HAT : DF_OPEN;
    if (dir == DF_DIR_LEFT) return button == DF_BTN_A ? DF_TAMMORRA : DF_BONGO;
    if (dir == DF_DIR_RIGHT) return button == DF_BTN_A ? DF_CAMPANA : DF_CLAP;
    return -1;
}

/* Inverse of df_pad_voice, for the on-screen pad legend. */
static inline int df_voice_dir(int voice) {
    static const uint8_t dirs[DF_VOICES] = {
        DF_DIR_NONE, DF_DIR_NONE, DF_DIR_UP,    DF_DIR_UP,
        DF_DIR_LEFT, DF_DIR_LEFT, DF_DIR_RIGHT, DF_DIR_RIGHT,
    };
    return dirs[voice & 7];
}

static inline int df_voice_button(int voice) { return voice & 1; }

/* ------------------------------------------------------------------ */
/* Groove judge                                                        */
/* ------------------------------------------------------------------ */

/* Metric weight of a sixteenth-note position in a 4/4 bar. */
static inline int df_metric(int step) {
    step &= 15;
    if (step == 0) return 4;
    if ((step & 7) == 0) return 3;
    if ((step & 3) == 0) return 2;
    if ((step & 1) == 0) return 1;
    return 0;
}

static inline int df_clamp(int value, int lo, int hi) {
    return value < lo ? lo : (value > hi ? hi : value);
}

/* Syncopation of one voice (Longuet-Higgins and Lee): a played note on a
 * weak position followed by silence up to and including the next stronger
 * position. Ghost notes count as silence. */
static inline int df_voice_syncopation(const df_pattern_t *p, int voice) {
    int sum = 0;
    for (int s = 0; s < DF_STEPS; ++s) {
        if (df_get(p, voice, s) < DF_HIT) continue;
        int w = df_metric(s);
        if (w == 4) continue;
        int filled = 0;
        int q = s + 1;
        for (;; ++q) {
            if (df_get(p, voice, q) >= DF_HIT) {
                filled = 1;
                break;
            }
            if (df_metric(q) > w) break;
        }
        if (!filled) sum += df_metric(q) - w;
    }
    return sum;
}

static inline void df_judge(const df_pattern_t *p, df_score_t *out) {
    int hits = 0, accents = 0, ghosts = 0, voices = 0;
    int per_voice[DF_VOICES];
    for (int v = 0; v < DF_VOICES; ++v) {
        int strong = 0;
        per_voice[v] = 0;
        for (int s = 0; s < DF_STEPS; ++s) {
            int level = df_get(p, v, s);
            if (level == DF_OFF) continue;
            ++per_voice[v];
            ++hits;
            if (level == DF_GHOST) ++ghosts;
            else ++strong;
            if (level == DF_ACCENT) ++accents;
        }
        if (strong) ++voices;
    }

    for (int c = 0; c < DF_CRITERIA; ++c) out->crit[c] = 0;
    out->base = 0;
    out->bonus = (uint16_t)(df_popcount8(p->moves) * DF_MOVE_BONUS);
    if (hits == 0) {
        /* Performed moves that were erased again earn nothing. */
        out->bonus = 0;
        out->total = 0;
        return;
    }

    /* 1. PULSE: the foundation every listener in the piazza can follow. */
    int pulse = 0;
    if (df_get(p, DF_KICK, 0) >= DF_HIT) pulse += 30;
    if (df_get(p, DF_KICK, 8) >= DF_HIT) pulse += 15;
    if (df_get(p, DF_SNARE, 4) >= DF_HIT || df_get(p, DF_CLAP, 4) >= DF_HIT) pulse += 20;
    if (df_get(p, DF_SNARE, 12) >= DF_HIT || df_get(p, DF_CLAP, 12) >= DF_HIT) pulse += 20;
    int keepers = 0;
    for (int s = 0; s < DF_STEPS; s += 2) {
        if (df_get(p, DF_HAT, s) || df_get(p, DF_OPEN, s) || df_get(p, DF_CAMPANA, s)) ++keepers;
    }
    pulse += (keepers > 5 ? 5 : keepers) * 3;

    /* 2. VARIETY: how many of the eight voices take part. */
    static const uint8_t variety_points[DF_VOICES + 1] = {0, 5, 15, 28, 42, 56, 70, 85, 100};
    int variety = variety_points[voices];

    /* 3. BALANCE: enough notes to groove, not so many that it is noise. */
    int balance;
    if (hits < 20) balance = hits * 5;
    else if (hits <= 56) balance = 100;
    else balance = 100 - (hits - 56) * 3;
    for (int s = 0; s < DF_STEPS; ++s) {
        int stack = 0;
        for (int v = 0; v < DF_VOICES; ++v) stack += df_get(p, v, s) != DF_OFF;
        if (stack > 5) balance -= 6;
    }
    for (int v = 0; v < DF_VOICES; ++v) {
        if (v != DF_HAT && per_voice[v] >= 13) balance -= 8;
    }
    balance = df_clamp(balance, 0, 100);

    /* 4. SYNCOPATION: off-beat tension; the closed hat only keeps time. */
    int sync_sum = 0;
    for (int v = 0; v < DF_VOICES; ++v) {
        if (v != DF_HAT) sync_sum += df_voice_syncopation(p, v);
    }
    int sync;
    if (sync_sum < 12) sync = sync_sum * 100 / 12;
    else if (sync_sum <= 30) sync = 100;
    else sync = 100 - (sync_sum - 30) * 3;
    sync = df_clamp(sync, 0, 100);

    /* 5. DYNAMICS: accents (10..35 % of the notes) and ghost notes. */
    int ratio = accents * 100 / hits;
    int dynamics;
    if (ratio < 10) dynamics = ratio * 7;
    else if (ratio <= 35) dynamics = 70;
    else dynamics = 70 - (ratio - 35) * 2;
    dynamics = df_clamp(dynamics, 0, 70);
    if (ghosts >= 1) dynamics += 15;
    if (ghosts >= 3) dynamics += 15;

    /* 6. STRUCTURE: call and response between the two halves of the bar. */
    int either = 0, same = 0;
    for (int v = 0; v < DF_VOICES; ++v) {
        for (int s = 0; s < 8; ++s) {
            int a = df_get(p, v, s) != DF_OFF;
            int b = df_get(p, v, s + 8) != DF_OFF;
            if (a || b) ++either;
            if (a && b) ++same;
        }
    }
    int similarity = either ? same * 100 / either : 0;
    int structure;
    if (similarity < 50) structure = 20 + similarity * 80 / 50;
    else if (similarity <= 85) structure = 100;
    else structure = 100 - (similarity - 85) * 40 / 15;

    out->crit[DF_CRIT_PULSE] = (uint8_t)pulse;
    out->crit[DF_CRIT_VARIETY] = (uint8_t)variety;
    out->crit[DF_CRIT_BALANCE] = (uint8_t)balance;
    out->crit[DF_CRIT_SYNCOPATION] = (uint8_t)sync;
    out->crit[DF_CRIT_DYNAMICS] = (uint8_t)dynamics;
    out->crit[DF_CRIT_STRUCTURE] = (uint8_t)structure;

    /* Weights 1.5 : 2 : 1 : 1.5 : 1 : 1 give a base of 0..800. Two factors
     * then scale the whole result: BALANCE (25..100 %), so filling every
     * step can never beat a pattern that leaves room to breathe, and the
     * size of the ensemble (9/16 for one voice up to 16/16 for all eight),
     * because a drum circle is more than a kick and a snare. */
    int base = (pulse * 3) / 2 + variety * 2 + balance + (sync * 3) / 2 + dynamics + structure;
    base = base * (100 + balance * 3) / 400;
    base = base * (voices + 8) / 16;
    out->base = (uint16_t)df_clamp(base, 0, DF_BASE_MAX);
    int total = out->base + out->bonus;
    out->total = (uint16_t)(total > DF_TOTAL_MAX ? DF_TOTAL_MAX : total);
}

/* ------------------------------------------------------------------ */
/* Special drum moves                                                  */
/* ------------------------------------------------------------------ */

/* Base groove a pattern must reach before the move unlocks. */
static inline int df_move_threshold(int move) {
    static const uint16_t thresholds[DF_MOVES] = {60, 120, 190, 260, 330, 400, 470, 540};
    return thresholds[move & 7];
}

/*
 * Motions, fighting-game style. Every motion starts with DOWN, which is
 * never a pad, so ordinary drumming cannot trigger a move by accident.
 * Each entry is: length, up to three directions, button.
 */
static inline int df_move_motion(int move, uint8_t dirs[3], int *button) {
    static const uint8_t motions[DF_MOVES][5] = {
        {2, DF_DIR_DOWN, DF_DIR_LEFT, 0, DF_BTN_A},            /* QUATTRO */
        {2, DF_DIR_DOWN, DF_DIR_UP, 0, DF_BTN_A},              /* CONTROTEMPO */
        {2, DF_DIR_DOWN, DF_DIR_RIGHT, 0, DF_BTN_A},           /* ROLLATA */
        {2, DF_DIR_DOWN, DF_DIR_RIGHT, 0, DF_BTN_B},           /* TRESILLO */
        {2, DF_DIR_DOWN, DF_DIR_LEFT, 0, DF_BTN_B},            /* TAMMURRIATA */
        {2, DF_DIR_DOWN, DF_DIR_UP, 0, DF_BTN_B},              /* ECO */
        {3, DF_DIR_DOWN, DF_DIR_UP, DF_DIR_DOWN, DF_BTN_B},    /* RITORNELLO */
        {3, DF_DIR_DOWN, DF_DIR_UP, DF_DIR_DOWN, DF_BTN_A},    /* VESUVIO */
    };
    const uint8_t *m = motions[move & 7];
    dirs[0] = m[1];
    dirs[1] = m[2];
    dirs[2] = m[3];
    *button = m[4];
    return m[0];
}

/* `recent` holds the latest direction presses, oldest first. Returns the
 * move whose motion ends the buffer, or -1. Longer motions win. */
static inline int df_move_match(const uint8_t *recent, int count, int button) {
    for (int length = 3; length >= 2; --length) {
        if (count < length) continue;
        for (int move = 0; move < DF_MOVES; ++move) {
            uint8_t dirs[3];
            int wanted;
            if (df_move_motion(move, dirs, &wanted) != length || wanted != button) continue;
            int ok = 1;
            for (int i = 0; i < length; ++i) {
                if (recent[count - length + i] != dirs[i]) ok = 0;
            }
            if (ok) return move;
        }
    }
    return -1;
}

/* Perform a move on the pattern. `selected` is the voice the player hit
 * last; ECO and RITORNELLO act on it. */
static inline void df_move_apply(df_pattern_t *p, int move, int selected) {
    selected &= 7;
    if (move == DF_MOVE_QUATTRO) {
        /* Four on the floor. */
        df_raise(p, DF_KICK, 0, DF_ACCENT);
        df_raise(p, DF_KICK, 4, DF_HIT);
        df_raise(p, DF_KICK, 8, DF_HIT);
        df_raise(p, DF_KICK, 12, DF_HIT);
    } else if (move == DF_MOVE_CONTROTEMPO) {
        /* Open hat on every off-beat eighth. */
        for (int s = 2; s < DF_STEPS; s += 4) df_raise(p, DF_OPEN, s, DF_HIT);
    } else if (move == DF_MOVE_ROLLATA) {
        /* Crescendo snare roll into the next bar. */
        df_raise(p, DF_SNARE, 12, DF_GHOST);
        df_raise(p, DF_SNARE, 13, DF_HIT);
        df_raise(p, DF_SNARE, 14, DF_HIT);
        df_raise(p, DF_SNARE, 15, DF_ACCENT);
    } else if (move == DF_MOVE_TRESILLO) {
        /* 3-3-2 bell pattern, twice per bar. */
        df_raise(p, DF_CAMPANA, 0, DF_ACCENT);
        df_raise(p, DF_CAMPANA, 3, DF_HIT);
        df_raise(p, DF_CAMPANA, 6, DF_HIT);
        df_raise(p, DF_CAMPANA, 8, DF_ACCENT);
        df_raise(p, DF_CAMPANA, 11, DF_HIT);
        df_raise(p, DF_CAMPANA, 14, DF_HIT);
    } else if (move == DF_MOVE_TAMMURRIATA) {
        /* The frame-drum figure of the Campanian tammurriata. */
        df_raise(p, DF_TAMMORRA, 0, DF_ACCENT);
        df_raise(p, DF_TAMMORRA, 3, DF_HIT);
        df_raise(p, DF_TAMMORRA, 6, DF_HIT);
        df_raise(p, DF_TAMMORRA, 7, DF_GHOST);
        df_raise(p, DF_TAMMORRA, 8, DF_ACCENT);
        df_raise(p, DF_TAMMORRA, 11, DF_HIT);
        df_raise(p, DF_TAMMORRA, 14, DF_HIT);
    } else if (move == DF_MOVE_ECO) {
        /* Every note of the selected voice echoes three steps later. */
        uint32_t source = p->row[selected];
        for (int s = 0; s < DF_STEPS; ++s) {
            if (((source >> (s * 2)) & 3u) >= DF_HIT) df_raise(p, selected, s + 3, DF_GHOST);
        }
    } else if (move == DF_MOVE_RITORNELLO) {
        /* The first half of the selected voice returns as the refrain. */
        uint32_t half = p->row[selected] & 0xffffu;
        p->row[selected] = half | (half << 16);
    } else if (move == DF_MOVE_VESUVIO) {
        /* Eruption: a full-kit fill that lands on a crash. */
        df_raise(p, DF_TAMMORRA, 12, DF_HIT);
        df_raise(p, DF_TAMMORRA, 13, DF_HIT);
        df_raise(p, DF_BONGO, 13, DF_HIT);
        df_raise(p, DF_BONGO, 14, DF_HIT);
        df_raise(p, DF_SNARE, 14, DF_HIT);
        df_raise(p, DF_SNARE, 15, DF_ACCENT);
        df_raise(p, DF_CLAP, 15, DF_ACCENT);
        df_raise(p, DF_KICK, 15, DF_HIT);
        df_raise(p, DF_KICK, 0, DF_ACCENT);
        df_raise(p, DF_OPEN, 0, DF_ACCENT);
    }
    p->moves |= (uint8_t)(1u << (move & 7));
}

/* ------------------------------------------------------------------ */
/* CPU opponent                                                        */
/* ------------------------------------------------------------------ */

/* Groove the CPU aims for: level 0 MATRICOLA, 1 FUORICORSO, 2 MAESTRO. */
static inline int df_ai_target(int level) {
    static const uint16_t targets[DF_AI_LEVELS] = {300, 560, 820};
    return targets[level < 0 ? 0 : (level >= DF_AI_LEVELS ? DF_AI_LEVELS - 1 : level)];
}

/*
 * The CPU plays by the same rules as a human: it lays down a foundation,
 * performs the moves its level knows, then improves the pattern by trial
 * and error against the same judge (hill climbing). It stops as soon as it
 * reaches the target of its level, so weaker opponents stay beatable.
 */
static inline void df_ai_compose(df_pattern_t *p, int level, uint32_t *rng) {
    df_score_t score;
    if (level < 0) level = 0;
    if (level >= DF_AI_LEVELS) level = DF_AI_LEVELS - 1;
    df_clear(p);

    df_set(p, DF_KICK, 0, DF_HIT);
    df_set(p, DF_SNARE, 4, DF_HIT);
    df_set(p, DF_SNARE, 12, DF_HIT);
    for (int s = 0; s < DF_STEPS; s += (level == 0 ? 4 : 2)) df_set(p, DF_HAT, s, DF_HIT);

    df_move_apply(p, DF_MOVE_QUATTRO, DF_KICK);
    if (level >= 1) {
        df_move_apply(p, DF_MOVE_CONTROTEMPO, DF_OPEN);
        df_move_apply(p, DF_MOVE_ROLLATA, DF_SNARE);
    }
    if (level >= 2) {
        df_move_apply(p, DF_MOVE_TRESILLO, DF_CAMPANA);
        df_move_apply(p, DF_MOVE_TAMMURRIATA, DF_TAMMORRA);
        df_move_apply(p, DF_MOVE_ECO, DF_SNARE);
        if (df_rand_below(rng, 2)) df_move_apply(p, DF_MOVE_VESUVIO, DF_KICK);
    }

    int target = df_ai_target(level) + df_rand_below(rng, 61) - 30;
    int iterations = level == 0 ? 200 : (level == 1 ? 400 : 500);
    df_judge(p, &score);
    int best = score.total;
    for (int i = 0; i < iterations; ++i) {
        if (best >= target && best <= target + 40) break;
        int v = df_rand_below(rng, DF_VOICES);
        int s = df_rand_below(rng, DF_STEPS);
        int roll = df_rand_below(rng, 20);
        int level_new = roll < 9 ? DF_OFF : (roll < 16 ? DF_HIT : (roll < 19 ? DF_ACCENT : DF_GHOST));
        int level_old = df_get(p, v, s);
        if (level_new == level_old) continue;
        df_set(p, v, s, level_new);
        df_judge(p, &score);
        int keep;
        if (best < target) keep = score.total > best && score.total <= target + 40;
        else keep = score.total < best && score.total >= target - 20; /* too good: play worse */
        if (keep) best = score.total;
        else df_set(p, v, s, level_old);
    }
}

/* ------------------------------------------------------------------ */
/* Network battle: snapshot packing                                    */
/* ------------------------------------------------------------------ */

/*
 * The PRG32 multiplayer service relays one small snapshot per player:
 * x, y, sprite and flags (16 bits each). There are no reliable messages,
 * so every board keeps repeating its state, one voice row per snapshot:
 *
 *   x      steps 0..7 of the row   (2 bits per step)
 *   y      steps 8..15 of the row
 *   sprite bits 0-2 voice, 3-4 phase, 5-7 round, 8 ready
 *   flags  bits 0-7 performed moves, 8-15 hash of the whole pattern
 *
 * A receiver rebuilds the pattern row by row and trusts it only when the
 * hash of what it assembled equals the advertised hash.
 */
#define DF_NET_LOBBY 0
#define DF_NET_COMPOSE 1
#define DF_NET_DONE 2
#define DF_NET_RESULT 3

typedef struct {
    uint8_t voice;
    uint8_t phase;
    uint8_t round;
    uint8_t ready;
} df_net_head_t;

static inline unsigned df_pattern_hash(const df_pattern_t *p) {
    uint32_t h = 2166136261u;
    for (int v = 0; v < DF_VOICES; ++v) {
        for (int b = 0; b < 4; ++b) {
            h ^= (p->row[v] >> (b * 8)) & 0xffu;
            h *= 16777619u;
        }
    }
    h ^= p->moves;
    h *= 16777619u;
    return (unsigned)((h ^ (h >> 8) ^ (h >> 16) ^ (h >> 24)) & 0xffu);
}

static inline void df_net_pack(const df_pattern_t *p, const df_net_head_t *head,
                               int16_t *x, int16_t *y, uint16_t *sprite, uint16_t *flags) {
    uint32_t row = p->row[head->voice & 7];
    *x = (int16_t)(uint16_t)(row & 0xffffu);
    *y = (int16_t)(uint16_t)(row >> 16);
    *sprite = (uint16_t)((head->voice & 7u) | ((head->phase & 3u) << 3) |
                         ((head->round & 7u) << 5) | ((head->ready & 1u) << 8));
    *flags = (uint16_t)(p->moves | (df_pattern_hash(p) << 8));
}

typedef struct {
    uint32_t id;
    df_pattern_t pattern;
    uint8_t present;  /* seen in the current peer list */
    uint8_t phase;
    uint8_t round;
    uint8_t ready;
    uint8_t hash;     /* hash advertised by the peer */
    uint8_t got;      /* rows received since the hash last changed */
    uint8_t complete; /* pattern assembled and verified */
} df_peer_t;

static inline void df_peer_reset(df_peer_t *peer, uint32_t id) {
    peer->id = id;
    df_clear(&peer->pattern);
    peer->present = 0;
    peer->phase = DF_NET_LOBBY;
    peer->round = 0;
    peer->ready = 0;
    peer->hash = 0;
    peer->got = 0;
    peer->complete = 0;
}

static inline void df_peer_feed(df_peer_t *peer, int16_t x, int16_t y,
                                uint16_t sprite, uint16_t flags) {
    int voice = sprite & 7;
    unsigned hash = (flags >> 8) & 0xffu;
    peer->phase = (uint8_t)((sprite >> 3) & 3u);
    peer->round = (uint8_t)((sprite >> 5) & 7u);
    peer->ready = (uint8_t)((sprite >> 8) & 1u);
    if (hash != peer->hash) {
        peer->hash = (uint8_t)hash;
        peer->got = 0;
        peer->complete = 0;
    }
    peer->pattern.row[voice] = (uint32_t)(uint16_t)x | ((uint32_t)(uint16_t)y << 16);
    peer->pattern.moves = (uint8_t)(flags & 0xffu);
    peer->got |= (uint8_t)(1u << voice);
    peer->complete = peer->got == 0xffu && df_pattern_hash(&peer->pattern) == hash;
}

/* A peer has delivered its pattern for `round` once it advertises DONE or
 * RESULT for that round (or has already moved on) and the pattern checks. */
static inline int df_peer_done(const df_peer_t *peer, int round) {
    if (!peer->complete) return 0;
    if (peer->round == round) return peer->phase >= DF_NET_DONE;
    return ((peer->round - round) & 7) == 1; /* already in the next round */
}

/* A peer is ready to start `round` when it waits in that lobby with its
 * ready flag up, or when it has already started composing that round. */
static inline int df_peer_ready(const df_peer_t *peer, int round) {
    if (peer->round != round) return 0;
    return peer->phase != DF_NET_LOBBY || peer->ready;
}

/* ------------------------------------------------------------------ */
/* Ranking                                                             */
/* ------------------------------------------------------------------ */

/* Round points: one point for every contestant with a strictly lower
 * groove. Ties earn the same points; the result does not depend on the
 * order in which a board lists the contestants. */
static inline int df_round_points(const uint16_t *totals, int count, int who) {
    int points = 0;
    for (int i = 0; i < count; ++i) {
        if (i != who && totals[i] < totals[who]) ++points;
    }
    return points;
}

/* Fill `order` with contestant indices, best key first (stable). */
static inline void df_rank(const uint16_t *keys, int count, uint8_t *order) {
    for (int i = 0; i < count; ++i) order[i] = (uint8_t)i;
    for (int i = 1; i < count; ++i) {
        uint8_t moving = order[i];
        int j = i - 1;
        while (j >= 0 && keys[order[j]] < keys[moving]) {
            order[j + 1] = order[j];
            --j;
        }
        order[j + 1] = moving;
    }
}

#endif
