#!/system/bin/sh
# Started by Magisk in late_start service mode.
#
#  1. Wait for boot_completed.
#  2. Install/refresh the helper APK (md5-gated).
#  3. Source optional config, pick the ABI binary.
#  4. Run torchd under a watchdog that backs off on crashes AND stops itself
#     (releasing the Power-key grab) when the module is disabled or removed,
#     so the user never has to reboot to get their Power button back.

MODDIR=${0%/*}
PKG=me.nogrep.torchbutton

# Wait until system services are up.
i=0
while [ "$(getprop sys.boot_completed)" != "1" ] && [ $i -lt 120 ]; do
    sleep 1
    i=$((i + 1))
done

# --- config + logging -------------------------------------------------------
# Source config first so it can override TORCHD_LOG and friends.
if [ -f "$MODDIR/config.sh" ]; then
    # shellcheck disable=SC1091
    . "$MODDIR/config.sh"
fi
# Default log lives in root-only storage (not world-readable /data/local/tmp).
: "${TORCHD_LOG:=/data/adb/torchbutton.log}"
# Truncate at boot so it can't grow without bound across reboots.
: > "$TORCHD_LOG" 2>/dev/null || TORCHD_LOG=/data/local/tmp/torchd.log
export TORCHD_LOG
export TORCHD_TORCH_PATH TORCHD_THRESHOLD_MS TORCHD_BRIGHTNESS \
       TORCHD_BACKEND TORCHD_PKG TORCHD_VERBOSE \
       TORCHD_ENABLE_FILE TORCHD_TORCH_STATE_FILE

log() { echo "$(date) [service.sh] $*" >> "$TORCHD_LOG"; }

# --- install / refresh the companion APK ------------------------------------
# Compare the bundled APK's md5 against a marker; reinstall only on change.
# Run in the background so a slow/unready package service can't delay torchd
# (the daemon doesn't need the APK to start grabbing the Power key).
APK="$MODDIR/bin/TorchButton.apk"
APK_MARKER="$MODDIR/bin/.installed.md5"

pm_ready() {
    # Package service can lag behind sys.boot_completed; wait up to ~60s.
    n=0
    while [ $n -lt 60 ]; do
        cmd package list packages >/dev/null 2>&1 && return 0
        sleep 1; n=$((n + 1))
    done
    return 1
}

pm_install_retry() {  # $1 = "-r" or "", returns pm's success
    # Don't redirect pm/cmd output into $TORCHD_LOG: `cmd` runs as system_server
    # which can't write /data/adb (would spam AVC denials). Discard it.
    i=0
    while [ $i -lt 5 ]; do
        pm install $1 "$APK" >/dev/null 2>&1 && return 0
        i=$((i + 1)); sleep 2
    done
    return 1
}

install_apk() {
    [ -f "$APK" ] || return 0
    WANT_MD5=$(md5sum "$APK" 2>/dev/null | awk '{print $1}')
    HAVE_MD5=$(cat "$APK_MARKER" 2>/dev/null)
    INSTALLED=$(pm path "$PKG" 2>/dev/null | head -1)
    [ -n "$INSTALLED" ] && [ "$WANT_MD5" = "$HAVE_MD5" ] && return 0

    pm_ready || { log "package service not ready; skipping APK install this boot"; return 1; }

    log "installing $PKG (md5=$WANT_MD5)"
    if pm_install_retry "-r"; then
        echo "$WANT_MD5" > "$APK_MARKER"
        return 0
    fi
    # Signature mismatch (different signing key). Preserve the user's enable
    # flag across the destructive reinstall (DE storage, root-readable).
    log "pm install -r failed; clean reinstall (preserving enable flag)"
    EN=$(cat "/data/user_de/0/$PKG/files/enabled" 2>/dev/null)
    pm uninstall "$PKG" >/dev/null 2>&1
    if pm_install_retry ""; then
        echo "$WANT_MD5" > "$APK_MARKER"
        if [ -n "$EN" ]; then
            D="/data/user_de/0/$PKG/files"
            mkdir -p "$D" 2>/dev/null
            printf '%s' "$EN" > "$D/enabled" 2>/dev/null && chmod 644 "$D/enabled" 2>/dev/null
        fi
    fi
}

install_apk &

# --- pick the ABI binary ----------------------------------------------------
ABI=$(getprop ro.product.cpu.abi)
case "$ABI" in
    arm64-v8a)   BIN="$MODDIR/bin/arm64-v8a/torchd"   ;;
    armeabi-v7a) BIN="$MODDIR/bin/armeabi-v7a/torchd" ;;
    x86_64)      BIN="$MODDIR/bin/x86_64/torchd"      ;;
    x86)         BIN="$MODDIR/bin/x86/torchd"         ;;
    *) log "unsupported ABI: $ABI"; exit 1 ;;
esac
[ -x "$BIN" ] || { log "binary missing: $BIN"; exit 1; }

# --- watchdog ---------------------------------------------------------------
(
    delay=3
    while true; do
        # Stop (and release the input grab) if the module was disabled/removed.
        if [ -f "$MODDIR/disable" ] || [ -f "$MODDIR/remove" ]; then
            pkill -KILL -x torchd 2>/dev/null
            log "module disabled/removed — watchdog stopping"
            exit 0
        fi

        log "launching torchd ($BIN)"
        start=$(date +%s 2>/dev/null || echo 0)
        "$BIN" >> "$TORCHD_LOG" 2>&1 &
        child=$!

        # Supervise: while torchd runs, watch for disable/remove and kill it.
        while kill -0 "$child" 2>/dev/null; do
            if [ -f "$MODDIR/disable" ] || [ -f "$MODDIR/remove" ]; then
                kill -TERM "$child" 2>/dev/null
                sleep 1
                kill -KILL "$child" 2>/dev/null
                log "module disabled/removed — stopped torchd"
                exit 0
            fi
            sleep 2
        done

        # torchd exited on its own — back off, but reset after a healthy run.
        end=$(date +%s 2>/dev/null || echo 0)
        if [ "$((end - start))" -ge 30 ]; then
            delay=3
        else
            delay=$((delay * 2)); [ "$delay" -gt 60 ] && delay=60
        fi
        log "torchd exited; restarting in ${delay}s"
        sleep "$delay"
    done
) &
