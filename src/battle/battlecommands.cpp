/***************************************************************************
 *   Copyright (C) 2020 by RuneWarsNA team <runewars.newage@gmail.com>     *
 *                                                                         *
 *   Part of the RuneWars: NewAge engine:                                  *
 *   https://github.com/AndreyBarmaley/runewars.newage                     *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#include <algorithm>
#include <list>
#include <vector>

#include "actionvalidation.h"
#include "aiprofile.h"
#include "battlecommands.h"
#include "battlesession.h"
#include "crashreport.h"
#include "gameplayrng.h"

namespace GameData
{
    extern Wind currentWind;
    extern int gamePart;
    extern std::list<BattleLegend> battleHistory;
    extern std::vector<LandInfo> landsInfo;

    LocalPlayer & playerOfAvatar(const Avatar &);
    LocalPlayer & playerOfClan(const Clan &);
}
bool GameData::emitPendingBattleChoice(ActionList & actions)
{
    if(!pendingBattle.isValid() || !pendingBattle.session.awaitsChoice()) return false;

    std::vector<int> actors = pendingBattle.session.legalActors();
    std::pair<int, int> recommended = pendingBattle.session.recommendedChoice();
    if(actors.empty()) return false;

    if(std::find(actors.begin(), actors.end(), recommended.first) == actors.end())
	recommended.first = actors.front();

    std::vector<int> targets = pendingBattle.session.legalTargets(recommended.first);
    if(pendingBattle.session.phase() == Battle::Session::Phase::OpeningLeader)
    {
	recommended.second = -1;
    }
    else
    {
	if(targets.empty()) return false;
	if(std::find(targets.begin(), targets.end(), recommended.second) == targets.end())
	    recommended.second = targets.front();
    }

    actions.push_back(AdventureBattleChoice(currentWind, pendingBattle.choiceLegend(),
	                                     pendingBattle.session.phaseName(),
	                                     pendingBattle.session.strikes(), actors, targets,
	                                     recommended, pendingBattle.session.choiceNumber(),
	                                     pendingBattle.session.choiceCount()));
    return true;
}

bool GameData::completePendingBattle(ActionList & actions)
{
    if(!pendingBattle.isValid() || !pendingBattle.session.isComplete()) return false;

    const PendingBattle resolved = pendingBattle;
    LocalPlayer & attacker = playerOfAvatar(resolved.attacker);
    LocalPlayer & defender = playerOfAvatar(resolved.defender);
    BattleParty* attackers = attacker.army.findParty(resolved.legend.land());
    BattleParty* defenders = defender.army.findParty(resolved.legend.land());

    if(!attackers || (resolved.session.hasDefenders() && !defenders))
    {
	ERROR("pending battle parties no longer match authoritative state");
	return rejectAction(ActionRejectReason::StaleSelection);
    }

    *attackers = resolved.session.attackers();
    if(defenders) *defenders = resolved.session.defenders();

    BattleLegend legend = resolved.legend;
    const Land territoryId = legend.land();
    LandInfo & territory = landsInfo[territoryId()];
    if(!resolved.session.town().isAlive())
    {
	DEBUG("battle wins");
	if(defenders) defenders->dismiss();
	legend.wins = true;
	territory.clan = attacker.clan;
    }
    else
    {
	DEBUG("battle loose");
	attackers->dismiss();
    }

    actions.push_back(AdventureCombat(currentWind, legend, resolved.session.strikes()));
    battleHistory.push_back(legend);
    CrashReport::breadcrumb(std::string("Adventure combat stage=end land=")
	.append(legend.land().toString()).append(" outcome=")
	.append(legend.wins ? "attacker_win" : "defender_win")
	.append(" interactive=true"));

    pendingBattle = PendingBattle();
    attacker.army.shrinkEmpty();
    defender.army.shrinkEmpty();
    return true;
}

bool GameData::clientBattleChoice(const Avatar & avatar, const ClientMessage & act, ActionList & actions)
{
    LocalPlayer & player = playerOfAvatar(avatar);
    if(gamePart != Menu::AdventurePart)
    {
	ERROR("battle choice outside Adventure phase: " << player.toString());
	return rejectAction(ActionRejectReason::WrongPhase);
    }

    if(currentWind != player.wind)
    {
	ERROR("battle choice outside current turn: " << player.toString());
	return rejectAction(ActionRejectReason::WrongTurn);
    }

    if(!player.adventurePartDone() || !pendingBattle.isValid() ||
       pendingBattle.attacker != avatar || !pendingBattle.session.awaitsChoice())
    {
	ERROR("battle choice outside pending combat: " << player.toString());
	return rejectAction(ActionRejectReason::NoPendingBattle);
    }

    const ClientBattleChoice & choice = static_cast<const ClientBattleChoice &>(act);
    const Battle::RandomRoll randomRoll = [](int minimum, int maximum)
    {
	return GameplayRng::uniform(minimum, maximum);
    };

    const bool accepted = choice.autoResolve() ?
	pendingBattle.session.autoResolve(randomRoll, true) :
	pendingBattle.session.choose(choice.actor(), choice.target(), randomRoll, true);
    if(!accepted)
    {
	ERROR("battle choice rejected: actor=" << choice.actor() <<
	      ", target=" << choice.target() << ", auto=" << choice.autoResolve());
	return rejectAction(ActionRejectReason::InvalidBattleChoice);
    }

    if(pendingBattle.session.isComplete())
	return completePendingBattle(actions);

    return emitPendingBattleChoice(actions);
}

bool GameData::adventureBattleAction(const Avatar & avatar, ActionList & actions)
{
    LocalPlayer & player = playerOfAvatar(avatar);

    if(pendingBattle.isValid())
    {
	if(pendingBattle.attacker != avatar) return false;
	if(pendingBattle.session.awaitsChoice())
	{
	    emitPendingBattleChoice(actions);
	    return false;
	}
	if(pendingBattle.session.isComplete())
	{
	    completePendingBattle(actions);
	    return false;
	}
	return false;
    }

    for(auto it = player.army.begin(); it != player.army.end(); ++it)
    {
	const Land & land = (*it).land();

	// skip public zone
	if(land.isTowerWinds()) continue;

	LandInfo & landInfo = landsInfo[land()];
	LocalPlayer & other = playerOfClan(landInfo.clan);

	// all armies moved to dest: need check clan
	if(!GameData::allied(player, other))
	{
	    BattleParty* defenders = other.army.findParty(land);
	    BattleTown town = BattleTown(land);
	    CrashReport::breadcrumb(std::string("Adventure combat stage=begin attacker=")
	        .append(player.avatar.toString()).append(" defender=").append(other.avatar.toString())
	        .append(" land=").append(land.toString())
	        .append(" attackers=").append(String::number((*it).count()))
	        .append(" defenders=").append(String::number(defenders ? defenders->count() : 0))
	        .append(" town_alive=").append(town.isAlive() ? "true" : "false"));
	    JsonObject recoveryGui;
	    recoveryGui.addString("type", "AdventureRecovery");
	    if(!saveRecovery(recoveryGui, std::string("before-battle-").append(land.toString())))
	        ERROR("pre-battle recovery checkpoint failed: " << land.toString());

	    DEBUG("attacker " << (*it).toString());
	    DEBUG("defender tower: " << town.toString());
	    DEBUG("defender " << (defenders ? defenders->toString() : "party: empty"));

	    BattleLegend legend(player.avatar, *it, other.avatar, (defenders ? *defenders : BattleParty()), town, false);

            // AI avatars resolve automatically. Human attackers choose the first
            // melee actor/target, after which the same resolver completes combat.
            const AI::BehaviorProfile attackersProfile = GameData::usesAI(player) ?
                AI::behaviorProfile(player) : AI::BehaviorProfile::Balanced;
            const AI::BehaviorProfile defendersProfile = GameData::usesAI(other) ?
                AI::behaviorProfile(other) : AI::BehaviorProfile::Balanced;

            if(!GameData::usesAI(player))
            {
		pendingBattle.attacker = player.avatar;
		pendingBattle.defender = other.avatar;
		pendingBattle.legend = legend;
		pendingBattle.session = Battle::Session(*it, town, defenders,
		                                         attackersProfile, defendersProfile);
		const Battle::RandomRoll randomRoll = [](int minimum, int maximum)
		{
		    return GameplayRng::uniform(minimum, maximum);
		};
		if(!pendingBattle.session.prepare(randomRoll, true))
		{
		    ERROR("interactive battle preparation failed: " << land.toString());
		    pendingBattle = PendingBattle();
		    return false;
		}

		if(pendingBattle.session.awaitsChoice())
		{
		    if(!emitPendingBattleChoice(actions))
		    {
			ERROR("interactive battle has no legal choice: " << land.toString());
			pendingBattle = PendingBattle();
		    }
		    return false;
		}

		completePendingBattle(actions);
		return false;
	    }

            const BattleStrikes strikes = Battle::doAttackParty(*it, town, defenders,
                                                                 attackersProfile, defendersProfile);
	    CrashReport::breadcrumb(std::string("Adventure combat stage=resolved land=")
	        .append(land.toString()).append(" attackers=").append(String::number((*it).count()))
	        .append(" defenders=").append(String::number(defenders ? defenders->count() : 0))
	        .append(" town_alive=").append(town.isAlive() ? "true" : "false")
	        .append(" strikes=").append(String::number(static_cast<int>(strikes.size()))));

	    // wins?
	    if(! town.isAlive())
	    {
		DEBUG("battle wins");
		if(defenders)
		{
		    defenders->dismiss();
		    other.army.shrinkEmpty();
		}
		legend.wins = true;
		landInfo.clan = player.clan;
	    }
	    else
	    {
		DEBUG("battle loose");
		(*it).dismiss();
	    }

	    DEBUG("legend: " << legend.toString());
	    actions.push_back(AdventureCombat(currentWind, legend, strikes));
	    battleHistory.push_back(legend);
	    CrashReport::breadcrumb(std::string("Adventure combat stage=end land=")
	        .append(land.toString()).append(" outcome=")
	        .append(legend.wins ? "attacker_win" : "defender_win"));
	}
    }

    player.army.shrinkEmpty();

    return true;
}
