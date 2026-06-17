# Changelog

## v1.1.5

- Icon: the power-ring gap now has two flat, vertical, symmetric ends (the gap
  is a vertical slot, not the lightning-bolt outline — which previously slanted
  the right end). Slightly thinner ring. No functional changes.

## v1.1.4

- Icon refresh: thicker ring on the power+bolt glyph (launcher icon and QS
  tile) — reads more clearly, especially at small Quick-Settings size. No
  functional changes.

## v1.1.3

Hardening release (from a full code audit).

**Correctness / security**
- Disabling or removing the module now releases the Power button **without a
  reboot**: the watchdog watches for Magisk's `disable`/`remove` markers and
  stops the daemon (releasing the input grab). `uninstall.sh` also kills it.
- The `SET` broadcast receiver is now `exported="false"`; the root daemon
  reaches it by explicit component. No other app can toggle the torch or
  corrupt the daemon's state.
- Enable flag and torch-state moved to **device-encrypted storage**
  (`/data/user_de/0/...`), written atomically (temp + rename). DE is readable
  by the daemon before the first unlock (verified on-device), so the gesture and
  its persisted state work right after a reboot while the device is still
  locked — credential-encrypted `/data/data` isn't available then.

**Robustness**
- The keyguard `dumpsys` lookup is bounded with `timeout` so a wedged
  WindowManager can never freeze the Power button.
- The daemon re-acquires the input device on hotplug/suspend errors instead of
  exiting, and guards against a `POLLERR` busy-loop. `O_CLOEXEC` on its fds.
- State files are written atomically (temp + rename) — no zero-length window.
- Torch state is seeded at boot by a `BootReceiver` and corrected on
  `onTorchModeUnavailable`, so a long-press toggles the right way even if the
  torch was changed by the system tile before our app ran.
- Deterministic power-device selection; sturdier sysfs torch-node discovery.

**Conventions / build / docs**
- SELinux rules shipped as a persistent `sepolicy.rule` (replacing
  `magiskpolicy --live`); dropped the over-broad generic-`sysfs` write grant.
- `update.json` for in-Manager auto-updates.
- APK version is derived from `module.prop` (single source of truth);
  `package.sh` asserts they match, ships `sepolicy.rule`, emits a `.sha256`.
- Log relocated to root-only `/data/adb/torchbutton.log`, truncated each boot.
- README/wiki accuracy sweep.

## v1.1.2
- Track the real torch state via `CameraManager.TorchCallback` so a torch lit
  by the system Quick Settings tile is turned off by long-press; QS tile and
  in-app switch stay in sync both ways.

## v1.1.1
- New icon: power symbol with a lightning bolt in the gap.

## v1.1.0
- Initial public release.
