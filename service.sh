#!/system/bin/sh
# Started by Magisk in late_start service mode (after `boot_completed`-ish).
#
#  1. Wait for boot_completed.
#  2. Install the helper APK if it isn't already on the device.
#  3. Pick the right ABI binary and source optional config.
#  4. Launch torchd in a watchdog loop so a crash never permanently steals
#     the Power key.

MODDIR=${0%/*}
LOG="/data/local/tmp/torchd.log"
PKG=me.nogrep.torchbutton

# Wait until system services are up.
i=0
while [ "$(getprop sys.boot_completed)" != "1" ] && [ $i -lt 120 ]; do
    sleep 1
    i=$((i + 1))
done

# Install / reinstall the companion APK. We compare the md5 of the bundled
# APK against the marker we wrote after the last install: if they differ,
# the module shipped a new APK and we update with `pm install -r`.
APK="$MODDIR/bin/TorchButton.apk"
APK_MARKER="$MODDIR/bin/.installed.md5"
if [ -f "$APK" ]; then
    INSTALLED=$(pm path "$PKG" 2>/dev/null | head -1)
    WANT_MD5=$(md5sum "$APK" 2>/dev/null | awk '{print $1}')
    HAVE_MD5=$(cat "$APK_MARKER" 2>/dev/null)
    NEED_INSTALL=0
    if [ -z "$INSTALLED" ]; then
        NEED_INSTALL=1
    elif [ "$WANT_MD5" != "$HAVE_MD5" ]; then
        NEED_INSTALL=1
    fi
    if [ "$NEED_INSTALL" = "1" ]; then
        echo "$(date) [service.sh] installing $PKG (md5=$WANT_MD5)" >> "$LOG"
        if pm install -r "$APK" >> "$LOG" 2>&1; then
            echo "$WANT_MD5" > "$APK_MARKER"
        else
            # Signature mismatch (different debug key) — try a clean install.
            # This wipes the app's enabled-flag file; user has to retoggle.
            echo "$(date) [service.sh] pm install -r failed; trying clean install" >> "$LOG"
            pm uninstall "$PKG" >> "$LOG" 2>&1
            pm install "$APK" >> "$LOG" 2>&1 && echo "$WANT_MD5" > "$APK_MARKER"
        fi
    fi
fi

# Optional user config.
if [ -f "$MODDIR/config.sh" ]; then
    # shellcheck disable=SC1091
    . "$MODDIR/config.sh"
fi
export TORCHD_LOG="$LOG"
export TORCHD_TORCH_PATH TORCHD_THRESHOLD_MS TORCHD_BRIGHTNESS \
       TORCHD_BACKEND TORCHD_PKG TORCHD_VERBOSE

ABI=$(getprop ro.product.cpu.abi)
case "$ABI" in
    arm64-v8a)   BIN="$MODDIR/bin/arm64-v8a/torchd"   ;;
    armeabi-v7a) BIN="$MODDIR/bin/armeabi-v7a/torchd" ;;
    x86_64)      BIN="$MODDIR/bin/x86_64/torchd"      ;;
    x86)         BIN="$MODDIR/bin/x86/torchd"         ;;
    *)
        echo "$(date) [service.sh] unsupported ABI: $ABI" >> "$LOG"
        exit 1
        ;;
esac

if [ ! -x "$BIN" ]; then
    echo "$(date) [service.sh] binary missing: $BIN" >> "$LOG"
    exit 1
fi

# Watchdog loop.
(
    while true; do
        echo "$(date) [service.sh] launching torchd ($BIN)" >> "$LOG"
        "$BIN" >> "$LOG" 2>&1
        rc=$?
        echo "$(date) [service.sh] torchd exited rc=$rc; restarting in 3s" >> "$LOG"
        sleep 3
    done
) &
