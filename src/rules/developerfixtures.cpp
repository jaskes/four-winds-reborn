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

#ifdef BUILD_DEBUG

#include <algorithm>
#include <string>
#include <vector>

#include "battlesession.h"
#include "crashreport.h"
#include "gamedata.h"
#include "matchtopology.h"
#include "recovery.h"
#include "runegameruleset.h"

namespace GameData
{
    extern LocalPlayers gamers;
    extern Wind currentWind;
    extern Wind roundWind;
    extern Wind partWind;
    extern bool assistedByDeveloper;
    extern Avatar developerAutoplayAvatar;
    extern std::vector<LandInfo> landsInfo;
}

bool GameData::initDeveloperBattleFixture(const Avatar & avatar, ActionList & actions,
                                          std::string* error)
{
    const auto fail = [error](const char* message)
    {
        if(error) *error = message;
        return false;
    };

    auto human = std::find_if(gamers.begin(), gamers.end(), [&avatar](const LocalPlayer & player)
    {
        return player.avatar == avatar;
    });
    if(human == gamers.end()) return fail("developer battle fixture could not find the human player");

    auto defender = std::find_if(gamers.begin(), gamers.end(), [&human](const LocalPlayer & player)
    {
        return player.avatar != human->avatar && player.clan != human->clan;
    });
    if(defender == gamers.end()) return fail("developer battle fixture could not find an opponent");

    for(LocalPlayer & player : gamers) player.army.clear();

    const Land battlefield(Land::Baliphon);
    landsInfo[battlefield()].clan = defender->clan;

    BattleParty attackers(human->clan, battlefield);
    if(!attackers.join(BattleCreature(human->clan, Creature::AdventureParty,
                                      nextBattleUnitId())) ||
       !attackers.join(BattleCreature(human->clan, Creature::GreatCarol,
                                      nextBattleUnitId())) ||
       !attackers.join(BattleCreature(human->clan, Creature::FireElemental,
                                      nextBattleUnitId())))
        return fail("developer battle fixture could not create the attacking party");

    BattleParty defenders(defender->clan, battlefield);
    if(!defenders.join(BattleCreature(defender->clan, Creature::Durlock,
                                      nextBattleUnitId())) ||
       !defenders.join(BattleCreature(defender->clan, Creature::StoneGolem,
                                      nextBattleUnitId())))
        return fail("developer battle fixture could not create the defending party");

    human->army.push_back(attackers);
    defender->army.push_back(defenders);
    human->setAI(false);
    defender->setAI(true);

    if(!initAdventure()) return fail("developer battle fixture could not initialize Adventure");
    currentWind = human->wind;
    human->setAdventurePartDone();
    developerAutoplayAvatar = Avatar();
    assistedByDeveloper = true;
    actions.clear();

    if(!adventure2Client(human->avatar, actions))
        return fail("developer battle fixture could not start combat");

    const bool hasChoice = std::any_of(actions.begin(), actions.end(), [](const ActionMessage & action)
    {
        return action.type() == Action::AdventureBattleChoice;
    });
    if(!pendingBattle.isValid() || pendingBattle.attacker != human->avatar || !hasChoice)
        return fail("developer battle fixture did not reach a manual combat choice");

    CrashReport::breadcrumb(std::string("Developer battle fixture attacker=")
        .append(human->avatar.toString()).append(" defender=")
        .append(defender->avatar.toString()).append(" land=")
        .append(battlefield.toString()));
    return true;
}

namespace
{
bool prepareDeveloperNearEndFixture(const Avatar & avatar, bool adventure,
                                    std::string* error)
{
    const auto fail = [error](const char* message)
    {
        if(error) *error = message;
        return false;
    };

    auto human = std::find_if(GameData::gamers.begin(), GameData::gamers.end(),
        [&avatar](const LocalPlayer & player)
        {
            return player.avatar == avatar;
        });
    if(human == GameData::gamers.end())
        return fail("developer near-end fixture could not find the human player");

    Wind finalRound;
    for(const auto round : winds_all)
    {
        if(activeRuneGameRuleset().advanceRound(round, Wind::North).complete)
        {
            finalRound = Wind(round);
            break;
        }
    }
    if(!finalRound.isValid()) return fail("ruleset has no supported final round");
    const auto & seats = activeMatchTopology().winds();
    human->setAI(false);
    GameData::roundWind = finalRound;
    GameData::partWind = Wind(seats[seats.size() - (adventure ? 1 : 2)]);

    const bool initialized = adventure ? GameData::initAdventure() : GameData::initMahjong();
    if(!initialized)
        return fail(adventure ? "developer fixture could not initialize final Adventure" :
                                "developer fixture could not initialize final Rune Game");

    GameData::developerAutoplayAvatar = Avatar();
    GameData::assistedByDeveloper = true;

    std::string validationError;
    if(!Recovery::validateSaveState(GameData::authoritativeState(), &validationError))
    {
        if(error) *error = std::string("developer near-end fixture is invalid: ") + validationError;
        return false;
    }

    CrashReport::breadcrumb(std::string("Developer near-end fixture phase=")
        .append(adventure ? "final_adventure" : "final_rune")
        .append(" avatar=").append(avatar.toString()));
    return true;
}
}

bool GameData::initDeveloperFinalRuneFixture(const Avatar & avatar, std::string* error)
{
    return prepareDeveloperNearEndFixture(avatar, false, error);
}

bool GameData::initDeveloperFinalAdventureFixture(const Avatar & avatar, std::string* error)
{
    return prepareDeveloperNearEndFixture(avatar, true, error);
}

#endif
