# Testing

## Host tests (no RISC-V toolchain)

```bash
tests/run_tests.sh
```

Needs a C compiler and the PRG32 checkout for its public headers
(`PRG32_REPO`, default `../PRG32`). Tests are compiled with
`-Wall -Wextra -Werror` and AddressSanitizer/UBSan.

| Suite | Checks | What it covers |
|---|---|---|
| `tests/test_core.c` | 744 | Pattern model; pad map; judge reference values and bounds; syncopation; every move, its motion and threshold; CPU levels over 200 seeds each; network packing under loss, repetition and reordering; ranking |
| `tests/test_game.c` | 188 | The unmodified cartridge source against `tests/host/host_prg32.h`: title, practice (quantisation, no double trigger, accents, erase, tempo drift), unlocking and performing all eight moves, a full match against the CPU, a four-player pass-the-pad match, a three-round network battle against a simulated board, a peer that never delivers |
| metadata check | | Metadata, colophon and audio JSON parse; versions agree; the multiplayer signature in the metadata equals the one in the source |

The host layer also fails the run if any drawn text uses a character outside
the portable set or leaves the screen.

Expected output (abridged):

```text
judge: rock 174, piazza 791 (+75), noise 102
ai level 0: mean 313, range 273..366 (target 300)
ai level 1: mean 571, range 560..629 (target 560)
ai level 2: mean 842, range 797..875 (target 820)
test_core: 744 checks, 0 failures
practice: groove 758 + moves 200 = 958, unlocked ff, used ff
test_game: 188 checks, 0 failures
metadata: version 1.0.0, signature drumfight-napoli97-v1, 0 failures
```

`tests/run_tests.sh shots` also writes every screen to `build/shots/*.png`
(needs Pillow). The images in `docs/media/` come from there.

## Host checks (built cartridges)

```bash
scripts/check_hosts.sh
```

| Host | How | Expected |
|---|---|---|
| any load address | `tools/check_relocatable.py` links at two addresses | `position independent: runs unchanged at any load address` |
| PRG32-QT emulator core | Qt-free `prg32qt-headless`, 300 frames with scripted pads; executes the real RISC-V image | `OK (300 frames) MEDIA graphics_non_black=... audio_events=...` |
| PRG32 QEMU firmware | `scripts/qemu_preview.py --script smoke`: the firmware loads the cartridge, a scripted player drums and performs QUATTRO | `loaded cartridge 'drumfight-napoli97' (21812 bytes code, 22500 bytes memory, 104 bytes audio)` and no panic |

The QEMU run also saves screenshots and the firmware's audio stream in
`build/hosts/qemu/`.

## Verified for release 1.0.0 (2026-10-01)

- Host tests: all pass.
- Position independence: proven.
- PRG32-QT (`9f49443`): both variants run.
- PRG32 QEMU firmware (PRG32 `a8669e5`, ABI hash `0x260f6136`): loads and
  plays; the preview session reaches a groove of 871 with seven moves;
  audio peak -13 dBFS, no clipping.
- Store bundle: accepted by CartridgeStore's own ingestion code
  (`212e060`), see [store_publishing.md](store_publishing.md).
- Deterministic build: two consecutive builds give identical checksums.

## Not verified: hardware acceptance list

No physical board was available for this release. Before calling the
cartridge hardware-proven, check on an ESP32-C6:

- [ ] Frame rate while composing stays at 30 fps (the playhead redraw is two
      grid columns per step; a move repaints the grid once).
- [ ] The kit through a real speaker: balance of the eight voices, kick
      audible on a small speaker, hats not too dull. Tune
      `tools/build_audio.py` and `VOICE_NOTES` / `VOICE_GAIN`.
- [ ] Stereo (PRG32 Audio Plus): voices sit where [audio.md](audio.md) says;
      mono collapse sounds balanced.
- [ ] Pad feel: quantisation with a real joystick, accent hold time
      (`ACCENT_HOLD_MS`), motion window (`MOTION_GAP_MS`).
- [ ] Network battle with two, three and four boards through a
      MultiplayerServer: lobby, simultaneous composing, identical results on
      every board, a board leaving mid-match.
- [ ] Scoreboard: a groove appears under `drumfight`.
- [ ] PRG32-iOS: not run (no checkout); the cartridge requires no feature
      and uses only the portable character set, so it is expected to load.
