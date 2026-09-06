#include <algorithm>
#include <limits>
#include <map>
#include "aibattle.h"
#include "aiadventure.h"

namespace GameData
{
    extern LocalPlayers gamers;
}

namespace
{
    int combatStrength(const BaseStat & stat)
    {
        return stat.attack + stat.ranger * 2 + stat.defense + stat.loyalty * 2;
    }

    const BattleParty* defendingParty(const Land & land)
    {
        const Clan owner = GameData::landInfo(land).clan;
        for(const auto & player : GameData::gamers)
        {
            if(player.clan != owner) continue;
            const BattleParty* party = player.army.findPartyConst(land);
            if(party) return party;
        }
        return nullptr;
    }

    int defendingStrength(const Land & land)
    {
        BaseStat result = GameData::landInfo(land).stat;
        const BattleParty* defenders = defendingParty(land);
        if(defenders) result += defenders->toBaseStatSummary();
        return combatStrength(result);
    }

    AI::BehaviorProfile defendingProfile(const Land & land)
    {
        const Clan owner = GameData::landInfo(land).clan;
        for(const auto & player : GameData::gamers)
            if(player.clan == owner)
                return player.isAI() ? AI::behaviorProfile(player) : AI::BehaviorProfile::Balanced;
        return AI::BehaviorProfile::Balanced;
    }

    AI::BattleForecast forecastLandBattle(const BattleParty & attackers, const Land & land,
                                          AI::BehaviorProfile attackersProfile,
                                          AI::BehaviorProfile defendersProfile,
                                          AI::Difficulty difficulty)
    {
        const BattleTown town(land);
        return AI::forecastBattle(attackers, town, defendingParty(land), attackersProfile,
                                  defendersProfile,
                                  AI::difficultyRules(difficulty).battleForecastSamples);
    }

    int friendlyBorders(const Land & land, const Clan & clan)
    {
        int result = 0;
        for(const Land & border : GameData::landInfo(land).borders)
            if(!border.isTowerWinds() &&
               GameData::allied(GameData::landInfo(border).clan, clan)) result++;
        return result;
    }

    int futureFrontiers(const Land & land, const Clan & clan)
    {
        int result = 0;
        for(const Land & border : GameData::landInfo(land).borders)
        {
            if(border.isTowerWinds()) continue;
            const Clan owner = GameData::landInfo(border).clan;
            if(owner.isValid() && !GameData::allied(owner, clan)) result++;
        }
        return result;
    }

    int claimScore(const RemotePlayer & player, const Land & land, const AI::BehaviorRules & rules)
    {
        const LandInfo & info = GameData::landInfo(land);
        const int friendly = friendlyBorders(land, player.clan);
        const int frontiers = futureFrontiers(land, player.clan);

        return info.stat.point * rules.claimValueWeight / 100 -
               info.stat.point * rules.claimCostPenalty / 100 +
               (info.stat.power ? rules.claimPowerBonus : 0) +
               frontiers * rules.claimFrontierWeight +
               friendly * rules.claimFriendlyBorderWeight;
    }

    int targetScore(const RemotePlayer & player, const Land & land,
                    const Lands & path, const AI::BehaviorRules & rules,
                    AI::BehaviorProfile profile, AI::Difficulty difficulty, int defenseStrength,
                    const AI::BattleForecast & forecast)
    {
        const LandInfo & info = GameData::landInfo(land);
        const BattleParty* defenders = defendingParty(land);
        const bool defended = defenders && !defenders->isEmpty();
        const bool winnable = forecast.captureChance >=
            AI::minimumBattleWinChance(profile, difficulty);
        const bool threatensClan = 0 < friendlyBorders(land, player.clan);

        return info.stat.point * rules.targetValueWeight / 100 -
               static_cast<int>(path.size()) * rules.targetDistancePenalty -
               defenseStrength * rules.targetDefensePenalty +
               (defended ? rules.targetDefendedBonus : rules.targetEmptyBonus) +
               (winnable ? rules.targetWinnableBonus : 0) +
               forecast.captureChance * rules.targetCaptureChanceWeight +
               forecast.attackerSurvival * rules.targetSurvivalWeight +
               (info.stat.power ? rules.targetPowerBonus : 0) +
               (threatensClan ? rules.targetThreatBonus : 0) +
               futureFrontiers(land, player.clan) * rules.targetFrontierWeight;
    }

    Land chooseDestination(const RemotePlayer & player, const BattleParty & party, const Land & target,
                           AI::BehaviorProfile profile, AI::Difficulty difficulty,
                           AI::BattleForecast* immediateForecast)
    {
        if(immediateForecast) *immediateForecast = AI::BattleForecast();
        const AI::BehaviorRules & rules = AI::behaviorRules(profile);
        const Lands path = Lands::pathfind(party.land(), target);
        if(path.empty() || party.movePoint() <= 0) return Land();

        Land normalDestination;
        AI::BattleForecast normalForecast;
        const int maxSteps = std::min<int>(party.movePoint(), path.size());
        for(int steps = maxSteps; 0 < steps; --steps)
        {
            Lands partial;
            for(int index = 0; index < steps; ++index) partial.push_back(path[index]);

            const Land destination = partial.back();
            const BattleParty* joined = player.army.findPartyConst(destination);
            if(joined && joined->count() + party.count() > 3) continue;

            const BattleCreatures creatures = party.toBattleCreatures();
            const bool allCanMove = std::all_of(creatures.begin(), creatures.end(),
                                                [&](const BattleCreature* creature)
            {
                return player.army.canMoveCreature(*creature, party.land(), partial);
            });
            if(!allCanMove) continue;

            const Clan destinationOwner = GameData::landInfo(destination).clan;
            AI::BattleForecast forecast;
            if(destinationOwner.isValid() &&
               !GameData::allied(destinationOwner, player.clan))
            {
                forecast = forecastLandBattle(party, destination, profile,
                                              defendingProfile(destination), difficulty);
                if(forecast.captureChance < AI::minimumBattleWinChance(profile, difficulty))
                    continue;
            }

            normalDestination = destination;
            normalForecast = forecast;
            break;
        }

        int bestRemaining = std::numeric_limits<int>::max();
        if(normalDestination.isValid())
        {
            const Lands remaining = Lands::pathfind(normalDestination, target);
            bestRemaining = normalDestination == target ? 0 : static_cast<int>(remaining.size());
        }

        Land gateDestination;
        for(auto landId : lands_all)
        {
            const Land candidate(landId);
            if(!player.army.canGateParty(party.land(), candidate)) continue;

            const BattleParty* joined = player.army.findPartyConst(candidate);
            if(joined && joined->count() + party.count() > 3) continue;

            Lands gatePath;
            gatePath << candidate;
            const BattleCreatures creatures = party.toBattleCreatures();
            if(!std::all_of(creatures.begin(), creatures.end(), [&](const BattleCreature* creature)
            {
                return player.army.canMoveCreature(*creature, party.land(), gatePath);
            })) continue;

            const Lands remaining = Lands::pathfind(candidate, target);
            if(remaining.empty()) continue;

            const int distance = static_cast<int>(remaining.size());
            if(distance < bestRemaining ||
               (distance == bestRemaining && gateDestination.isValid() && candidate() < gateDestination()))
            {
                gateDestination = candidate;
                bestRemaining = distance;
            }
        }

        if(gateDestination.isValid()) return gateDestination;
        if(immediateForecast) *immediateForecast = normalForecast;
        return normalDestination;
    }

    bool canMoveParty(const RemotePlayer & player, const BattleParty & party, const Lands & path)
    {
        if(path.empty()) return false;

        const BattleCreatures creatures = party.toBattleCreatures();
        return std::all_of(creatures.begin(), creatures.end(), [&](const BattleCreature* creature)
        {
            return player.army.canMoveCreature(*creature, party.land(), path);
        });
    }

    std::vector<int> partyUnits(const BattleParty & party)
    {
        BattleCreatures creatures = party.toBattleCreatures();
        std::stable_sort(creatures.begin(), creatures.end(), [](const BattleCreature* left,
                                                                const BattleCreature* right)
        {
            const bool leftGate = left && left->haveSpeciality(Speciality::Gate);
            const bool rightGate = right && right->haveSpeciality(Speciality::Gate);
            return leftGate < rightGate;
        });

        std::vector<int> result;
        for(const BattleCreature* creature : creatures)
            result.push_back(creature->battleUnit());
        return result;
    }

    int friendlyPartySize(const RemotePlayer & player, const Land & land)
    {
        const BattleParty* party = player.army.findPartyConst(land);
        return party ? party->count() : 0;
    }

    struct PartyCandidate
    {
        const BattleParty* party;
        std::vector<int> units;
        int strength;
        bool assigned;

        PartyCandidate(const BattleParty & value)
            : party(&value), units(partyUnits(value)),
              strength(combatStrength(value.toBaseStatSummary())), assigned(false) {}
    };

    AI::AdventurePartyOrder partyOrder(const PartyCandidate & candidate)
    {
        AI::AdventurePartyOrder order;
        order.units = candidate.units;
        order.origin = candidate.party->land();
        return order;
    }
}

AI::AdventureClaimPlan AI::chooseAdventureClaim(const RemotePlayer & player, BehaviorProfile profile)
{
    return chooseAdventureClaim(player, profile, GameData::aiDifficulty());
}

AI::AdventureClaimPlan AI::chooseAdventureClaim(const RemotePlayer & player, BehaviorProfile profile,
                                                Difficulty difficulty)
{
    AdventureClaimPlan best;
    if(!difficultyRules(difficulty).allowLandClaims) return best;

    const BehaviorRules & rules = behaviorRules(profile);

    for(const Land & land : GameData::claimableLands(player))
    {
        const LandInfo & info = GameData::landInfo(land);
        if(player.landClaimPoints(info.clan) - info.stat.point < rules.claimPointReserve) continue;

        AdventureClaimPlan candidate;
        candidate.land = land;
        candidate.score = claimScore(player, land, rules);

        if(!best.isValid() || candidate.score > best.score ||
           (candidate.score == best.score && candidate.land() < best.land()))
            best = candidate;
    }
    return best;
}

AI::AdventureMovePlan AI::chooseAdventureMove(const RemotePlayer & player, const BattleParty & party,
                                              BehaviorProfile profile)
{
    return chooseAdventureMove(player, party, profile, GameData::aiDifficulty());
}

AI::AdventureMovePlan AI::chooseAdventureMove(const RemotePlayer & player, const BattleParty & party,
                                              BehaviorProfile profile, Difficulty difficulty)
{
    Lands targets;
    for(auto landId : lands_all) targets.push_back(Land(landId));
    return chooseAdventureMove(player, party, profile, difficulty, targets);
}

AI::AdventureMovePlan AI::chooseAdventureMove(const RemotePlayer & player, const BattleParty & party,
                                              BehaviorProfile profile, const Lands & targets)
{
    return chooseAdventureMove(player, party, profile, GameData::aiDifficulty(), targets);
}

AI::AdventureMovePlan AI::chooseAdventureMove(const RemotePlayer & player, const BattleParty & party,
                                              BehaviorProfile profile, Difficulty difficulty,
                                              const Lands & targets)
{
    AdventureMovePlan best;
    if(!difficultyRules(difficulty).allowEnemyLandMoves) return best;

    const BehaviorRules & rules = behaviorRules(profile);
    const int attackStrength = combatStrength(party.toBaseStatSummary());

    for(const Land & land : targets)
    {
        if(!land.isValid() || land.isTowerWinds()) continue;

        const LandInfo & info = GameData::landInfo(land);
        if(!info.clan.isValid() || GameData::allied(info.clan, player.clan)) continue;

        const Lands path = Lands::pathfind(party.land(), land);
        if(path.empty()) continue;

        const int defenseStrength = defendingStrength(land);
        BattleForecast immediateForecast;
        const Land destination = chooseDestination(player, party, land, profile, difficulty,
                                                   &immediateForecast);
        if(!destination.isValid()) continue;

        const bool engagesEnemy = GameData::landInfo(destination).clan.isValid() &&
                                  !GameData::allied(GameData::landInfo(destination).clan,
                                                    player.clan);
        BattleForecast forecast = engagesEnemy ? immediateForecast :
            forecastLandBattle(party, land, profile, defendingProfile(land), difficulty);
        if(!forecast.isValid()) continue;

        AdventureMovePlan candidate;
        candidate.target = land;
        candidate.destination = destination;
        candidate.attackStrength = attackStrength;
        candidate.defenseStrength = defenseStrength;
        candidate.captureChance = forecast.captureChance;
        candidate.attackerSurvival = forecast.attackerSurvival;
        candidate.engagesEnemy = engagesEnemy;
        candidate.score = targetScore(player, land, path, rules, profile, difficulty,
                                      defenseStrength, forecast);

        if(!best.isValid() || candidate.score > best.score ||
           (candidate.score == best.score && candidate.target() < best.target()))
            best = candidate;
    }
    return best;
}

std::vector<AI::AdventureThreat> AI::predictAdventureThreats(const RemotePlayer & player,
                                                             BehaviorProfile profile)
{
    return predictAdventureThreats(player, profile, GameData::aiDifficulty());
}

std::vector<AI::AdventureThreat> AI::predictAdventureThreats(const RemotePlayer & player,
                                                             BehaviorProfile profile,
                                                             Difficulty difficulty)
{
    std::vector<AdventureThreat> result;
    const BehaviorRules & rules = behaviorRules(profile);

    for(auto landId : lands_all)
    {
        const Land land(landId);
        if(land.isTowerWinds() || GameData::landInfo(land).clan != player.clan) continue;

        AdventureThreat threat;
        threat.land = land;
        threat.defenseStrength = defendingStrength(land);
        threat.distance = std::numeric_limits<int>::max();

        for(const auto & enemy : GameData::gamers)
        {
            if(GameData::allied(enemy.clan, player.clan)) continue;
            const BehaviorProfile enemyProfile = enemy.isAI() ? behaviorProfile(enemy) :
                                                   BehaviorProfile::Balanced;

            for(const BattleParty & enemyParty : enemy.army)
            {
                int distance = 0;
                if(enemyParty.land() != land)
                {
                    const Lands path = Lands::pathfind(enemyParty.land(), land);
                    if(!canMoveParty(enemy, enemyParty, path)) continue;
                    distance = static_cast<int>(path.size());
                }

                const int strength = combatStrength(enemyParty.toBaseStatSummary());
                threat.enemyStrength += strength;
                const BattleForecast forecast = forecastLandBattle(enemyParty, land, enemyProfile,
                                                                   profile, difficulty);
                const bool moreDangerous = forecast.captureChance > threat.captureChance ||
                    (forecast.captureChance == threat.captureChance &&
                     forecast.attackerSurvival > threat.attackerSurvival) ||
                    (forecast.captureChance == threat.captureChance &&
                     forecast.attackerSurvival == threat.attackerSurvival &&
                     distance < threat.distance);
                if(moreDangerous || !threat.enemyOrigin.isValid())
                {
                    threat.distance = distance;
                    threat.enemyOrigin = enemyParty.land();
                    threat.captureChance = forecast.captureChance;
                    threat.attackerSurvival = forecast.attackerSurvival;
                }
            }
        }

        if(!threat.enemyStrength || threat.captureChance <
           minimumThreatCaptureChance(profile, difficulty))
            continue;

        const LandInfo & info = GameData::landInfo(land);
        threat.score = threat.captureChance * 100 +
                       threat.attackerSurvival * 10 +
                       threat.enemyStrength * 20 -
                       threat.defenseStrength * 10 +
                       info.stat.point * rules.threatValueWeight / 100 +
                       (info.stat.power ? rules.threatPowerBonus : 0) -
                       threat.distance * rules.threatDistancePenalty;
        result.push_back(threat);
    }

    std::sort(result.begin(), result.end(), [](const AdventureThreat & left,
                                               const AdventureThreat & right)
    {
        return left.score == right.score ? left.land() < right.land() : left.score > right.score;
    });
    return result;
}

AI::AdventureTurnPlan AI::chooseAdventureTurn(const RemotePlayer & player, BehaviorProfile profile)
{
    return chooseAdventureTurn(player, profile, GameData::aiDifficulty());
}

AI::AdventureTurnPlan AI::chooseAdventureTurn(const RemotePlayer & player, BehaviorProfile profile,
                                              Difficulty difficulty)
{
    AdventureTurnPlan plan;
    plan.profile = profile;
    plan.threats = predictAdventureThreats(player, profile, difficulty);

    const BehaviorRules & rules = behaviorRules(profile);
    const int partiesPerTarget = maximumPartiesPerTarget(profile, difficulty);
    std::vector<PartyCandidate> parties;
    for(const BattleParty & party : player.army)
        if(!party.isEmpty()) parties.emplace_back(party);

    const int reserveLimit = std::min<int>(parties.size(),
        (static_cast<int>(parties.size()) * defensiveReservePercent(profile, difficulty) + 99) / 100);
    std::map<int, int> plannedArrivals;

    for(const AdventureThreat & threat : plan.threats)
    {
        if(plan.reservedParties >= reserveLimit) break;

        int selected = -1;
        int selectedDistance = std::numeric_limits<int>::max();
        int selectedStrength = -1;

        for(int index = 0; index < static_cast<int>(parties.size()); ++index)
        {
            const PartyCandidate & candidate = parties[index];
            if(candidate.assigned) continue;

            const bool guardsAnotherThreat = candidate.party->land() != threat.land &&
                std::any_of(plan.threats.begin(), plan.threats.end(), [&](const AdventureThreat & other)
            {
                return other.land == candidate.party->land();
            });
            if(guardsAnotherThreat) continue;

            int distance = 0;
            if(candidate.party->land() != threat.land)
            {
                const Lands path = Lands::pathfind(candidate.party->land(), threat.land);
                if(!canMoveParty(player, *candidate.party, path)) continue;
                if(friendlyPartySize(player, threat.land) + plannedArrivals[threat.land()] +
                   candidate.party->count() > 3)
                    continue;
                distance = static_cast<int>(path.size());
            }

            if(distance < selectedDistance ||
               (distance == selectedDistance && candidate.strength > selectedStrength))
            {
                selected = index;
                selectedDistance = distance;
                selectedStrength = candidate.strength;
            }
        }

        if(selected < 0) continue;

        PartyCandidate & candidate = parties[selected];
        AdventurePartyOrder order = partyOrder(candidate);
        order.defend = threat.land;
        order.score = threat.score;
        order.hold = candidate.party->land() == threat.land;
        if(!order.hold)
        {
            order.move.target = threat.land;
            order.move.destination = threat.land;
            order.move.score = threat.score;
            order.move.attackStrength = candidate.strength;
            order.move.defenseStrength = threat.defenseStrength;
            order.move.engagesEnemy = false;
            plannedArrivals[threat.land()] += candidate.party->count();
        }

        candidate.assigned = true;
        plan.orders.push_back(order);
        plan.reservedParties++;
    }

    // Training players defend threatened friendly territory, but never turn the
    // remaining parties into an invasion force.
    if(!difficultyRules(difficulty).allowEnemyLandMoves)
    {
        for(PartyCandidate & candidate : parties)
        {
            if(candidate.assigned) continue;
            plan.orders.push_back(partyOrder(candidate));
            candidate.assigned = true;
        }
        return plan;
    }

    std::map<int, int> targetAssignments;
    for(PartyCandidate & candidate : parties)
    {
        if(candidate.assigned) continue;

        Lands targets;
        for(auto landId : lands_all)
        {
            const Land land(landId);
            const Clan owner = GameData::landInfo(land).clan;
            if(land.isTowerWinds() || !owner.isValid() ||
               GameData::allied(owner, player.clan)) continue;
            if(targetAssignments[land()] < partiesPerTarget) targets.push_back(land);
        }

        AdventureMovePlan move;
        while(!targets.empty())
        {
            move = chooseAdventureMove(player, *candidate.party, profile, difficulty, targets);
            if(!move.isValid()) break;

            const int occupied = friendlyPartySize(player, move.destination) +
                                 plannedArrivals[move.destination()];
            if(occupied + candidate.party->count() <= 3) break;

            targets.erase(std::remove(targets.begin(), targets.end(), move.target), targets.end());
            move = AdventureMovePlan();
        }

        AdventurePartyOrder order = partyOrder(candidate);
        if(move.isValid())
        {
            order.hold = false;
            order.move = move;
            order.score = move.score;
            plannedArrivals[move.destination()] += candidate.party->count();
            targetAssignments[move.target()]++;
        }

        candidate.assigned = true;
        plan.orders.push_back(order);
    }

    return plan;
}
