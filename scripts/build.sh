#!/usr/bin/env bash
# Build the Drumfight Napoli 97 cartridges for every PRG32 target.
#
#   scripts/build.sh            # esp32c6 + qemu
#   scripts/build.sh qemu       # one architecture
#
# Environment:
#   PRG32_REPO   path to the PRG32 repository (default ../PRG32)
#   IDF_EXPORT   ESP-IDF export script, sourced when riscv32-esp-elf-gcc is
#                neither on PATH nor under ~/.espressif (default
#                $HOME/esp-idf/export.sh)
#
# Outputs dist/drumfight-napoli97-<arch>.prg32 (Store-ready: metadata, icon,
# screenshot and colophon attached), dist/SHA256SUMS, and prints the budgets.
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
prg32_repo="$(cd "${PRG32_REPO:-"$repo_dir/../PRG32"}" && pwd)"
name="drumfight-napoli97"
cart_ram_kib=32          # fits the PRG32 classroom profile (and the 64 KiB default)
package_limit=65536      # one cartridge slot: code + audio + Store trailer
load_limit=54500         # measured QEMU limit for header + code/data + audio

if ! command -v riscv32-esp-elf-gcc >/dev/null 2>&1; then
  # The toolchain alone is enough; prefer it to the whole ESP-IDF environment.
  toolchain="$(ls -d "$HOME"/.espressif/tools/riscv32-esp-elf/*/riscv32-esp-elf/bin 2>/dev/null | tail -1 || true)"
  if [[ -n "$toolchain" ]]; then
    export PATH="$toolchain:$PATH"
  else
    idf_export="${IDF_EXPORT:-$HOME/esp-idf/export.sh}"
    # shellcheck disable=SC1090
    [[ -f "$idf_export" ]] && source "$idf_export" >/dev/null 2>&1
  fi
fi
command -v riscv32-esp-elf-gcc >/dev/null 2>&1 || {
  echo "error: riscv32-esp-elf-gcc not found; install ESP-IDF or source its export.sh" >&2
  exit 2
}

archs=("$@")
[[ ${#archs[@]} -eq 0 ]] && archs=(esp32c6 qemu)

build_dir="$repo_dir/build"
dist_dir="$repo_dir/dist"
mkdir -p "$build_dir" "$dist_dir"

prg32() { (cd "$prg32_repo" && python3 -m prg32 "$@"); }

# 1. Regenerate the drum kits (deterministic; the results are committed).
python3 "$repo_dir/tools/build_audio.py"

# 2. AUDIO block: eight PCM one-shots, sixteen instruments, no tracks.
python3 "$prg32_repo/tools/prg32audio_pack.py" "$repo_dir/audio/audio.json" \
  --out "$build_dir/audio.block"

# 3. Prove position independence (no relocations: hosts load anywhere).
PRG32_REPO="$prg32_repo" python3 "$repo_dir/tools/check_relocatable.py" | tail -1

for arch in "${archs[@]}"; do
  raw="$build_dir/$name-$arch.raw.prg32"
  out="$dist_dir/$name-$arch.prg32"
  log="$build_dir/$name-$arch.build.log"
  # 4. Portable cartridge. Nothing is *required*: audio, stereo and
  # multiplayer are optional features, so the same image also runs on hosts
  # that lack them (the game checks the advertised features at run time).
  prg32 cartridge build "$repo_dir/src/drumfight.c" \
    --portable \
    --optional-feature audio \
    --optional-feature audio_plus \
    --optional-feature multiplayer \
    --entry-prefix drumfight \
    --name "$name" \
    --architecture "$arch" \
    --cart-ram-kib "$cart_ram_kib" \
    --audio-block "$build_dir/audio.block" \
    --out "$raw" | tee "$log" | grep -E "code=|error" || true
  sizes="$(grep -o 'code=[0-9]* mem=[0-9]* audio=[0-9]*' "$log" | tail -1)"
  code="$(sed -E 's/code=([0-9]+).*/\1/' <<< "$sizes")"
  mem="$(sed -E 's/.*mem=([0-9]+).*/\1/' <<< "$sizes")"
  audio="$(sed -E 's/.*audio=([0-9]+)/\1/' <<< "$sizes")"
  load=$(( code + audio + 128 ))

  # 5. Store trailer: metadata, icon, screenshot and colophon.
  prg32 store attach-metadata "$raw" --out "$out" \
    --metadata "$repo_dir/metadata/metadata.json" \
    --icon "$repo_dir/assets/store/icon.png" \
    --screenshot "$repo_dir/assets/store/screenshot.png" \
    --colophon "$repo_dir/metadata/colophon.json" \
    --architecture "$arch" >/dev/null

  size=$(wc -c < "$out" | tr -d ' ')
  echo "$out: package $size/$package_limit, RAM $mem/$(( cart_ram_kib * 1024 )), load ~$load/$load_limit"
  (( size <= package_limit )) || { echo "error: package too large" >&2; exit 1; }
  (( mem <= cart_ram_kib * 1024 )) || { echo "error: executable RAM exceeded" >&2; exit 1; }
  (( load <= load_limit )) || { echo "error: load image too large for QEMU" >&2; exit 1; }
done

# 6. Inspect what was produced.
for f in "$dist_dir"/$name-*.prg32; do
  prg32 cartridge summary "$f" > "$build_dir/$(basename "$f").summary.txt"
  prg32 store inspect-metadata "$f" > "$build_dir/$(basename "$f").metadata.txt"
done
(cd "$dist_dir" && shasum -a 256 $name-*.prg32 > SHA256SUMS)
echo "summaries in build/*.summary.txt; checksums in dist/SHA256SUMS"
