#!/usr/bin/env bash
# Cross-compile torchd for all supported ABIs using the Android NDK.
#
# Requires: Android NDK (r21+ recommended).
# Usage:    ANDROID_NDK_HOME=/path/to/ndk ./build.sh
#
# Output:   ../dist/bin/<abi>/torchd
# Intermediates: ../dist/.obj/

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
OUT="$ROOT/dist/bin"
OBJ="$ROOT/dist/.obj"

if [[ -z "${ANDROID_NDK_HOME:-}" && -z "${NDK:-}" ]]; then
    # Try common install locations.
    for candidate in \
        "$HOME/Library/Android/sdk/ndk-bundle" \
        "$HOME/Library/Android/sdk/ndk/"* \
        "$HOME/Android/Sdk/ndk-bundle" \
        "$HOME/Android/Sdk/ndk/"* \
        "/opt/android-ndk"
    do
        if [[ -x "$candidate/ndk-build" ]]; then
            ANDROID_NDK_HOME="$candidate"
            break
        fi
    done
fi
NDK="${NDK:-${ANDROID_NDK_HOME:-}}"
if [[ -z "$NDK" || ! -x "$NDK/ndk-build" ]]; then
    echo "ERROR: Android NDK not found." >&2
    echo "Set ANDROID_NDK_HOME (or NDK) to an NDK install with ndk-build." >&2
    exit 1
fi
echo "Using NDK: $NDK"

mkdir -p "$OUT" "$OBJ"

"$NDK/ndk-build" \
    NDK_PROJECT_PATH="$HERE/.." \
    NDK_APPLICATION_MK="$HERE/Application.mk" \
    APP_BUILD_SCRIPT="$HERE/Android.mk" \
    NDK_LIBS_OUT="$OUT" \
    NDK_OUT="$OBJ" \
    -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"

for abi_dir in "$OUT"/*; do
    [[ -d "$abi_dir" ]] || continue
    abi="$(basename "$abi_dir")"
    if [[ -f "$abi_dir/torchd" ]]; then
        echo "  built dist/bin/$abi/torchd"
    fi
done

echo "Done. Binaries in: $OUT"
