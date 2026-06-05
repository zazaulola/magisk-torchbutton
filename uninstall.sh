#!/system/bin/sh
# Called by Magisk on module removal:
#   1. Kill the daemon so /dev/input is freed.
#   2. Uninstall the helper APK so we don't leave it orphaned.

pkill -TERM -f '/bin/.*/torchd' 2>/dev/null
sleep 1
pkill -KILL -f '/bin/.*/torchd' 2>/dev/null

pm uninstall me.nogrep.torchbutton >/dev/null 2>&1

exit 0
