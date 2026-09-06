# Rules Decisions
This ledger records intentional Four Winds: Reborn behavior where the original
`reference/classic/Manual.txt`, inherited theme data, and the implemented rules
disagree. A data change is not canonical merely because it appears in the
current JSON: changing one of these decisions requires updating this document
and its focused test.

## RD-001: Tornado and Griffon prices

Status: accepted on 2026-07-16.

The classic manual prices Tornado at 240 and Griffon at 170. The inherited
Reborn theme had those values transposed: Tornado cost 170 and Griffon cost
240. Reborn now follows the manual: Tornado costs 240 and Griffon costs 170.

The old values made two-Tornado parties disproportionately efficient and
materially inflated Dayla and Nucrus in the deterministic balance diagnostic.
The transposition had no accompanying design note, while the creature order
and manual values provide a narrow, internally consistent correction.

Regression contract: `theme_data_validation` and `gameplay_regressions` assert
both prices. The avatar balance baseline must be reviewed after this change.

## RD-002: Carol and three spell prices

Status: accepted as the current Reborn balance baseline on 2026-07-16.

There are four total price differences, not four spell differences:

| Item | Classic manual | Reborn | Delta |
| --- | ---: | ---: | ---: |
| Carol the Great | 160 | 180 | +20 |
| Demonic Compulsion | 20 | 30 | +10 |
| Dispel Magic | 100 | 120 | +20 |
| Heroism | 50 | 30 | -20 |

These inherited values are retained until playtesting gives evidence for a
specific rebalance. Unlike the Tornado/Griffon transposition, they do not form
an obvious swap, and changing all four together would obscure which adjustment
caused an AI or balance shift.

Regression contract: `theme_data_validation` and `gameplay_regressions` assert
the Reborn values. Any future change should be a separate decision with balance
metrics and targeted playtest notes.

## RD-003: territory ranged timing

Status: accepted on 2026-07-16.

The manual's general combat paragraph calls the whole ranged round
simultaneous, including the territory. Its worked combat example is more
specific: territory fire resolves first, then creature ranged attacks resolve
simultaneously. Reborn follows the worked example.

The territory selects and damages an attacker first. A creature killed by that
shot does not enter the creature volley. Surviving attacking and defending
creatures then plan their shots before either creature side takes damage, so a
creature killed by the opposing creature volley still fires its planned shot.

This gives defending territory fire a real initiative advantage without
turning creature missiles into alternating attacks. `gameplay_regressions`
covers both a lethal territory shot and mutually lethal creature shooters.

## RD-004: Ziag lore and Monocle

Status: accepted on 2026-07-16.

The concise classic ability entry gives Ziag permanent See Invisibility. One
long inherited biography additionally claimed that all of his creatures are
invisible. Reborn implements Monocle as global visibility for Ziag and does not
grant Invisibility to his roster.

Blanket roster invisibility would be a second, very strong passive that is not
represented by the avatar ability or creature data. The player-facing English
biography now describes the implemented global reveal and no longer promises
the unimplemented passive.

Compatibility contract: the corrected player-facing spelling is `Monocle`, but
the historical data/save identifier remains `monacle`. The Classic theme must
assign Ziag `monacle`, and
`gameplay_regressions` proves that he receives invisible units anywhere on the
map without changing authoritative army state.

## RD-005: See Invisible observers

Status: accepted on 2026-07-16.

See Invisible is observer-specific. For ordinary avatars, an invisible enemy
on the map is revealed only when that observer's own army has a creature with
See Invisible in an adjacent territory. Territory ownership is irrelevant: a
detector in an invaded enemy territory still observes its neighbors. A third
player's Adventure Party or Griffon never shares visibility with the observer.

Additional visibility rules are explicit:

- A player always sees their own invisible creatures.
- Ziag's Monocle reveals all invisible creatures globally.
- Invisible creatures at the Tower of Four Winds are public to every player.
- Filtering changes only the copied `LocalData`; the authoritative armies are
  never modified.
- In combat, the resolver uses authoritative parties; map filtering does not
  remove a combatant from battle.

This preserves the manual's adjacent scouting role for Adventure Party and
Griffon while preventing unrelated armies from leaking hidden information.
`gameplay_regressions` covers no detector, a non-adjacent detector, an adjacent
detector on invaded land, a third-party detector, Monocle, own units, the Tower,
and authoritative-state purity.

## RD-006: canonical clan identities

Status: revised and accepted on 2026-07-17.

Reborn identifies the four playable factions by neutral color names in code,
theme data, saves, replays, command-line tools and reports:

| Canonical ID | Display name | Legacy named alias |
| --- | --- | --- |
| `red` | Red | `maitha` |
| `yellow` | Yellow | `kartha` |
| `aqua` | Aqua | `iz` |
| `purple` | Purple | `marz` |

The named strings are accepted only as load/input aliases for inherited saves,
early Reborn development replays and commands. Reborn never emits them as
faction identity. Original lore may still mention the historical peoples; that
restoration text is content, not an authoritative ID or current UI label.

The enum order remains unchanged, so the migration does not alter faction
rules, map ownership or seat scheduling. Serialized strings do change, which
intentionally changes authoritative state hashes and requires a fresh
deterministic canary. `theme_data_validation` locks the canonical theme IDs and
names; `gameplay_regressions` locks color output plus legacy named-alias loading.

## RD-007: versioned Rune Game rulesets

Status: accepted on 2026-07-21.

Classic Rune Game behavior is identified as `classic@1`. New saves, recovery
metadata, action replays, simulations and balance reports persist that exact
id/version. An artifact naming an unavailable id or incompatible version is
rejected instead of being opened under different rules.

Old artifacts without ruleset metadata remain load-compatible and resolve to
`classic@1` only. This fallback is deliberately load-only: every newly written
artifact is explicit. Replay state hashes omit the separately validated
identity field so historical Classic fixed-seed hashes remain stable; the
journal and its initial state must still name the same available ruleset.

Regression contract: `gameplay_regressions` covers explicit identity,
legacy fallback, mismatched or unavailable rejection, save/recovery/replay
round trips and simulation/report propagation. The controlled cohort manifest
includes the emitted `classic@1` CSV columns while the remaining gameplay
columns must stay byte-identical to the pre-persistence baseline.

## RD-008: versioned match topologies

Status: accepted on 2026-08-08.

Match topology is independent from the Rune Game ruleset. A ruleset defines
how the rune wall, calls, scoring and round flow work; a topology defines how
active seats and stable clans are assigned to controllers and competitive teams. Wind
is deliberately not used as ownership identity because seats rotate between
Rune Game rounds while control and allegiance must remain stable.
This separation lets Classic, Duel and Coalition reuse a compatible ruleset
without embedding player ownership into Mahjong rules.

Classic free-for-all is identified as `classic-ffa@1`: four clans, four
controllers and four teams. Legacy Duel is `duel@1`: the Red and Purple clans share
controller/team 0, while Yellow and Aqua share controller/team 1. The local
player therefore controls two hands and two clans; the active local hand
follows the Rune Game turn, and one discard-response window covers both local
hands. Coalition is `coalition@1`: all four clans retain separate controllers,
but Red/Purple and Yellow/Aqua are fixed allied teams. Allies do not attack,
target one another with hostile spells or occupy each other's land; their
scores are aggregated and they win or lose together.

New saves, recovery metadata and action replays persist the active topology.
An unavailable identity or a mismatch between an artifact and its embedded
state is rejected before gameplay state changes. Old artifacts without this
metadata resolve to `classic-ffa@1` only. As with ruleset and content-package
identity, the separately validated topology field is omitted from replay state
hashes. The authoritative Classic hash changed once when ruleset/topology
identity entered embedded game state and is locked by the fixed-seed replay
test thereafter.

## RD-009: Quick Rune Game ruleset

Status: accepted on 2026-08-08.

Quick Rune Game is identified as `quick@1`. It retains the complete Classic
rules for the wall, legal calls, scoring, spell-point awards, four wind seats
and seat rotation. Its only rules change is tournament length: it plays the
four hands of the East round and finishes after the North hand instead of
continuing through South, West and North rounds.

With the two-player topology below, each round instead has two deals (East
and West); Quick therefore ends after the West deal of the East round.

The setting is a default for newly created games only. Once a match starts,
its explicit ruleset identity is persisted in saves, recovery checkpoints and
replays; changing Settings cannot silently convert that match. Existing and
legacy games remain `classic@1` unless their artifact explicitly names an
available alternative.

Quick is the first production proof that the ruleset seam supports a real
player-facing variant without changing Classic behavior. Regression coverage
locks its registered identity, East-round completion point, settings
persistence, legacy fallback and restoration of Classic as the default.

## RD-010: two-player Duel and visible team modes

Status: accepted by the owner on 2026-09-06 during candidate review.

New Duel games use `duel@2`: exactly two players, one hand per player, one
human against one AI. Active seats are East and West. Turn advance, Chao's
next-player eligibility, AI responses, settlement and Adventure completion
visit only those seats. The original wall, 13-rune hand, draw limit and win
formula remain unchanged. A normal match plays two deals in each of four
rounds (eight hands); Quick plays two deals in the East round.

The selected wizard's clan owns the whole corresponding island half. Original
Red/Purple territory forms the western half; Yellow/Aqua forms the eastern
half. Each half is reassigned to its participating wizard, including its
summoning circles. The central Tower remains neutral. Captures can subsequently
change the border. Starting another four-player game restores original owners.

The mode uses two participant cards, two opposite table seats, two map-status
sections and two result columns. Coalition retains four hands and clans, with
the same two teams indicated on rosters, names, island outlines and summaries.
Its result columns aggregate each team's raw categories and standing points;
the team total, rank and victory are common to both members.

`duel@1` saves/recovery/replays retain their original four-hand semantics;
they are not silently converted. New `duel@2` saves require two unique seats,
opposite clan halves, and explicit ownership of every town by a participant.
Changing Settings affects new games only; Continue restores the saved mode.
