#!/system/bin/sh
# Called by Magisk on module removal:
#   1. Stop the watchdog and the daemon so /dev/input is freed immediately
#      (the watchdog also self-exits on the `remove` marker, but kill here too).
#   2. Uninstall the helper APK so it isn't left orphaned.

# Kill the daemon by exact process name (its comm is "torchd").
pkill -TERM -x torchd 2>/dev/null
sleep 1
pkill -KILL -x torchd 2>/dev/null

# Kill the watchdog subshell (service.sh) if it's still looping.
pkill -KILL -f 'modules/torchbutton/service.sh' 2>/dev/null

pm uninstall me.nogrep.torchbutton >/dev/null 2>&1

exit 0
