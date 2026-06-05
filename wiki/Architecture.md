# Architecture

## At a glance

```
  physical Power press
        │
        ▼
/dev/input/event0 ── EVIOCGRAB ──► torchd ──► /dev/uinput ──► Android input
                                     │
                                     │ (long-press decision)
                                     │
                       ┌─────────────┼──────────────┐
                       ▼             ▼              ▼
               /sys/class/leds  am broadcast    nothing
                (sysfs backend) (APK backend)  (system shows
                                                power dialog)
```

## `torchd` step by step

1. **Grab the input device.** Scan `/dev/input/event*`, find the one whose
   key-bitmask contains `KEY_POWER`, and call `EVIOCGRAB`. After this, the
   Android framework no longer sees raw Power events — we're the only
   consumer.
2. **Mirror through uinput.** Create a `torchd-virtual` device with the
   same key/sw/ev bits as the source (on Pixel that's `gpio_keys` — Volume
   Up/Down + Power). Any event we don't intercept (Volume, etc.) is
   forwarded verbatim.
3. **On Power press** there are three branches:
   - **Module disabled** (`enabled` flag is `0`) → press is forwarded
     immediately, transparent passthrough, no further logic.
   - **Power+Volume chord** (a Volume key is already held) → press is
     forwarded immediately so the framework's `ScreenshotChord` can match
     both keys within its ~150 ms window. If Volume goes down *while*
     Power is still buffered, the daemon retroactively forwards Power.
   - **Otherwise (plain press)** → press is **buffered**, nothing is sent
     to the system. Without this, the Pixel long-press timer (well under
     400 ms) would open the power dialog before our decision could
     intervene.
4. **Long-press timer** (`TORCHD_THRESHOLD_MS`, 400 ms). If a release
   arrives first, it was a short tap — we replay press + `usleep(40 ms)`
   + release, and the system handles sleep/wake as if nothing happened.
5. **On long-press fire:**
   - **disabled** → do nothing, wait for release;
   - **torch ON** → turn the flashlight off; if we'd already forwarded
     the press (chord/passthrough), also emit release to cancel the
     system's view of the long-press;
   - **screen OFF or locked** → turn flashlight on (same release-cancel
     trick if applicable);
   - **screen ON + unlocked + torch OFF** → **only here** do we forward
     the press to uinput (if not already). The system starts *its* own
     long-press timer from this point and opens the power dialog ~500 ms
     later.
6. **Release after long-press:** HANDLED → nothing; FORWARDED → emit
   release.

## Why "always buffer"

If we forwarded the press immediately, the system's long-press timer
would start ticking at t=0. On Pixel the timer is shorter than our 400 ms,
so by the time we want to take over the menu has already opened — and
even a cancel-release only dismisses it as a flicker.

Buffering hides the press entirely until we decide. Trade-off: when we
*do* want the system to handle the press (unlocked + torch off), we
forward at the threshold, so the user sees the dialog at ~`our_threshold
+ system_threshold` ≈ 400 + 500 = 900 ms from the physical press
instead of the usual ~500 ms. Acceptable, and only happens in that
specific case.

## Torch backends: sysfs vs APK

- **sysfs backend.** Direct write to `/sys/class/leds/.../brightness`.
  Instant, no IPC. Works on most Qualcomm/MediaTek devices.
- **APK backend.** The daemon `fork()`s `am broadcast` to
  `me.nogrep.torchbutton`, whose receiver calls `CameraManager.setTorchMode()`.
  Latency is ~50–150 ms (cold-start cost of starting the Java process), but
  this is the only way to reach the flashlight on Pixel/Tensor, where the
  flash is owned by the Camera HAL through `/dev/lwis-flash-lm3644`.

The daemon picks the backend automatically:
1. If there's a writable `/sys/class/leds/.../brightness` (looked up from a
   known list and then by scanning `/sys/class/leds/`) → sysfs.
2. Otherwise → APK.

Force one with `TORCHD_BACKEND=apk` in `config.sh`.

## Flashlight state tracking

- **sysfs**: re-read on every press from the same `brightness` file —
  always up to date.
- **APK**: kept in memory by the daemon. Caveat: if the flashlight is
  toggled externally (system QS tile, another app), the daemon's notion
  of the state drifts until the next long-press — that press performs
  one "redundant" toggle, after which state is back in sync.

## Lock detection

`dumpsys window` is invoked once per long-press and scanned for a few
markers — multi-line `KeyguardServiceDelegate:` / `KeyguardController:`
(Android 13+) and the legacy single-line `mShowingLockscreen=true`.
Takes 50–200 ms; only triggered on long-press, so short presses are
unaffected.

## SELinux

`post-fs-data.sh` runs `magiskpolicy --live` with grants for the
`magisk` domain to read/write `sysfs_leds`, `sysfs_camera`, `sysfs_lights`,
`input_device`, and `uinput_device`. On stock Magisk these usually already
exist, but tighter OEM policies need the explicit rules — otherwise the
daemon hits `EACCES` opening the torch sysfs node or `/dev/uinput`.

## Failure mode

- if the daemon crashes, `service.sh` restarts it within 3 seconds — the
  Power button is "frozen" for at most that long;
- holding Power 8–10 seconds triggers a **hardware** PMIC reset below the
  kernel — independent of the daemon or Android, always available as an
  emergency exit.
