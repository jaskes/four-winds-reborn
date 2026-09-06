# Four Winds Reborn changelog

This document records the cumulative changes made by **Four Winds Reborn**
after it diverged from
[`AndreyBarmaley/runewars.newage`](https://github.com/AndreyBarmaley/runewars.newage).
The repositories share history through upstream commit
[`9d1a47f`](https://github.com/AndreyBarmaley/runewars.newage/commit/9d1a47f58dcf8da5fd6913012e33fad0917c3842)
(`fix clang build`, 2024-03-24). Reborn is hosted as a standalone repository,
so that commit, rather than GitHub fork metadata, is the comparison baseline.

The project retains the original credits and license. Entries below describe
Reborn work only; they do not claim authorship of inherited code or assets.

## [Unreleased] - planned v0.5.0

### Added - local match modes and rulesets

- Added a versioned match-topology contract independent from Rune Game rules,
  persisted in saves, recovery checkpoints, replays and simulation artifacts.
- Added the Quick ruleset, which plays only the East round: four hands in
  Classic/Coalition, or two hands in Duel.
- Added two-player Duel (`duel@2`): one local player against one AI, one hand
  each, opposite East/West seats, and half of the island owned by each wizard.
  Classic-length Duel plays eight hands across four rounds. The first
  four-hand implementation (`duel@1`) remains available for existing saves.
- Added Coalition: four controllers remain active, with fixed Red/Purple and
  Yellow/Aqua teams sharing score and victory.
- Added localized Match Mode and Rune Game Rules selectors to Settings.
- Added two-player rosters, rune table, island status and summaries, plus
  colour-coded Coalition teams and a shared final score for each team.

### Fixed - candidate review

- Fixed clipped setting values in the compact menu and clarified that
  Continue preserves the saved match mode.
- Fixed loss of the selected rune after continuing a legacy Duel's second
  hand, automatic Pass that skipped that hand's claim, and a disabled Done
  button when Adventure control moved to the second local clan.
- Reset original land owners when starting a four-player game after a Duel.
- Added native rendered UI regressions and full two-player match/replay checks.

### Changed - topology-aware gameplay

- Local ownership, AI decisions, hostile spell targeting, Adventure movement,
  land occupation and score summaries now respect controller and team
  topology. Allies cannot attack, target one another with hostile magic or
  occupy each other's land.
- Legacy data without topology metadata continues to load as versioned Classic
  free for all; invalid or incompatible topology data is rejected explicitly.

### Changed - Android ergonomics

- Small legacy buttons now receive Android-only touch slop up to a 44-pixel
  logical target without changing art, layout, saves or desktop mouse input.
- Advanced the Android package contract to versionCode 500 for v0.5.x.

## [0.4.0] - 2026-08-07

### Added - Android foundation

- Added a native Android arm64 application built through SDLActivity, Gradle,
  CMake and the NDK, with a reproducible pinned SDL2 dependency toolchain.
- Packaged Classic and Reborn as read-only APK assets with a generated virtual
  filesystem manifest, while keeping settings, saves, recovery and replays in
  writable application storage.
- Added touch/drag input through the existing 1024x768 logical canvas, Android
  Back handling and pause/resume lifecycle integration for audio, focus and
  redraws.
- Added Android package validation, an installable arm64 APK release artifact
  and a physical-device acceptance checklist. The release candidate completed
  an extended real-device gameplay pass; Play Store signing and publication
  remain deferred.

## [0.3.0] - 2026-07-24

This release completes the v0.3.0 player-facing slice: deterministic replay
recording and playback, versioned content packages, the complete optional
Reborn presentation, expanded difficulty and developer-testing support,
clearer combat and final screens, and the R1-R12 architecture refactor.

### Added - Reborn content package

- Expanded the optional Reborn package into a substantially complete alternate presentation
  that can be installed beside Classic and selected from Settings without
  changing the fixed gameplay contract.
- Added package-specific wizard and creature identities, lore, English and
  Russian localization, narrated intro, announcer and hero voices.
- Replaced the major inherited presentation with Reborn avatar and creature
  art, Guardians and combat animation, rune and wind symbols, clan emblems,
  map and town art, intro scenes, combat UI, score summaries and victory
  screens.
- Added an eight-track OGG soundtrack covering the menu, Rune Game, clans,
  Adventure map and final presentation.
- Moved Reborn runtime media into semantic `assets/images`, `assets/audio`,
  `assets/music` and `assets/fonts` directories, with editable sources and
  reproducible preparation helpers kept outside the shipped package.
- Added machine-readable provenance auditing for inherited media and narrative
  fields. Reborn is release-ready; its remaining generic non-voice effects are
  an explicit compatibility layer rather than unfinished presentation work.

### Changed - Reborn media audit

- Removed 203 unused byte-identical Classic files from Reborn: 170 OGG files,
  all 16 inherited MIDI tracks and 17 PNG files.
- Redirected 47 default announcer fallbacks to the Reborn English voice set, so
  unknown locales no longer require the inherited Classic announcements.
- Removed the final 10 legacy UI/atlas dependencies. The only retained Classic
  compatibility media are 25 explicitly allowlisted generic gameplay effects.
  Five newer package-neutral difficulty portraits are shared by both packages
  and verified byte-identical. Reborn now has zero unclassified inherited media
  and zero inherited narrative fields.
- A decoded-media follow-up found no additional Classic images hidden by
  re-encoding and no Classic audio hidden behind alternate OGG compression.

### Changed - theme naming and default

- Renamed the inherited `default` theme to `classic` and reorganized its
  runtime media into the same semantic `assets/images`, `assets/audio`,
  `assets/music` and `assets/fonts` layout used by Reborn.
- Classic remains the default presentation for v0.3.0. Reborn is a complete
  opt-in package selected from Settings; changing the initial default is
  deferred until the public screenshots are refreshed and player feedback is
  available.
- Existing user settings and command lines that request `default` migrate
  transparently to `classic`. The stable `classic-original` content identity
  is unchanged, preserving save and replay compatibility.

### Added - replay library

- Added a player-facing replay library to the main menu with localized replay
  metadata, compatibility status, scrolling, details and confirmed deletion.
- Completed sessions are archived as standalone `.fwr` replay journals beside
  saves. Replay discovery validates the journal, ruleset and content-package
  identity without mutating the current game state, and safely reports broken
  or incompatible files.
- Replay journals now retain their start/save time, phase, difficulty and
  developer-assisted status. Normal player sessions keep their full contiguous
  action history, while bounded test journals remain available to regression
  and headless tools.
- Replays can now be imported from and exported to portable `.fwr` files. The
  transfer path validates the complete journal before an atomic copy, avoids
  accidental library overwrites and provides a native Windows file picker with
  a portable path-entry fallback for Linux and macOS.
- Deterministic playback failures now identify the first divergent action and
  show the expected and actual outcome or state hash, making incompatible,
  modified and damaged recordings diagnosable without reading the engine log.

### Added - content package identity

- Added a versioned content-package manifest with a stable package identifier,
  package version, compatible-version list and an explicit engine content
  contract.
- New saves, recovery checkpoints and deterministic replay journals now record
  their content-package identity. Incompatible or unavailable packages are
  rejected before state is changed, while pre-manifest Classic artifacts remain
  load-compatible.

### Changed - theme and game-data separation

- Moved authoritative gameplay bonuses and indexed object tables out of the
  presentation theme manifest into its declared `content.json` payload.
- Extended theme validation and regression coverage so incomplete packages,
  mixed presentation/gameplay manifests and unsupported engine contracts fail
  clearly instead of being loaded partially.

### Added - difficulty and developer testing

- Added peaceful Training and openly unfair challenge difficulties alongside
  Easy, Normal and Hard, with deliberately different spell, economy, combat
  and expansion behavior while preserving honest RNG and hidden information.
- Added near-end, phase and generated-battle developer fixtures so manual
  Mahjong, Adventure, combat and final-screen regressions can be reached
  without replaying a complete campaign.
- Added observer-safe Adventure battle forecasts that explain visible-force
  assumptions without exposing hidden defenders.

### Added - battle and final presentation

- Clarified manual battle interaction with localized action counters,
  accepted-choice feedback and stable target selection.
- Added explicit joint-winner handling, per-winner victory art and a readable
  final summary that identifies the winning wizard or tied winners before the
  score table.

### Changed - architecture and regression gate

- Completed the R1-R12 mechanical refactor of battle, Adventure, Rune Game,
  state I/O, rules, AI, UI and platform seams without changing save, replay,
  RNG or fixed-seed outcomes.
- Added a resumable Quick/Full cohort gate with deterministic contracts,
  parallel isolated matches and byte-for-byte comparison of all 13 scenarios
  and the aggregate result.

## [0.2.0] - 2026-07-18

This release completes the stable post-`v0.1.0` player-facing slice before the
next measured Easy/Normal/Hard behavior pass.

### Added - Encyclopedia

- Added a bilingual, data-driven in-game Encyclopedia covering the chronicle,
  factions, wizards, creatures, spells, classic rules and a practical game
  guide, with archived official Arcanium material preserved in context.
- Added a six-part Rune Game guide explaining the winning hand,
  Chow/Pung/Kong calls, winds and deals, spell-point economy, hand scoring and
  final campaign victory.
- Creature and spell entries now show their actual rune formulas as tile
  images alongside their spell-point cost.
- Added keyboard, mouse-wheel, Page Up/Page Down and secondary-click navigation
  for long lists and articles.

### Added - Action feedback

- Rejected human actions now show a localized reason for wrong turns and
  phases, illegal Rune Game calls, unavailable spells or summons, invalid
  targets, insufficient points, full parties and armies, illegal map orders and
  invalid battle choices.
- Expected AI candidate failures, replays and headless simulations remain
  silent and deterministic.

### Fixed - Rune Game scoring

- Restored the original 1998 final-hand formula: all base, set, pair and hand
  points are accumulated before doubles are applied, with the classic 500-point
  cap. The behavior was verified directly against the original executable and
  is covered by exact-value regression fixtures.

### Added - Display scaling

- Added selectable 75-200% smoothly scaled window sizes while keeping the
  original 1024x768 logical game canvas unchanged.
- Window-size choices apply without restarting and survive
  Windowed/Fullscreen transitions.
- Settings choice rows support left-click to advance and right-click to move
  backward, using SDL's cross-platform secondary-click handling.

### Fixed - Display scaling

- Fixed runtime window resizing so it no longer replaces the logical canvas
  with the physical window size or recreates the renderer and invalidates UI
  textures.
- Settings reads now start from stable defaults before applying theme and user
  overrides, preventing stale in-memory values when a base config is absent.

### Added - Settings

- Added independent 0–100% volume controls for music, sound effects and voices.
  The controls support mouse positioning plus Left/Right keyboard adjustment,
  apply without restarting and persist in the per-user `settings.json`.
- Kept load compatibility with existing boolean `music` and `sound` settings.
  Voice calls, Guardian announcements and both narrated intros now follow the
  voice level, while UI, rune, spell and battle sounds follow the effects level.

### Changed - Windows packaging

- Tagged Windows releases now start as ordinary desktop applications without
  opening a separate console window. Untagged development and Debug builds
  keep the console so live engine diagnostics remain available.

### Fixed - UI

- Replaced the legacy per-line scene widgets used by scrollable dialog text
  with a self-contained wrapped-text renderer. Repeatedly opening creature,
  spell or avatar information no longer revives stale rows from earlier
  dialogs or stacks descriptions on top of one another.
- Reserved every wrapped row from the font's line metrics, preventing a
  missing or undersized rendered texture from collapsing subsequent rows.
- Cleared the scrollable text surface before every redraw. Moving through a long
  description can no longer leave glyphs from the previous scroll position
  underneath the current lines.
- Fixed SWE's transparent render-target initialization. Full texture clears
  now temporarily disable alpha blending, preventing newly allocated text
  textures from inheriting fragments of previously freed GPU content.

### Testing

- Kept the Windows native crash-report regression strict while allowing for
  delayed filesystem visibility of a report already written by the crashed
  child process.

## [0.1.0] - 2026-07-17

First SemVer release candidate. This is the cumulative player-facing release
since the upstream baseline, including the earlier Reborn development
checkpoints listed at the end of this file.

### Fixed - Mahjong calls

- Fixed exposed Kong announcements from AI being sent as the concealed-Kong
  action type and therefore rejected.
- Restored the mandatory replacement draw after an exposed Kong. The missing
  draw could leave a player one rune short and eventually produce an empty
  concealed hand while the part continued.
- Prevented the dropper's stale local snapshot from briefly offering Game on
  their own discard, and added authoritative revalidation before a Game claim
  can finish the part.
- Cleared a completed multi-variant Chao selection before rendering refreshed
  state, avoiding invalid-rune additions in the log.

### Added - front end and presentation

- Added a full 1024x768 main menu with Reborn branding, the owner-selected
  remastered central artwork and working Continue, New Game, Load Game,
  Settings and Quit paths. Encyclopedia and Multiplayer are visible as
  deliberately disabled future features.
- Added development/release version information to the main menu. Development
  builds show their build date and revision; tagged builds show the release
  version.
- Restored the original seventeen-frame story introduction with authentic
  English and Russian narration. It plays once per launch and can be skipped
  with Esc, Enter, Space or a left click.
- Restored the original state-aware soundtrack: menu/selection, clan-specific
  Mahjong, Adventure map and victory/summary music. Visible battles remain
  intentionally unscored and resume map music when combat ends.
- Added persistent settings for English/Russian language, music, sound effects,
  Guardian voices, presentation speed and Windowed/Fullscreen display mode.
  Language and display mode can be changed without restarting.
- Added Classic, Normal and Fast presentation profiles. They affect deal,
  draw, discard, Mahjong-call and visible-combat pacing without changing rules,
  random results or authoritative state.

### Added - saves and recovery

- Added a stable autosave used by Continue and refreshed after completed
  Mahjong/Adventure rounds and confirmed returns to the main menu.
- Added user-named saves from the Mahjong and Adventure menus, including name
  entry, overwrite confirmation and a hidden backup of the replaced save.
- Added a Load Game screen for autosave and named saves. Valid rows support the
  Load button and a 500 ms double click.
- Added deletion of named saves with confirmation. Autosave and emergency
  recovery checkpoints cannot be deleted from the ordinary save list.
- Added rotating crash-recovery checkpoints in a separate Recovery view.
  Checkpoints are validated as complete Classic four-player states before they
  may be promoted to the current autosave; invalid files are preserved for
  diagnosis.
- Added save/recovery support for pending interactive battles, including saves
  between individual missile assignments.

### Added - gameplay and rules

- Implemented the missing Adventure and battle foundations needed to play
  complete rounds, including movement, Land Claim, combat resolution and score
  accounting.
- Added authoritative interactive battles with explicit leader, ranged and
  melee choices; stable battle-unit IDs; legal actor/target validation; AI
  recommendations; Auto Resolve; and deterministic event playback.
- Added Nucrus's Luck rune-draw choice and validation.
- Added observer-specific invisible-unit filtering. A player's adjacent See
  Invisible creatures reveal enemies only for that player, while Ziag's
  Monocle provides global detection and the Tower remains public.
- Added Easy, Normal and Hard AI difficulty. Difficulty changes planning depth,
  not unit statistics or random rolls. Hard uses the largest private evaluation
  budget and hides the player's map battle forecast.
- Added deterministic gameplay RNG, serialized random state, authoritative
  action replay and fixed-seed replay verification.

### Added - strategic AI and balance laboratory

- Added an observer-safe AI boundary so strategic planning cannot read hidden
  authoritative information unavailable to that player.
- Added bounded AI planning for rune retention, Mahjong calls, summons, spells,
  Adventure movement and battle choices.
- Added distinct behavior profiles and independent difficulty budgets, with
  deterministic traces for retained plans and their visible inputs.
- Added isolated headless full-match simulation with versioned JSON, CSV and
  text reports, per-event telemetry, complete replay retention and failure/
  statistical-outlier capture.
- Added balance tournaments, fixed faction/seat rotation, controlled
  avatar/faction cohorts, published tier reports and paired Ziag/Kierac
  comparisons in the Aqua slot. Cohort scenario IDs, filters and report rows use
  the same neutral color identities as the game.
- Added documented match-scoring and replay contracts so future balance changes
  can be compared against a reproducible baseline.

### Restored - localization and original-edition resources

- Added runtime English/Russian localization using terminology decoded from
  the localized original edition. The current Russian catalog contains 433
  translated messages; 68 long-form encyclopedia-oriented messages remain
  deferred.
- Restored Russian avatar, creature, clan, round and Mahjong announcements from
  the original localized sound resources. All 57 identified calls that differ
  between the English and Russian editions now follow the selected language;
  four unidentified differing resources remain unmapped.
- Restored localized Russian image buttons for shared controls and Mahjong
  actions. Reborn's new Order action has a matching Russian button; OK remains
  language-neutral, as in the localized original.
- Added translated player-facing ability descriptions and corrected visible
  English spellings for `Monocle`, `Catastrophic` and `Supplemental`. Historical
  internal identifiers such as `monacle` and `catasrophic` remain accepted for
  save/data compatibility.

### Changed - canonical data and documented rules

- Standardized neutral color faction identities throughout themes, saves,
  replays, command-line tools and balance reports: Red (`red`), Yellow
  (`yellow`), Aqua (`aqua`) and Purple (`purple`). The earlier named IDs remain
  load/input compatibility aliases and are no longer emitted as identity.
- Corrected the inherited transposed creature prices to match the classic
  manual: Griffon costs 170 and Tornado costs 240.
- Defined territory ranged fire as resolving before the simultaneous creature
  missile volley, following the classic manual's worked example.
- Defined Ziag's Monocle as global See Invisible without also granting
  Invisibility to his entire roster.
- Recorded intentional rules/data differences and compatibility contracts in
  `docs/RulesDecisions.md`, with focused regression coverage.

### Fixed - gameplay and stability

- Made exhaustion of the Mahjong wall a valid draw and taught summary screens
  to handle a round with no winner instead of crashing.
- Fixed a re-entrant duplicate Nucrus Luck prompt. Invalid selections are
  rejected without consuming either offered rune.
- Fixed a local Mahjong countdown surviving a submitted move and expiring
  during a later AI turn.
- Fixed invisible or invalid rune rendering paths and hardened theme resource
  validation.
- Fixed Adventure Save Game committing prepared movement orders before the
  name/overwrite dialogs were accepted. Cancel is now a true no-op.
- Fixed irregular Adventure province hit testing and dead strips along diagonal
  borders with exact point-in-polygon tests. All 45 province centers have
  regression coverage.
- Fixed drag movement being intercepted by the cursor-following clan flag;
  province hover and release now remain owned by the map.
- Fixed save deletion ordering so a Windows-locked authoritative `.sav` leaves
  its screenshot and backup intact for a safe retry.
- Fixed false `.mo` loading errors when running with built-in English, C or
  POSIX locales.
- Added state validation around actions, battle IDs, saves and recovery so
  invalid input is rejected without partially mutating authoritative state.

### Fixed - UI and display

- Fixed clipping and collisions in wizard selection, long spell lists, ability
  descriptions, AI difficulty, battle forecast, score tables and the Russian
  Load/Recovery button row.
- Fixed player forecast layout and prevented Hard difficulty from leaking it.
- Fixed Windowed/Fullscreen round trips, central 4:3 scaling of the logical
  1024x768 canvas, coordinate conversion and texture invalidation after a mode
  switch.
- Fixed map mouse coordinates and polygon/rectangle hit testing in the bundled
  engine continuation.
- Fixed music leaking between spell, map, Mahjong and battle screens or
  restarting unnecessarily when two screens request the same track.

### Added - diagnostics, tests and delivery

- Added rotating crash reports with recent-action breadcrumbs, uncaught C++/SWE
  exception details, clean-shutdown markers and native Windows crash fallback.
- Added focused regression suites for theme validation, gameplay, avatar
  metrics, crash reporting, recovery, settings, fixed-seed replay, headless
  simulation and native Windows crash capture.
- Added semantic JSON/theme validation in addition to syntax checks.
- Added repeatable CMake C++17 build and packaging scripts for Windows
  MSYS2/UCRT64, Linux and macOS. Windows packaging stages output separately and
  refuses to overwrite a running live distribution.
- Added GitHub Actions Debug/Release builds and tests on Windows, Linux and
  macOS for `develop` and `main`.
- Added SemVer release automation that requires the tagged commit to be on
  `main`, verifies the tag against the CMake project version and publishes
  platform archives with SHA-256 checksums.
- Adopted a `develop` integration branch and `main` release branch workflow,
  with explicit release reconciliation and documented hotfix handling.
- Split the inherited engine into the separately maintained
  `jaskes/four-winds-engine` submodule so engine fixes can be reviewed and
  versioned independently.

### Known deferred scope

- Encyclopedia content and Multiplayer remain disabled placeholders.
- Volume sliders, selectable resolutions, integer/high-DPI scaling and
  accessibility settings are not part of 0.1.0.
- Duel/two-hand and coalition modes require separate versioned rules, save and
  replay designs before implementation.
- Four unidentified Russian sound resources and 68 long-form localization
  messages remain restoration work.

## Historical Reborn development checkpoints

These date-based tags predate the SemVer release convention and are included
for traceability. Their changes are cumulative in 0.1.0.

### v20260716.3 - 2026-07-16

- Added observer-safe strategic AI, behavior/difficulty profiles and bounded
  multi-step planning.
- Added deterministic full-match simulation, replay verification, balance
  reports, controlled cohorts, tier lists and paired comparisons.
- Canonicalized clan identities and closed the remaining rules/visibility audit
  gaps found by the balance work.

### v20260716.2 - 2026-07-16

- Added interactive authoritative battles, stable choice validation, playback
  and battle/adventure regression coverage.
- Locked canonical combat, visibility and creature-price decisions in the rules
  ledger.

### v20260716.1 - 2026-07-16

- Stabilized Windows gameplay and dependency/build wrappers.
- Added deterministic replay foundations, crash/recovery diagnostics and the
  first cross-platform CI/release pipeline.
