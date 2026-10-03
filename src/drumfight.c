/*
 * Drumfight Napoli 97 - a PRG32 cartridge.
 *
 * "During the magical nights of 1997, in the heart of downtown at Piazza
 * San Domenico, university students by day became percussion warriors by
 * night... The rest of the story? Pure legend!"
 *
 * The game is a drum machine: an eight-voice, sixteen-step loop on the
 * PRG32 eight-voice stereo mixer, with a kit of analogue-style drum
 * one-shots (or, as an alternative, the SID-like synthesizer), a clock
 * locked to the frame so every step is even, swing, accents and a choking
 * hi-hat. Players record a bar live with the joystick and the A/B buttons,
 * unlock special drum moves with joystick motions, and a judge scores the
 * groove. Modes: practice, one CPU opponent, pass-the-pad for 2-4 players,
 * and a network battle for up to four boards.
 *
 * This file is the whole cartridge: state machine, sequencer, input, audio
 * and drawing. The rules live in df_core.h, which has no PRG32 calls and is
 * unit-tested on the host. See docs/architecture.md.
 *
 * Portable-cartridge rules followed here (docs/reproduce.md explains how
 * they are checked): no pointers in initialised static data, no libc, no
 * floating point, no 64-bit division.
 */
#include "prg32.h"

#include "df_core.h"

/* ------------------------------------------------------------------ */
/* Host features                                                       */
/* ------------------------------------------------------------------ */

/* The builder's portable stubs store the firmware ABI table pointer in
 * __prg32_abi on every entry. Optional services (multiplayer) are used
 * only when the running host advertises them. */
#ifdef DF_HOST
uint32_t df_host_features(void);
#else
extern const prg32_abi_table_t *__prg32_abi;
static inline uint32_t df_host_features(void) {
    return __prg32_abi ? __prg32_abi->provided_features : 0u;
}

/* The cartridge is linked with -nostdlib; the compiler may still emit
 * calls to these two for structure copies and clears. */
void *memset(void *dst, int value, size_t n) __attribute__((optimize("no-tree-loop-distribute-patterns")));
void *memcpy(void *dst, const void *src, size_t n) __attribute__((optimize("no-tree-loop-distribute-patterns")));
void *memset(void *dst, int value, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    while (n--) *d++ = (unsigned char)value;
    return dst;
}
void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) *d++ = *s++;
    return dst;
}
#endif

/* ------------------------------------------------------------------ */
/* Constants                                                           */
/* ------------------------------------------------------------------ */

#define RGB(r, g, b) ((uint16_t)((((r) & 0xf8) << 8) | (((g) & 0xfc) << 3) | ((b) >> 3)))

#define C_BG RGB(8, 10, 28)          /* the night over the piazza */
#define C_PANEL RGB(20, 24, 56)
#define C_CELL RGB(30, 36, 76)
#define C_CELL_BEAT RGB(46, 54, 104)
#define C_HEAD RGB(110, 118, 176)
#define C_TEXT RGB(236, 236, 220)
#define C_DIM RGB(120, 128, 168)
#define C_GOLD RGB(255, 204, 40)
#define C_AZZURRO RGB(40, 160, 240)
#define C_RED RGB(232, 60, 60)
#define C_GREEN RGB(80, 220, 110)
#define C_WHITE RGB(255, 255, 255)

#define GRID_X 68
#define GRID_Y 22
#define CELL_W 13
#define CELL_H 11
#define LEGEND_X 280
#define METER_X 304
#define SCORE_Y 114
#define CRIT_Y 126
#define BANNER_Y 138
#define MOVES_Y 150
#define HELP_Y 192

#define FRAME_MS 33          /* the firmware calls update and draw every 33 ms */
#define ROUNDS 3
#define COMPOSE_BARS 16
#define SHOW_BARS 2
#define MAX_CONTESTANTS 4
#define MAX_PEERS 3
#define MOTION_GAP_MS 450u
#define ACCENT_HOLD_MS 180u
#define NET_WAIT_MS 15000u
#define NET_SIGNATURE "drumfight-napoli97-v1"
#define SCORE_GAME "drumfight"

enum { ST_TITLE, ST_SETUP, ST_READY, ST_COMPOSE, ST_NETLOBBY, ST_NETWAIT, ST_SHOW, ST_RESULT, ST_FINAL };
enum { MODE_PRACTICE, MODE_CPU, MODE_PIAZZA, MODE_NET, MODE_COUNT };
enum { KIND_HUMAN, KIND_CPU, KIND_NET };

/* Redraw requests. The display driver sends only the bounding box of what
 * changed, so every screen redraws the smallest region it can. */
#define DR_ALL (1u << 0)
#define DR_STATUS (1u << 1)
#define DR_SCORE (1u << 2)
#define DR_MOVES (1u << 3)
#define DR_BANNER (1u << 4)
#define DR_LABELS (1u << 5)
#define DR_MENU (1u << 6)
#define DR_HEAD (1u << 7)
#define DR_PADS (1u << 8)
#define DR_COUNT (1u << 9) /* only the numbers of the score line */

/* Text tables: fixed-size character arrays, never pointers. Only the
 * characters every PRG32 host can draw are used: A-Z 0-9 ?!.,:-+/ */
static const char VOICE_NAMES[DF_VOICES][9] = {
    "KICK", "SNARE", "HAT", "OPEN HAT", "TAMMORRA", "BONGO", "CAMPANA", "CLAP",
};
static const char MOVE_NAMES[DF_MOVES][12] = {
    "QUATTRO", "CONTROTEMPO", "ROLLATA", "TRESILLO", "TAMMURRIATA", "ECO", "RITORNELLO", "VESUVIO",
};
static const char CRIT_NAMES[DF_CRITERIA][4] = {"PUL", "VAR", "BAL", "SYN", "DYN", "STR"};
static const char CPU_NAMES[DF_AI_LEVELS][11] = {"MATRICOLA", "FUORICORSO", "MAESTRO"};
static const char MODE_NAMES[MODE_COUNT][20] = {
    "PRACTICE", "VS CPU", "PIAZZA 2-4 PLAYERS", "NETWORK BATTLE",
};
static const uint16_t VOICE_COLORS[DF_VOICES] = {
    RGB(236, 64, 64),  RGB(255, 150, 40), RGB(250, 226, 60), RGB(170, 232, 70),
    RGB(60, 210, 130), RGB(70, 220, 230), RGB(70, 150, 255), RGB(236, 110, 220),
};
/* Two kits (audio/audio.json): ANALOG, PCM one-shots on instruments 0..7,
 * and SID, procedural instruments 8..15. A PCM voice plays at its natural
 * speed on note 60. For the SID kit the note is the pitch of a tonal voice
 * or the clock of a noise voice. */
#define KIT_ANALOG 0
#define KIT_SID 1
#define PCM_NOTE 60
static const char KIT_NAMES[2][7] = {"ANALOG", "SID"};
static const uint8_t SID_NOTES[DF_VOICES] = {36, 106, 127, 124, 45, 64, 81, 112};
static const uint8_t VOICE_GAIN[2][DF_VOICES] = {
    {100, 96, 78, 72, 92, 82, 76, 90},
    {100, 92, 74, 70, 96, 86, 72, 88},
};
static const uint8_t LEVEL_VOLUME[4] = {0, 112, 188, 255};
/* Tempo is a whole number of frames per sixteenth, so every step lands on
 * a frame and the groove is perfectly even: 6, 5, 4, 3 frames are 76, 91,
 * 114 and 152 BPM. Swing delays every off sixteenth by one frame. */
static const uint8_t ROUND_FRAMES[ROUNDS] = {5, 4, 4};
static const uint8_t ROUND_SWING[ROUNDS] = {0, 0, 1};
static const uint8_t PRACTICE_FRAMES[4] = {6, 5, 4, 3};

/* 5x7 capitals and digits for the big titles (the glyph shapes are those
 * of the PRG32-QT host font, MIT, PRG32 contributors). Digits, then A-Z;
 * bit 4 is the leftmost pixel. */
static const uint8_t BIG_FONT[36][7] = {
    {14, 17, 19, 21, 25, 17, 14}, {4, 12, 4, 4, 4, 4, 14},      {14, 17, 1, 2, 4, 8, 31},
    {30, 1, 1, 14, 1, 1, 30},     {2, 6, 10, 18, 31, 2, 2},     {31, 16, 30, 1, 1, 17, 14},
    {6, 8, 16, 30, 17, 17, 14},   {31, 1, 2, 4, 8, 8, 8},       {14, 17, 17, 14, 17, 17, 14},
    {14, 17, 17, 15, 1, 2, 12},   {14, 17, 17, 31, 17, 17, 17}, {30, 17, 17, 30, 17, 17, 30},
    {14, 17, 16, 16, 16, 17, 14}, {28, 18, 17, 17, 17, 18, 28}, {31, 16, 16, 30, 16, 16, 31},
    {31, 16, 16, 30, 16, 16, 16}, {14, 17, 16, 23, 17, 17, 15}, {17, 17, 17, 31, 17, 17, 17},
    {14, 4, 4, 4, 4, 4, 14},      {7, 2, 2, 2, 2, 18, 12},      {17, 18, 20, 24, 20, 18, 17},
    {16, 16, 16, 16, 16, 16, 31}, {17, 27, 21, 21, 17, 17, 17}, {17, 25, 21, 19, 17, 17, 17},
    {14, 17, 17, 17, 17, 17, 14}, {30, 17, 17, 30, 16, 16, 16}, {14, 17, 17, 17, 21, 18, 13},
    {30, 17, 17, 30, 20, 18, 17}, {15, 16, 16, 14, 1, 1, 30},   {31, 4, 4, 4, 4, 4, 4},
    {17, 17, 17, 17, 17, 17, 14}, {17, 17, 17, 17, 17, 10, 4},  {17, 17, 17, 21, 21, 21, 10},
    {17, 17, 10, 4, 10, 17, 17},  {17, 17, 10, 4, 4, 4, 4},     {31, 1, 2, 4, 8, 16, 31},
};
/* 7x7 arrows for the pad legend and the motions: up, down, left, right;
 * bit 6 is the leftmost pixel. */
static const uint8_t ARROWS[4][7] = {
    {0x08, 0x1c, 0x3e, 0x08, 0x08, 0x08, 0x08},
    {0x08, 0x08, 0x08, 0x08, 0x3e, 0x1c, 0x08},
    {0x00, 0x08, 0x18, 0x3f, 0x18, 0x08, 0x00},
    {0x00, 0x08, 0x0c, 0x7e, 0x0c, 0x08, 0x00},
};

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t kind;
    uint8_t peer;   /* index in peers[] for KIND_NET */
    uint8_t level;  /* CPU level */
    uint8_t points; /* match points so far */
    uint8_t gained; /* points won in the last round */
    uint16_t best;  /* best groove of the match */
    df_pattern_t pattern;
    df_score_t score;
    char name[11];
} contestant_t;

typedef struct {
    uint8_t frames;          /* frames per sixteenth: the tempo */
    uint8_t swing;           /* off sixteenths are one frame late */
    int32_t acc;             /* ms since the last step */
    uint8_t step;            /* step that fired last */
    uint8_t running;
    int16_t bar;             /* bar being played, 0-based */
    uint8_t skip[DF_VOICES]; /* step + 1 already heard through a live pad */
} seq_t;

typedef struct {
    uint8_t delay; /* frames */
    uint8_t voice;
    uint8_t note;
    uint8_t volume;
} jingle_t;

static struct {
    uint8_t state, mode;
    uint16_t draw;
    uint32_t now, last_ms, frame;
    uint32_t in, prev;
    uint32_t rng;
    uint32_t features;

    /* menus */
    uint8_t title_item, setup_item, menu_open, menu_item;
    uint8_t cpu_level, piazza_players, practice_tempo, practice_swing;
    uint8_t kit;              /* KIT_ANALOG or KIT_SID */

    /* match */
    contestant_t who[MAX_CONTESTANTS];
    uint8_t count, round, turn, show;
    uint8_t order[MAX_CONTESTANTS];
    uint16_t practice_best;

    /* composing */
    seq_t seq;
    const df_pattern_t *view; /* pattern on screen and in the sequencer */
    const df_score_t *view_score;
    df_pattern_t demo;        /* title-screen groove */
    df_score_t demo_score;
    uint8_t sel, unlocked, soft;
    uint8_t erasing;          /* DOWN + A is held to erase */
    uint8_t hold_voice, hold_step; /* the pad hit that may become an accent */
    uint32_t hold_button, hold_ms; /* hold_button 0 = none pending */
    uint8_t judge_dirty;
    uint16_t cell_dirty[DF_VOICES];
    uint8_t head_shown;       /* column drawn as playhead, 0xff = none */
    uint8_t flash[DF_VOICES];
    uint16_t shown_total;     /* count-up in the showcase */
    uint8_t meter_shown;      /* height of the groove meter on screen */
    uint32_t show_start;

    /* joystick motions */
    uint8_t motion[3], motion_count, dir_held;
    uint32_t motion_ms;

    /* banner */
    char banner[41];
    uint16_t banner_color;
    uint8_t banner_frames;

    jingle_t jingles[6];

    /* network */
    df_peer_t peers[MAX_PEERS];
    uint8_t net_ok, net_ready, net_voice, net_hold;
    uint32_t wait_start;
} g;

/* ------------------------------------------------------------------ */
/* Small text helpers (no libc)                                        */
/* ------------------------------------------------------------------ */

static int str_len(const char *s) {
    int n = 0;
    while (s[n]) ++n;
    return n;
}

static char *put_str(char *dst, const char *src) {
    while (*src) *dst++ = *src++;
    *dst = 0;
    return dst;
}

/* Decimal, right aligned in `width` characters, padded with `pad`. */
static char *put_num(char *dst, uint32_t value, int width, char pad) {
    char tmp[10];
    int n = 0;
    do {
        tmp[n++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value && n < 10);
    for (int i = n; i < width; ++i) *dst++ = pad;
    while (n) *dst++ = tmp[--n];
    *dst = 0;
    return dst;
}

static void text(int x, int y, const char *s, uint16_t fg, uint16_t bg) {
    prg32_gfx_text8(x, y, s, fg, bg);
}

static void text_center(int y, const char *s, uint16_t fg, uint16_t bg) {
    text((PRG32_GAME_W - str_len(s) * 8) / 2, y, s, fg, bg);
}

static void big_text(int x, int y, const char *s, int scale, uint16_t color) {
    for (; *s; ++s, x += 6 * scale) {
        int glyph;
        if (*s >= '0' && *s <= '9') glyph = *s - '0';
        else if (*s >= 'A' && *s <= 'Z') glyph = *s - 'A' + 10;
        else continue;
        for (int row = 0; row < 7; ++row) {
            unsigned bits = BIG_FONT[glyph][row];
            for (int col = 0; col < 5; ++col) {
                if (bits & (0x10u >> col)) prg32_gfx_rect(x + col * scale, y + row * scale, scale, scale, color);
            }
        }
    }
}

static void big_center(int y, const char *s, int scale, uint16_t color) {
    big_text((PRG32_GAME_W - (str_len(s) * 6 - 1) * scale) / 2, y, s, scale, color);
}

static void arrow(int x, int y, int dir, uint16_t fg, uint16_t bg) {
    prg32_gfx_rect(x, y, 8, 8, bg);
    if (dir < DF_DIR_UP || dir > DF_DIR_RIGHT) return;
    for (int row = 0; row < 7; ++row) {
        unsigned bits = ARROWS[dir - 1][row];
        for (int col = 0; col < 7; ++col) {
            if (bits & (0x40u >> col)) prg32_gfx_pixel(x + col, y + row, fg);
        }
    }
}

/* The pad that plays a voice: its direction arrow and its button. */
static void pad_legend(int x, int y, int voice, uint16_t fg, uint16_t bg) {
    arrow(x, y, df_voice_dir(voice), fg, bg);
    text(x + 8, y, df_voice_button(voice) == DF_BTN_A ? "A" : "B", fg, bg);
}

/* ------------------------------------------------------------------ */
/* Audio                                                               */
/* ------------------------------------------------------------------ */

/* Voice v plays on mixer channel v, with the instrument of the selected
 * kit, so all eight voices can sound together and a new hit cuts only its
 * own voice. Stereo positions are the instrument defaults in
 * audio/audio.json. See docs/audio.md. */
static void drum(int voice, int level) {
    unsigned volume = (unsigned)LEVEL_VOLUME[level & 3] * VOICE_GAIN[g.kit][voice] / 100u;
    if (g.soft) volume = volume * 2u / 3u;
    if (volume == 0) return;
    /* A closed hat chokes a ringing open hat, as on every drum machine. */
    if (voice == DF_HAT) prg32_audio_stop_channel(DF_OPEN);
    prg32_audio_note_on_pan((uint8_t)voice, (uint8_t)(voice + g.kit * DF_VOICES),
                            g.kit == KIT_SID ? SID_NOTES[voice] : PCM_NOTE, (uint8_t)volume,
                            PRG32_AUDIO_PAN_CENTER);
    g.flash[voice] = 3;
}

/* Interface sounds are short tunes on the ANALOG kit; the note transposes
 * the sample (60 is its natural pitch). */
static void jingle(int delay, int voice, int note, int volume) {
    for (unsigned i = 0; i < sizeof(g.jingles) / sizeof(g.jingles[0]); ++i) {
        if (g.jingles[i].volume == 0) {
            g.jingles[i].delay = (uint8_t)delay;
            g.jingles[i].voice = (uint8_t)voice;
            g.jingles[i].note = (uint8_t)note;
            g.jingles[i].volume = (uint8_t)volume;
            return;
        }
    }
}

static void jingles_tick(void) {
    for (unsigned i = 0; i < sizeof(g.jingles) / sizeof(g.jingles[0]); ++i) {
        jingle_t *j = &g.jingles[i];
        if (j->volume == 0) continue;
        if (j->delay) {
            --j->delay;
            continue;
        }
        prg32_audio_note_on_pan(j->voice, j->voice, j->note, j->volume, PRG32_AUDIO_PAN_CENTER);
        j->volume = 0;
    }
}

static void sfx_move(void) { jingle(0, DF_CAMPANA, 72, 120); }
static void sfx_select(void) {
    jingle(0, DF_CAMPANA, 60, 150);
    jingle(2, DF_CAMPANA, 67, 150);
}
static void sfx_back(void) { jingle(0, DF_BONGO, 53, 170); }
static void sfx_unlock(void) {
    jingle(0, DF_CAMPANA, 60, 170);
    jingle(2, DF_CAMPANA, 64, 170);
    jingle(4, DF_CAMPANA, 67, 190);
}
static void sfx_special(void) {
    jingle(0, DF_BONGO, 60, 220);
    jingle(2, DF_BONGO, 65, 220);
    jingle(4, DF_BONGO, 72, 240);
}
static void sfx_locked(void) { jingle(0, DF_TAMMORRA, 53, 170); }
static void sfx_fanfare(void) {
    jingle(0, DF_CAMPANA, 60, 200);
    jingle(3, DF_CAMPANA, 64, 200);
    jingle(6, DF_CAMPANA, 67, 200);
    jingle(9, DF_CAMPANA, 72, 220);
    jingle(9, DF_OPEN, PCM_NOTE, 200);
    jingle(9, DF_KICK, PCM_NOTE, 250);
}

/* ------------------------------------------------------------------ */
/* Sequencer                                                           */
/* ------------------------------------------------------------------ */

static int pattern_empty(const df_pattern_t *p) {
    uint32_t any = 0;
    for (int v = 0; v < DF_VOICES; ++v) any |= p->row[v];
    return any == 0;
}

/* Milliseconds from the step that fired last to the next one. With swing
 * the gap after an on sixteenth is one frame longer and the gap after an
 * off sixteenth one frame shorter: the pair keeps its length. */
static int32_t seq_gap(void) {
    int32_t gap = g.seq.frames * FRAME_MS;
    if (g.seq.swing) gap += (g.seq.step & 1) ? -FRAME_MS : FRAME_MS;
    return gap;
}

/* The tempo a number of frames per sixteenth amounts to, rounded. */
static int frames_bpm(int frames) {
    return (15000 + frames * FRAME_MS / 2) / (frames * FRAME_MS);
}

static int seq_bpm(void) { return frames_bpm(g.seq.frames); }

static void seq_start(const df_pattern_t *pattern, const df_score_t *score, int frames, int swing) {
    g.view = pattern;
    g.view_score = score;
    g.seq.frames = (uint8_t)frames;
    g.seq.swing = (uint8_t)swing;
    g.seq.step = DF_STEPS - 1;
    g.seq.acc = seq_gap() - FRAME_MS; /* the first step fires on the next update */
    g.seq.bar = -1;
    g.seq.running = 1;
    g.head_shown = 0xff;
    for (int v = 0; v < DF_VOICES; ++v) g.seq.skip[v] = 0;
}

static void seq_stop(void) { g.seq.running = 0; }

static void seq_fire(void) {
    int step = g.seq.step;
    for (int v = 0; v < DF_VOICES; ++v) {
        if (g.seq.skip[v] == step + 1) {
            g.seq.skip[v] = 0; /* the player already made this sound */
            continue;
        }
        int level = df_get(g.view, v, step);
        if (level) drum(v, level);
    }
    /* A quiet metronome keeps the tempo audible over an empty bar. */
    if (g.state == ST_COMPOSE && (step & 3) == 0 && pattern_empty(g.view)) {
        prg32_audio_note_on_pan(DF_CAMPANA, DF_CAMPANA, step == 0 ? 72 : 67, 96, PRG32_AUDIO_PAN_CENTER);
    }
    g.draw |= DR_HEAD | DR_PADS;
}

/* Advance by `dt` ms. A step fires on the frame nearest to its time. As a
 * gap is a whole number of frames, that is the same frame count every
 * time: no step is ever a frame early or late against its neighbours. The
 * clock is still the millisecond clock, so a slow frame cannot slow the
 * tempo down. */
static void seq_tick(uint32_t dt) {
    if (!g.seq.running) return;
    g.seq.acc += (int32_t)dt;
    for (int guard = 0; guard < 2 && g.seq.acc + FRAME_MS / 2 >= seq_gap(); ++guard) {
        g.seq.acc -= seq_gap();
        g.seq.step = (uint8_t)((g.seq.step + 1) & 15);
        if (g.seq.step == 0) {
            ++g.seq.bar;
            g.draw |= DR_STATUS;
        }
        seq_fire();
    }
    if (g.seq.acc > 2 * seq_gap()) g.seq.acc = 0; /* after a long stall */
}

/* ------------------------------------------------------------------ */
/* Banner                                                              */
/* ------------------------------------------------------------------ */

static void banner(const char *message, uint16_t color) {
    int n = 0;
    while (message[n] && n < 40) {
        g.banner[n] = message[n];
        ++n;
    }
    g.banner[n] = 0;
    g.banner_color = color;
    g.banner_frames = 60;
    g.draw |= DR_BANNER;
}

/* ------------------------------------------------------------------ */
/* State changes                                                       */
/* ------------------------------------------------------------------ */

static void set_state(int state) {
    g.state = (uint8_t)state;
    g.draw = DR_ALL;
    g.menu_open = 0;
    g.banner_frames = 0;
    g.banner[0] = 0;
}

static void name_set(contestant_t *c, const char *name) {
    int n = 0;
    while (name[n] && n < 10) {
        c->name[n] = name[n];
        ++n;
    }
    c->name[n] = 0;
}

static void contestant_reset(contestant_t *c, int kind) {
    c->kind = (uint8_t)kind;
    c->peer = 0;
    c->level = 0;
    c->points = 0;
    c->gained = 0;
    c->best = 0;
    df_clear(&c->pattern);
    df_judge(&c->pattern, &c->score);
    c->name[0] = 0;
}

static void enter_title(void) {
    g.soft = 1;
    seq_start(&g.demo, &g.demo_score, 4, 1);
    set_state(ST_TITLE);
    prg32_band_set_game_info("DRUMFIGHT NAPOLI 97");
}

static contestant_t *composer(void) { return &g.who[g.turn]; }

static void compose_begin(void) {
    contestant_t *c = composer();
    df_clear(&c->pattern);
    df_judge(&c->pattern, &c->score);
    g.sel = DF_KICK;
    g.unlocked = 0;
    g.judge_dirty = 0;
    g.motion_count = 0;
    g.dir_held = DF_DIR_NONE;
    g.erasing = 0;
    g.hold_button = 0;
    g.soft = 0;
    if (g.mode == MODE_PRACTICE) {
        seq_start(&c->pattern, &c->score, PRACTICE_FRAMES[g.practice_tempo], g.practice_swing);
    } else {
        seq_start(&c->pattern, &c->score, ROUND_FRAMES[g.round], ROUND_SWING[g.round]);
    }
    set_state(ST_COMPOSE);
}

/* The next local human who still has to compose this round, or -1. */
static int next_human(int from) {
    for (int i = from; i < g.count; ++i) {
        if (g.who[i].kind == KIND_HUMAN) return i;
    }
    return -1;
}

static void round_begin(void) {
    seq_stop();
    g.turn = (uint8_t)next_human(0);
    if (g.mode == MODE_NET) compose_begin(); /* every board starts together */
    else set_state(ST_READY);
}

static void show_begin(int index) {
    g.show = (uint8_t)index;
    contestant_t *c = &g.who[index];
    df_judge(&c->pattern, &c->score);
    if (c->score.total > c->best) c->best = c->score.total;
    g.soft = 0;
    g.shown_total = 0;
    g.show_start = g.now;
    seq_start(&c->pattern, &c->score, ROUND_FRAMES[g.round], ROUND_SWING[g.round]);
    set_state(ST_SHOW);
}

static void result_begin(void) {
    uint16_t totals[MAX_CONTESTANTS];
    seq_stop();
    for (int i = 0; i < g.count; ++i) totals[i] = g.who[i].score.total;
    for (int i = 0; i < g.count; ++i) {
        g.who[i].gained = (uint8_t)df_round_points(totals, g.count, i);
        g.who[i].points = (uint8_t)(g.who[i].points + g.who[i].gained);
    }
    df_rank(totals, g.count, g.order);
    set_state(ST_RESULT);
    sfx_select();
}

static void final_begin(void) {
    /* Match ranking: points first, best groove of the match breaks ties. */
    uint16_t keys[MAX_CONTESTANTS];
    uint16_t best = 0;
    for (int i = 0; i < g.count; ++i) {
        keys[i] = (uint16_t)(g.who[i].points * 1000u + g.who[i].best);
        if (g.who[i].kind == KIND_HUMAN && g.who[i].best > best) best = g.who[i].best;
    }
    df_rank(keys, g.count, g.order);
    if (best) prg32_score_submit_current_player(SCORE_GAME, best);
    set_state(ST_FINAL);
    sfx_fanfare();
}

/* All local players have composed: bring in the CPU and the network. */
static void compose_finished(void) {
    seq_stop();
    int next = next_human(g.turn + 1);
    if (next >= 0) {
        g.turn = (uint8_t)next;
        set_state(ST_READY);
        return;
    }
    for (int i = 0; i < g.count; ++i) {
        if (g.who[i].kind == KIND_CPU) df_ai_compose(&g.who[i].pattern, g.who[i].level, &g.rng);
    }
    if (g.mode == MODE_NET) {
        g.wait_start = g.now;
        set_state(ST_NETWAIT);
    } else {
        show_begin(0);
    }
}

static void match_begin(int mode) {
    g.mode = (uint8_t)mode;
    g.round = 0;
    if (mode == MODE_PRACTICE) {
        g.count = 1;
        contestant_reset(&g.who[0], KIND_HUMAN);
        name_set(&g.who[0], "YOU");
        g.turn = 0;
        g.practice_best = 0;
        compose_begin();
        return;
    }
    if (mode == MODE_CPU) {
        g.count = 2;
        contestant_reset(&g.who[0], KIND_HUMAN);
        name_set(&g.who[0], "YOU");
        contestant_reset(&g.who[1], KIND_CPU);
        g.who[1].level = g.cpu_level;
        name_set(&g.who[1], CPU_NAMES[g.cpu_level]);
    } else if (mode == MODE_PIAZZA) {
        g.count = g.piazza_players;
        for (int i = 0; i < g.count; ++i) {
            char name[11];
            char *end = put_str(name, "PLAYER ");
            put_num(end, (uint32_t)(i + 1), 1, ' ');
            contestant_reset(&g.who[i], KIND_HUMAN);
            name_set(&g.who[i], name);
        }
    }
    round_begin();
}

/* ------------------------------------------------------------------ */
/* Network battle                                                      */
/* ------------------------------------------------------------------ */

static int net_phase(void) {
    if (g.state == ST_COMPOSE) return DF_NET_COMPOSE;
    if (g.state == ST_NETWAIT) return DF_NET_DONE;
    if (g.state == ST_SHOW || g.state == ST_RESULT || g.state == ST_FINAL) return DF_NET_RESULT;
    return DF_NET_LOBBY;
}

static df_peer_t *net_peer(uint32_t id) {
    for (int i = 0; i < MAX_PEERS; ++i) {
        if (g.peers[i].id == id && id != 0) return &g.peers[i];
    }
    for (int i = 0; i < MAX_PEERS; ++i) {
        if (g.peers[i].id == 0) {
            df_peer_reset(&g.peers[i], id);
            return &g.peers[i];
        }
    }
    return 0; /* a fifth board: the battle is for four */
}

/* Publish the local state and collect the peers. Each voice row is held
 * for three frames (about 100 ms) so the relay, which samples every 50 ms,
 * sees every row; the full pattern repeats in under a second. */
static void net_tick(void) {
    if (!g.net_ok) return;
    prg32_multiplayer_tick();
    if (++g.net_hold >= 3) {
        g.net_hold = 0;
        g.net_voice = (uint8_t)((g.net_voice + 1) & 7);
    }
    df_net_head_t head;
    head.voice = g.net_voice;
    head.phase = (uint8_t)net_phase();
    head.round = g.round;
    head.ready = g.net_ready;
    int16_t x, y;
    uint16_t sprite, flags;
    df_net_pack(&g.who[0].pattern, &head, &x, &y, &sprite, &flags);
    prg32_multiplayer_set_input(g.in & 0x7fu);
    prg32_multiplayer_set_local_state(x, y, sprite, flags);

    for (int i = 0; i < MAX_PEERS; ++i) g.peers[i].present = 0;
    int count = prg32_multiplayer_get_peer_count();
    for (int i = 0; i < count; ++i) {
        prg32_player_state_t state;
        if (prg32_multiplayer_get_peer(i, &state) != 0 || state.player_id == 0) continue;
        df_peer_t *peer = net_peer(state.player_id);
        if (!peer) continue;
        df_peer_feed(peer, state.x, state.y, state.sprite, state.flags);
        peer->present = 1;
    }
}

static void net_leave(void) {
    if (g.net_ok) prg32_multiplayer_leave();
    g.net_ok = 0;
}

static void net_enter(void) {
    g.mode = MODE_NET;
    g.round = 0;
    g.net_ok = 0;
    g.net_ready = 0;
    g.count = 1;
    for (int i = 0; i < MAX_PEERS; ++i) df_peer_reset(&g.peers[i], 0);
    contestant_reset(&g.who[0], KIND_HUMAN);
    name_set(&g.who[0], "YOU");
    seq_stop();
    if (g.features & PRG32_FEATURE_MULTIPLAYER) {
        prg32_multiplayer_init();
        if (prg32_multiplayer_available() &&
            prg32_multiplayer_join(NET_SIGNATURE, PRG32_MP_FLAG_ENABLE) == 0) {
            g.net_ok = 1;
        }
    }
    set_state(ST_NETLOBBY);
}

static void net_peer_name(char *out, uint32_t id) {
    static const char hex[17] = "0123456789ABCDEF";
    out[0] = 'P';
    out[1] = '-';
    for (int i = 0; i < 4; ++i) out[2 + i] = hex[(id >> ((3 - i) * 4)) & 15u];
    out[6] = 0;
}

/* Round 0 fixes the roster: this board plus every peer in the lobby. */
static void net_roster(void) {
    g.count = 1;
    for (int i = 0; i < MAX_PEERS && g.count < MAX_CONTESTANTS; ++i) {
        if (!g.peers[i].present) continue;
        contestant_t *c = &g.who[g.count++];
        char name[11];
        contestant_reset(c, KIND_NET);
        c->peer = (uint8_t)i;
        net_peer_name(name, g.peers[i].id);
        name_set(c, name);
    }
}

static void net_lobby_update(uint32_t pressed) {
    if (pressed & PRG32_BTN_B) {
        net_leave();
        sfx_back();
        enter_title();
        return;
    }
    if (!g.net_ok) return;
    if (pressed & PRG32_BTN_A) {
        g.net_ready ^= 1;
        g.draw |= DR_MENU;
        sfx_select();
    }
    if ((g.frame & 7u) == 0) g.draw |= DR_MENU; /* peers come and go */
    if (!g.net_ready) return;
    int waiting = 0, partners = 0;
    if (g.round == 0) {
        for (int i = 0; i < MAX_PEERS; ++i) {
            if (!g.peers[i].present) continue;
            ++partners;
            if (!df_peer_ready(&g.peers[i], 0)) ++waiting;
        }
    } else {
        for (int i = 1; i < g.count; ++i) {
            const df_peer_t *peer = &g.peers[g.who[i].peer];
            if (!peer->present) continue; /* a board that left no longer blocks */
            ++partners;
            if (!df_peer_ready(peer, g.round)) ++waiting;
        }
    }
    if (partners == 0 || waiting) return;
    if (g.round == 0) net_roster();
    g.net_ready = 0;
    round_begin();
}

static void net_wait_update(void) {
    int waiting = 0;
    for (int i = 1; i < g.count; ++i) {
        const df_peer_t *peer = &g.peers[g.who[i].peer];
        if (peer->present && !df_peer_done(peer, g.round)) ++waiting;
    }
    if ((g.frame & 7u) == 0) g.draw |= DR_MENU;
    if (waiting && g.now - g.wait_start < NET_WAIT_MS) return;
    /* Take what arrived. A board that vanished scores an empty bar. */
    for (int i = 1; i < g.count; ++i) {
        const df_peer_t *peer = &g.peers[g.who[i].peer];
        if (peer->complete) df_copy(&g.who[i].pattern, &peer->pattern);
        else df_clear(&g.who[i].pattern);
    }
    show_begin(0);
}

/* ------------------------------------------------------------------ */
/* Composing                                                           */
/* ------------------------------------------------------------------ */

static void cells_dirty_all(void) {
    for (int v = 0; v < DF_VOICES; ++v) g.cell_dirty[v] = 0xffffu;
}

static void pattern_changed(int voice, int step) {
    g.cell_dirty[voice] |= (uint16_t)(1u << (step & 15));
    g.judge_dirty = 1;
}

/* The step a live hit belongs to: the one that just fired or, in the
 * second half of the gap, the one about to fire. */
static int live_step(int *early) {
    *early = g.seq.acc * 2 >= seq_gap();
    return *early ? (g.seq.step + 1) & 15 : g.seq.step;
}

static void pad_hit(int voice, uint32_t button) {
    contestant_t *c = composer();
    int early;
    int step = live_step(&early);
    int level = df_get(&c->pattern, voice, step);
    drum(voice, level >= DF_HIT ? level : DF_HIT);
    df_live_hit(&c->pattern, voice, step);
    if (early) g.seq.skip[voice] = (uint8_t)(step + 1);
    /* Keep the button down and this hit becomes an accent. */
    g.hold_voice = (uint8_t)voice;
    g.hold_step = (uint8_t)step;
    g.hold_button = button;
    g.hold_ms = g.now;
    if (g.sel != voice) {
        g.sel = (uint8_t)voice;
        g.draw |= DR_LABELS;
    }
    pattern_changed(voice, step);
}

static void do_move(int move) {
    contestant_t *c = composer();
    char message[41];
    if (!(g.unlocked & (1u << move))) {
        char *end = put_str(message, "LOCKED: NEED GROOVE ");
        put_num(end, (uint32_t)df_move_threshold(move), 3, ' ');
        banner(message, C_RED);
        sfx_locked();
        return;
    }
    df_move_apply(&c->pattern, move, g.sel);
    cells_dirty_all();
    g.judge_dirty = 1;
    g.draw |= DR_MOVES;
    put_str(put_str(message, MOVE_NAMES[move]), "!");
    banner(message, C_GOLD);
    sfx_special();
}

static void rejudge(void) {
    contestant_t *c = composer();
    char message[41];
    df_judge(&c->pattern, &c->score);
    g.judge_dirty = 0;
    g.draw |= DR_SCORE;
    for (int move = 0; move < DF_MOVES; ++move) {
        if ((g.unlocked & (1u << move)) || c->score.base < df_move_threshold(move)) continue;
        g.unlocked |= (uint8_t)(1u << move);
        g.draw |= DR_MOVES;
        /* A move performed in this very frame keeps its own banner. */
        if (g.banner_frames != 60) {
            put_str(put_str(message, "NEW MOVE: "), MOVE_NAMES[move]);
            banner(message, C_GREEN);
        }
        sfx_unlock();
    }
    if (g.mode == MODE_PRACTICE && c->score.total > g.practice_best) g.practice_best = c->score.total;
}

static int dir_of(uint32_t buttons) {
    if (buttons & PRG32_BTN_UP) return DF_DIR_UP;
    if (buttons & PRG32_BTN_DOWN) return DF_DIR_DOWN;
    if (buttons & PRG32_BTN_LEFT) return DF_DIR_LEFT;
    if (buttons & PRG32_BTN_RIGHT) return DF_DIR_RIGHT;
    return DF_DIR_NONE;
}

static uint32_t dir_bit(int dir) {
    if (dir == DF_DIR_UP) return PRG32_BTN_UP;
    if (dir == DF_DIR_DOWN) return PRG32_BTN_DOWN;
    if (dir == DF_DIR_LEFT) return PRG32_BTN_LEFT;
    if (dir == DF_DIR_RIGHT) return PRG32_BTN_RIGHT;
    return 0;
}

/* Track direction presses for the motions and the direction a pad uses:
 * the most recently pressed one that is still held. */
static void motion_track(uint32_t pressed) {
    for (int dir = DF_DIR_UP; dir <= DF_DIR_RIGHT; ++dir) {
        if (!(pressed & dir_bit(dir))) continue;
        if (g.now - g.motion_ms > MOTION_GAP_MS) g.motion_count = 0;
        if (g.motion_count == 3) {
            g.motion[0] = g.motion[1];
            g.motion[1] = g.motion[2];
            g.motion_count = 2;
        }
        g.motion[g.motion_count++] = (uint8_t)dir;
        g.motion_ms = g.now;
        g.dir_held = (uint8_t)dir;
    }
    if (!(g.in & dir_bit(g.dir_held))) g.dir_held = (uint8_t)dir_of(g.in);
}

static void menu_update(uint32_t pressed);

static void compose_update(uint32_t pressed) {
    contestant_t *c = composer();
    if (g.menu_open) {
        menu_update(pressed);
        return;
    }
    if (pressed & PRG32_BTN_START) {
        g.menu_open = 1;
        g.menu_item = 0;
        g.draw |= DR_MENU;
        sfx_move();
        return;
    }
    motion_track(pressed);
    for (int button = DF_BTN_A; button <= DF_BTN_B; ++button) {
        if (!(pressed & (button == DF_BTN_A ? PRG32_BTN_A : PRG32_BTN_B))) continue;
        int move = -1;
        if (g.now - g.motion_ms <= MOTION_GAP_MS) move = df_move_match(g.motion, g.motion_count, button);
        g.motion_count = 0;
        if (move >= 0) {
            do_move(move);
        } else if (g.dir_held == DF_DIR_DOWN) {
            if (button == DF_BTN_A) g.erasing = 1;
            if (button == DF_BTN_B && c->pattern.row[g.sel]) {
                /* DOWN + B: wipe the selected voice. */
                c->pattern.row[g.sel] = 0;
                g.cell_dirty[g.sel] = 0xffffu;
                g.judge_dirty = 1;
                sfx_back();
            }
        } else {
            pad_hit(df_pad_voice(g.dir_held, button), button == DF_BTN_A ? PRG32_BTN_A : PRG32_BTN_B);
        }
    }
    /* A pad held for a moment: its hit becomes an accent. */
    if (g.hold_button) {
        if (!(g.in & g.hold_button)) {
            g.hold_button = 0;
        } else if (g.now - g.hold_ms >= ACCENT_HOLD_MS) {
            g.hold_button = 0;
            if (df_get(&c->pattern, g.hold_voice, g.hold_step) != DF_ACCENT) {
                df_live_accent(&c->pattern, g.hold_voice, g.hold_step);
                pattern_changed(g.hold_voice, g.hold_step);
            }
        }
    }
    /* DOWN + A held: erase the selected voice under the playhead. A motion
     * that ends on DOWN + A never arms the eraser. */
    if (!(g.in & PRG32_BTN_DOWN) || !(g.in & PRG32_BTN_A)) g.erasing = 0;
    if (g.erasing && df_get(&c->pattern, g.sel, g.seq.step)) {
        df_set(&c->pattern, g.sel, g.seq.step, DF_OFF);
        pattern_changed(g.sel, g.seq.step);
    }
    if (g.judge_dirty) rejudge();
    if (g.mode != MODE_PRACTICE && g.seq.bar >= COMPOSE_BARS) compose_finished();
}

/* START menu while composing. The loop keeps playing underneath, so the
 * tempo, the swing and the kit can be changed by ear. */
enum { MI_RESUME, MI_TEMPO, MI_SWING, MI_DONE, MI_KIT, MI_CLEAR, MI_EXIT };

static int menu_count(void) { return g.mode == MODE_PRACTICE ? 6 : 5; }

static int menu_id(int index) {
    static const uint8_t practice[6] = {MI_RESUME, MI_TEMPO, MI_SWING, MI_KIT, MI_CLEAR, MI_EXIT};
    static const uint8_t battle[5] = {MI_RESUME, MI_DONE, MI_KIT, MI_CLEAR, MI_EXIT};
    return g.mode == MODE_PRACTICE ? practice[index] : battle[index];
}

static void menu_label(int id, char *out) {
    if (id == MI_RESUME) put_str(out, "RESUME");
    else if (id == MI_TEMPO) {
        put_str(put_num(put_str(out, "TEMPO "), (uint32_t)seq_bpm(), 3, ' '), " BPM");
    } else if (id == MI_SWING) put_str(out, g.seq.swing ? "SWING ON" : "SWING OFF");
    else if (id == MI_DONE) put_str(out, "DONE - PLAY IT");
    else if (id == MI_KIT) put_str(put_str(out, "KIT "), KIT_NAMES[g.kit]);
    else if (id == MI_CLEAR) put_str(out, "CLEAR ALL");
    else put_str(out, g.mode == MODE_PRACTICE ? "EXIT" : "QUIT MATCH");
}

static void menu_close(void) {
    g.menu_open = 0;
    g.draw = DR_ALL;
}

static void menu_update(uint32_t pressed) {
    contestant_t *c = composer();
    if (pressed & (PRG32_BTN_B | PRG32_BTN_START)) {
        menu_close();
        return;
    }
    if (pressed & PRG32_BTN_UP) g.menu_item = (uint8_t)((g.menu_item + menu_count() - 1) % menu_count());
    if (pressed & PRG32_BTN_DOWN) g.menu_item = (uint8_t)((g.menu_item + 1) % menu_count());
    if (pressed & (PRG32_BTN_UP | PRG32_BTN_DOWN)) {
        g.draw |= DR_MENU;
        sfx_move();
    }
    int id = menu_id(g.menu_item);
    /* Settings change with LEFT, RIGHT or A and keep the menu open. */
    int turn = 0;
    if (pressed & (PRG32_BTN_RIGHT | PRG32_BTN_A)) turn = 1;
    if (pressed & PRG32_BTN_LEFT) turn = -1;
    if (turn && (id == MI_TEMPO || id == MI_SWING || id == MI_KIT)) {
        if (id == MI_TEMPO) {
            g.practice_tempo = (uint8_t)((g.practice_tempo + 4 + turn) % 4);
            g.seq.frames = PRACTICE_FRAMES[g.practice_tempo];
        } else if (id == MI_SWING) {
            g.practice_swing ^= 1;
            g.seq.swing = g.practice_swing;
        } else {
            g.kit ^= 1;
        }
        g.draw |= DR_MENU | DR_STATUS;
        sfx_move();
        return;
    }
    if (!(pressed & PRG32_BTN_A)) return;
    sfx_select();
    if (id == MI_RESUME) {
        menu_close();
    } else if (id == MI_DONE) {
        compose_finished();
    } else if (id == MI_CLEAR) {
        df_clear(&c->pattern);
        cells_dirty_all();
        rejudge();
        menu_close();
    } else {
        if (g.mode == MODE_PRACTICE && g.practice_best) {
            prg32_score_submit_current_player(SCORE_GAME, g.practice_best);
        }
        if (g.mode == MODE_NET) net_leave();
        enter_title();
    }
}

/* ------------------------------------------------------------------ */
/* Update                                                              */
/* ------------------------------------------------------------------ */

static void title_update(uint32_t pressed) {
    if (pressed & PRG32_BTN_UP) g.title_item = (uint8_t)((g.title_item + MODE_COUNT - 1) % MODE_COUNT);
    if (pressed & PRG32_BTN_DOWN) g.title_item = (uint8_t)((g.title_item + 1) % MODE_COUNT);
    if (pressed & (PRG32_BTN_UP | PRG32_BTN_DOWN)) {
        g.draw |= DR_MENU;
        sfx_move();
    }
    if (pressed & PRG32_BTN_START) {
        prg32_scoreboard_show(SCORE_GAME, "LEGENDS OF THE PIAZZA");
        g.last_ms = prg32_ticks_ms(); /* the scoreboard is modal */
        g.prev = prg32_input_read();
        g.draw = DR_ALL;
        return;
    }
    if (!(pressed & PRG32_BTN_A)) return;
    g.rng ^= g.now * 2654435761u + g.frame;
    sfx_select();
    if (g.title_item == MODE_PRACTICE) match_begin(MODE_PRACTICE);
    else if (g.title_item == MODE_NET) net_enter();
    else {
        g.mode = g.title_item;
        g.setup_item = g.mode == MODE_CPU ? g.cpu_level : (uint8_t)(g.piazza_players - 2);
        set_state(ST_SETUP);
    }
}

static void setup_update(uint32_t pressed) {
    if (pressed & PRG32_BTN_B) {
        sfx_back();
        set_state(ST_TITLE);
        return;
    }
    if (pressed & PRG32_BTN_UP) g.setup_item = (uint8_t)((g.setup_item + 2) % 3);
    if (pressed & PRG32_BTN_DOWN) g.setup_item = (uint8_t)((g.setup_item + 1) % 3);
    if (pressed & (PRG32_BTN_UP | PRG32_BTN_DOWN)) {
        g.draw |= DR_MENU;
        sfx_move();
    }
    if (!(pressed & PRG32_BTN_A)) return;
    sfx_select();
    if (g.mode == MODE_CPU) g.cpu_level = g.setup_item;
    else g.piazza_players = (uint8_t)(g.setup_item + 2);
    match_begin(g.mode);
}

static void show_update(uint32_t pressed) {
    const contestant_t *c = &g.who[g.show];
    /* The judge reveals the groove over the first bar. */
    uint32_t elapsed = g.now - g.show_start;
    uint16_t shown = elapsed >= 2000u ? c->score.total : (uint16_t)(c->score.total * elapsed / 2000u);
    if (shown != g.shown_total) {
        g.shown_total = shown;
        g.draw |= DR_COUNT;
    }
    if (g.seq.bar < SHOW_BARS && !(pressed & PRG32_BTN_A)) return;
    if (g.show + 1 < g.count) show_begin(g.show + 1);
    else result_begin();
}

static void result_update(uint32_t pressed) {
    if (!(pressed & PRG32_BTN_A)) return;
    if (g.round + 1 >= ROUNDS) {
        final_begin();
        return;
    }
    ++g.round;
    sfx_select();
    if (g.mode == MODE_NET) {
        g.net_ready = 1; /* pressing A is the ready signal for the next round */
        set_state(ST_NETLOBBY);
    } else {
        round_begin();
    }
}

void drumfight_init(void) {
    /* Executable RAM is zeroed by the loader; set everything explicitly so
     * a restart of the cartridge behaves the same. */
    unsigned char *bytes = (unsigned char *)&g;
    for (unsigned i = 0; i < sizeof(g); ++i) bytes[i] = 0;
    g.features = df_host_features();
    g.rng = 0x19971997u;
    g.cpu_level = 0;
    g.piazza_players = 2;
    g.practice_tempo = 2;
    g.practice_swing = 0;
    g.kit = KIT_ANALOG;
    g.head_shown = 0xff;
    /* The title groove: what the MAESTRO would play. Fixed seed, so the
     * attract loop is the same on every board. */
    uint32_t seed = 0x5a0d0e1cu;
    df_ai_compose(&g.demo, 2, &seed);
    df_judge(&g.demo, &g.demo_score);
    g.last_ms = prg32_ticks_ms();
    g.now = g.last_ms;
    g.prev = 0x7fu; /* ignore buttons still held from the launcher */
    enter_title();
}

void drumfight_update(void) {
    g.now = prg32_ticks_ms();
    uint32_t dt = g.now - g.last_ms;
    g.last_ms = g.now;
    if (dt > 100u) dt = 100u;
    ++g.frame;

    /* Either controller may be the pad that is passed around. */
    uint32_t raw = prg32_input_read();
    g.in = (raw | (raw >> 8)) & 0x7fu;
    uint32_t pressed = g.in & ~g.prev;
    g.prev = g.in;

    seq_tick(dt);
    jingles_tick();
    if (g.mode == MODE_NET && g.state != ST_TITLE) net_tick();
    if (g.banner_frames && --g.banner_frames == 0) {
        g.banner[0] = 0;
        g.draw |= DR_BANNER;
    }
    for (int v = 0; v < DF_VOICES; ++v) {
        if (g.flash[v] && --g.flash[v] == 0) g.draw |= DR_PADS;
    }

    if (g.state == ST_TITLE) title_update(pressed);
    else if (g.state == ST_SETUP) setup_update(pressed);
    else if (g.state == ST_READY) {
        if (pressed & PRG32_BTN_A) {
            sfx_select();
            compose_begin();
        }
    } else if (g.state == ST_COMPOSE) compose_update(pressed);
    else if (g.state == ST_NETLOBBY) net_lobby_update(pressed);
    else if (g.state == ST_NETWAIT) net_wait_update();
    else if (g.state == ST_SHOW) show_update(pressed);
    else if (g.state == ST_RESULT) result_update(pressed);
    else if (g.state == ST_FINAL) {
        if (pressed & PRG32_BTN_A) {
            if (g.mode == MODE_NET) net_leave();
            g.mode = MODE_PRACTICE;
            enter_title();
        }
    }
}

/* ------------------------------------------------------------------ */
/* Drawing: the grid screen (composing and showcase)                   */
/* ------------------------------------------------------------------ */

static void draw_cell(int voice, int step) {
    int x = GRID_X + step * CELL_W;
    int y = GRID_Y + voice * CELL_H;
    int level = df_get(g.view, voice, step);
    uint16_t bg = step == g.head_shown ? C_HEAD : ((step & 3) == 0 ? C_CELL_BEAT : C_CELL);
    prg32_gfx_rect(x, y, CELL_W - 1, CELL_H - 1, bg);
    if (level == DF_GHOST) {
        prg32_gfx_rect(x + 4, y + 3, 4, 4, VOICE_COLORS[voice]);
    } else if (level >= DF_HIT) {
        prg32_gfx_rect(x + 1, y + 1, CELL_W - 3, CELL_H - 3, VOICE_COLORS[voice]);
        if (level == DF_ACCENT) prg32_gfx_rect(x + 3, y + 3, CELL_W - 7, CELL_H - 7, C_WHITE);
    }
}

static void draw_labels(void) {
    int composing = g.state == ST_COMPOSE;
    for (int v = 0; v < DF_VOICES; ++v) {
        int y = GRID_Y + v * CELL_H + 1;
        int selected = composing && v == g.sel;
        prg32_gfx_rect(0, y - 1, GRID_X - 2, CELL_H - 1, selected ? C_PANEL : C_BG);
        text(2, y, VOICE_NAMES[v], selected ? C_WHITE : VOICE_COLORS[v], selected ? C_PANEL : C_BG);
    }
}

static void draw_status(void) {
    char line[41];
    char *end;
    prg32_gfx_rect(0, 0, PRG32_GAME_W, 10, C_PANEL);
    if (g.mode == MODE_PRACTICE) {
        put_str(put_str(line, "PRACTICE "), KIT_NAMES[g.kit]);
        text(2, 1, line, C_GOLD, C_PANEL);
    } else {
        end = put_str(line, "R");
        end = put_num(end, (uint32_t)(g.round + 1), 1, ' ');
        end = put_str(end, "/3 ");
        put_str(end, g.state == ST_SHOW ? g.who[g.show].name : composer()->name);
        text(2, 1, line, C_GOLD, C_PANEL);
    }
    end = put_str(line, "BPM ");
    end = put_num(end, (uint32_t)seq_bpm(), 3, ' ');
    if (g.seq.swing) end = put_str(end, " SW");
    int bar = g.seq.bar < 0 ? 0 : g.seq.bar;
    if (g.state == ST_SHOW) {
        put_str(end, "  SHOWCASE");
    } else if (g.mode == MODE_PRACTICE) {
        end = put_str(end, " BAR ");
        put_num(end, (uint32_t)(bar % 1000 + 1), 3, ' ');
    } else {
        end = put_str(end, " BAR ");
        end = put_num(end, (uint32_t)(bar + 1 > COMPOSE_BARS ? COMPOSE_BARS : bar + 1), 2, '0');
        put_str(end, "/16");
    }
    text(PRG32_GAME_W - 2 - str_len(line) * 8, 1, line, C_TEXT, C_PANEL);
}

/* `all` repaints the whole panel; otherwise only what a changing number
 * touches, which keeps the showcase count-up cheap on the SPI display. */
static void draw_score(int all) {
    const df_score_t *s = g.view_score;
    char line[41];
    char *end;
    unsigned total = s->total, base = s->base, bonus = s->bonus;
    if (g.state == ST_SHOW) {
        /* Count-up: the judge has not finished yet. */
        total = g.shown_total;
        base = total > s->base ? s->base : total;
        bonus = total - base;
    }
    end = put_str(line, "GROOVE ");
    end = put_num(end, base, 3, '0');
    end = put_str(end, "  MOVES +");
    end = put_num(end, bonus, 3, '0');
    put_str(end, "  TOTAL ");
    text(2, SCORE_Y, line, C_TEXT, C_BG);
    put_num(line, total, 3, '0');
    text(2 + 30 * 8, SCORE_Y, line, C_GOLD, C_BG);
    /* Groove meter: the height is the total out of 999. */
    int height = (int)(total * 88u / 999u);
    if (all || height != g.meter_shown) {
        g.meter_shown = (uint8_t)height;
        prg32_gfx_rect(METER_X, GRID_Y, 12, 88 - height, C_CELL);
        if (height) prg32_gfx_rect(METER_X, GRID_Y + 88 - height, 12, height, C_GOLD);
    }
    if (!all) return;
    for (int c = 0; c < DF_CRITERIA; ++c) {
        int x = 4 + c * 52;
        int width = s->crit[c] * 22 / 100;
        text(x, CRIT_Y, CRIT_NAMES[c], C_DIM, C_BG);
        prg32_gfx_rect(x + 26, CRIT_Y + 1, 22, 6, C_CELL);
        if (width) prg32_gfx_rect(x + 26, CRIT_Y + 1, width, 6, s->crit[c] >= 70 ? C_GREEN : C_AZZURRO);
    }
}

static void draw_moves(void) {
    unsigned used = g.view->moves;
    int composing = g.state == ST_COMPOSE;
    unsigned unlocked = composing ? g.unlocked : 0u;
    for (int move = 0; move < DF_MOVES; ++move) {
        int x = (move & 1) * 160;
        int y = MOVES_Y + (move >> 1) * 10;
        uint8_t dirs[3];
        int button;
        int length = df_move_motion(move, dirs, &button);
        int is_used = (used >> move) & 1u;
        int is_open = (unlocked >> move) & 1u;
        uint16_t color = is_used ? C_GOLD : (is_open ? C_TEXT : C_DIM);
        char tail[5];
        prg32_gfx_rect(x, y, 160, 9, C_BG);
        for (int i = 0; i < length; ++i) arrow(x + i * 8, y, dirs[i], color, C_BG);
        text(x + length * 8, y, button == DF_BTN_A ? "A" : "B", color, C_BG);
        text(x + 38, y, MOVE_NAMES[move], color, C_BG);
        if (is_used) put_str(tail, "+25");
        else if (is_open) put_str(tail, "OK");
        else if (composing) put_num(tail, (uint32_t)df_move_threshold(move), 3, ' ');
        else tail[0] = 0; /* showcase: only the performed moves light up */
        text(x + 130, y, tail, color, C_BG);
    }
}

static void draw_banner(void) {
    prg32_gfx_rect(0, BANNER_Y, PRG32_GAME_W, 9, C_BG);
    if (g.banner[0]) text_center(BANNER_Y, g.banner, g.banner_color, C_BG);
}

static void draw_menu(void) {
    char label[24];
    int height = 24 + menu_count() * 11;
    prg32_gfx_rect(76, 30, 168, height + 4, C_GOLD);
    prg32_gfx_rect(78, 32, 164, height, C_PANEL);
    text_center(36, "DRUM BREAK", C_GOLD, C_PANEL);
    for (int i = 0; i < menu_count(); ++i) {
        int selected = i == g.menu_item;
        menu_label(menu_id(i), label);
        prg32_gfx_rect(82, 48 + i * 11, 156, 10, selected ? C_AZZURRO : C_PANEL);
        text(92, 49 + i * 11, label, selected ? C_WHITE : C_TEXT, selected ? C_AZZURRO : C_PANEL);
    }
}

static void draw_grid_screen(void) {
    if (g.draw & DR_ALL) {
        prg32_gfx_clear(C_BG);
        for (int beat = 0; beat < 4; ++beat) {
            char digit[2];
            put_num(digit, (uint32_t)(beat + 1), 1, ' ');
            text(GRID_X + beat * 4 * CELL_W + 2, 12, digit, C_DIM, C_BG);
        }
        for (int v = 0; v < DF_VOICES; ++v) {
            pad_legend(LEGEND_X, GRID_Y + v * CELL_H + 1, v, VOICE_COLORS[v], C_BG);
        }
        if (g.state == ST_COMPOSE) {
            text_center(HELP_Y, "PADS: DIR+A/B  DOWN+A ERASE  START MENU", C_DIM, C_BG);
        } else {
            text_center(HELP_Y, "THE PIAZZA JUDGES...  A: SKIP", C_DIM, C_BG);
        }
        g.head_shown = g.seq.bar >= 0 ? g.seq.step : 0xff;
        cells_dirty_all();
        g.draw |= DR_STATUS | DR_SCORE | DR_MOVES | DR_BANNER | DR_LABELS;
        if (g.menu_open) g.draw |= DR_MENU;
    }
    if (g.menu_open && !(g.draw & DR_ALL)) {
        /* The menu covers the grid: keep the status line and menu fresh. */
        if (g.draw & DR_STATUS) draw_status();
        if (g.draw & DR_MENU) draw_menu();
        g.draw = 0;
        return;
    }
    if ((g.draw & DR_HEAD) && g.seq.step != g.head_shown) {
        int old = g.head_shown;
        g.head_shown = g.seq.step;
        for (int v = 0; v < DF_VOICES; ++v) {
            if (old != 0xff) g.cell_dirty[v] |= (uint16_t)(1u << old);
            g.cell_dirty[v] |= (uint16_t)(1u << g.head_shown);
        }
    }
    for (int v = 0; v < DF_VOICES; ++v) {
        uint16_t dirty = g.cell_dirty[v];
        if (!dirty) continue;
        g.cell_dirty[v] = 0;
        for (int s = 0; s < DF_STEPS; ++s) {
            if (dirty & (1u << s)) draw_cell(v, s);
        }
    }
    if (g.draw & DR_LABELS) draw_labels();
    if (g.draw & DR_STATUS) draw_status();
    if (g.draw & DR_SCORE) draw_score(1);
    else if (g.draw & DR_COUNT) draw_score(0);
    if (g.draw & DR_MOVES) draw_moves();
    if (g.draw & DR_BANNER) draw_banner();
    if (g.menu_open) draw_menu();
    g.draw = 0;
}

/* ------------------------------------------------------------------ */
/* Drawing: the other screens                                          */
/* ------------------------------------------------------------------ */

/* The piazza at night: a sky, the obelisk of San Domenico and the roofs. */
static void draw_piazza(int ground) {
    prg32_gfx_clear(C_BG);
    for (int i = 0; i < 24; ++i) {
        /* A fixed scatter of stars. */
        int x = (i * 97 + 13) % 316;
        int y = (i * 53 + 7) % (ground - 56);
        prg32_gfx_pixel(x, y, (i & 3) ? C_DIM : C_TEXT);
    }
    prg32_gfx_rect(0, ground, PRG32_GAME_W, PRG32_GAME_H - ground, C_PANEL);
    for (int i = 0; i < 8; ++i) {
        int height = 18 + ((i * 37) % 22);
        prg32_gfx_rect(i * 40, ground - height, 38, height, RGB(14, 16, 40));
        prg32_gfx_rect(i * 40 + 8, ground - height + 6, 4, 5, (i & 1) ? C_GOLD : RGB(30, 34, 70));
        prg32_gfx_rect(i * 40 + 24, ground - height + 10, 4, 5, (i % 3) ? RGB(30, 34, 70) : C_GOLD);
    }
    /* The guglia: stepped base, shaft and the statue on top. */
    prg32_gfx_rect(148, ground - 8, 24, 8, RGB(70, 70, 96));
    prg32_gfx_rect(152, ground - 20, 16, 12, RGB(86, 86, 114));
    prg32_gfx_rect(155, ground - 38, 10, 18, RGB(102, 102, 132));
    prg32_gfx_rect(157, ground - 48, 6, 10, RGB(118, 118, 148));
    prg32_gfx_rect(159, ground - 53, 2, 5, C_GOLD);
}

static void draw_title_menu(void) {
    for (int i = 0; i < MODE_COUNT; ++i) {
        int selected = i == g.title_item;
        int y = 120 + i * 12;
        prg32_gfx_rect(76, y - 1, 168, 10, selected ? C_AZZURRO : C_PANEL);
        text_center(y, MODE_NAMES[i], selected ? C_WHITE : C_TEXT, selected ? C_AZZURRO : C_PANEL);
    }
}

/* Eight pads along the bottom flash with the title groove. */
static void draw_title_pads(void) {
    for (int v = 0; v < DF_VOICES; ++v) {
        prg32_gfx_rect(84 + v * 20, 169, 16, 7, g.flash[v] ? VOICE_COLORS[v] : C_CELL_BEAT);
    }
}

static void draw_title(void) {
    if (g.draw & DR_ALL) {
        draw_piazza(114);
        big_center(4, "DRUMFIGHT", 4, C_GOLD);
        big_center(36, "NAPOLI 97", 2, C_AZZURRO);
        text_center(53, "PIAZZA SAN DOMENICO, 1997.", C_TEXT, C_BG);
        g.draw |= DR_MENU | DR_PADS;
        text_center(180, "STUDENTS BY DAY, DRUM WARRIORS BY NIGHT", C_DIM, C_PANEL);
        text_center(190, "A: PLAY   START: LEGENDS", C_GOLD, C_PANEL);
    }
    if (g.draw & DR_MENU) draw_title_menu();
    if (g.draw & DR_PADS) draw_title_pads();
    g.draw = 0;
}

static void draw_setup(void) {
    if (g.draw & DR_ALL) {
        draw_piazza(168);
        big_center(10, g.mode == MODE_CPU ? "VS CPU" : "PIAZZA", 3, C_GOLD);
        text_center(40, g.mode == MODE_CPU ? "WHO DARES TO CHALLENGE YOU?" : "HOW MANY DRUMMERS PASS THE PAD?",
                    C_TEXT, C_BG);
        text_center(174, "3 ROUNDS: 91, 114 AND 114 SWING BPM", C_DIM, C_PANEL);
        text_center(186, "A: START   B: BACK", C_GOLD, C_PANEL);
        g.draw |= DR_MENU;
    }
    if (g.draw & DR_MENU) {
        for (int i = 0; i < 3; ++i) {
            char label[24];
            int selected = i == g.setup_item;
            int y = 62 + i * 14;
            if (g.mode == MODE_CPU) put_str(label, CPU_NAMES[i]);
            else put_str(put_num(label, (uint32_t)(i + 2), 1, ' '), " PLAYERS");
            prg32_gfx_rect(92, y - 2, 136, 12, selected ? C_AZZURRO : C_PANEL);
            text_center(y, label, selected ? C_WHITE : C_TEXT, selected ? C_AZZURRO : C_PANEL);
        }
        if (g.mode == MODE_CPU) {
            prg32_gfx_rect(0, 108, PRG32_GAME_W, 9, C_BG);
            text_center(108,
                        g.setup_item == 0 ? "A FRESHMAN WITH A BORROWED BONGO"
                        : g.setup_item == 1 ? "TEN YEARS OF EXAMS, TEN OF RHYTHM"
                                            : "THE LEGEND OF THE PIAZZA",
                        C_DIM, C_BG);
        }
    }
    g.draw = 0;
}

static void draw_ready(void) {
    char line[41];
    char *end;
    if (!(g.draw & DR_ALL)) {
        g.draw = 0;
        return;
    }
    prg32_gfx_clear(C_BG);
    end = put_str(line, "ROUND ");
    end = put_num(end, (uint32_t)(g.round + 1), 1, ' ');
    end = put_str(end, " OF 3 - ");
    end = put_num(end, (uint32_t)frames_bpm(ROUND_FRAMES[g.round]), 3, ' ');
    put_str(end, ROUND_SWING[g.round] ? " BPM SWING" : " BPM");
    text_center(4, line, C_DIM, C_BG);
    big_center(18, composer()->name, 3, C_GOLD);
    text_center(46, "16 BARS TO BUILD YOUR GROOVE", C_TEXT, C_BG);
    for (int v = 0; v < DF_VOICES; ++v) {
        int x = 40 + (v & 1) * 136;
        int y = 62 + (v >> 1) * 11;
        pad_legend(x, y, v, VOICE_COLORS[v], C_BG);
        text(x + 24, y, VOICE_NAMES[v], VOICE_COLORS[v], C_BG);
    }
    text_center(110, "TAP: HIT   HOLD: ACCENT", C_TEXT, C_BG);
    arrow(40, 124, DF_DIR_DOWN, C_TEXT, C_BG);
    text(48, 124, "+A HOLD: ERASE THE LAST VOICE", C_TEXT, C_BG);
    arrow(40, 135, DF_DIR_DOWN, C_TEXT, C_BG);
    text(48, 135, "+B: WIPE THE LAST VOICE", C_TEXT, C_BG);
    text_center(150, "RAISE THE GROOVE TO UNLOCK MOVES,", C_AZZURRO, C_BG);
    text_center(160, "THEN PLAY THEIR MOTION: +25 EACH", C_AZZURRO, C_BG);
    text_center(182, "PRESS A WHEN READY", C_GOLD, C_BG);
    g.draw = 0;
}

static void draw_net(void) {
    char line[41];
    if (g.draw & DR_ALL) {
        draw_piazza(168);
        big_center(8, "NETWORK", 3, C_GOLD);
        if (!g.net_ok) {
            text_center(60, "NO NETWORK ON THIS HOST", C_RED, C_BG);
            text_center(74, "CONNECT THE BOARD TO WI-FI AND TO", C_TEXT, C_BG);
            text_center(84, "A PRG32 MULTIPLAYER SERVER", C_TEXT, C_BG);
            text_center(180, "B: BACK", C_GOLD, C_PANEL);
        } else if (g.state == ST_NETLOBBY) {
            text_center(180, "A: READY   B: LEAVE", C_GOLD, C_PANEL);
        } else {
            text_center(180, "WAITING FOR THE OTHER DRUMMERS", C_GOLD, C_PANEL);
        }
        g.draw |= DR_MENU;
    }
    if ((g.draw & DR_MENU) && g.net_ok) {
        char *end = put_str(line, g.state == ST_NETLOBBY ? "LOBBY - ROUND " : "ROUND ");
        end = put_num(end, (uint32_t)(g.round + 1), 1, ' ');
        put_str(end, " OF 3");
        prg32_gfx_rect(0, 36, PRG32_GAME_W, 60, C_BG);
        text_center(38, line, C_TEXT, C_BG);
        text(72, 52, "YOU", C_GOLD, C_BG);
        text(152, 52, g.state == ST_NETWAIT ? "DONE" : (g.net_ready ? "READY" : "PRESS A"),
             g.net_ready || g.state == ST_NETWAIT ? C_GREEN : C_DIM, C_BG);
        int row = 0;
        for (int i = 0; i < MAX_PEERS; ++i) {
            const df_peer_t *peer = &g.peers[i];
            if (!peer->present) continue;
            int ok = g.state == ST_NETWAIT ? df_peer_done(peer, g.round) : df_peer_ready(peer, g.round);
            net_peer_name(line, peer->id);
            text(72, 63 + row * 11, line, C_AZZURRO, C_BG);
            text(152, 63 + row * 11,
                 ok ? (g.state == ST_NETWAIT ? "DONE" : "READY")
                    : (peer->phase == DF_NET_COMPOSE ? "PLAYING" : "WAITING"),
                 ok ? C_GREEN : C_DIM, C_BG);
            ++row;
        }
        if (row == 0) text_center(66, "NO OTHER DRUMMERS YET...", C_DIM, C_BG);
    }
    g.draw = 0;
}

static void draw_table(int y, int final) {
    char line[41];
    for (int rank = 0; rank < g.count; ++rank) {
        const contestant_t *c = &g.who[g.order[rank]];
        char *end = put_num(line, (uint32_t)(rank + 1), 1, ' ');
        end = put_str(end, ". ");
        end = put_str(end, c->name);
        while (end - line < 14) *end++ = ' ';
        end = put_num(end, final ? c->best : c->score.total, 3, '0');
        if (!final) {
            end = put_str(end, "  +");
            end = put_num(end, c->gained, 1, ' ');
        }
        end = put_str(end, "  PTS ");
        put_num(end, c->points, 2, ' ');
        text_center(y + rank * 12, line, rank == 0 ? C_GOLD : C_TEXT, C_BG);
    }
}

static void draw_result(void) {
    char line[41];
    if (!(g.draw & DR_ALL)) {
        g.draw = 0;
        return;
    }
    draw_piazza(168);
    char *end = put_str(line, "ROUND ");
    put_num(end, (uint32_t)(g.round + 1), 1, ' ');
    big_center(8, line, 3, C_GOLD);
    text_center(40, "NAME        GROOVE  WON  MATCH", C_DIM, C_BG);
    draw_table(54, 0);
    text_center(180, g.round + 1 >= ROUNDS ? "A: THE VERDICT" : "A: NEXT ROUND", C_GOLD, C_PANEL);
    g.draw = 0;
}

static void draw_final(void) {
    if (!(g.draw & DR_ALL)) {
        g.draw = 0;
        return;
    }
    draw_piazza(168);
    text_center(4, "THE LEGEND OF THE PIAZZA IS", C_TEXT, C_BG);
    big_center(18, g.who[g.order[0]].name, 3, C_GOLD);
    text_center(48, "NAME     BEST GROOVE  MATCH", C_DIM, C_BG);
    draw_table(60, 1);
    text_center(174, "THE REST OF THE STORY? PURE LEGEND!", C_AZZURRO, C_PANEL);
    text_center(186, "A: BACK TO THE PIAZZA", C_GOLD, C_PANEL);
    g.draw = 0;
}

void drumfight_draw(void) {
    if (!g.draw) return;
    if (g.state == ST_TITLE) draw_title();
    else if (g.state == ST_SETUP) draw_setup();
    else if (g.state == ST_READY) draw_ready();
    else if (g.state == ST_COMPOSE || g.state == ST_SHOW) draw_grid_screen();
    else if (g.state == ST_NETLOBBY || g.state == ST_NETWAIT) draw_net();
    else if (g.state == ST_RESULT) draw_result();
    else draw_final();
}
