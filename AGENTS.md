# AGENTS.md

Guidance for automated coding agents and maintainers of Drumfight Napoli 97.

## Project intent

A PRG32 cartridge that is a playable drum machine, a teaching example of
real-time audio and deterministic game rules on RV32IMC, and a small tribute
to the drum circles of Piazza San Domenico Maggiore, Naples. The story is
fiction; do not present it as history.

## Rules that are easy to break

- **Rules live in `src/df_core.h`, with no PRG32 calls.** Anything that
  decides a score, a move or a network result goes there and gets a test in
  `tests/test_core.c`.
- **One source per build:** `src/drumfight.c` includes the core; the
  builder takes one file. Do not add compiler flags.
- **No pointers in initialised static data** (portable cartridges have no
  relocations): names are fixed-size `char` arrays, tables hold numbers.
  `tools/check_relocatable.py` enforces it.
- **No libc, no floats, no 64-bit division.**
- **Text uses only `A-Z 0-9 ? ! . , : - + /` and space**, at most 40
  characters per line; the host tests fail otherwise. Draw arrows with
  `arrow()`.
- **Draw only what changed.** Add a `DR_*` request instead of clearing the
  screen; the SPI display pays for the bounding box of every frame.
- **Voice n = channel n; instrument n (ANALOG kit) or n + 8 (SID kit).**
  Keep all eight. Sound design is `tools/build_audio.py`, which computes
  the PCM one-shots and `audio/audio.json` from formulas, plus `VOICE_GAIN`
  and `SID_NOTES`. Never add recorded or third-party samples. The AUD0
  block must keep the load image under the limit `scripts/build.sh` checks.
- **Tempo is a whole number of 33 ms frames per sixteenth** (and swing is
  one frame). Do not add tempos in BPM: uneven steps are the one thing a
  drum machine must not have.
- **A change to the pattern format, snapshot layout, judge or moves must
  bump the multiplayer signature** in `src/drumfight.c` and
  `metadata/metadata.json`.
- **Committed build inputs:** `audio/*.raw`, `audio/audio.json`, `assets/store/*`,
  `tests/host/font8.h`. Committed build outputs: `dist/*.prg32`, the Store
  bundle and `dist/SHA256SUMS`; rebuild them whenever source, metadata or
  assets change.
- **Do not publish** to the Cartridge Store from scripts; publication is a
  human-approved, authenticated step. No credentials in the repository.
- **Do not modify the PRG32 checkout**; if a firmware bug is found, describe
  it and ask.

## Workflow

```bash
tests/run_tests.sh shots
scripts/build.sh && scripts/pack-store-bundle.sh
scripts/check_hosts.sh
```

Update `README.md`, `CHANGELOG.md` and the relevant `docs/*.md` (including
the expected outputs and budgets they quote) whenever gameplay, judge,
audio, protocol or budgets change.
