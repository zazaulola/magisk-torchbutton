# Troubleshooting

## Verbose log

```sh
adb shell 'su -c "echo TORCHD_VERBOSE=1 >> /data/adb/modules/torchbutton/config.sh"'
# then reboot — the watchdog inside service.sh inherits its env from when it
# was launched at boot, so editing config.sh after boot has no effect on the
# already-running watchdog.
adb reboot
adb shell 'su -c "tail -F /data/adb/torchbutton.log"'
```

Expected lines:

```
[torchd] POWER press (vol_held=0, fwd=0, en=1)
[torchd] POWER release (held 120ms, state was LIVE)         # short press
[torchd] long-press: torch=0 screen_on=1 locked=0 fwd=0     # long-press fires
[torchd] POWER release (held 870ms, state was FORWARDED)    # release after
```

- `vol_held` — number of Volume keys currently held when Power went down.
- `fwd` (on press) — whether we forwarded the press immediately (chord or
  passthrough mode).
- `en` — whether the module is enabled (1) or in passthrough (0).
- `fwd` (on `long-press` line) — same flag at the moment of the long-press
  decision.

## Daemon doesn't start

```sh
adb shell 'su -c "cat /data/adb/torchbutton.log; echo ---; ls -la /data/adb/modules/torchbutton/bin/$(getprop ro.product.cpu.abi)/torchd"'
```

Common causes: no binary for this ABI (rebuild with the matching `ndk-build`),
no SELinux access to `/dev/uinput` (the bundled `sepolicy.rule` should grant
it; verify with `dmesg | grep avc`), or something else has the input device
grabbed (check `lsof /dev/input/event0`).

## Flashlight doesn't turn on at long-press

```sh
adb shell 'logcat -d -s TorchReceiver:*'
# APK backend: expect "torch ON on camera 0"
adb shell 'su -c "grep long-press /data/adb/torchbutton.log | tail -5"'
# check which backend is in use and the recorded torch state
```

For the sysfs backend, find and test the node directly (don't rely on
`$TORCHD_TORCH_PATH` — it's unset unless you set it in `config.sh`):

```sh
adb shell 'su -c "ls /sys/class/leds | grep -iE \"torch|flash|spot\""'
adb shell 'su -c "echo 255 > /sys/class/leds/<name>/brightness"'   # paste the real node
```

## Power dialog flashes briefly while the flashlight turns off

The system long-press timer fired before our threshold did. Drop
`TORCHD_THRESHOLD_MS` to 250–300 in `config.sh` and reboot.

## Power+VolumeDown screenshot doesn't fire

That used to be broken; it should work now. If it doesn't:

```sh
adb shell 'su -c "tail -F /data/adb/torchbutton.log"' | grep POWER
```

When you press Volume first, then Power, the line should look like
`POWER press (vol_held=1, fwd=1, en=1)`. When Power is first then Volume,
look for `POWER retro-forwarded (chord start)`. If neither appears, the
Volume events aren't reaching the grabbed input device — your device may
have separate input devices for Power and Volume, which the current daemon
doesn't handle (Pixel/Tensor has all three on one `gpio_keys`).

## Power button stops responding entirely

This shouldn't happen, but if it does:

1. wait 3 seconds — the `service.sh` watchdog re-execs the daemon;
2. if that doesn't help — hold Power 8–10 seconds for a hardware PMIC
   reset (works below the kernel, independent of the daemon).
