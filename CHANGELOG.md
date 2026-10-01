# Changelog

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
