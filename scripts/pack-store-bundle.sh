#!/usr/bin/env bash
# Pack the Cartridge Store bundle from the built cartridges.
#
#   scripts/build.sh && scripts/pack-store-bundle.sh
#
# The bundle holds the esp32c6 and qemu variants. Its manifest is generated
# from metadata/metadata.json and metadata/colophon.json (single source of
# truth). The bundle is then validated with the CartridgeStore's own
# ingestion code when that repository is available (CARTRIDGE_STORE_REPO,
# default ../CartridgeStore; set STORE_PYTHON to an interpreter that has its
# requirements installed). Publishing is a separate, authenticated,
# human-controlled step: this script never contacts a Store.
set -euo pipefail
repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
prg32_repo="$(cd "${PRG32_REPO:-"$repo_dir/../PRG32"}" && pwd)"
name="drumfight-napoli97"
meta="$repo_dir/metadata/metadata.json"
version="$(python3 -c "import json;print(json.load(open('$meta'))['version'])")"
stage="$repo_dir/dist/store-bundle"
bundle="$repo_dir/dist/$name-$version-store.zip"

rm -rf "$stage"
mkdir -p "$stage"
cp "$repo_dir/assets/store/icon.png" "$stage/icon.png"
cp "$repo_dir/assets/store/screenshot.png" "$stage/splash.png"
for arch in esp32c6 qemu; do
  cart="$repo_dir/dist/$name-$arch.prg32"
  [[ -f "$cart" ]] || { echo "missing $cart; run scripts/build.sh first" >&2; exit 1; }
  cp "$cart" "$stage/"
done
python3 - "$repo_dir" "$stage/manifest.json" <<'PY'
import json, sys
from pathlib import Path
root, out = Path(sys.argv[1]), Path(sys.argv[2])
manifest = json.loads((root / "metadata/metadata.json").read_text(encoding="utf-8"))
# The Store ingests a prg32-metadata-1.0 object (CartridgeStore docs/api.md,
# "Bundle Publish"): it rebuilds each cartridge from this manifest, deriving
# runtime.architecture per variant and using assets.splash as the screenshot.
manifest.pop("runtime", None)
assert manifest["abi"] == "prg32-metadata-1.0"
manifest["colophon"] = json.loads((root / "metadata/colophon.json").read_text(encoding="utf-8"))
manifest["assets"] = {"icon": "icon.png", "splash": "splash.png"}
manifest["architectures"] = [
    {"id": "esp32c6", "file": "drumfight-napoli97-esp32c6.prg32"},
    {"id": "qemu", "file": "drumfight-napoli97-qemu.prg32"},
]
out.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
PY
# Reproducible ZIP: entries carry file timestamps, so pin them to the
# release date (metadata updated_at) instead of the build time.
stamp="$(python3 -c "import json;d=json.load(open('$meta'))['updated_at'];print(d[0:4]+d[5:7]+d[8:10]+d[11:13]+d[14:16])")"
find "$stage" -type f -exec touch -t "$stamp" {} +
rm -f "$bundle"
(cd "$prg32_repo" && python3 -m prg32 store pack-bundle --manifest "$stage/manifest.json" --out "$bundle") >/dev/null
"${STORE_PYTHON:-python3}" "$repo_dir/tools/validate_bundle.py" "$bundle"
(cd "$repo_dir/dist" && shasum -a 256 $name-*.prg32 $name-*-store.zip > SHA256SUMS)
echo "$bundle"
