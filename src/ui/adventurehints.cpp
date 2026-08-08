/***************************************************************************
 *   Four Winds Reborn: observer-safe Adventure map hints                 *
 ***************************************************************************/

#include <algorithm>

#include "adventurehints.h"
#include "aibattle.h"

namespace
{
bool hasObservedOccupants(const LocalData & data, const Land & land)
{
    return std::any_of(data.players.begin(), data.players.end(), [&](const LocalPlayer & player)
    {
        const BattleParty* party = player.army.findPartyConst(land);
        return party && !party->isEmpty();
    });
}

BattleParty selectedAttackers(const LocalPlayer & player, const Land & origin)
{
    BattleParty attackers(player.clan, origin);
    for(const BattleCreature* creature : player.army.partySelected(origin))
        if(creature) attackers.join(*creature);
    return attackers;
}
}

bool AdventureHints::canClaimObserved(const LocalData & data, const Land & land)
{
    if(!land.isValid() || land.isTowerWinds()) return false;

    const LocalPlayer & player = data.myPlayer();
    if(!player.clan.isValid()) return false;

    const LandInfo & target = GameData::landInfo(land);
    const Clan previousOwner = target.clan;
    if(!previousOwner.isValid() || GameData::allied(previousOwner, player.clan)) return false;
    if(player.landClaimPoints(previousOwner) < target.stat.point) return false;

    const bool sharesBorder = std::any_of(target.borders.begin(), target.borders.end(),
        [&](const Land & border)
    {
        return !border.isTowerWinds() &&
               GameData::allied(GameData::landInfo(border).clan, player.clan);
    });
    if(!sharesBorder) return false;

    return !hasObservedOccupants(data, land);
}

AdventureHints::DestinationCue AdventureHints::destinationCue(const LocalData & data,
                                                               const Land & origin,
                                                               const Land & target)
{
    if(!origin.isValid() || !target.isValid() || origin == target) return DestinationCue::None;

    BattleArmy planned = data.myPlayer().army;
    if(planned.moveSelectedCreatures(origin, target).empty()) return DestinationCue::None;

    const Clan owner = GameData::landInfo(target).clan;
    return target.isTowerWinds() || GameData::allied(owner, data.myPlayer().clan) ?
        DestinationCue::Move : DestinationCue::Attack;
}

AdventureHints::BattlePreview AdventureHints::battlePreview(const LocalData & data,
                                                             const Land & origin,
                                                             const Land & target,
                                                             AI::Difficulty difficulty)
{
    BattlePreview preview;
    if(!origin.isValid() || !target.isValid() || origin == target || target.isTowerWinds())
        return preview;

    const LocalPlayer & player = data.myPlayer();
    const Clan owner = GameData::landInfo(target).clan;
    if(!owner.isValid() || GameData::allied(owner, player.clan)) return preview;

    BattleParty attackers = selectedAttackers(player, origin);
    if(attackers.isEmpty()) return preview;

    const RemotePlayer & defender = data.playerOfClan(owner);
    const BattleParty* defenders = defender.army.findPartyConst(target);
    BattleTown town(target);

    preview.available = true;
    preview.attackerCount = attackers.count();
    preview.visibleDefenderCount = defenders ? defenders->count() : 0;
    preview.townLoyalty = town.loyalty();

    const AI::DifficultyRules & rules = AI::difficultyRules(difficulty);
    preview.showPercentages = rules.showPlayerBattleForecast;
    if(!preview.showPercentages) return preview;

    const AI::BehaviorProfile defenderProfile = defender.isAI() ?
        AI::behaviorProfile(defender) : AI::BehaviorProfile::Balanced;
    const AI::BattleForecast result = AI::forecastBattle(
        attackers, town, defenders, AI::BehaviorProfile::Balanced,
        defenderProfile, rules.battleForecastSamples);
    preview.samples = result.samples;
    preview.captureChance = result.captureChance;
    preview.attackerSurvival = result.attackerSurvival;
    return preview;
}
