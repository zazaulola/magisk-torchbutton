# Optional configuration for torchd. Edit and save, then reboot.
#
# Backend selection:
#   - If a writable sysfs torch node is auto-detected, it is used by default.
#   - On devices without one (e.g. Pixel/Tensor) the daemon automatically falls
#     back to broadcasting an intent to the bundled APK
#     (me.nogrep.torchbutton). You can force this with TORCHD_BACKEND=apk.
#
# Auto-detection covers most sysfs devices; only set TORCHD_TORCH_PATH if your
# flashlight LED node is unusual. Find candidates with:
#     ls /sys/class/leds | grep -iE 'torch|flash|spot'
#
# TORCHD_BACKEND=apk
# TORCHD_PKG=me.nogrep.torchbutton
# TORCHD_TORCH_PATH=/sys/class/leds/led:torch_0/brightness
# TORCHD_THRESHOLD_MS=400
# TORCHD_BRIGHTNESS=255
# TORCHD_VERBOSE=1     # log every press/release event
#
# Log file (default /data/adb/torchbutton.log, truncated each boot):
# TORCHD_LOG=/data/local/tmp/torchd.log
#
# Override the daemon-visible state files. Usually leave these alone:
# TORCHD_ENABLE_FILE=/data/user_de/0/me.nogrep.torchbutton/files/enabled
# TORCHD_TORCH_STATE_FILE=/data/user_de/0/me.nogrep.torchbutton/files/torch_state
