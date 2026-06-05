# Building from source

Three artifacts: the native daemon, the helper APK, and the Magisk module zip
that bundles both.

## Requirements

| Component | Purpose | Where to get it |
|---|---|---|
| Android NDK r21+ | cross-compile `torchd` for arm64/arm/x86/x86_64 | `brew install --cask android-ndk` |
| Android cmdline-tools (build-tools 34+, platform-34+) | build the APK without Gradle | `brew install --cask android-commandlinetools` |
| JDK 11+ | `javac`, `keytool` | system or `brew install openjdk` |
| `zip`, `bash`, `make` | standard | preinstalled on macOS / Linux |
| `adb` (optional) | deploy to device | `brew install --cask android-platform-tools` |

Verified on macOS 14 (Apple Silicon) and should work on Linux unchanged.

## Layout

All build outputs and intermediates land under `dist/` — the source tree stays
clean.

```
dist/
├── bin/                       ← goes inside the Magisk module zip
│   ├── TorchButton.apk
│   ├── arm64-v8a/torchd
│   ├── armeabi-v7a/torchd
│   ├── x86/torchd
│   └── x86_64/torchd
├── torchbutton-vX.Y.Z.zip     ← Magisk-installable module
├── .obj/                      ← ndk-build intermediates (.o, .d)
└── .apk-build/                ← javac/d8/aapt2 intermediates
```

## 1. Native daemon (`torchd`)

```sh
export ANDROID_NDK_HOME=/path/to/android-ndk
./src/build.sh
```

`build.sh` searches `$ANDROID_NDK_HOME`, `$NDK`, and the usual install paths
(`~/Library/Android/sdk/ndk-bundle`, `~/Android/Sdk/ndk/*`, `/opt/android-ndk`).
It runs `ndk-build` with `src/Android.mk` and `src/Application.mk` and writes
binaries to `dist/bin/<abi>/torchd`.

```sh
file dist/bin/arm64-v8a/torchd
# ELF 64-bit LSB pie executable, ARM aarch64, ..., dynamically linked
```

## 2. Helper APK

Needed when the device has no writable sysfs torch node (Pixel/Tensor, where
the flash is exclusively owned by the Camera HAL). When sysfs works the daemon
runs without the APK; bundling it anyway is harmless — backend choice is
automatic.

```sh
./apk/build.sh
```

Pipeline (no Gradle): `aapt2 compile` → `aapt2 link` → `javac` → `d8` →
`zipalign` → `apksigner`. Signed with a self-generated debug key
(`apk/.debug.keystore`, created on first build). Output: `dist/bin/TorchButton.apk`.

Smoke test:

```sh
adb install -r dist/bin/TorchButton.apk
adb shell 'am broadcast -a me.nogrep.torchbutton.SET --es state on \
   -p me.nogrep.torchbutton --include-stopped-packages'
# Flashlight should turn on.
adb shell 'am broadcast -a me.nogrep.torchbutton.SET --es state off \
   -p me.nogrep.torchbutton --include-stopped-packages'
```

## 3. Module zip

```sh
./package.sh
# -> dist/torchbutton-vX.Y.Z.zip
```

`package.sh` reads `dist/bin/` and produces the final zip. No `META-INF` is
included — Magisk Manager injects its own installer template.
