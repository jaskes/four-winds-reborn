#include <algorithm>
#include <cmath>
#include <tuple>

#include "aispell.h"

namespace GameData
{
    extern LocalPlayers gamers;
}

namespace
{
    enum class SpellRole
    {
        Damage,
        Control,
        Support,
        Economy,
        Mobility,
        Utility
    };

    using SpellCandidates = std::vector<AI::SpellCastStep>;
    using PlayerView = std::vector<const LocalPlayer*>;

    PlayerView authoritativePlayerView(void)
    {
        PlayerView result;
        result.reserve(GameData::gamers.size());
        for(const LocalPlayer & player : GameData::gamers) result.push_back(&player);
        return result;
    }

    PlayerView observerPlayerView(const LocalData & local)
    {
        PlayerView result;
        result.reserve(local.players.size());
        for(const LocalPlayer & player : local.players) result.push_back(&player);
        return result;
    }

    const BattleCreature* visibleBattleCreature(const PlayerView & players, int unit)
    {
        for(const LocalPlayer* player : players)
        {
            if(!player) continue;
            const BattleCreature* creature = player->army.findBattleUnitConst(unit);
            if(creature) return creature;
        }
        return nullptr;
    }

    int statValue(const BaseStat & stat)
    {
        return stat.attack * 30 + stat.ranger * 24 + stat.defense * 28 + stat.loyalty * 26;
    }

    int creatureValue(const BattleCreature & creature)
    {
        return creature.attack() * 12 + creature.ranger() * 10 + creature.defense() * 11 +
               creature.loyalty() * 12 + creature.freeMovePoint() * 4;
    }

    int resistanceAdjusted(const BattleCreature & creature, int score)
    {
        if(!creature.haveSpeciality(Speciality::MagicResistence)) return score;

        const int chance = SpecialityMagicResistence().chance(creature.id());
        return score * (100 - chance) / 100;
    }

    int enchantmentValue(const AffectedSpell & affected)
    {
        const SpellInfo & info = GameData::spellInfo(affected);
        int result = 20 + std::abs(statValue(info.effect));

        switch(affected())
        {
            case Spell::Paralyze: result += 65; break;
            case Spell::ForceShield: result += 45; break;
            case Spell::MysticalFountainAttack:
            case Spell::MysticalFountainRange:
            case Spell::MysticalFountainLoyalty: result += 30; break;
            default: break;
        }
        return result;
    }

    int dispelValue(const BattleCreature & creature, bool friendly)
    {
        int result = 0;
        for(const auto & affected : creature.affectedSpells())
        {
            const SpellInfo & info = GameData::spellInfo(affected);
            const bool removalHelps = friendly ? info.isCurse() : !info.isCurse();
            result += (removalHelps ? 1 : -1) * enchantmentValue(affected);
        }
        return resistanceAdjusted(creature, result);
    }

    bool hasMysticalFountain(const BattleCreature & creature)
    {
        return creature.isAffectedSpell(Spell::MysticalFountainAttack) ||
               creature.isAffectedSpell(Spell::MysticalFountainRange) ||
               creature.isAffectedSpell(Spell::MysticalFountainLoyalty);
    }

    int friendlySpellValue(const SpellInfo & info, const BattleCreature & creature)
    {
        if(!creature.canReceiveSpell(info.id)) return 0;
        if(info.persistent && creature.isAffectedSpell(info.id)) return 0;
        if(info.id() == Spell::MysticalFountain && hasMysticalFountain(creature)) return 0;

        switch(info.id())
        {
            case Spell::Healing:
            {
                const int missing = std::max(0, creature.baseLoyalty() - creature.loyalty());
                const int score = missing ? 20 + std::min(missing, std::max(1, info.effect.loyalty)) * 45 : 0;
                return resistanceAdjusted(creature, score);
            }

            case Spell::ForceShield:
                return resistanceAdjusted(creature, 70 + creature.baseLoyalty() * 2);

            case Spell::MysticalFountain:
                return resistanceAdjusted(creature, 70 + creatureValue(creature) / 12);

            case Spell::BlindAmbition:
                if(creature.loyalty() + info.effect.loyalty <= 0) return 0;
                break;

            case Spell::DispelMagic:
                return std::max(0, dispelValue(creature, true));

            default: break;
        }

        const int result = 35 + statValue(info.effect) + (info.persistent ? 20 : 0) + creatureValue(creature) / 15;
        return resistanceAdjusted(creature, result);
    }

    int enemySpellValue(const SpellInfo & info, const BattleCreature & creature)
    {
        if(!creature.canReceiveSpell(info.id)) return 0;
        if(info.persistent && creature.isAffectedSpell(info.id)) return 0;

        int result = 0;
        switch(info.id())
        {
            case Spell::LightningBolt:
                result = 50 + creatureValue(creature) / 10;
                if(creature.loyalty() <= std::abs(info.effect.loyalty)) result += 120;
                break;

            case Spell::Paralyze:
                result = creature.freeMovePoint() ? 60 + creature.freeMovePoint() * 14 : 0;
                break;

            case Spell::DispelMagic:
                result = std::max(0, dispelValue(creature, false));
                break;

            default:
                result = 35 - statValue(info.effect) + creatureValue(creature) / 18;
                if(info.effect.ranger < 0 && creature.ranger() <= 0) result = 0;
                if(info.effect.attack < 0 && creature.attack() <= 0) result = 0;
                if(info.effect.defense < 0 && creature.defense() <= 0) result = 0;
                break;
        }
        return resistanceAdjusted(creature, result);
    }

    int partySpellValue(const SpellInfo & info, const BattleParty & party)
    {
        int result = 0;
        for(auto creature : party.toBattleCreatures())
        {
            switch(info.id())
            {
                case Spell::HellBlast:
                {
                    int creatureScore = 45 + creatureValue(*creature) / 14;
                    if(creature->loyalty() <= std::abs(info.effect.loyalty)) creatureScore += 100;
                    result += resistanceAdjusted(*creature, creatureScore);
                    break;
                }

                case Spell::MassPanic:
                    if(!creature->isAffectedSpell(info.id)) result += enemySpellValue(info, *creature);
                    break;

                default:
                    result += enemySpellValue(info, *creature);
                    break;
            }
        }
        return result;
    }

    int borderThreat(const Land & land, const Clan & clan)
    {
        if(!land.isValid()) return 0;

        int result = 0;
        for(const Land & border : GameData::landInfo(land).borders)
        {
            if(border.isTowerWinds()) continue;
            const Clan & owner = GameData::landInfo(border).clan;
            if(owner.isValid() && !GameData::allied(owner, clan)) result++;
        }
        return result;
    }

    bool betterStep(const AI::SpellCastStep & candidate, const AI::SpellCastStep & current)
    {
        if(candidate.score != current.score) return candidate.score > current.score;
        return std::make_tuple(candidate.spell(), candidate.target(), candidate.land(), candidate.unit) <
               std::make_tuple(current.spell(), current.target(), current.land(), current.unit);
    }

    bool sameStep(const AI::SpellCastStep & left, const AI::SpellCastStep & right)
    {
        return left.spell == right.spell && left.target == right.target && left.land == right.land &&
               left.unit == right.unit;
    }

    void addCandidate(SpellCandidates & candidates, const AI::SpellCastStep & candidate)
    {
        if(0 < candidate.score) candidates.push_back(candidate);
    }

    void evaluatePlayerSpell(SpellCandidates & candidates, const LocalPlayer & player,
                             const SpellInfo & info, const PlayerView & players)
    {
        if(info.target() == SpellTarget::MyPlayer)
        {
            if(player.isAffectedSpell(info.id)) return;

            AI::SpellCastStep candidate;
            candidate.spell = info.id;
            candidate.score = 55;
            addCandidate(candidates, candidate);
            return;
        }

        if(info.target() == SpellTarget::AllPlayers)
        {
            int enemyPressure = 0;
            for(const LocalPlayer* other : players)
            {
                if(!other || GameData::allied(*other, player) || other->isAffectedSpell(info.id)) continue;
                enemyPressure += 25 + other->points / 20 + (other->isCasted() ? 0 : 15);
            }

            AI::SpellCastStep candidate;
            candidate.spell = info.id;
            candidate.score = 25 + enemyPressure - player.points / 25;
            addCandidate(candidates, candidate);
            return;
        }

        if(info.target() != SpellTarget::OtherPlayer) return;

        for(const LocalPlayer* other : players)
        {
            if(!other || GameData::allied(*other, player) || other->isAffectedSpell(info.id)) continue;

            int score = 0;
            switch(info.id())
            {
                case Spell::Silence:
                    if(GameData::avatarInfo(other->avatar).ability() == Ability::Telepath) continue;
                    score = 70 + other->points / 20 + (other->isCasted() ? 0 : 15);
                    break;

                case Spell::RandomDiscard:
                    score = 45 + static_cast<int>(other->stones.size()) * 3;
                    break;

                case Spell::ScryRunes:
                    score = 35 + static_cast<int>(other->stones.size()) * 2;
                    break;

                default: break;
            }

            AI::SpellCastStep candidate;
            candidate.spell = info.id;
            candidate.target = other->avatar;
            candidate.score = score;
            addCandidate(candidates, candidate);
        }
    }

    void evaluateTeleport(SpellCandidates & candidates, const LocalPlayer & player, const SpellInfo & info)
    {
        for(const auto & source : player.army)
        {
            const int sourceThreat = borderThreat(source.land(), player.clan);
            for(auto creature : source.toBattleCreatures())
            {
                for(auto landId : lands_all)
                {
                    const Land destination(landId);
                    if(destination == source.land()) continue;

                    const LandInfo & destinationInfo = GameData::landInfo(destination);
                    if(!destination.isTowerWinds() && destinationInfo.clan != player.clan) continue;

                    const BattleParty* destinationParty = player.army.findPartyConst(destination);
                    if(destinationParty && !destinationParty->canJoin()) continue;

                    const int destinationThreat = borderThreat(destination, player.clan);
                    AI::SpellCastStep candidate;
                    candidate.spell = info.id;
                    candidate.land = destination;
                    candidate.unit = creature->battleUnit();
                    candidate.score = 30 + (destinationThreat - sourceThreat) * 30 +
                                      destinationInfo.stat.point / 20 + (destinationInfo.stat.power ? 10 : 0) +
                                      creatureValue(*creature) / 20;
                    addCandidate(candidates, candidate);
                }
            }
        }
    }

    void evaluateLandDispel(SpellCandidates & candidates, const LocalPlayer & player,
                            const SpellInfo & info, const PlayerView & players)
    {
        for(auto landId : lands_all)
        {
            const Land land(landId);
            int score = 0;
            int targets = 0;

            for(const LocalPlayer* other : players)
            {
                if(!other) continue;
                const BattleParty* party = other->army.findPartyConst(land);
                if(!party) continue;

                for(auto creature : party->toBattleCreatures())
                {
                    score += dispelValue(*creature, GameData::allied(other->clan, player.clan));
                    targets++;
                }
            }

            if(!targets || score <= 0) continue;

            AI::SpellCastStep candidate;
            candidate.spell = info.id;
            candidate.land = land;
            candidate.score = 25 + score;
            addCandidate(candidates, candidate);
        }
    }

    void evaluateBoardSpell(SpellCandidates & candidates, const LocalPlayer & player,
                            const SpellInfo & info, const PlayerView & players)
    {
        if(info.id() == Spell::Teleport)
        {
            evaluateTeleport(candidates, player, info);
            return;
        }

        if(info.target() == SpellTarget::Land)
        {
            evaluateLandDispel(candidates, player, info, players);
            return;
        }

        for(const LocalPlayer* other : players)
        {
            if(!other) continue;
            const bool friendly = GameData::allied(other->clan, player.clan);
            const bool allowedOwner = ((info.target() & SpellTarget::Friendly) && friendly) ||
                                      ((info.target() & SpellTarget::Enemy) && !friendly);
            if(!allowedOwner) continue;

            for(const auto & party : other->army)
            {
                const BattleCreatures creatures = party.toBattleCreatures();
                if(creatures.empty()) continue;

                if(info.target() & SpellTarget::Party)
                {
                    AI::SpellCastStep candidate;
                    candidate.spell = info.id;
                    candidate.land = party.land();
                    candidate.unit = creatures.front()->battleUnit();
                    candidate.score = partySpellValue(info, party);
                    addCandidate(candidates, candidate);
                    continue;
                }

                for(auto creature : creatures)
                {
                    AI::SpellCastStep candidate;
                    candidate.spell = info.id;
                    candidate.land = party.land();
                    candidate.unit = creature->battleUnit();
                    candidate.score = friendly ? friendlySpellValue(info, *creature) :
                                                 enemySpellValue(info, *creature);
                    addCandidate(candidates, candidate);
                }
            }
        }
    }

    SpellCandidates generateCandidates(const LocalPlayer & player, const Spells & spells,
                                       const PlayerView & players)
    {
        SpellCandidates candidates;
        for(const Spell & spell : spells)
        {
            const SpellInfo & info = GameData::spellInfo(spell);
            if(info.cost > player.points) continue;

            if(info.target() == SpellTarget::MyPlayer || info.target() == SpellTarget::OtherPlayer ||
               info.target() == SpellTarget::AllPlayers)
                evaluatePlayerSpell(candidates, player, info, players);
            else
                evaluateBoardSpell(candidates, player, info, players);
        }
        return candidates;
    }

    SpellRole spellRole(const SpellInfo & info)
    {
        switch(info.id())
        {
            case Spell::LightningBolt:
            case Spell::HellBlast:
                return SpellRole::Damage;

            case Spell::Smoke:
            case Spell::MassPanic:
            case Spell::Paralyze:
            case Spell::Reduction:
            case Spell::DustCloud:
            case Spell::BrilliantLights:
            case Spell::DispelMagic:
            case Spell::RandomDiscard:
            case Spell::ScryRunes:
            case Spell::Silence:
            case Spell::ManaFog:
            case Spell::MassDispel:
                return SpellRole::Control;

            case Spell::DrawSkull:
            case Spell::DrawSword:
            case Spell::DrawNumber:
                return SpellRole::Economy;

            case Spell::Teleport:
                return SpellRole::Mobility;

            default:
                if(info.target() & SpellTarget::Friendly) return SpellRole::Support;
                return SpellRole::Utility;
        }
    }

    int roleWeight(const AI::BehaviorRules & rules, SpellRole role)
    {
        switch(role)
        {
            case SpellRole::Damage: return rules.damageWeight;
            case SpellRole::Control: return rules.controlWeight;
            case SpellRole::Support: return rules.supportWeight;
            case SpellRole::Economy: return rules.economyWeight;
            case SpellRole::Mobility: return rules.mobilityWeight;
            default: return 100;
        }
    }

    bool isOffensiveSpell(const SpellInfo & info)
    {
        const int target = info.target();
        return spellRole(info) == SpellRole::Damage ||
               target == SpellTarget::Enemy ||
               target == (SpellTarget::Enemy | SpellTarget::Party) ||
               target == SpellTarget::OtherPlayer || target == SpellTarget::AllPlayers;
    }

    int profileScore(const AI::SpellCastStep & step, AI::BehaviorProfile profile,
                     AI::Difficulty difficulty, int availablePoints)
    {
        const AI::BehaviorRules & rules = AI::behaviorRules(profile);
        const SpellInfo & info = GameData::spellInfo(step.spell);
        int score = step.score * roleWeight(rules, spellRole(info)) / 100;
        score -= info.cost * rules.spellCostPenalty / 100;
        if(isOffensiveSpell(info))
            score -= AI::difficultyRules(difficulty).offensiveSpellPenalty;

        const int pointsAfterCast = availablePoints - info.cost;
        const int reserve = AI::manaReserve(profile, difficulty);
        if(pointsAfterCast < reserve && step.score < 150)
            score -= (reserve - pointsAfterCast) * rules.reservePenalty / 100;

        return score;
    }

    bool isDirectKill(const AI::SpellCastStep & step, const PlayerView & players)
    {
        if(step.unit <= 0) return false;
        if(step.spell() != Spell::LightningBolt) return false;

        const BattleCreature* creature = visibleBattleCreature(players, step.unit);
        const SpellInfo & info = GameData::spellInfo(step.spell);
        return creature && creature->loyalty() <= std::abs(info.effect.loyalty);
    }

    int comboSynergy(const AI::SpellCastStep & first, const AI::SpellCastStep & second,
                     const PlayerView & players)
    {
        if(sameStep(first, second) || first.spell() == Spell::ManaFog) return 0;

        const bool sameUnit = 0 < first.unit && first.unit == second.unit;
        const bool sameLand = first.land.isValid() && first.land == second.land;
        const bool samePlayer = first.target.isValid() && first.target == second.target;
        if(sameUnit && isDirectKill(first, players)) return 0;

        const SpellRole firstRole = spellRole(GameData::spellInfo(first.spell));
        const SpellRole secondRole = spellRole(GameData::spellInfo(second.spell));
        int result = 0;

        switch(first.spell())
        {
            case Spell::Smoke:
            case Spell::Reduction:
            case Spell::DustCloud:
            case Spell::BrilliantLights:
                if(sameUnit && second.spell() == Spell::Paralyze) result += 55;
                if(sameUnit && secondRole == SpellRole::Damage) result += 35;
                break;

            case Spell::Paralyze:
                if(sameUnit && secondRole == SpellRole::Damage) result += 75;
                if(sameLand && second.spell() == Spell::HellBlast) result += 50;
                break;

            case Spell::MassPanic:
                if(sameLand && secondRole == SpellRole::Damage) result += 85;
                break;

            case Spell::DispelMagic:
                if(sameUnit && (secondRole == SpellRole::Control || secondRole == SpellRole::Damage))
                    result += 45;
                break;

            case Spell::MassDispel:
                if(sameLand && (secondRole == SpellRole::Control || secondRole == SpellRole::Damage))
                    result += 45;
                break;

            case Spell::BlindAmbition:
                if(sameUnit && second.spell() == Spell::Healing) result += 80;
                break;

            case Spell::ScryRunes:
                if(samePlayer && second.spell() == Spell::Silence) result += 35;
                if(samePlayer && second.spell() == Spell::RandomDiscard) result += 65;
                break;

            case Spell::Silence:
                if(samePlayer && second.spell() == Spell::ScryRunes) result += 35;
                if(samePlayer && second.spell() == Spell::RandomDiscard) result += 55;
                break;

            case Spell::RandomDiscard:
                if(samePlayer && second.spell() == Spell::ScryRunes) result += 30;
                break;

            case Spell::DrawSkull:
            case Spell::DrawSword:
            case Spell::DrawNumber:
                if(secondRole != SpellRole::Economy) result += 25;
                break;

            case Spell::BattleFury:
            case Spell::Guidance:
            case Spell::ForceShield:
            case Spell::Heroism:
            case Spell::MysticalFountain:
            case Spell::MagicalAura:
                if(sameUnit && second.spell() == Spell::Teleport) result += 45;
                break;

            case Spell::Teleport:
                if(sameUnit && secondRole == SpellRole::Support) result += 35;
                break;

            default: break;
        }

        if(sameUnit && firstRole == SpellRole::Control && secondRole == SpellRole::Damage)
            result += 25;
        if(sameUnit && firstRole == SpellRole::Support && secondRole == SpellRole::Mobility)
            result += 20;

        return result;
    }

    bool stepWasUsed(const std::vector<AI::SpellCastStep> & used, const AI::SpellCastStep & candidate)
    {
        return std::any_of(used.begin(), used.end(), [&](const AI::SpellCastStep & step)
        {
            return sameStep(step, candidate);
        });
    }

    void projectFollowUps(AI::SpellCastPlan & plan, const LocalPlayer & player,
                          const SpellCandidates & futureCandidates, const PlayerView & players,
                          AI::Difficulty difficulty)
    {
        const AI::BehaviorRules & rules = AI::behaviorRules(plan.profile);
        const int planningHorizon = AI::spellPlanningHorizon(plan.profile, difficulty);
        if(planningHorizon <= 1 || plan.spell() == Spell::ManaFog) return;

        AI::SpellCastStep root = plan;
        root.score = plan.immediateScore;
        AI::SpellCastStep previous = root;
        std::vector<AI::SpellCastStep> used(1, root);
        int availablePoints = player.points - GameData::spellInfo(plan.spell).cost;
        int discount = rules.futureDiscount;

        for(int depth = 1; depth < planningHorizon; ++depth)
        {
            AI::SpellCastStep best;
            AI::SpellCastStep bestRaw;

            for(const AI::SpellCastStep & candidate : futureCandidates)
            {
                const SpellInfo & info = GameData::spellInfo(candidate.spell);
                if(info.cost > availablePoints || stepWasUsed(used, candidate)) continue;

                int synergy = comboSynergy(previous, candidate, players);
                if(1 < depth) synergy += comboSynergy(root, candidate, players) / 2;
                if(synergy <= 0) continue;

                AI::SpellCastStep scored = candidate;
                const int weighted = profileScore(candidate, plan.profile, difficulty,
                                                  availablePoints);
                const int combined = weighted + synergy * rules.comboWeight / 100;
                scored.score = std::max(0, combined) * discount / 100;
                if(!scored.isValid()) continue;

                if(!best.isValid() || betterStep(scored, best))
                {
                    best = scored;
                    bestRaw = candidate;
                }
            }

            if(!best.isValid()) break;

            plan.followUps.push_back(best);
            plan.futureScore += best.score;
            availablePoints -= GameData::spellInfo(best.spell).cost;
            used.push_back(bestRaw);
            previous = bestRaw;
            discount = discount * rules.futureDiscount / 100;
        }
    }

    bool betterPlan(const AI::SpellCastPlan & candidate, const AI::SpellCastPlan & current)
    {
        if(candidate.score != current.score) return candidate.score > current.score;
        if(candidate.immediateScore != current.immediateScore)
            return candidate.immediateScore > current.immediateScore;
        return betterStep(candidate, current);
    }

    void sortUnique(Spells & spells)
    {
        std::sort(spells.begin(), spells.end(), [](const Spell & left, const Spell & right)
        {
            return left() < right();
        });
        spells.erase(std::unique(spells.begin(), spells.end()), spells.end());
    }

    AI::SpellCastPlan chooseSpellCastFromView(const LocalPlayer & player, const Spells & spells,
                                              AI::BehaviorProfile profile,
                                              AI::Difficulty difficulty,
                                              const Spells & futureSpells,
                                              const PlayerView & players)
    {
        AI::SpellCastPlan empty;
        empty.profile = profile;
        if(player.isCasted() || player.isSilenced() || player.isAffectedSpell(Spell::ManaFog))
            return empty;

        const AI::BehaviorRules & rules = AI::behaviorRules(profile);
        const SpellCandidates candidates = generateCandidates(player, spells, players);
        const SpellCandidates futureCandidates = generateCandidates(player, futureSpells, players);
        std::vector<AI::SpellCastPlan> plans;
        const bool supportOnly = AI::difficultyRules(difficulty).supportSpellsOnly;

        for(const AI::SpellCastStep & step : candidates)
        {
            if(supportOnly && step.spell != Spell(Spell::Healing)) continue;

            AI::SpellCastPlan candidate(step);
            candidate.profile = profile;
            candidate.immediateScore = profileScore(step, profile, difficulty, player.points);
            if(supportOnly)
            {
                SpellCandidates healingOnly;
                std::copy_if(futureCandidates.begin(), futureCandidates.end(),
                             std::back_inserter(healingOnly), [](const AI::SpellCastStep & future)
                {
                    return future.spell == Spell(Spell::Healing);
                });
                projectFollowUps(candidate, player, healingOnly, players, difficulty);
            }
            else
            {
                projectFollowUps(candidate, player, futureCandidates, players, difficulty);
            }
            candidate.score = candidate.immediateScore + candidate.futureScore;

            if(candidate.score < rules.minimumSpellScore) continue;
            plans.push_back(candidate);
        }

        if(plans.empty()) return empty;
        std::sort(plans.begin(), plans.end(), [](const AI::SpellCastPlan & left,
                                                 const AI::SpellCastPlan & right)
        {
            return betterPlan(left, right);
        });

        const int decisionKey = player.avatar() * 97 + player.points * 7 +
            static_cast<int>(player.stones.size()) * 13 +
            static_cast<int>(player.army.size()) * 17;
        if(1 < plans.size() && AI::preferNearBestChoice(
               difficulty, plans[0].score, plans[1].score, decisionKey))
            return plans[1];
        return plans.front();
    }
}

AI::SpellCastPlan::SpellCastPlan()
    : profile(BehaviorProfile::Balanced), immediateScore(0), futureScore(0)
{
}

AI::SpellCastPlan::SpellCastPlan(const SpellCastStep & step)
    : SpellCastStep(step), profile(BehaviorProfile::Balanced), immediateScore(step.score), futureScore(0)
{
}

Spells AI::knownSpellCasts(const LocalPlayer & player)
{
    Spells result = GameData::avatarInfo(player.avatar).spells;
    const Spells armySpells = player.army.allCastSpells();
    result.insert(result.end(), armySpells.begin(), armySpells.end());
    sortUnique(result);
    return result;
}

Spells AI::availableSpellCasts(const LocalPlayer & player)
{
    Spells result = knownSpellCasts(player);
    result.erase(std::remove_if(result.begin(), result.end(), [&](const Spell & spell)
    {
        const SpellInfo & info = GameData::spellInfo(spell);
        return player.isCasted() || info.cost > player.points || !player.allowCastSpell(spell);
    }), result.end());
    return result;
}

AI::SpellCastPlan AI::chooseSpellCast(const LocalPlayer & player, const Spells & spells)
{
    return chooseSpellCast(player, spells, behaviorProfile(player), knownSpellCasts(player));
}

AI::SpellCastPlan AI::chooseSpellCast(const LocalPlayer & player, const Spells & spells,
                                      BehaviorProfile profile)
{
    return chooseSpellCast(player, spells, profile, GameData::aiDifficulty(),
                           knownSpellCasts(player));
}

AI::SpellCastPlan AI::chooseSpellCast(const LocalPlayer & player, const Spells & spells,
                                      BehaviorProfile profile, Difficulty difficulty)
{
    return chooseSpellCast(player, spells, profile, difficulty, knownSpellCasts(player));
}

AI::SpellCastPlan AI::chooseSpellCast(const LocalPlayer & player, const Spells & spells,
                                      BehaviorProfile profile, const Spells & futureSpells)
{
    return chooseSpellCast(player, spells, profile, GameData::aiDifficulty(), futureSpells);
}

AI::SpellCastPlan AI::chooseSpellCast(const LocalPlayer & player, const Spells & spells,
                                      BehaviorProfile profile, Difficulty difficulty,
                                      const Spells & futureSpells)
{
    return chooseSpellCastFromView(player, spells, profile, difficulty, futureSpells,
                                   authoritativePlayerView());
}

AI::SpellCastPlan AI::chooseSpellCast(const LocalData & local, const Spells & spells,
                                      BehaviorProfile profile)
{
    return chooseSpellCast(local, spells, profile, GameData::aiDifficulty(),
                           knownSpellCasts(local.myPlayer()));
}

AI::SpellCastPlan AI::chooseSpellCast(const LocalData & local, const Spells & spells,
                                      BehaviorProfile profile, Difficulty difficulty)
{
    return chooseSpellCast(local, spells, profile, difficulty,
                           knownSpellCasts(local.myPlayer()));
}

AI::SpellCastPlan AI::chooseSpellCast(const LocalData & local, const Spells & spells,
                                      BehaviorProfile profile, const Spells & futureSpells)
{
    return chooseSpellCast(local, spells, profile, GameData::aiDifficulty(), futureSpells);
}

AI::SpellCastPlan AI::chooseSpellCast(const LocalData & local, const Spells & spells,
                                      BehaviorProfile profile, Difficulty difficulty,
                                      const Spells & futureSpells)
{
    return chooseSpellCastFromView(local.myPlayer(), spells, profile, difficulty, futureSpells,
                                   observerPlayerView(local));
}

bool AI::shouldCastBeforeSummon(const SpellCastPlan & plan)
{
    return plan.isValid() && AI::behaviorRules(plan.profile).summonOverrideScore <= plan.score;
}

ClientCastSpell AI::spellCastCommand(const SpellCastStep & step)
{
    if(!step.spell.isValid()) return ClientCastSpell(Spell());

    const SpellInfo & info = GameData::spellInfo(step.spell);
    if(info.target() == SpellTarget::OtherPlayer) return ClientCastSpell(step.spell, step.target);
    if(info.target() == SpellTarget::MyPlayer || info.target() == SpellTarget::AllPlayers)
        return ClientCastSpell(step.spell);
    return ClientCastSpell(step.spell, step.land, step.unit);
}

bool AI::executeSpellCast(const LocalPlayer & player, const SpellCastPlan & plan, ActionList & actions)
{
    if(!plan.isValid()) return false;

    DEBUG("AI spell profile: " << behaviorProfileName(plan.profile) << ", spell: " << plan.spell.toString() <<
          ", score: " << plan.score << " (now " << plan.immediateScore << ", future " << plan.futureScore << ")" <<
          ", target: " << plan.target.toString() << ", land: " << plan.land.toString() <<
          ", unit: " << String::hex(plan.unit, 8));
    for(const SpellCastStep & followUp : plan.followUps)
        DEBUG("AI projected follow-up: " << followUp.spell.toString() << ", score: " << followUp.score);

    return GameData::client2Mahjong(player.avatar, spellCastCommand(plan), actions);
}
