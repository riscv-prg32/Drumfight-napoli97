#!/usr/bin/env bash
# Run the built cartridges unchanged on every PRG32 host that can be driven
# headlessly from a workstation:
#
#   1. position independence (tools/check_relocatable.py)
#   2. PRG32-QT emulator core   (prg32qt-headless, Qt-free build)
#   3. PRG32 QEMU firmware      (scripts/qemu_preview.py --script smoke)
#
# Environment (each host is skipped when its checkout/tool is missing):
#   PRG32_REPO      PRG32 checkout with build-qemu/ (default ../PRG32)
#   PRG32_QT_REPO   PRG32-QT checkout (default ../PRG32-QT)
# The physical ESP32-C6 is checked by hand (docs/testing.md).
#
# Adapted from scripts/check_hosts.sh of Galleria2007 (PRG32 project family,
# MIT).
set -uo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
prg32="${PRG32_REPO:-$repo/../PRG32}"
qt="${PRG32_QT_REPO:-$repo/../PRG32-QT}"
out="$repo/build/hosts"
mkdir -p "$out"
fail=0
# The RISC-V toolchain without the ESP-IDF Python venv (which lacks Pillow).
if ! command -v riscv32-esp-elf-gcc >/dev/null; then
  tc="$(ls -d "$HOME"/.espressif/tools/riscv32-esp-elf/*/riscv32-esp-elf/bin 2>/dev/null | tail -1)"
  [[ -n "$tc" ]] && export PATH="$tc:$PATH"
fi
carts=("$repo"/dist/drumfight-napoli97-esp32c6.prg32 "$repo"/dist/drumfight-napoli97-qemu.prg32)

echo "== position independence"
PRG32_REPO="$prg32" python3 "$repo/tools/check_relocatable.py" > "$out/reloc.log" 2>&1 \
  && tail -1 "$out/reloc.log" || { echo "FAILED"; tail -3 "$out/reloc.log"; fail=1; }

echo "== PRG32-QT"
if [[ -d "$qt" ]] && command -v cmake >/dev/null; then
  echo "PRG32-QT $(git -C "$qt" rev-parse --short HEAD 2>/dev/null)"
  rm -rf "$out/qt-core"          # never reuse a cache configured for another checkout
  cmake -S "$qt" -B "$out/qt-core" -G Ninja -DPRG32QT_BUILD_APP=OFF >/dev/null && cmake --build "$out/qt-core" >/dev/null || fail=1
  # Title -> A (practice), then pads on the beat (masks: A=16, B=32, UP=4).
  qtargs=(--input 30:16 --input 32:0)
  for bar in 0 1 2; do
    base=$(( 60 + bar * 64 ))
    qtargs+=(--input "$base:16" --input "$(( base + 2 )):0" --input "$(( base + 16 )):32" --input "$(( base + 18 )):0"
             --input "$(( base + 32 )):20" --input "$(( base + 34 )):0" --input "$(( base + 48 )):36" --input "$(( base + 50 )):0")
  done
  for c in "${carts[@]}"; do
    "$out/qt-core/prg32qt-headless" "$c" 300 --verify-media "${qtargs[@]}" \
      --dump-ppm "$out/qt-$(basename "$c" .prg32).ppm" | tr '\n' ' ' || fail=1
    echo
  done
else
  echo "skipped (no $qt or no cmake)"
fi

echo "== PRG32 QEMU firmware"
if [[ -f "$prg32/build-qemu/qemu_flash.bin" ]] && python3 -c "import PIL" 2>/dev/null; then
  c="$repo/dist/drumfight-napoli97-qemu.prg32"
  PRG32_REPO="$prg32" python3 "$repo/scripts/qemu_preview.py" --script smoke --cart "$c" \
    --out "$out/qemu" > "$out/qemu.log" 2>&1 || tail -3 "$out/qemu.log"
  grep -a "loaded cartridge" "$out/qemu/console.log" || { echo "$c: not loaded"; fail=1; }
  if grep -a -i -E "guru meditation|panic|abort\(\)" "$out/qemu/console.log"; then fail=1; fi
else
  echo "skipped (needs $prg32/build-qemu and Pillow in python3; run without the ESP-IDF venv)"
fi
echo "== result: $([[ $fail == 0 ]] && echo PASS || echo FAIL)"
exit $fail
