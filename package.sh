#!/usr/bin/env bash
# Package the module into a Magisk-installable zip.
# Reads built artifacts from dist/bin/ (produced by src/build.sh and apk/build.sh)
# and writes dist/torchbutton-v<version>.zip.

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
NAME="torchbutton"
VERSION="$(awk -F= '$1=="version"{print $2}' "$HERE/module.prop")"
DIST="$HERE/dist"
OUT="$DIST/${NAME}-${VERSION}.zip"

mkdir -p "$DIST"

if ! ls "$DIST"/bin/*/torchd >/dev/null 2>&1; then
    echo "WARN: no built native binaries in dist/bin/. Run src/build.sh first." >&2
fi
if [[ ! -f "$DIST/bin/TorchButton.apk" ]]; then
    echo "WARN: no built APK in dist/bin/. Run apk/build.sh first." >&2
fi

rm -f "$OUT"

# Assert the APK was built from this module.prop version (single source of truth).
if [[ -f "$DIST/bin/TorchButton.apk" ]]; then
    BT_DIR="$(ls -d /opt/homebrew/share/android-commandlinetools/build-tools/* "$HOME"/Library/Android/sdk/build-tools/* 2>/dev/null | sort -V | tail -1 || true)"
    if [[ -x "$BT_DIR/aapt" ]]; then
        APK_BADGING=$("$BT_DIR/aapt" dump badging "$DIST/bin/TorchButton.apk" 2>/dev/null) || true
        APK_VC=$(printf '%s\n' "$APK_BADGING" | awk -F"'" '/^package:/{print $4; exit}')
        WANT_VC=$(awk -F= '$1=="versionCode"{print $2}' "$HERE/module.prop")
        if [[ -n "$APK_VC" && "$APK_VC" != "$WANT_VC" ]]; then
            echo "ERROR: APK versionCode ($APK_VC) != module.prop versionCode ($WANT_VC)." >&2
            echo "       Rebuild the APK (./apk/build.sh) so versions stay in sync." >&2
            exit 1
        fi
    fi
fi

# Magisk modules just need the files at the top of the zip — no META-INF needed
# when installing through Magisk Manager (it injects its own installer).
# `-X` strips extra file attributes for a more reproducible archive.
#
# Two-step zip so binaries from dist/bin/ land at bin/... inside the archive:
#   1. add the root-level scripts, module.prop, and sepolicy.rule;
#   2. cd into dist/ and add bin/ — that strips the dist/ prefix.

( cd "$HERE" && zip -r9 -X "$OUT" \
        module.prop \
        sepolicy.rule \
        customize.sh \
        service.sh \
        post-fs-data.sh \
        uninstall.sh \
        config.sh \
        -x '*.DS_Store' '*/.*' )

( cd "$DIST" && zip -r9 -X "$OUT" bin \
        -x '*.DS_Store' '*/.*' '*/.apk-build/*' '*/.obj/*' )

# Checksum next to the zip for release integrity.
( cd "$DIST" && shasum -a 256 "$(basename "$OUT")" > "$(basename "$OUT").sha256" )

echo "Packaged: $OUT"
ls -la "$OUT" "$OUT.sha256"
