# Enabling and disabling

This behaviour isn't for everyone, so there's a one-tap toggle in two places.

## Launcher app

The bundled APK installs as **Torch Button** (amber square with a white
flashlight) in the app drawer. Tapping it opens a single-screen Settings UI:

- a Switch labeled "Enable long-press torch"
- a paragraph describing what the module does

## Quick Settings tile

There's also a QS tile labeled **Torch Button** with the same flashlight glyph.
Add it once via the QS shade → **Edit tiles** → drag the tile into the active
area. After that one tap toggles the module on and off without leaving whatever
you were doing.

## How it works under the hood

Both UIs flip the same one-byte file:
`/data/data/me.nogrep.torchbutton/files/enabled` — `1` for enabled, `0` for
disabled. The native daemon polls this file on every Power press (cached for
500 ms so it doesn't hammer the filesystem).

When disabled, the daemon stays running and still holds the input-device grab,
but every Power event is forwarded immediately and untouched. Externally it's
indistinguishable from the daemon not being there — the Pixel "Press and hold
Power button" setting resumes its default behaviour (Assistant or power menu).

If the file doesn't exist (fresh install, or after `pm uninstall`) the daemon
defaults to **enabled**. That way a ROM bake-in works without the user having
to open the helper app first, but they can always disable it with one tap if
they don't like it.

## ROM defaults

ROM maintainers who want default-off can either pre-populate the file at
build time, or modify `service.sh` to write `0` to it on first boot.
