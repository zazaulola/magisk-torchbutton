#!/system/bin/sh
# Magisk install-time customisation.
#
# We don't need to overlay anything into /system; service.sh launches the
# daemon from inside $MODPATH/bin/<abi>/. Just sanity-check that a binary
# for this device's ABI is present.

ui_print "- torchbutton: Power-long-press flashlight"

ABI=$(getprop ro.product.cpu.abi)
case "$ABI" in
    arm64-v8a|armeabi-v7a|x86_64|x86) : ;;
    *)
        ui_print "! unsupported ABI: $ABI"
        abort   "! aborting installation"
        ;;
esac

BIN="$MODPATH/bin/$ABI/torchd"
if [ ! -f "$BIN" ]; then
    ui_print "! no prebuilt binary for $ABI at $BIN"
    ui_print "! build src/torchd.c with the Android NDK (see README) and"
    ui_print "! re-package the module. Installing anyway — daemon will not start."
fi

set_perm_recursive "$MODPATH" 0 0 0755 0644
[ -f "$BIN" ] && set_perm "$BIN" 0 0 0755
set_perm "$MODPATH/service.sh"       0 0 0755
set_perm "$MODPATH/post-fs-data.sh"  0 0 0755
set_perm "$MODPATH/uninstall.sh"     0 0 0755

ui_print "- installed; reboot to start torchd"
