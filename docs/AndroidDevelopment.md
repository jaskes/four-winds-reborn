# Android Development

Android support debuted in v0.4.0. The port is a native SDL2 application built
with Gradle, CMake and the Android NDK; it is not a streamed or web wrapper.

## Supported development target

- Android 6.0 (API 23) or newer
- 64-bit ARM (`arm64-v8a`)
- landscape orientation and the existing 1024x768 logical canvas
- package id `com.jaskes.fourwindsreborn`

The first Android release is intentionally distributed as a directly
installable APK. Play Store signing and publication are separate distribution
work and are not part of the v0.4.0 development gate.

## Reproducible toolchain

The Android build pins its native inputs instead of depending on system SDL
packages:

- JDK 17
- Gradle 8.10.2 and Android Gradle Plugin 8.8.2
- Android SDK/Build Tools 35
- CMake 3.22.1
- Android NDK 27.2.12479018
- SDL2 2.32.10
- SDL2_image 2.8.12
- SDL2_mixer 2.8.2
- SDL2_ttf 2.24.0

Downloaded archives, extracted dependencies and local SDK/JDK installations
live below `android/` and are ignored by Git.

On Windows, bootstrap and build a debug APK with:

```powershell
.\scripts\bootstrap-android.ps1
.\scripts\build-android.ps1 -Configuration Debug
```

Subsequent builds do not need `bootstrap-android.ps1` unless the toolchain was
removed. `scripts/fetch-android-dependencies.ps1` can restore only the pinned
SDL sources. `scripts/test-android-package.ps1` verifies package identity,
ABI, native libraries, assets and signing.

The generated test package is copied to:

```text
dist/android/four-winds-reborn-v0.5.0-dev-android-arm64-debug.apk
```

## Runtime model

Read-only content is packaged in the APK under `assets/themes/`. Gradle also
generates a sorted `assets.list` manifest so the engine can provide the same
directory and file-discovery contract used by desktop themes without
extracting the full package at startup.

Writable settings, saves, recovery checkpoints, replays and logs continue to
use SDL's per-application preference directory. They are therefore kept in
Android application storage and never written into the APK asset tree. Save
and replay formats remain platform-compatible.

SDLActivity owns the native window and translates touch input into the same
logical pointer events used on desktop. Android Back follows the existing
secondary/system action path. App background/foreground events pause and
resume audio, release input focus and force a redraw without advancing game
state. Small legacy buttons receive Android-only touch slop: targets below 44
logical pixels grow by at most 6 pixels per side without changing their art,
layout, save data or desktop mouse behaviour.

## CI and acceptance

GitHub Actions builds and lints the arm64 Debug package, verifies its manifest,
native libraries and both bundled themes, and retains the APK as an artifact.
Desktop validation remains unchanged.

An Android release candidate is not accepted from compilation alone. The
v0.4.0 candidate completed this checklist on a physical arm64 device; test the
exact APK on a physical device:

1. Install over a clean profile and reach the main menu.
2. Switch Classic/Reborn and English/Russian; restart and verify persistence.
3. Start a game and exercise tap, drag, scrolling and Android Back.
4. Complete at least one Rune Game call and open spells/summons.
5. Issue and cancel Adventure orders, then enter a manual battle.
6. Create and load a named save; verify autosave and recovery.
7. Background and resume during the menu, Rune Game and Adventure phases;
   verify audio resumes once and no turn advances while suspended.
8. Kill the app from Android recents, relaunch and use Continue.
9. Start Quick, Duel and Coalition games and verify small selectors, Pass and
   compact Rune Game action buttons remain comfortable and accurate to tap.
   A new Duel must show two players, one hand each and 22 starting towns per
   player; Coalition must identify both teams and show their shared scores.

Any lifecycle, input-coordinate, storage or asset-discovery failure blocks the
release even if CI is green.
