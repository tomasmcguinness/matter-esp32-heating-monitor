#!/usr/bin/env bash
#
# Reproduces, in a clean ESP-IDF environment, the parts of the local build tree that live outside
# this repository: esp-matter and connectedhomeip at the commits in versions.env with
# patches/esp-matter/ applied, and the lwIP fix in patches/lwip/ applied to ESP-IDF's lwIP.
#
#   ci/setup-toolchain.sh esp-matter <dir>   Clone, patch and bootstrap esp-matter into <dir>.
#                                            Slow (pigweed bootstrap), so CI caches <dir>.
#   ci/setup-toolchain.sh esp-idf            Patch $IDF_PATH. Idempotent: a patch already
#                                            applied is left alone.
#
# Run with ESP-IDF's export.sh already sourced. Written for CI and the espressif/idf container;
# a developer's own trees should be set up by hand, not with this.

set -euo pipefail

CI_DIR=$(cd "$(dirname "$0")" && pwd)
# shellcheck source=versions.env
source "$CI_DIR/versions.env"

# Fetch one commit with no history. GitHub serves any commit reachable from a branch this way.
fetch_commit() {
    local dir=$1 repo=$2 commit=$3
    mkdir -p "$dir"
    git -C "$dir" init -q
    git -C "$dir" remote add origin "$repo" 2>/dev/null || git -C "$dir" remote set-url origin "$repo"
    git -C "$dir" fetch -q --depth 1 origin "$commit"
    git -C "$dir" -c advice.detachedHead=false checkout -q FETCH_HEAD
}

apply_patches() {
    local dir=$1 patches=$2
    for p in "$patches"/*.patch; do
        [ -e "$p" ] || continue
        if git -C "$dir" apply --reverse --check "$p" 2>/dev/null; then
            echo "Already applied: $(basename "$p")"
        else
            echo "Applying: $(basename "$p")"
            git -C "$dir" apply "$p"
        fi
    done
}

setup_esp_matter() {
    local dir=$1
    : "${IDF_PATH:?source ESP-IDF export.sh first}"

    if [ -e "$dir" ]; then
        echo "$dir already exists" >&2
        exit 1
    fi

    echo "esp-matter $ESP_MATTER_COMMIT"
    fetch_commit "$dir" "$ESP_MATTER_REPO" "$ESP_MATTER_COMMIT"
    apply_patches "$dir" "$CI_DIR/patches/esp-matter"

    local chip="$dir/connectedhomeip/connectedhomeip"
    echo "connectedhomeip $CHIP_COMMIT"
    rmdir "$chip" 2>/dev/null || true
    fetch_commit "$chip" "$CHIP_REPO" "$CHIP_COMMIT"
    (cd "$chip" && python3 scripts/checkout_submodules.py --platform esp32 --shallow)

    # chip-tool and chip-cert are host tools; the firmware build does not need them.
    (cd "$dir" && ./install.sh --no-host-tool)

    prune_environment "$chip/.environment"
}

# The bootstrap installs tools for every platform CHIP supports: about 5 GB, which is too much to
# cache. An ESP32 build compiles with ESP-IDF's Xtensa GCC, and needs only gn, ninja and the Python
# environment from here, so the ARM GCC, host LLVM/Clang and QEMU go.
prune_environment() {
    local env=$1 pw="$1/cipd/packages/pigweed"
    rm -rf "$env/cipd/cache" "$env/cipd/packages/arm"
    rm -rf "$pw/clang_sysroot" "$pw/lib/clang" "$pw/lib/debug" "$pw"/lib/*-fuchsia
    find "$pw/bin" -maxdepth 1 \( -name 'llvm*' -o -name 'clang*' -o -name 'qemu-*' -o -name 'lld*' \
        -o -name 'ld.lld' -o -name 'find-all-symbols' -o -name 'modularize' -o -name 'pp-trace' \) -delete
    du -sh "$env"
}

setup_esp_idf() {
    : "${IDF_PATH:?source ESP-IDF export.sh first}"
    # lwIP is a submodule of ESP-IDF, so its patches apply inside it.
    apply_patches "$IDF_PATH/components/lwip/lwip" "$CI_DIR/patches/lwip"
}

case "${1:-}" in
    esp-matter) setup_esp_matter "${2:?usage: $0 esp-matter <dir>}" ;;
    esp-idf)    setup_esp_idf ;;
    *)          echo "usage: $0 esp-matter <dir> | esp-idf" >&2; exit 2 ;;
esac
