# Changelog

## 1.1.0 - 2026-10-03

The audio was redone: 1.0.0 did not sound or feel like a drum machine.

- New default kit, ANALOG: eight PCM one-shots computed from formulas on
  the model of the classic analogue drum machines (swept-sine kick,
  shell-and-wires snare, six-oscillator metallic hats, burst clap, cowbell,
  tammorra with jingles, bongo). No recordings. The 1.0.0 synthesizer kit
  remains selectable as SID.
- The closed hat chokes the open hat.
- Frame-locked clock: the tempo is a whole number of frames per sixteenth
  (76, 91, 114, 152 BPM), so steps are exactly even. 1.0.0 fired steps up to
  a frame late, unevenly.
- Swing (off sixteenths one frame late), in practice and in round 3.
- Rounds are now 91 BPM, 114 BPM and 114 BPM with swing.
- START menu: tempo, swing, kit.
- `tools/build_audio.py --demo` renders the kit to a WAV for listening.
- Budgets: package 54,496 bytes (was 28,815), load image about 47.4 KB.
- Multiplayer signature unchanged: patterns, judge and moves are the same.

## 1.0.0 - 2026-10-01

First release.

- Eight-voice, sixteen-step drum machine on the PRG32 SID-like stereo
  synthesizer; live recording with joystick + A/B pads, held accents,
  erase and wipe.
- Groove judge with six criteria and a 0-999 total.
- Eight special drum moves unlocked by groove and performed with joystick
  motions.
- Modes: practice, VS CPU (three opponents), pass-the-pad for 2-4 players,
  network battle for up to four boards (multiplayer signature
  `drumfight-napoli97-v1`).
- Portable cartridges for ESP32-C6 and QEMU, Cartridge Store bundle, host
  tests, QEMU capture tooling and documentation.
- Verified on host tests, PRG32-QT and the QEMU firmware; not yet on a
  physical ESP32-C6 (see `docs/testing.md`).
