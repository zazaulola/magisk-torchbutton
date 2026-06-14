# Known limitations

- **APK backend + external torch toggles — needs the QS tile added.** The
  helper APK tracks the real LED state via `CameraManager.registerTorchCallback()`
  and writes it to `files/torch_state` for the daemon (see
  [Architecture](Architecture#state-storage)). The callback is live while our
  **Torch Button Quick Settings tile is in the shade** (the tile must be added
  to your active QS), or while the app is in the foreground, plus a boot seed
  (`BootReceiver`) and `onTorchModeUnavailable` → off. So catching a torch lit
  by the *system* flashlight tile requires our tile to also be in that shade —
  **add the Torch Button tile** for this to work. Without it (or the app open),
  an external toggle isn't noticed until our callback next registers. Closing
  the gap entirely would need a persistent foreground service (permanent
  notification) — deliberately not done.

- **Locked screen ≠ idle device.** If the screen is on but the lockscreen
  is showing, we treat that as "user not actively using the phone" and
  long-press toggles the torch. Matches the spec, but means "I'm looking
  at the lockscreen and want the power menu" doesn't work — you have to
  unlock first.

- **Time-to-power-menu when unlocked** = our threshold + system threshold ≈
  400 + 500 = ~900 ms from physical press. AOSP without us is ~500 ms. We
  pay this for the always-buffer race fix. Disabling the module via the
  QS tile returns latency to baseline.

- **The "Press and hold Power button" system setting** no longer matters
  while the module is enabled — we grab the event before the framework
  sees it. To get the assistant or power menu back, disable the module
  with the QS tile or the helper app. We don't try to add a third radio
  option to that system menu — that would need LSPosed or smali-patching
  SettingsGoogle.apk.

- **Power+Vol on devices with separate Volume input devices.** Our chord
  detection only works when Volume keys land on the same input device we
  grab. On Pixel that's all on `gpio_keys`; on some OEM hardware Volume
  is a separate `event*`. There, the daemon won't notice the Volume press
  in time and the chord won't form. Workaround: also grab the Volume
  device and forward — not implemented.

- **Force-shutdown via long Power hold.** Android's framework
  force-shutdown (typically 5–10 seconds of Power held alone) goes through
  the same long-press path we intercept, so it doesn't work while the
  module is enabled. The hardware PMIC reset (8–10 s) still does, and it's
  what most users actually use anyway.
