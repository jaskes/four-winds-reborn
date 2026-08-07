# Release Process

Four Winds Reborn uses two permanent branches:

- `develop` is the integration branch. Normal gameplay, UI, content and tooling
  changes land here as focused commits.
- `main` is the published release history. It receives completed release merges
  and exceptional hotfixes; it is not a general work branch.

Do not force-push either permanent branch or rewrite published release history.

## Versioning

Release tags use SemVer in the `vMAJOR.MINOR.PATCH` form. While the project is
pre-1.0, increment MINOR for a completed player-facing roadmap slice and PATCH
for a compatible hotfix. Reserve `v1.0.0` for the accepted stable core roadmap.

The tag must exactly match `project(... VERSION ...)` in `CMakeLists.txt`.
The release workflow rejects malformed tags, version mismatches and commits
that are not already present on `origin/main`.

## Normal development

Start work from current `develop`, keep related changes reviewable and push the
integration branch:

```bash
git switch develop
git pull --ff-only origin develop
# edit, test and commit
git push origin develop
```

CI runs for pushes and pull requests targeting both permanent branches.

## Release gate

Before merging a release, freeze its scope and complete all of the following:

1. Update `CHANGELOG.md`, the public `README.md`, the CMake version and any
   affected contracts in `docs/`.
2. Run the theme and content validator:

   ```bash
   python scripts/validate-json.py
   ```

3. Produce a clean Windows package and pass all local CTest targets.
4. Run the full deterministic refactor gate:

   ```powershell
   .\scripts\run-refactor-gate.ps1 -Mode Full
   ```

   This compares all 52 controlled matches with the retained fixed-seed
   baseline. A deliberate rules or balance change requires reviewed evidence
   and an intentional baseline update, never a silent replacement.
5. Require green Linux, macOS, Windows and Android CI for the release source.
6. For an Android release, verify the exact APK with
   `scripts/test-android-package.ps1` and complete the physical-device matrix
   in [`AndroidDevelopment.md`](AndroidDevelopment.md). Compilation or an
   emulator-only pass is not sufficient for lifecycle, touch and storage
   acceptance.
7. Smoke-test the exact desktop player package rather than a development-tree
   binary:

   - verify that a clean profile starts in Classic;
   - switch between Classic and Reborn and verify that the selection persists;
   - verify that an old `content:theme = default` setting migrates to Classic;
   - start English and Russian games and check intro, voices, music and effects;
   - create, load, overwrite and delete a named save; verify autosave/recovery;
   - record, browse, export/import and play a compatible replay;
   - switch windowed/fullscreen modes and resize the window;
   - complete a Rune Game hand, Adventure movement, a manual battle and the
     score/victory flow.

Reborn may ship with `release` provenance status when the validator reports
zero unclassified inherited live media and zero identical narrative fields.
Generic non-voice compatibility effects are permitted only when every exact
path is declared in `retainedCompatibilityMedia`.

Advertising Reborn as legally distinct and standalone is a stronger claim:
`themes/reborn/provenance.json` must then have `standalone` status and the
validator must additionally report zero retained compatibility media.
Package-neutral project assets may remain shared only when their exact paths
are listed in `sharedIdenticalMedia`.

For v0.3.0, Classic is the initial presentation and Reborn is an opt-in
release-status package. Reborn must not be labelled Preview solely because
generic compatibility effects remain. Switching the initial presentation to
Reborn is a separate product decision to make after README screenshots are
updated and player feedback has been reviewed.

## Tag and publish

After the gate and owner smoke test pass, merge the complete integration branch
into `main` as an explicit release commit. Replace `0.3.0` below with the
version being published:

```bash
git switch main
git pull --ff-only origin main
git merge --no-ff develop -m "release: v0.3.0"
git push origin main
git tag -a v0.3.0 -m "Four Winds Reborn v0.3.0"
git push origin v0.3.0
```

Push `main` before the tag. The accepted tag builds and tests Linux, macOS and
Windows packages, verifies that the Windows executable uses the GUI subsystem,
generates `SHA256SUMS.txt` and publishes the GitHub Release.

Verify all three downloadable archives and their checksums after publication,
then return to `develop`. If the release merge itself contains any release-only
change, reconcile `main` back into `develop`.

## Hotfix

Create a focused hotfix from `main`, validate the affected paths and the normal
release gate, then publish a PATCH version with the same merge-and-tag order.
Merge the released `main` back into `develop` so the correction cannot disappear
from the next MINOR release:

```bash
git switch develop
git pull --ff-only origin develop
git merge --no-ff main -m "reconcile hotfix v0.3.1"
git push origin develop
```

Generated builds, diagnostics, dumps, downloaded packages, credentials and
local handoff notes must never be staged in either branch.
