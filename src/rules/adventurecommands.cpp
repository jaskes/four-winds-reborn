/***************************************************************************
 *   Copyright (C) 2020 by RuneWarsNA team <runewars.newage@gmail.com>     *
 *                                                                         *
 *   Part of the RuneWars: NewAge engine.                                  *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#include <algorithm>
#include <vector>

#include "actionvalidation.h"
#include "adventurecommands.h"
#include "aiturn.h"
#include "battlecommands.h"
#include "battlesession.h"
#include "crashreport.h"
#include "replay.h"
#include "matchtopology.h"

namespace
{
    struct LandClaimRecord
    {
        Avatar player;
        Land land;
        Clan previousOwner;
        int cost;

        LandClaimRecord(const Avatar & avatar, const Land & territory,
                        const Clan & owner, int value)
            : player(avatar), land(territory), previousOwner(owner), cost(value) {}
    };

    std::vector<LandClaimRecord> landClaimJournal;
    Avatar adventureSnapshotPlayer;
    BattleArmy adventureArmySnapshot;
}

namespace GameData
{
    extern LocalPlayers gamers;
    extern Wind currentWind;
    extern Wind roundWind;
    extern Wind partWind;
    extern bool skipRepeatSay;
    extern int gamePart;
    extern std::vector<LandInfo> landsInfo;

    LocalPlayer & playerOfAvatar(const Avatar &);
    LocalPlayer & playerOfWind(const Wind &);

    bool clientUnitMoved(const Avatar &, const ClientMessage &, ActionList &);
    bool clientLandClaim(const Avatar &, const ClientMessage &, ActionList &);
    bool clientAdventureUndo(const Avatar &, const ClientMessage &, ActionList &);
    bool clientBattleReady(const Avatar &, const ClientMessage &, ActionList &);
}

void GameData::resetAdventureCommandState(void)
{
    landClaimJournal.clear();
    adventureSnapshotPlayer = Avatar();
    adventureArmySnapshot.clear();
}

bool GameData::initAdventure(void)
{
    VERBOSE("wind round: " << roundWind.toString());
    VERBOSE("wind part: " << partWind.toString());

    gamePart = Menu::AdventurePart;
    currentWind = Wind(Wind::East);

    for(auto & lp : gamers)
        lp.initAdventurePart();

    resetAdventureCommandState();
    pendingBattle = PendingBattle();
    skipRepeatSay = false;
    return true;
}

bool GameData::adventure2Client(const Avatar & avatar, ActionList & actions)
{
    const LocalPlayer & player = GameData::playerOfWind(currentWind);

    if(player.adventurePartDone())
    {
        if(gamePart == Menu::AdventurePart)
        {
            if(!adventureBattleAction(player.avatar, actions)) return true;
            if(currentWind() == activeMatchTopology().winds().back()) gamePart = Menu::BattleSummaryPart;
            currentWind = Wind(activeMatchTopology().nextWind(currentWind()));
            skipRepeatSay = false;
        }
        else
        {
            actions.push_back(AdventureEnd(currentWind));
            return true;
        }
    }
    else
    {
        // DEBUG("wind: " << currentWind.toString() << ", " << "person: " << player.name());
        if(adventureSnapshotPlayer != player.avatar)
        {
            adventureSnapshotPlayer = player.avatar;
            adventureArmySnapshot = player.army;
            landClaimJournal.clear();
        }

        if(GameData::usesAI(player))
        {
            actions.push_back(AdventureTurn(currentWind));
            AI::adventureMove(player, actions);
            client2Adventure(player.avatar, ClientBattleReady(), actions);
        }
        else
        {
            if(skipRepeatSay)
                return false;

            actions.push_back(AdventureTurn(currentWind));
            skipRepeatSay = true;
        }
    }

    return true;
}

bool GameData::client2Adventure(const Avatar & avatar, const ClientMessage & act,
                                ActionList & actions, ActionRejection* rejection)
{
    ScopedActionRejection rejectionScope(rejection);
    const std::size_t actionsBefore = actions.size();
    const bool recordAction = Replay::actionRecordingEnabled();
    const JsonObject beforeState = recordAction ? authoritativeState() : JsonObject();
    CrashReport::breadcrumb(std::string("GameData phase=Adventure stage=before avatar=")
        .append(avatar.toString()).append(" type=").append(String::number(act.type()))
        .append(" payload=").append(act.toString()));

    bool accepted = false;
    bool recognized = true;
    switch(act.type())
    {
        case Action::ClientUnitMoved: accepted = clientUnitMoved(avatar, act, actions); break;
        case Action::ClientLandClaim: accepted = clientLandClaim(avatar, act, actions); break;
        case Action::ClientAdventureUndo: accepted = clientAdventureUndo(avatar, act, actions); break;
        case Action::ClientBattleReady: accepted = clientBattleReady(avatar, act, actions); break;
        case Action::ClientBattleChoice: accepted = clientBattleChoice(avatar, act, actions); break;
        default: recognized = false; rejectAction(ActionRejectReason::UnknownAction); break;
    }

    if(recognized && !accepted && (!rejection || !rejection->isValid()))
        rejectAction(ActionRejectReason::UnknownAction);

    if(recognized && accepted && recordAction)
        Replay::recordAcceptedAction(beforeState, avatar, act, authoritativeState());

    CrashReport::breadcrumb(std::string("GameData phase=Adventure stage=after avatar=")
        .append(avatar.toString()).append(" type=").append(String::number(act.type()))
        .append(" recognized=").append(recognized ? "true" : "false")
        .append(" accepted=").append(accepted ? "true" : "false")
        .append(" rejection=").append(rejection ? actionRejectReasonName(rejection->reason) : "not_requested")
        .append(" emitted=").append(String::number(static_cast<int>(actions.size() - actionsBefore))));
    return accepted;
}

bool GameData::clientUnitMoved(const Avatar & avatar, const ClientMessage & act,
                               ActionList & actions)
{
    LocalPlayer & client = playerOfAvatar(avatar);
    auto ca = static_cast<const ClientUnitMoved &>(act);

    if(gamePart != Menu::AdventurePart)
    {
        ERROR("adventure action outside Adventure phase: " << client.toString());
        return rejectAction(ActionRejectReason::WrongPhase);
    }

    if(currentWind != client.wind || client.adventurePartDone())
    {
        ERROR("adventure action outside current turn: " << client.toString());
        return rejectAction(ActionRejectReason::WrongTurn);
    }

    Land land = ca.land();
    int unit = ca.unit();

    DEBUG(client.toString() << ", " << "unit: " << String::hex(unit, 8)
          << ", to land: " << Land(land).toString());

    const BattleCreature* bcr = client.army.findBattleUnitConst(unit);

    if(!bcr)
    {
        ERROR("unit not found: " << String::hex(unit, 8));
        return rejectAction(ActionRejectReason::UnitNotFound);
    }

    if(!land.isValid())
    {
        client.army.remove(*bcr);
        actions.push_back(AdventureMoves(currentWind, unit, land));
        return true;
    }

    if(client.army.moveCreature(*bcr, land))
    {
        actions.push_back(AdventureMoves(currentWind, unit, land));
        return true;
    }

    return rejectAction(ActionRejectReason::IllegalMovement);
}

bool GameData::canClaimLand(const RemotePlayer & player, const Land & land)
{
    if(!land.isValid() || land.isTowerWinds() || !player.clan.isValid()) return false;

    const LandInfo & target = landInfo(land);
    const Clan previousOwner = target.clan;
    if(!previousOwner.isValid() || GameData::allied(previousOwner, player.clan)) return false;
    if(player.landClaimPoints(previousOwner) < target.stat.point) return false;

    const bool sharesBorder = std::any_of(target.borders.begin(), target.borders.end(),
        [&](const Land & border)
    {
        return !border.isTowerWinds() && GameData::allied(landInfo(border).clan, player.clan);
    });
    if(!sharesBorder) return false;

    // A deed cannot hide a creature: all parties, including invisible ones, block a claim.
    for(const auto & other : gamers)
    {
        const BattleParty* party = other.army.findPartyConst(land);
        if(party && !party->isEmpty()) return false;
    }

    return true;
}

Lands GameData::claimableLands(const RemotePlayer & player)
{
    Lands result;
    for(auto landId : lands_all)
    {
        const Land land(landId);
        if(canClaimLand(player, land)) result.push_back(land);
    }
    return result;
}

bool GameData::clientLandClaim(const Avatar & avatar, const ClientMessage & act,
                               ActionList & actions)
{
    LocalPlayer & player = playerOfAvatar(avatar);
    if(gamePart != Menu::AdventurePart)
    {
        ERROR("land claim outside Adventure phase: " << player.toString());
        return rejectAction(ActionRejectReason::WrongPhase);
    }

    if(currentWind != player.wind || player.adventurePartDone())
    {
        ERROR("land claim outside current turn: " << player.toString());
        return rejectAction(ActionRejectReason::WrongTurn);
    }

    const Land land = static_cast<const ClientLandClaim &>(act).land();
    if(!canClaimLand(player, land))
    {
        ERROR("land claim rejected: " << land.toString());
        return rejectAction(ActionRejectReason::IllegalLandClaim);
    }

    LandInfo & target = landsInfo[land()];
    const Clan previousOwner = target.clan;
    const int cost = target.stat.point;
    if(!player.spendLandClaimPoints(previousOwner, cost))
        return rejectAction(ActionRejectReason::InsufficientClaimPoints,
                            player.landClaimPoints(previousOwner), cost);

    target.clan = player.clan;
    landClaimJournal.emplace_back(player.avatar, land, previousOwner, cost);
    actions.push_back(AdventureClaim(currentWind, land, previousOwner, player.clan, cost));
    return true;
}

bool GameData::clientAdventureUndo(const Avatar & avatar, const ClientMessage & act,
                                   ActionList & actions)
{
    LocalPlayer & player = playerOfAvatar(avatar);
    if(gamePart != Menu::AdventurePart)
    {
        ERROR("adventure undo outside Adventure phase: " << player.toString());
        return rejectAction(ActionRejectReason::WrongPhase);
    }

    if(currentWind != player.wind || player.adventurePartDone())
    {
        ERROR("adventure undo outside current turn: " << player.toString());
        return rejectAction(ActionRejectReason::WrongTurn);
    }

    if(adventureSnapshotPlayer != avatar)
    {
        ERROR("adventure undo snapshot missing: " << player.toString());
        return rejectAction(ActionRejectReason::NothingToUndo);
    }

    while(!landClaimJournal.empty() && landClaimJournal.back().player == avatar)
    {
        const LandClaimRecord record = landClaimJournal.back();
        landClaimJournal.pop_back();

        LandInfo & target = landsInfo[record.land()];
        const Clan claimedOwner = target.clan;
        target.clan = record.previousOwner;
        player.addLandClaimPoints(record.previousOwner, record.cost);
        actions.push_back(AdventureClaim(currentWind, record.land, claimedOwner,
                                         record.previousOwner, record.cost, true));
    }

    player.army = adventureArmySnapshot;
    return true;
}

bool GameData::clientBattleReady(const Avatar & avatar, const ClientMessage & act,
                                 ActionList & actions)
{
    LocalPlayer & player = playerOfAvatar(avatar);

    if(gamePart != Menu::AdventurePart)
    {
        ERROR("battle ready outside Adventure phase: " << player.toString());
        return rejectAction(ActionRejectReason::WrongPhase);
    }

    if(currentWind != player.wind || player.adventurePartDone())
    {
        ERROR("battle ready outside current turn: " << player.toString());
        return rejectAction(ActionRejectReason::WrongTurn);
    }

    //auto ca = static_cast<const ClientBattleReady &>(act);
    DEBUG(player.toString());

    player.setAdventurePartDone();
    landClaimJournal.erase(std::remove_if(landClaimJournal.begin(), landClaimJournal.end(),
        [&](const LandClaimRecord & record)
    {
        return record.player == avatar;
    }), landClaimJournal.end());
    adventureSnapshotPlayer = Avatar();
    adventureArmySnapshot.clear();
    return true;
}
