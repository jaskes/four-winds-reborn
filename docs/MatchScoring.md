# Match scoring

The final strategic score is a deterministic rules contract shared by the live
Game Summary screen and headless simulations. It is intentionally calculated
from authoritative end-of-match state rather than UI state or a simplified
balance model.

## Category scores

Each player receives five raw category scores:

| Category | Raw score |
| --- | --- |
| Territory | Sum of the point values of all owned territories |
| Summon Circle | Number of owned territories marked as summoning circles |
| Unit | Sum of the summon costs of all surviving creatures |
| Spell Point | Unspent spell points |
| Land Claim | Unspent land-claim points against opposing clans (allies excluded) |

Negative resource values are clamped to zero. Stable avatar, clan and wind IDs
identify a player; display names are never used as identity. Canonical faction
IDs are `red`, `yellow`, `aqua` and `purple` as defined by
[`RulesDecisions.md`](RulesDecisions.md#rd-006-canonical-clan-identities).

## Ranks and total

Each category is ranked independently, highest raw score first. Ties use
competition ranking: `1, 1, 3, 4`. With four players, a category rank awards
`4, 3, 2, 1` standing points respectively (`player count - rank + 1`). Tied
players receive the same standing points and the next rank is skipped.

`Total Score` is the sum of the five category standing-point awards. The final
rank is a second competition ranking over that total, again highest first.
This keeps land, armies and both resource economies relevant without allowing
the much larger numeric unit-cost scale to dominate every match.

The Game Summary screen displays each raw category score beside its category
rank, followed by total standing points and final rank. `Simulation::MatchResult`
contains the same structured result. Any formula change must update this file,
the pure tie/rank regression and the deterministic full-match canary together.

The original 1998 executable and installed Win98 saves are useful behavioral
references, but no recoverable source-level final-map scoring contract exists in
the inherited engine. This documented Reborn contract is therefore explicit
instead of presenting an uncertain reverse-engineering guess as classic fact.

## Duel and Coalition

Two-player Duel ranks the two active players in each category, awarding 2 or
1 standing points (2 each on a tie). Its total is the sum over five categories.
Coalition first calculates each of the four players' category standing points,
then sums the two allies' totals into their shared team score. Team ranks and
victory compare those two team totals. A tied match shows both sides as winners.

The Duel/Coalition result screen has two columns, one per player/team. Each
category shows its raw score and awarded standing points; in Coalition both
are sums of the two allies' values. The displayed final score equals the sum
of the five displayed point awards. Classic keeps its existing four-player
score/rank table.