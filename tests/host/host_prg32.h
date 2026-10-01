/*
 * Host implementation of the PRG32 calls used by Drumfight Napoli 97.
 *
 * A test includes this header and then src/drumfight.c, so the unmodified
 * cartridge source runs on the workstation against:
 *   - a 320x200 RGB565 framebuffer with the firmware font (font8.h);
 *   - a clock and a controller the test drives;
 *   - a log of every synth note;
 *   - a multiplayer service whose peers the test injects.
 * Declarations come from the real PRG32 public headers, so a signature
 * mismatch with the firmware is a compile error.
 */
#ifndef DF_HOST_PRG32_H
#define DF_HOST_PRG32_H

#include <stdio.h>
#include <string.h>

#define DF_HOST 1
#include "prg32.h"

#include "font8.h"

#define HOST_MAX_NOTES 8192

typedef struct {
    uint32_t ms;
    uint8_t channel, instrument, note, volume;
} host_note_t;

static uint16_t host_fb[PRG32_GAME_H][PRG32_GAME_W];
static uint32_t host_ms;
static uint32_t host_input;
static uint32_t host_features = PRG32_FEATURE_AUDIO | PRG32_FEATURE_AUDIO_PLUS;
static host_note_t host_notes[HOST_MAX_NOTES];
static int host_note_count;
static int host_bad_chars;      /* characters outside the portable set */
static int host_pixels_drawn;   /* since the last host_frame() */
static char host_band[64];
static uint32_t host_score_last;
static int host_score_count;
static int host_scoreboard_shown;
static int host_mp_joined;
static char host_mp_signature[48];
static prg32_player_state_t host_mp_local;
static prg32_player_state_t host_mp_peers[PRG32_MP_MAX_PEERS];
static int host_mp_peer_count;

uint32_t df_host_features(void) { return host_features; }

uint32_t prg32_ticks_ms(void) { return host_ms; }
uint32_t prg32_input_read(void) { return host_input; }

void prg32_audio_note_on_pan(uint8_t channel, uint8_t instrument, uint8_t note, uint8_t volume, int8_t pan) {
    (void)pan;
    if (host_note_count < HOST_MAX_NOTES) {
        host_note_t *n = &host_notes[host_note_count++];
        n->ms = host_ms;
        n->channel = channel;
        n->instrument = instrument;
        n->note = note;
        n->volume = volume;
    }
}

void prg32_gfx_rect(int x, int y, int w, int h, uint16_t color) {
    for (int py = y; py < y + h; ++py) {
        if (py < 0 || py >= PRG32_GAME_H) continue;
        for (int px = x; px < x + w; ++px) {
            if (px < 0 || px >= PRG32_GAME_W) continue;
            host_fb[py][px] = color;
            ++host_pixels_drawn;
        }
    }
}

void prg32_gfx_clear(uint16_t color) { prg32_gfx_rect(0, 0, PRG32_GAME_W, PRG32_GAME_H, color); }

void prg32_gfx_pixel(int x, int y, uint16_t color) { prg32_gfx_rect(x, y, 1, 1, color); }

/* PRG32-QT and PRG32-iOS draw text with a reduced font: capitals, digits
 * and these signs. Anything else would show as '?' there. */
static int host_portable_char(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || (c && strchr(" ?!.,:-+/", c));
}

void prg32_gfx_text8(int x, int y, const char *s, uint16_t fg, uint16_t bg) {
    for (; *s; ++s, x += 8) {
        unsigned ch = (unsigned char)*s;
        if (!host_portable_char(*s)) {
            ++host_bad_chars;
            printf("non-portable character '%c' drawn at %d,%d\n", *s, x, y);
        }
        if (x + 8 > PRG32_GAME_W) {
            ++host_bad_chars;
            printf("text runs off the screen at %d,%d\n", x, y);
        }
        if (ch < 32 || ch > 126) ch = '?';
        for (int row = 0; row < 8; ++row) {
            unsigned bits = host_font8[ch - 32][row];
            for (int col = 0; col < 8; ++col) {
                prg32_gfx_pixel(x + col, y + row, (bits & (0x80u >> col)) ? fg : bg);
            }
        }
    }
}

void prg32_band_set_game_info(const char *s) { snprintf(host_band, sizeof(host_band), "%s", s); }

int prg32_score_submit_current_player(const char *game, uint32_t score) {
    (void)game;
    host_score_last = score;
    ++host_score_count;
    return 0;
}

int prg32_scoreboard_show(const char *game, const char *title) {
    (void)game;
    (void)title;
    ++host_scoreboard_shown;
    return 0;
}

void prg32_multiplayer_init(void) {}
bool prg32_multiplayer_available(void) { return (host_features & PRG32_FEATURE_MULTIPLAYER) != 0; }
int prg32_multiplayer_join(const char *signature, uint32_t flags) {
    (void)flags;
    snprintf(host_mp_signature, sizeof(host_mp_signature), "%s", signature);
    host_mp_joined = 1;
    return 0;
}
int prg32_multiplayer_leave(void) {
    host_mp_joined = 0;
    return 0;
}
void prg32_multiplayer_tick(void) {}
int prg32_multiplayer_set_local_state(int16_t x, int16_t y, uint16_t sprite, uint16_t flags) {
    host_mp_local.x = x;
    host_mp_local.y = y;
    host_mp_local.sprite = sprite;
    host_mp_local.flags = flags;
    return 0;
}
int prg32_multiplayer_set_input(uint32_t input) {
    host_mp_local.input = input & 0x7fu;
    return 0;
}
int prg32_multiplayer_get_peer_count(void) { return host_mp_joined ? host_mp_peer_count : 0; }
int prg32_multiplayer_get_peer(int index, prg32_player_state_t *out) {
    if (!host_mp_joined || index < 0 || index >= host_mp_peer_count) return -1;
    *out = host_mp_peers[index];
    return 0;
}

/* Write the framebuffer as a binary PPM (tests/run_tests.sh converts the
 * screenshots to PNG). */
static int host_save_ppm(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P6\n%d %d\n255\n", PRG32_GAME_W, PRG32_GAME_H);
    for (int y = 0; y < PRG32_GAME_H; ++y) {
        for (int x = 0; x < PRG32_GAME_W; ++x) {
            unsigned v = host_fb[y][x];
            unsigned r = (v >> 11) & 31u, gr = (v >> 5) & 63u, b = v & 31u;
            fputc((int)((r * 527u + 23u) >> 6), f);
            fputc((int)((gr * 259u + 33u) >> 6), f);
            fputc((int)((b * 527u + 23u) >> 6), f);
        }
    }
    fclose(f);
    return 0;
}

#endif
