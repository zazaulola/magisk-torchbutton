# torchbutton wiki

Magisk module that overrides the Power button long-press to toggle the
flashlight when the device isn't actively being used.

## Pages

- [Building from source](Building) — native daemon + APK + module zip
- [Installation](Installation) — install the zip, verify it's running
- [Enabling and disabling](Enabling-and-disabling) — UI toggle, QS tile, file flag
- [Configuration](Configuration) — `config.sh` reference
- [Architecture](Architecture) — state machine, torch backends, EVIOCGRAB
- [Android 16 / Pixel specifics](Android-16-Pixel-specifics) — why Pixel needs the APK backend, multi-line keyguard
- [Troubleshooting](Troubleshooting) — verbose log, common issues
- [Limitations](Limitations) — known trade-offs

## What it does

| Condition | Long-press Power |
|---|---|
| Flashlight ON (any screen state) | Turn flashlight OFF |
| Flashlight OFF, screen off/locked | Turn flashlight ON (screen stays off) |
| Flashlight OFF, screen on + unlocked | System power dialog (default) |
| Short press (always) | Standard sleep / wake |
| Power + VolumeDown | Screenshot chord (unchanged) |

Tested on **Google Pixel 8 Pro / Android 16 (CP1A.260505.005) / Magisk 30.7**.
Should work on other rooted devices with the same input subsystem (`gpio_keys`
holding KEY_POWER) and a Camera HAL torch.
