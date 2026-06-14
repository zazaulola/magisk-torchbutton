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
- **APK**: the helper APK mirrors the *real* LED state into
  `files/torch_state` and the daemon reads that file (its in-memory
  `g_torch_state` is only a fallback for when the file doesn't exist yet).
  The file is kept current by:
  - `TorchReceiver` writing it right after every `setTorchMode()` we issue;
  - `TorchWatch`, which registers a `CameraManager.registerTorchCallback()`
    that fires for *any* torch change — ours, the system flashlight QS tile,
    other apps — and reports the current state immediately on registration.
    It's registered while the QS shade is open (`TileService` is listening,
    and that's exactly where the system flashlight tile is tapped) and while
    `MainActivity` is in the foreground.

  So a torch lit by the system QS tile is detected and a long-press correctly
  turns it off. Residual gap: a torch toggled by a *third* app while neither
  our app nor the QS shade is active isn't seen until our callback next
  registers (shade/app opened) or we set the torch ourselves — at which point
  it re-syncs. Closing that gap entirely would need a persistent foreground
  service (a permanent notification), which isn't worth it for that narrow
  case.

## Lock detection

`timeout 1 dumpsys window` is invoked once per long-press and scanned for a few
markers — multi-line `KeyguardServiceDelegate:` / `KeyguardController:`
(Android 13+) and the legacy single-line `mShowingLockscreen=true`. The
`timeout` bound means a wedged WindowManager can never block the input loop
(it would otherwise leave the Power button dead); on timeout we fail open
(treat as unlocked → system power dialog). Takes 50–200 ms; only triggered on
long-press, so short presses are unaffected.

## State storage

The enable flag and mirrored torch state live in the app's **device-encrypted**
storage (`/data/user_de/0/me.nogrep.torchbutton/files/{enabled,torch_state}`),
written by the APK via `createDeviceProtectedStorageContext()`, atomically
(temp + rename), and made world-readable so the root daemon can read them. DE
(unlike credential-encrypted `/data/data`) is available before the first unlock
(BFU), and the `magisk` daemon domain can read it — verified on-device — so the
enable flag and torch state are honored even right after a reboot, before any
unlock. The `directBootAware` receivers also run in BFU, so the torch toggles
and seeds its state then too.

## Talking to the APK

The daemon drives the torch with `am broadcast -n me.nogrep.torchbutton/.TorchReceiver`
— an **explicit component** target. The receiver is `exported="false"`, so no
other app can reach it; the daemon runs as root, which is allowed to deliver to
a non-exported component.

## Device loss / recovery

The acquire-and-run logic lives in `run_session()`; `main()` calls it in a
backoff loop. On a `read`/`poll` error or `POLLERR/POLLHUP` (suspend-resume
re-enumeration, hotplug) the session returns and `main()` re-acquires the
device in place, rather than exiting and waiting for the watchdog.

## SELinux

Rules ship as a persistent `sepolicy.rule` (applied by Magisk every boot, more
reliable than runtime `magiskpolicy --live`): write to `sysfs_leds` /
`sysfs_camera` / `sysfs_lights`, read-only to `sysfs_lcd_backlight` /
`sysfs_graphics`, and read/write the `input_device` / `uinput_device` char
devices. Least-privilege — no blanket generic-`sysfs` write. On stock Magisk
the magisk domain often already has enough; the rules make it robust on
stricter ROMs, and Magisk silently skips rules for types absent on a device.

## Failure mode / disable

- if the daemon crashes, the `service.sh` watchdog restarts it (3 s, backing
  off on repeated fast failures) — the Power button is "frozen" for at most
  that long;
- when the module is **disabled or removed** in Magisk, the watchdog notices
  the `disable`/`remove` marker, kills the daemon (releasing the input grab),
  and stops — no reboot needed to get the Power button back;
- holding Power 8–10 seconds triggers a **hardware** PMIC reset below the
  kernel — independent of the daemon or Android, always available as an
  emergency exit.
