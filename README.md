# torchbutton

Magisk module that overrides the long-press of the **Power button** to toggle
the flashlight when the screen is off or locked. When the device is on and
unlocked, long-press still shows the system power dialog. `Power+VolumeDown`
(screenshot, assistant, etc.) keep working.

Tested on **Google Pixel 8 Pro / Android 16 (CP1A.260505.005) / Magisk 30.7**.

## Behaviour

| Condition | Long-press Power |
|---|---|
| Flashlight ON (any screen state) | Turn flashlight OFF |
| Flashlight OFF, screen off/locked | Turn flashlight ON (screen stays off) |
| Flashlight OFF, screen on + unlocked | System power dialog (default) |
| Short press (always) | Standard sleep / wake |
| Power + VolumeDown | Screenshot chord (unchanged) |

## Install

Grab the latest `torchbutton-v*.zip` from [Releases](../../releases) and:

```sh
adb push torchbutton-v*.zip /data/local/tmp/torchbutton.zip
adb shell su -c 'magisk --install-module /data/local/tmp/torchbutton.zip'
adb reboot
```

Or via the Magisk app → *Modules* → *Install from storage*.

## Toggle

The module ships with a launcher app **Torch Button** (amber square + white
flashlight) and a Quick Settings tile. Both flip a single on/off flag the
daemon reads. When disabled, the daemon stays loaded but forwards every
Power event transparently — indistinguishable from not having the module.

Default is **on**.

## More

The [wiki](../../wiki) has the rest:

- [Building from source](../../wiki/Building)
- [Configuration](../../wiki/Configuration)
- [Architecture](../../wiki/Architecture)
- [Android 16 / Pixel specifics](../../wiki/Android-16-Pixel-specifics)
- [Troubleshooting](../../wiki/Troubleshooting)
- [Known limitations](../../wiki/Limitations)

## License

GPLv3 — see [LICENSE](LICENSE).
