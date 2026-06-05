# Configuration

Everything is in `/data/adb/modules/torchbutton/config.sh`. Defaults are
sensible — you only need to touch this file if auto-detection picks the wrong
torch node, or you want to tune the long-press threshold.

| Variable | Default | Purpose |
|---|---|---|
| `TORCHD_BACKEND` | (auto) | `sysfs` or `apk`. Overrides auto-detection. |
| `TORCHD_TORCH_PATH` | (auto) | path to the `brightness` sysfs file (sysfs backend only) |
| `TORCHD_PKG` | `me.nogrep.torchbutton` | package name of the helper APK |
| `TORCHD_THRESHOLD_MS` | `400` | long-press threshold in ms |
| `TORCHD_BRIGHTNESS` | `255` | value written to sysfs `brightness` (sysfs backend only) |
| `TORCHD_VERBOSE` | (off) | `1` to log every press/release event |
| `TORCHD_ENABLE_FILE` | `/data/data/me.nogrep.torchbutton/files/enabled` | path to the on/off flag (see [Enabling and disabling](Enabling-and-disabling)) |

After editing, restart the daemon: `adb shell su -c 'pkill -x torchd'`
(the watchdog inside `service.sh` will relaunch). Beware: the watchdog process
itself was forked at boot and won't pick up new env vars from `config.sh`
until the device reboots — `pkill -x torchd` only re-execs the daemon under
the *existing* env, not a freshly-sourced one.

`-x torchd` (exact process name) — not `-f torchd` (full command line),
because the latter also matches any shell process that has the word "torchd"
in its argv, including yours.

## When to lower `TORCHD_THRESHOLD_MS`

Our threshold must be **shorter** than the system long-press timer, otherwise
the framework can open the power dialog (or the assistant) before we decide
what to do, and you'll see a brief flash before we cancel. AOSP defaults to
~500 ms; Pixel is noticeably less. The shipped 400 ms is intentionally
conservative.

- if you see the power dialog briefly appear while turning off the flashlight,
  drop to 250–300;
- if you want faster trigger and don't mind that 200 ms taps become
  long-presses, drop to 250.

## When to set `TORCHD_TORCH_PATH`

Auto-detection covers all common sysfs locations (Qualcomm, MediaTek, classic
Pixel). If yours is unusual:

```sh
adb shell 'su -c "ls /sys/class/leds | grep -iE \"torch|flash|spot\""'
# pick a candidate and test:
adb shell 'su -c "echo 255 > /sys/class/leds/<name>/brightness"'
```

Whichever path actually lit the LED — put that in `config.sh`.

If no `/sys/class/leds/*` entries match, the daemon falls back to the APK
backend automatically, which uses `CameraManager.setTorchMode()` (this is the
case on Pixel/Tensor — see [Android 16 / Pixel specifics](Android-16-Pixel-specifics)).
