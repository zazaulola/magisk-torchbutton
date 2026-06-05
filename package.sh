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

# Magisk modules just need the files at the top of the zip — no META-INF needed
# when installing through Magisk Manager (it injects its own installer).
#
# Two-step zip so binaries from dist/bin/ land at bin/... inside the archive:
#   1. add the root-level scripts and module.prop;
#   2. cd into dist/ and add bin/ — that strips the dist/ prefix.

( cd "$HERE" && zip -r9 "$OUT" \
        module.prop \
        customize.sh \
        service.sh \
        post-fs-data.sh \
        uninstall.sh \
        config.sh \
        -x '*.DS_Store' '*/.*' )

( cd "$DIST" && zip -r9 "$OUT" bin \
        -x '*.DS_Store' '*/.*' '*/.apk-build/*' '*/.obj/*' )

echo "Packaged: $OUT"
ls -la "$OUT"
