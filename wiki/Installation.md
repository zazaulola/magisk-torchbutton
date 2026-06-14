# Installation

## Via Magisk Manager

1. Copy the `torchbutton-vX.Y.Z.zip` somewhere on the device.
2. Magisk app → **Modules** → **Install from storage** → pick the zip.
3. Reboot.

## Via ADB

```sh
adb push torchbutton-vX.Y.Z.zip /data/local/tmp/torchbutton.zip
adb shell su -c 'magisk --install-module /data/local/tmp/torchbutton.zip'
adb reboot
```

## Verify after boot

```sh
adb shell 'su -c "ps -ef | grep torchd | grep -v grep"'
# should show a torchd process
adb shell 'su -c "tail -10 /data/adb/torchbutton.log"'
# should end with "torchd ready"
```

The daemon is started by `service.sh` in `late_start service` mode and is
wrapped in a watchdog loop — if it crashes, it restarts in 3 seconds.

`service.sh` also installs the bundled APK via `pm install -r` on first run
(or on updates — it compares the bundled APK md5 against a marker file
`bin/.installed.md5`).
