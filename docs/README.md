# Documentation

This directory separates current Four Winds Reborn contracts from preserved
material belonging to the original game.

The public project overview and short roadmap live in the repository
[`README.md`](../README.md). Completed player-facing changes are recorded in
[`CHANGELOG.md`](../CHANGELOG.md). Source code and focused tests remain the
authority for implemented behaviour; the documents below explain the contracts
and evidence that should change with them.

The [v0.5.0 candidate review](RCReview-v0.5.0.md) records the local-mode fixes,
two-player Duel contract, verification evidence and remaining manual checks.

## Rules and contracts

- [`RulesDecisions.md`](RulesDecisions.md) — intentional decisions where
  surviving manuals, inherited data and current behaviour differ.
- [`BattleAndAdventureRules.md`](BattleAndAdventureRules.md) — current battle
  and map-phase contract.
- [`SpellLifecycle.md`](SpellLifecycle.md) — spell timing, validation and
  target lifecycle.
- [`MatchScoring.md`](MatchScoring.md) — final campaign ranking and tie
  handling.

## Balance and verification

- [`BalanceLab.md`](BalanceLab.md) — deterministic simulations, controlled
  cohorts, replay verification and the refactor gate.
- [`BalanceTiers.md`](BalanceTiers.md) — current measured avatar tiers and the
  evidence used to derive them.

## Content, localization and history

- [`Localization.md`](Localization.md) — English/Russian text, localized
  images, voice assets and per-theme intro timing.
- [`ArcaniumArchive.md`](ArcaniumArchive.md) — provenance ledger for the
  archived Arcanium website material used by the Encyclopedia.
- [`reference/classic/README.md`](reference/classic/README.md) — index of
  preserved original manuals, background texts and reference files. These are
  historical sources, not current instructions.

## Shipping

- [`AndroidDevelopment.md`](AndroidDevelopment.md) — reproducible native Android
  toolchain, runtime model, package contract and device acceptance checklist.
- [`ReleaseProcess.md`](ReleaseProcess.md) — branch policy, release gate,
  tagging, publication and hotfix flow.

The ignored local `WINDOWS_HANDOFF_CONTEXT.txt` is a working-machine handoff,
not public project documentation. It may contain current diagnostics and
uncommitted work notes that do not belong in a release.
