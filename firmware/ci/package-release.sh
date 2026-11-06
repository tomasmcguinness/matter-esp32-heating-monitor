#!/usr/bin/env bash
#
# Collects a finished build into the files a release carries:
#
#   heating-monitor.bin, bootloader.bin,     Each flash part, written at the offsets `idf.py flash`
#   partition-table.bin, ota_data_initial.bin uses. heating-monitor.bin alone is the OTA image.
#   manifest.json                            ESP Web Tools manifest listing those parts.
#   heating-monitor-<version>-merged.bin     One image for offset 0x0, for esptool.
#   SHA256SUMS
#
#   ci/package-release.sh <build dir> <out dir> <version>
#
# The parts and offsets come from <build dir>/flasher_args.json, so they follow partitions.csv.
#
# The manifest lists the parts separately rather than the merged image because the merged image
# fills the gaps between parts with 0xFF, and nvs (0x10000) lies in the gap between the partition
# table and the app. Writing it wipes every node, room and radiator. Written part by part, an
# install over an existing board keeps them unless the installer is asked to erase.

set -euo pipefail

BUILD=${1:?usage: $0 <build dir> <out dir> <version>}
OUT=${2:?usage: $0 <build dir> <out dir> <version>}
VERSION=${3:?usage: $0 <build dir> <out dir> <version>}

mkdir -p "$OUT"

python3 - "$BUILD" "$OUT" "$VERSION" <<'EOF'
import json, os, shutil, subprocess, sys

build, out, version = sys.argv[1:]
args = json.load(open(os.path.join(build, "flasher_args.json")))
chip = args["extra_esptool_args"]["chip"]
settings = args["flash_settings"]

# Keyed by offset in flasher_args.json; sort so the manifest and merge run low to high.
parts = sorted(((int(offset, 16), path) for offset, path in args["flash_files"].items()))

names = set()
manifest_parts = []
for offset, path in parts:
    name = os.path.basename(path)
    if name in names:
        sys.exit(f"Two flash parts are both called {name}")
    names.add(name)
    shutil.copyfile(os.path.join(build, path), os.path.join(out, name))
    manifest_parts.append({"path": name, "offset": offset})

families = {"esp32": "ESP32", "esp32s2": "ESP32-S2", "esp32s3": "ESP32-S3", "esp32c3": "ESP32-C3",
            "esp32c6": "ESP32-C6", "esp32h2": "ESP32-H2"}

manifest = {
    "name": "Heating Monitor",
    "version": version,
    "new_install_prompt_erase": True,
    "builds": [{"chipFamily": families[chip], "parts": manifest_parts}],
}
with open(os.path.join(out, "manifest.json"), "w") as f:
    json.dump(manifest, f, indent=2)
    f.write("\n")

merged = os.path.join(out, f"heating-monitor-{version}-merged.bin")
cmd = [sys.executable, "-m", "esptool", "--chip", chip, "merge_bin", "-o", merged,
       "--flash_mode", settings["flash_mode"], "--flash_freq", settings["flash_freq"],
       "--flash_size", settings["flash_size"]]
for offset, path in parts:
    cmd += [hex(offset), os.path.join(build, path)]
subprocess.run(cmd, check=True)

for offset, path in parts:
    print(f"  {offset:#08x}  {os.path.basename(path)}")
EOF

(cd "$OUT" && sha256sum -- *.bin manifest.json > SHA256SUMS)
ls -l "$OUT"
