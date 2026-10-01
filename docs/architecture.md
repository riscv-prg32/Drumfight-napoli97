# Architecture

## Two source files

| File | Role | PRG32 calls |
|---|---|---|
| [`src/df_core.h`](../src/df_core.h) | The rules: pattern model, judge, special moves, CPU opponent, network packing, ranking | none |
| [`src/drumfight.c`](../src/drumfight.c) | The cartridge: state machine, sequencer, input, audio, drawing | all of them |

`df_core.h` is pure integer C99. It runs unchanged inside the cartridge and
inside the host unit tests, which is what makes the judge and the network
protocol testable without a board. `drumfight.c` includes it; the PRG32
builder takes exactly one source file.

The cartridge exports `drumfight_init`, `drumfight_update` and
`drumfight_draw` (entry prefix `drumfight`). The firmware calls update and
draw once per 33 ms frame.

## Pattern

```c
typedef struct {
    uint32_t row[8];   /* one voice each; step s uses bits 2s+1..2s */
    uint8_t moves;     /* bit m = special move m was performed */
} df_pattern_t;
```

Levels: 0 off, 1 ghost, 2 hit, 3 accent. Thirty-three bytes describe a bar,
which is what the network sends.

## State machine

```text
TITLE --A--> SETUP --A--> READY --A--> COMPOSE --16 bars / DONE--+
  |            (VS CPU, PIAZZA)          ^                       |
  |                                      | next local player     |
  +--A (PRACTICE)--> COMPOSE             +-----------------------+
  |                                                              v
  +--A (NETWORK)--> NETLOBBY --all ready--> COMPOSE --> NETWAIT --> SHOW (each contestant)
                                                                     |
                      TITLE <-- FINAL <-- (after round 3) <-- RESULT <+
                                              RESULT --A--> READY / NETLOBBY (next round)
```

A match is a list of up to four *contestants*, each `KIND_HUMAN` (composes
on this board), `KIND_CPU` (composed by `df_ai_compose`) or `KIND_NET`
(pattern received from a peer). The three battle modes differ only in how
that list is filled, so showcase, result and final are shared code.

## One frame

`drumfight_update`:

1. read the clock and the pad (both controllers merged), compute press
   edges;
2. `seq_tick`: advance the sequencer clock and fire due steps
   ([audio.md](audio.md));
3. play queued jingle notes; publish and collect network snapshots in
   network mode;
4. run the state handler (menus, composing, waiting...).

Composing input, in order: START opens the menu; direction presses are
tracked for motions; an A/B press is first matched against the motions, then
treated as an edit (DOWN) or a pad; a held pad becomes an accent after
180 ms; DOWN + A held erases under the playhead; if the pattern changed it is
judged again and moves may unlock.

## Drawing: only what changed

The PRG32 display driver sends the bounding box of everything drawn in a
frame over SPI, so the cost of a frame is the size of that box. The
cartridge never clears the screen while playing. It keeps redraw requests
(`DR_*` bits and one dirty mask per grid row) and redraws:

| Event | Redrawn |
|---|---|
| Playhead moves (every step) | two grid columns |
| A hit is recorded or erased | its cell, the score panel |
| A move is performed | the grid, score, move panel, banner line |
| A new bar starts | the status line |
| Showcase count-up | the number line, the meter when its height changes |
| Title groove | the row of eight pads |

A full repaint happens only when the screen changes.
`tests/test_game.c` asserts that an idle title frame draws no more than the
pad row.

Text goes through `prg32_gfx_text8()`. Only `A-Z 0-9 ? ! . , : - + /` and
space are used, because the PRG32-QT and PRG32-iOS hosts draw a reduced
font; arrows are drawn pixel by pixel and the big titles from a small 5x7
glyph table. The host test layer fails the test run on any other character
or on text that leaves the screen.

## Portable-cartridge rules

A PRG32 portable cartridge is one position-independent image with no
relocation records, linked with `-nostdlib`:

- **No pointers in initialised static data.** Names are fixed-size `char`
  arrays, tables hold numbers. Pointers exist only at run time (`g.view`).
- **No libc.** `memset` and `memcpy` are defined in the cartridge because
  the compiler may emit calls to them; text formatting is `put_str` and
  `put_num`.
- **No floating point, no 64-bit division** (no libgcc).
- **Optional features only.** The image requires no feature; audio, stereo
  and multiplayer are declared optional and multiplayer is checked at run
  time through the ABI table (`df_host_features()`).

`tools/check_relocatable.py` proves the first rule on every build: it scans
the object for absolute relocations and links the image at two addresses,
which must give identical bytes.

## Budgets

| Budget | Release 1.0.0 | Limit | Enforced by |
|---|---|---|---|
| Code and data | 21,812 bytes | | |
| Executable RAM (`mem`) | 22,500 bytes | 32,768 (classroom profile) | `scripts/build.sh`, `--cart-ram-kib 32` |
| Stored package (code + audio + Store trailer) | 28,815 bytes | 65,536 | `scripts/build.sh` |
| AUDIO block | 104 bytes | | |

## Scores and status band

The best groove is submitted with
`prg32_score_submit_current_player("drumfight", total)`; START on the title
calls the firmware's modal `prg32_scoreboard_show()`. The firmware status
band shows "DRUMFIGHT NAPOLI 97" through `prg32_band_set_game_info()`.
