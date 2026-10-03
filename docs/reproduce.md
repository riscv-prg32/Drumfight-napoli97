# Reproducing the Drumfight Napoli 97 cartridges

This guide rebuilds, from a clean machine, the exact artefacts published in
`dist/`: two cartridges (ESP32-C6 and QEMU) and one Cartridge Store bundle,
and re-runs every check. The build is **deterministic**: with the same tool
versions it reproduces `dist/SHA256SUMS` byte for byte (verified by building
twice on 2026-10-03).

## 1. Reference environment

| Component | Version used for release 1.1.0 | Needed for |
|---|---|---|
| OS | macOS 26 (Apple Silicon); Linux should work | everything |
| PRG32 | `riscv-prg32/PRG32` `main` @ `a8669e5` (ABI hash `0x260f6136`) | builder, ABI headers, Store tools |
| RISC-V toolchain | `riscv32-esp-elf-gcc` esp-14.2.0_20241119 (GCC 14.2.0), installed by ESP-IDF v5.4 | building; a different compiler changes the code bytes |
| Python | 3.12 | build scripts |
| C compiler | Apple clang (any C99 compiler with ASan/UBSan) | host tests |
| Pillow | 11.3 | screenshots, store art, QEMU capture |
| Espressif QEMU | `qemu-riscv32` esp_develop_9.0.0_20240606 | QEMU runs only |
| ffmpeg | 9.0 | preview video only |
| CMake + Ninja | any recent | PRG32-QT headless check only |
| PRG32-QT | `riscv-prg32/PRG32-QT` @ `9f49443` | host check only |
| CartridgeStore | `riscv-prg32/CartridgeStore` @ `212e060` | bundle validation only |

Only PRG32, the toolchain and Python are needed to build the cartridges.

## 2. Directory layout

The repositories are siblings; each location can be overridden with an
environment variable.

```text
riscv-prg32/
├── Drumfight-napoli97/   this repository
├── PRG32/                PRG32_REPO
├── PRG32-QT/             PRG32_QT_REPO          (optional)
└── CartridgeStore/       CARTRIDGE_STORE_REPO   (optional)
```

```bash
mkdir -p riscv-prg32 && cd riscv-prg32
git clone https://github.com/riscv-prg32/Drumfight-napoli97
git clone https://github.com/riscv-prg32/PRG32
git -C PRG32 checkout a8669e5
git clone https://github.com/riscv-prg32/PRG32-QT          # optional
git clone https://github.com/riscv-prg32/CartridgeStore    # optional
```

## 3. Toolchain

Install ESP-IDF 5.4 for `esp32c3,esp32c6` and Espressif QEMU as described in
the PRG32 [getting-started guide](https://github.com/riscv-prg32/PRG32/blob/main/docs/usage/getting_started.md):

```bash
cd ~/esp-idf && ./install.sh esp32c3,esp32c6
python3 ~/esp-idf/tools/idf_tools.py install qemu-riscv32
python3 -m pip install --user Pillow
```

`scripts/build.sh` finds the compiler on `PATH`, else under
`~/.espressif/tools/riscv32-esp-elf/`, else it sources
`~/esp-idf/export.sh` (override with `IDF_EXPORT`).

> The ESP-IDF virtual environment's `python3` usually lacks Pillow. Run the
> tests with `shots`, `tools/make_store_art.py` and
> `scripts/qemu_preview.py` from a shell where ESP-IDF is **not** sourced.
> `scripts/build.sh` itself does not need Pillow.

## 4. Host tests

```bash
cd Drumfight-napoli97
tests/run_tests.sh
```

Expected: `test_core: 744 checks, 0 failures`,
`test_game: 231 checks, 0 failures`, `metadata: ... 0 failures`
(full output in [testing.md](testing.md)).

## 5. Build the cartridges

```bash
scripts/build.sh
```

The script:

1. regenerates the drum kits, `audio/*.raw` and `audio/audio.json`, from
   the formulas in `tools/build_audio.py` (committed; `git status audio`
   must stay clean);
2. packs the AUD0 block with PRG32's `tools/prg32audio_pack.py`;
3. proves position independence (`tools/check_relocatable.py`);
4. builds a **portable** cartridge per architecture (`--portable`, optional
   features `audio`, `audio_plus`, `multiplayer`, `--cart-ram-kib 32`);
5. attaches metadata, icon, screenshot and colophon;
6. enforces the budgets (package, executable RAM, load image) and writes summaries, metadata dumps and
   `dist/SHA256SUMS`.

Expected output (release 1.1.0):

```text
position independent: runs unchanged at any load address
[INFO] code=22476 mem=23168 audio=24804
dist/drumfight-napoli97-esp32c6.prg32: package 54496/65536, RAM 23168/32768, load ~47408/54500
[INFO] code=22476 mem=23168 audio=24804
dist/drumfight-napoli97-qemu.prg32: package 54494/65536, RAM 23168/32768, load ~47408/54500
```

Build one architecture with `scripts/build.sh qemu`.

## 6. Pack and validate the Store bundle

```bash
python3 -m venv ~/.venvs/cartridgestore
grep -v -i saml ../CartridgeStore/requirements.txt > /tmp/store-requirements.txt
~/.venvs/cartridgestore/bin/pip install -r /tmp/store-requirements.txt
STORE_PYTHON=~/.venvs/cartridgestore/bin/python scripts/pack-store-bundle.sh
```

(`python3-saml` needs system libraries and is not used by the ingestion
code, so it is skipped.) Expected:

```text
esp32c6  drumfight-napoli97-esp32c6.prg32    54392 bytes (+11144 free) OK [store-ingest]
qemu     drumfight-napoli97-qemu.prg32       54390 bytes (+11146 free) OK [store-ingest]
drumfight-napoli97-1.1.0-store.zip: READY for submission (store-ingest)
```

Without the optional checkout, plain `scripts/pack-store-bundle.sh` packs
the same ZIP and validates it structurally.

## 7. Verify the checksums

```bash
cd dist && shasum -a 256 -c SHA256SUMS
```

or compare with git: after steps 5 and 6, `git status --short dist` must be
empty. The cartridges are reproducible because the kit generator uses its
own fixed noise source and no library randomness, the compiler is pinned and the metadata timestamps are fixed;
the bundle because `pack-store-bundle.sh` pins the ZIP entry timestamps to
the release date (`updated_at`).

Release 1.1.0:

```text
5dbf201000f2c1b7c73175f3f239119161987dd44c99844e50af9b35a20a603f  drumfight-napoli97-esp32c6.prg32
724f2a11e61b13135485b964690e38fa939196ac8a9cdc477a7a1bb3f3e71cac  drumfight-napoli97-qemu.prg32
47d13a77034b68aae0a9ed22ab5cfe657ec7e4d8d9237c67ac231a12b10ef754  drumfight-napoli97-1.1.0-store.zip
```

## 8. Run on every host

```bash
scripts/check_hosts.sh
```

Expected: position independence, PRG32-QT `OK (300 frames)` for both
variants, QEMU `loaded cartridge 'drumfight-napoli97' ...`, and
`== result: PASS`. The QEMU firmware must exist in `../PRG32/build-qemu`
(`python3 -m prg32 qemu build` in the PRG32 checkout).

### Interactive QEMU

```bash
cd ../PRG32
python3 -m prg32 qemu upload ../Drumfight-napoli97/dist/drumfight-napoli97-qemu.prg32
python3 -m prg32 qemu run
```

Keys: `W` `A` `S` `D` joystick, `J` = A, `K` = B, Enter = START. The
firmware holds each key for 120 ms, so press the direction and the button
together for a pad, and tap the directions of a motion one after the other.
A held accent needs key repeat; on a real pad it is simply a longer press.

### ESP32-C6 board

Flash the PRG32 firmware (`python3 -m prg32 esp32c6 build-and-flash` in the
PRG32 checkout), join its Wi-Fi, then:

```bash
python3 -m prg32 esp32c6 upload ../Drumfight-napoli97/dist/drumfight-napoli97-esp32c6.prg32 --url http://192.168.4.1
```

Record the results against the acceptance list in [testing.md](testing.md).

## 9. Screenshots, store art and the 30-second preview

These are committed inputs of the build, not outputs: regenerate them only
when the visuals change.

```bash
tests/run_tests.sh shots
scripts/qemu_preview.py --script preview --video --out build/qemu-preview
python3 tools/make_store_art.py
ffmpeg -y -i build/qemu-preview/preview.mp4 -c:v libx264 -crf 26 -preset slow -pix_fmt yuv420p -c:a aac -b:a 96k assets/store/preview.mp4
python3 ../PRG32/tools/validate_cartridge_media.py assets/store/screenshot.png assets/store/preview.mp4
```

- `tests/run_tests.sh shots` renders every screen with the host layer
  (deterministic) into `build/shots/`; `docs/media/*.png` are those images
  doubled in size.
- `scripts/qemu_preview.py` runs the QEMU cartridge with a scripted player
  and records the firmware framebuffer (through QMP) and its audio stream.
  The frame `build/qemu-preview/groove.png` becomes
  `assets/store/screenshot.png`.
- QEMU timing varies between runs, so the screenshot and the video are not
  bit-reproducible. That is why they are committed and why the cartridge
  checksums are stable: rebuilding uses the committed files. After
  regenerating them, rebuild and repack; the checksums change.
- `python3 tools/make_store_art.py --host` uses the deterministic host
  render for the screenshot instead.
- `python3 tools/build_audio.py --demo build/kit-demo.wav` renders the
  ANALOG kit to a WAV for listening ([audio.md](audio.md)).
- `python3 tools/build_font.py` regenerates `tests/host/font8.h` from the
  PRG32 firmware font (only the host tests use it).

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `riscv32-esp-elf-gcc not found` | install ESP-IDF, or source `~/esp-idf/export.sh`, or set `IDF_EXPORT` |
| `ModuleNotFoundError: PIL` | run outside the ESP-IDF venv, or `pip install Pillow` in it |
| `unrecognized arguments: --cart-ram-kib` | PRG32 checkout older than the reference; update it |
| different checksums | a different compiler or PRG32 builder version; compare `build/*.summary.txt`. If `git status audio` shows changes, the Python floating-point results differ on this machine: use the committed `audio/` files |
| `error: load image too large for QEMU` | the drum kit grew; shorten a sample in `tools/build_audio.py` |
| `missing .../build-qemu/qemu_flash.bin` | build the QEMU firmware in the PRG32 checkout |
| QEMU shows the PRG32 setup screen | the flash image holds other cartridges; `scripts/qemu_preview.py` always uses a fresh copy with only this one |
| `Store ingestion code unavailable` | the Store's Python requirements are not installed in `STORE_PYTHON`; the weaker validation level is used |
