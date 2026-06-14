#!/usr/bin/env bash
# Build a minimal torchbutton helper APK without Gradle.
#
# Pipeline:
#     javac → .class
#     d8    → classes.dex
#     aapt2 compile + link → unsigned APK
#     apksigner sign (debug keystore, auto-created)
#
# Requires: Android cmdline-tools (aapt2, d8, apksigner) and a JDK.
#
# Output: ../dist/bin/TorchButton.apk
# Intermediates: ../dist/.apk-build/

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
OUT_DIR="$ROOT/dist/bin"
mkdir -p "$OUT_DIR"

# ---- Resolve SDK locations ------------------------------------------------
SDK_ROOT="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}"
if [[ -z "$SDK_ROOT" ]]; then
    for candidate in \
        "/opt/homebrew/share/android-commandlinetools" \
        "$HOME/Library/Android/sdk" \
        "$HOME/Android/Sdk"
    do
        [[ -d "$candidate/build-tools" ]] && SDK_ROOT="$candidate" && break
    done
fi
[[ -d "$SDK_ROOT/build-tools" ]] || { echo "ERROR: Android SDK not found" >&2; exit 1; }

BUILD_TOOLS_DIR="$(ls -1 "$SDK_ROOT/build-tools" | sort -V | tail -1)"
BT="$SDK_ROOT/build-tools/$BUILD_TOOLS_DIR"
PLATFORM_DIR="$(ls -1 "$SDK_ROOT/platforms" | sort -V | tail -1)"
ANDROID_JAR="$SDK_ROOT/platforms/$PLATFORM_DIR/android.jar"
[[ -f "$ANDROID_JAR" ]] || { echo "ERROR: no $ANDROID_JAR" >&2; exit 1; }

AAPT2="$BT/aapt2"
D8="$BT/d8"
APKSIGNER="$BT/apksigner"
ZIPALIGN="$BT/zipalign"
for t in "$AAPT2" "$D8" "$APKSIGNER" "$ZIPALIGN"; do
    [[ -x "$t" ]] || { echo "ERROR: missing $t" >&2; exit 1; }
done

echo "SDK         : $SDK_ROOT"
echo "build-tools : $BUILD_TOOLS_DIR"
echo "platform    : $PLATFORM_DIR"

# ---- Workspace -------------------------------------------------------------
WORK="$ROOT/dist/.apk-build"
rm -rf "$WORK"
mkdir -p "$WORK/classes" "$WORK/dex" "$WORK/compiled-res" "$WORK/gen"

# ---- Compile resources (drawables, etc.) -----------------------------------
echo "==> aapt2 compile (resources)"
RES_DIR="$HERE/res"
if [[ -d "$RES_DIR" ]]; then
    "$AAPT2" compile \
        --dir "$RES_DIR" \
        -o "$WORK/compiled-res"
fi

# ---- Link APK (and generate R.java) ---------------------------------------
echo "==> aapt2 link (R.java + skeleton APK)"
UNSIGNED="$WORK/unsigned.apk"
RES_FLATS=()
if [[ -d "$WORK/compiled-res" ]]; then
    while IFS= read -r f; do
        RES_FLATS+=("$f")
    done < <(find "$WORK/compiled-res" -type f -name '*.flat')
fi
# Single source of truth for the version: derive from module.prop so the APK
# and the Magisk module never drift. (versionName = module 'version' minus 'v'.)
MODULE_PROP="$(cd "$HERE/.." && pwd)/module.prop"
VCODE=$(awk -F= '$1=="versionCode"{print $2}' "$MODULE_PROP")
VNAME=$(awk -F= '$1=="version"{print $2}' "$MODULE_PROP" | sed 's/^v//')
: "${VCODE:=1}"; : "${VNAME:=1.0}"
echo "version     : $VNAME (code $VCODE) from module.prop"

"$AAPT2" link \
    -o "$UNSIGNED" \
    --manifest "$HERE/AndroidManifest.xml" \
    -I "$ANDROID_JAR" \
    --min-sdk-version 24 \
    --target-sdk-version 34 \
    --version-code "$VCODE" \
    --version-name "$VNAME" \
    --java "$WORK/gen" \
    "${RES_FLATS[@]}"

# ---- Compile Java (now with generated R.java) -----------------------------
echo "==> javac"
SRC_LIST=$(mktemp)
{ find "$HERE/src" -name '*.java'; find "$WORK/gen" -name '*.java'; } > "$SRC_LIST"
javac -source 1.8 -target 1.8 \
    -bootclasspath "$ANDROID_JAR" \
    -d "$WORK/classes" \
    @"$SRC_LIST"
rm -f "$SRC_LIST"

# ---- Dex classes -----------------------------------------------------------
echo "==> d8"
"$D8" --release \
    --lib "$ANDROID_JAR" \
    --min-api 24 \
    --output "$WORK/dex" \
    $(find "$WORK/classes" -name '*.class')

# Drop classes.dex into the APK (aapt2 link doesn't bundle it for us).
( cd "$WORK/dex" && zip -j -q "$UNSIGNED" classes.dex )

# ---- Zipalign --------------------------------------------------------------
ALIGNED="$WORK/aligned.apk"
"$ZIPALIGN" -f 4 "$UNSIGNED" "$ALIGNED"

# ---- Sign ------------------------------------------------------------------
KEYSTORE="$HERE/.debug.keystore"
if [[ ! -f "$KEYSTORE" ]]; then
    echo "==> generating debug keystore"
    keytool -genkeypair -keystore "$KEYSTORE" -alias torchbutton \
        -storepass android -keypass android -validity 10000 \
        -keyalg RSA -keysize 2048 \
        -dname "CN=torchbutton, O=local, C=XX" 2>/dev/null
fi

OUT_APK="$OUT_DIR/TorchButton.apk"
echo "==> apksigner sign"
"$APKSIGNER" sign \
    --ks "$KEYSTORE" \
    --ks-pass pass:android \
    --key-pass pass:android \
    --ks-key-alias torchbutton \
    --out "$OUT_APK" \
    "$ALIGNED"

echo "==> verifying"
"$APKSIGNER" verify "$OUT_APK" && echo "OK: $OUT_APK"

# .idsig is for Incremental Install; the Magisk module doesn't need it.
rm -f "$OUT_APK.idsig"

ls -la "$OUT_APK"
