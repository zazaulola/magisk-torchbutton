# Android 16 and Pixel specifics

Primary test platform: `CP1A.260505.005` (Android 16 on Pixel 8 Pro, Tensor
G3). A few things had to be adapted compared to vanilla AOSP.

## Multi-line `dumpsys window` parsing

Recent AOSP output for the Keyguard delegate is multi-line:

```
KeyguardServiceDelegate:
    showing=true
    showingAndNotOccluded=true
    inputRestricted=false
    occluded=false
    ...
```

The daemon tracks "are we inside a Keyguard section?" while reading and
matches `showing=true`, `mShowing=true`, `mShowingAndNotOccluded=true`,
`mIsShowing=true`. Also recognises `KeyguardController:` and
`KeyguardStateMonitor:` headers. Legacy single-line `mShowingLockscreen=true`
still works for older Android.

## SELinux on Pixel

The `magisk` SELinux domain already has very broad access on stock Magisk,
but on tighter Pixel policies a write to `/sys/.../brightness` or an open
of `/dev/uinput` can hit `EACCES`. Rules ship as a persistent `sepolicy.rule`
(applied by Magisk every boot):

```
allow magisk sysfs_leds file { read write open getattr ioctl }
allow magisk sysfs_camera file { read write open getattr ioctl }
allow magisk sysfs_lights file { read write open getattr ioctl }
allow magisk sysfs_lcd_backlight file { read open getattr }
allow magisk sysfs_graphics file { read open getattr }
allow magisk input_device chr_file { read write open getattr ioctl }
allow magisk uinput_device chr_file { read write open getattr ioctl }
```

On Pixel 8 Pro under Magisk 30.7 the rules turned out to be unnecessary
in practice — Magisk's default policy already grants enough — but they're
least-privilege (no blanket `sysfs` write) and make the module robust on
stricter ROMs. Magisk skips rules for types that don't exist on a device.

## Tensor flash unit

Pixel/Tensor devices don't expose the flashlight in `/sys/class/leds/`
at all. The flash is owned by the Camera HAL through the proprietary
LWIS driver (`/dev/lwis-flash-lm3644`). Userspace ioctls into LWIS are
undocumented and version-specific.

That's why the module ships a small helper APK — `me.nogrep.torchbutton` —
that calls `CameraManager.setTorchMode()`. The daemon falls back to
broadcasting an intent to the APK when no sysfs torch is found:

```sh
am broadcast -a me.nogrep.torchbutton.SET --es state on \
    -p me.nogrep.torchbutton --include-stopped-packages
```

Latency is ~50–150 ms per toggle (cold start of the broadcast receiver).
Not zero, but imperceptible in the long-press context — the user is still
holding the button when the broadcast lands.

## What didn't work

- **`service call media.camera <txn>` with `setTorchMode`**: the binder
  transaction needs an `IBinder` argument that `service call` can't
  construct. Dead end.
- **`cmd media.camera`**: has lots of subcommands (rotate-and-crop,
  use-case overrides, …) but nothing for torch.
- **Smali-patching `SettingsGoogle.apk`** to add a third option under
  *Press and hold Power button*: would technically work but breaks on
  every Pixel update — not worth it.
- **LSPosed hook into the same Settings panel**: cleaner, but requires
  LSPosed as a separate Magisk dependency, which makes the module
  awkward to bake into a stock ROM. Decided against.

The simple toggle in the QS tile + helper app is the path of least
friction.
