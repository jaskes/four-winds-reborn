# Four Winds Reborn

Community continuation of the retro strategy game *The Isle of Four Winds:
Rune War*, focused on completing its unfinished systems, restoring the original
presentation and making the game comfortable on modern platforms.

This project continues
[RuneWars: New Age](https://github.com/AndreyBarmaley/runewars.newage). Original
copyright notices and the GPL-3.0 license are preserved. The bundled
[Four Winds Engine](https://github.com/jaskes/four-winds-engine) retains its
LGPL-3.0-or-later license and upstream attribution.

## Download

The latest stable release is
[Four Winds Reborn v0.4.0](https://github.com/jaskes/four-winds-reborn/releases/latest),
with ready-to-run Windows, Linux and macOS x64 archives, a directly installable
Android arm64 APK and SHA-256 checksums. On Windows, unpack the archive and
launch `four-winds-reborn.exe`; on Android 6.0 or newer, install the APK.

## Roadmap

- Improve Android ergonomics with larger touch targets and broader device
  coverage after the initial native arm64 release.
- Prove the shared ruleset contract with additional local Rune Game rules,
  followed by separate Duel and Coalition modes.
- Add authoritative multiplayer last, after Android and the local rulesets are
  stable.

## Screenshots

### Main menu

![Four Winds Reborn main menu](docs/images/main-menu.jpg)

### Rune table

![Four Winds Reborn Mahjong rune table](docs/images/mahjong-table.jpg)

### Restored story intro

![Four Winds Reborn narrated story intro](docs/images/story-intro.jpg)

### Encyclopedia and game guide

![Four Winds Reborn illustrated Encyclopedia battle guide](docs/images/encyclopedia.jpg)

## Highlights

- A complete main menu with new game, continue, named saves, recovery and
  persistent settings.
- English and Russian interface, terminology, buttons, narration and restored
  original voice resources.
- Windowed and fullscreen modes with smooth fixed-canvas scaling.
- Independent music, effects and voice volume controls, plus Classic, Normal
  and Fast presentation speeds.
- Restored story intro and separate menu, faction, map and summary music.
- Two complete presentation packages: the default Classic theme and the
  optional Reborn theme, selectable in Settings without changing game rules.
- Five deliberately distinct AI levels, selected in Settings for new games:
  peaceful Training, honest Easy, Normal and Hard, and an openly unfair
  economy-boosted challenge. No level receives favorable RNG or access to
  hidden information.
- Manual tactical battles with legal target selection, AI recommendations and
  optional automatic resolution.
- A bilingual Encyclopedia covering lore, factions, wizards, creatures, spells,
  classic rules and a practical illustrated Rune Game guide.
- Numerous crash, save, Rune Game, UI, localization, map input and invisible
  rune fixes inherited from years of unfinished development.

## Build from source

Clone the game with its engine submodule:

```bash
git clone --recurse-submodules https://github.com/jaskes/four-winds-reborn.git
cd four-winds-reborn
```

On Linux or macOS:

```bash
./scripts/build.sh --install-deps
```

On Windows, install [MSYS2](https://www.msys2.org/) and run:

```powershell
.\scripts\build-windows.ps1 -InstallDeps
```

For the native Android arm64 build, bootstrap the pinned JDK, SDK/NDK, Gradle
and SDL toolchain, then build the APK:

```powershell
.\scripts\bootstrap-android.ps1
.\scripts\build-android.ps1 -Configuration Debug
```

The Android architecture, package contract and physical-device acceptance
checklist are documented in
[Android Development](docs/AndroidDevelopment.md).

## Documentation

Rules, balance evidence, localization/provenance, platform development,
archived original sources and the release workflow are indexed in
[the documentation guide](docs/README.md).
Completed player-facing work is recorded in the [changelog](CHANGELOG.md).

## License and attribution

Four Winds Reborn preserves the original project credits and GPL-3.0 license.
The Four Winds Engine submodule is LGPL-3.0-or-later. This repository documents
Reborn changes without claiming authorship of inherited code or game assets.
