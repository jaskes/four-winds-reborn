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

#include <set>
#include <limits>
#include <algorithm>

#include "aiturn.h"
#include "aiadventure.h"
#include "aispell.h"
#include "aistrategy.h"
#include "gamedata.h"
#include "matchtopology.h"
#include "gameplayrng.h"
#include "runegameruleset.h"

namespace GameData
{
    extern LocalPlayers gamers;

    LocalPlayer &	playerOfAvatar(const Avatar &);
    LocalPlayer &	playerOfClan(const Clan &);
    LocalPlayer &	playerOfWind(const Wind &);
}

bool AI::mahjongTurn(const Wind & currentWind, const Avatar & avatar, const VecStones & trash,
    const WinRules & left, const WinRules & right, const WinRules & top, bool showGame, bool showKong, ActionList & actions)
{
    // simple AI
    actions.push_back(MahjongTurn(currentWind, Stone(), false, false));

    if(showGame)
    {
	GameData::client2Mahjong(avatar, ClientSayGame(), actions);
	GameData::client2Mahjong(avatar, ClientButtonGame(), actions);
	return true;
    }
    else
    if(showKong)
    {
	GameData::client2Mahjong(avatar, ClientSayKong(2), actions);
	GameData::client2Mahjong(avatar, ClientButtonKong2(), actions);
	return true;
    }

    // AI select
    WinRules other; other.reserve(12);
    other.insert(other.end(), left.begin(), left.end());
    other.insert(other.end(), right.begin(), right.end());
    other.insert(other.end(), top.begin(), top.end());

    LocalPlayer & player = GameData::playerOfAvatar(avatar);

    if(player.newStone.isValid())
    {
	player.stones.add(player.newStone);
	player.newStone = GameStone(Stone::None, true);
    }

    // cast or summon
    const AvatarInfo & avatarInfo = GameData::avatarInfo(avatar);

    if(!player.isCasted() && !player.isSilenced() &&
       !player.isAffectedSpell(Spell::ManaFog))
    {
	Creatures summons;
        Spells casts;

	// allow summon creatures
	for(auto & cr : avatarInfo.creatures)
	{
	    const CreatureInfo & creatureInfo = GameData::creatureInfo(cr);

	    // allow summon
	    if(creatureInfo.cost <= player.points &&
		player.stones.allowCast(creatureInfo.stones)) summons.push_back(creatureInfo.id);
	}

	casts = availableSpellCasts(player);

	if(summons.size() || casts.size())
	    mahjongSummonCast(avatar, summons, casts, actions);
    }

    // Keep rune decisions connected to the same observer-legal strategic goals
    // that later summon/spell planning will consume.
    const StrategicIntent intent = chooseStrategicIntent(
        observePlayer(avatar), behaviorProfile(player), GameData::aiDifficulty());
    DEBUG("AI strategy: " << intent.trace());

    // drop stone
    int dropIndex = mahjongSelect(player.stones, trash, other, intent);
    return GameData::client2Mahjong(avatar, ClientDropIndex(dropIndex), actions);
}

void AI::mahjongSummonCast(const Avatar & avatar, const Creatures & summons, const Spells & casts, ActionList & actions)
{
    const LocalPlayer & player = GameData::playerOfAvatar(avatar);
    const TurnPlan plan = chooseStrategicTurnPlan(
        observePlayer(avatar), summons, casts, behaviorProfile(player), GameData::aiDifficulty());
    DEBUG("AI turn plan: " << plan.trace());

    for(const TurnBranch & branch : plan.branches)
    {
        if(branch.action == StrategicAction::Spell)
        {
            if(executeSpellCast(player, branch.spell, actions)) return;
            continue;
        }

        // A hidden globally unique creature may still make this observer-legal
        // branch fail authoritative validation. Continue through the bounded plan
        // without consulting hidden state.
        if(GameData::client2Mahjong(player.avatar,
                                    ClientSummonCreature(branch.summon.creature,
                                                         branch.summon.destination), actions))
            return;
    }
}

void AI::mahjongOtherPass(const Wind & currentWind, ActionList & actions, const Wind & skip)
{
    for(auto & id : activeMatchTopology().winds())
    {
	LocalPlayer & playerAI = GameData::playerOfWind(id);

	if(id == currentWind() || id == skip() || !GameData::usesAI(playerAI))
		continue;

	actions.push_back(MahjongPass(id));
    }
}

namespace
{
    Stones mahjongCallRunes(AI::MahjongCallType type, const Stone & dropStone,
                            const Stone & chaoStart = Stone())
    {
        Stones consumed;
        if(type == AI::MahjongCallType::Pung || type == AI::MahjongCallType::Kong)
        {
            const int count = type == AI::MahjongCallType::Kong ? 3 : 2;
            for(int index = 0; index < count; ++index) consumed.push_back(dropStone);
            return consumed;
        }

        if(type == AI::MahjongCallType::Chao)
        {
            const Stone sequence[] = { chaoStart, chaoStart.next(), chaoStart.next().next() };
            bool usedDrop = false;
            for(const Stone & stone : sequence)
            {
                if(!usedDrop && stone == dropStone)
                {
                    usedDrop = true;
                    continue;
                }
                consumed.push_back(stone);
            }
        }
        return consumed;
    }

    int mahjongRunePreservationCost(const GameStones & hand, const Stones & consumed,
                                    const AI::StrategicIntent & intent)
    {
        std::vector<bool> used(hand.size(), false);
        int result = 0;
        for(const Stone & rune : consumed)
        {
            for(std::size_t index = 0; index < hand.size(); ++index)
            {
                if(used[index] || hand[index] != rune) continue;
                used[index] = true;
                if(!GameStone::isCasted(hand[index])) result += intent.runeValue(rune);
                break;
            }
        }
        return result;
    }

    int mahjongCallScore(const LocalPlayer & player, AI::MahjongCallType type,
                         const Stones & consumed, const AI::StrategicIntent & intent,
                         const RuneGameRuleset & ruleset)
    {
        int result = 45 + 12 * (static_cast<int>(player.rules.size()) + 1);
        switch(type)
        {
            case AI::MahjongCallType::Kong:
                result += 35 + ruleset.spellPointAward(RuneGameSpellPointEvent::Kong) -
                    ruleset.spellPointAward(RuneGameSpellPointEvent::Discard);
                break;
            case AI::MahjongCallType::Pung:
                result += 20 + ruleset.spellPointAward(RuneGameSpellPointEvent::Pung) -
                    ruleset.spellPointAward(RuneGameSpellPointEvent::Discard);
                break;
            case AI::MahjongCallType::Chao:
                result += 5 + ruleset.spellPointAward(RuneGameSpellPointEvent::Chao) -
                    ruleset.spellPointAward(RuneGameSpellPointEvent::Discard);
                break;
            case AI::MahjongCallType::Pass:
                return 0;
        }
        return result - mahjongRunePreservationCost(player.stones, consumed, intent);
    }

    RuneGameCall toRulesetCall(AI::MahjongCallType type)
    {
        switch(type)
        {
            case AI::MahjongCallType::Kong: return RuneGameCall::Kong;
            case AI::MahjongCallType::Pung: return RuneGameCall::Pung;
            case AI::MahjongCallType::Chao: return RuneGameCall::Chao;
            case AI::MahjongCallType::Pass: break;
        }
        return RuneGameCall::None;
    }
}

AI::MahjongCallPlan AI::chooseMahjongCall(const LocalPlayer & player, const Wind & currentWind,
                                          const Stone & dropStone, const StrategicIntent & intent)
{
    return chooseMahjongCall(player, currentWind, dropStone, intent,
                             classicRuneGameRuleset());
}

AI::MahjongCallPlan AI::chooseMahjongCall(const LocalPlayer & player, const Wind & currentWind,
                                          const Stone & dropStone, const StrategicIntent & intent,
                                          const RuneGameRuleset & ruleset)
{
    MahjongCallPlan best;
    auto consider = [&](MahjongCallType type, const Stones & consumed, int variant)
    {
        const int score = mahjongCallScore(player, type, consumed, intent, ruleset);
        if(score > best.score ||
           (score == best.score &&
            ruleset.callChoicePriority(toRulesetCall(type)) >
                ruleset.callChoicePriority(toRulesetCall(best.type))))
        {
            best.type = type;
            best.variant = variant;
            best.score = score;
        }
    };

    if(player.isMahjongKong1(currentWind, dropStone, ruleset))
        consider(MahjongCallType::Kong,
                 mahjongCallRunes(MahjongCallType::Kong, dropStone), -1);
    if(player.isMahjongPung(currentWind, dropStone, ruleset))
        consider(MahjongCallType::Pung,
                 mahjongCallRunes(MahjongCallType::Pung, dropStone), -1);
    if(player.isMahjongChao(currentWind, dropStone, ruleset))
    {
        const Stones variants = player.stones.findChaoVariants(dropStone);
        for(std::size_t index = 0; index < variants.size(); ++index)
            consider(MahjongCallType::Chao,
                     mahjongCallRunes(MahjongCallType::Chao, dropStone, variants[index]),
                     static_cast<int>(index));
    }

    return best;
}

bool AI::mahjongGameKongPungChao(const Wind & currentWind, const Wind & roundWind, const Stone & dropStone, WinResults & winResult, ActionList & actions, bool sayOnly)
{
    return mahjongGameKongPungChao(currentWind, roundWind, dropStone, winResult,
                                   actions, sayOnly, classicRuneGameRuleset());
}

bool AI::mahjongGameKongPungChao(const Wind & currentWind, const Wind & roundWind,
                                 const Stone & dropStone, WinResults & winResult,
                                 ActionList & actions, bool sayOnly,
                                 const RuneGameRuleset & ruleset)
{
    // set game
    for(auto & id : activeMatchTopology().winds())
    {
	if(id == currentWind())
		continue;

	LocalPlayer & playerAI = GameData::playerOfWind(id);

	if(GameData::usesAI(playerAI) &&
	    playerAI.isWinMahjong(currentWind, roundWind, dropStone, & winResult, ruleset))
	{
	    if(sayOnly)
		return GameData::client2Mahjong(playerAI.avatar, ClientSayGame(), actions);
	    else
		return GameData::client2Mahjong(playerAI.avatar, ClientButtonGame(), actions);
	}
    }

	std::vector<std::pair<Avatar, MahjongCallPlan>> callPlans;
	callPlans.reserve(3);
	for(auto & id : activeMatchTopology().winds())
	{
	    if(id == currentWind())
		continue;

	    LocalPlayer & playerAI = GameData::playerOfWind(id);
	    if(!GameData::usesAI(playerAI)) continue;

	    const StrategicIntent intent = chooseStrategicIntent(
		observePlayer(playerAI.avatar), behaviorProfile(playerAI), GameData::aiDifficulty());
	    const MahjongCallPlan plan = chooseMahjongCall(playerAI, currentWind, dropStone,
                                                         intent, ruleset);
	    DEBUG("AI Mahjong call: avatar=" << playerAI.avatar.toString() <<
		  ", type=" << static_cast<int>(plan.type) << ", score=" << plan.score <<
		  ", variant=" << plan.variant);
	    if(plan.isCall()) callPlans.emplace_back(playerAI.avatar, plan);
	}

	// Game is always accepted above. Optional calls use ruleset priority while
	// stable wind iteration preserves the established tie break.
	auto selected = callPlans.end();
	int selectedPriority = 0;
	for(auto iter = callPlans.begin(); iter != callPlans.end(); ++iter)
	{
	    const int priority = ruleset.discardClaimPriority(toRulesetCall(iter->second.type));
	    if(selected == callPlans.end() || selectedPriority < priority)
	    {
		selected = iter;
		selectedPriority = priority;
	    }
	}

	if(selected == callPlans.end() || selectedPriority <= 0) return false;

	switch(selected->second.type)
	{
	    case MahjongCallType::Kong:
		return sayOnly ? GameData::client2Mahjong(selected->first, ClientSayKong(1), actions) :
		                 GameData::client2Mahjong(selected->first, ClientButtonKong1(), actions);
	    case MahjongCallType::Pung:
		return sayOnly ? GameData::client2Mahjong(selected->first, ClientSayPung(), actions) :
		                 GameData::client2Mahjong(selected->first, ClientButtonPung(), actions);
	    case MahjongCallType::Chao:
		return sayOnly ? GameData::client2Mahjong(selected->first, ClientSayChao(), actions) :
		                 GameData::client2Mahjong(selected->first,
		                     ClientChaoVariant(selected->second.variant), actions);
	    case MahjongCallType::Pass:
		break;
	}

    return false;
}

struct StoneCost
{
    GameStone		stone;
    int			index;
    int			cost;

    StoneCost() : index(-1), cost(0) {}
    StoneCost(const GameStone & gs, int pos, int val) : stone(gs), index(pos), cost(val) {}

    bool		operator< (const StoneCost & sc) const { return cost < sc.cost; }
};

int mahjongStoneValue(const GameStones & stones, const GameStone & stone,
                      const VecStones & trash, const WinRules & other)
{
    int cost = 100;

    const int count1 = std::count(stones.begin(), stones.end(), stone);
    const int count2 = std::count(trash.begin(), trash.end(), stone) +
                       std::count(other.begin(), other.end(), stone);

    if(stone.isSpecial())
    {
	if(1 < count1)
	    cost += 3 < count1 + count2 ? -20 * count2 : 20 * count1;
	return cost;
    }

    if(1 < count1)
	cost += 3 < count1 + count2 ? -10 * count2 : 10 * count1;

    switch(stone.order())
    {
	case 1:
	    cost += 10 * ((stones.findStone(stone.next()) ? 1 : 0) +
			 (stones.findStone(stone.next().next()) ? 1 : 0));
	    break;
	case 2:
	    cost += 10 * ((stones.findStone(stone.prev()) ? 1 : 0) +
			 (stones.findStone(stone.next()) ? 1 : 0) +
			 (stones.findStone(stone.next().next()) ? 1 : 0));
	    break;
	case 3:
	case 4:
	case 5:
	case 6:
	case 7:
	    cost += 10 * ((stones.findStone(stone.prev().prev()) ? 1 : 0) +
			 (stones.findStone(stone.prev()) ? 1 : 0) +
			 (stones.findStone(stone.next()) ? 1 : 0) +
			 (stones.findStone(stone.next().next()) ? 1 : 0));
	    break;
	case 8:
	    cost += 10 * ((stones.findStone(stone.prev().prev()) ? 1 : 0) +
			 (stones.findStone(stone.prev()) ? 1 : 0) +
			 (stones.findStone(stone.next()) ? 1 : 0));
	    break;
	case 9:
	    cost += 10 * ((stones.findStone(stone.prev().prev()) ? 1 : 0) +
			 (stones.findStone(stone.prev()) ? 1 : 0));
	    break;
	default: break;
    }

    return cost;
}

int AI::mahjongSelect(const GameStones & stones, const VecStones & trash, const WinRules & other)
{
    return mahjongSelect(stones, trash, other, StrategicIntent());
}

int AI::mahjongSelect(const GameStones & stones, const VecStones & trash, const WinRules & other,
                      const StrategicIntent & intent)
{
    std::multiset<StoneCost> result;

    for(auto it = stones.begin(); it != stones.end(); ++it)
    {
	const GameStone & stone = *it;
	const int strategicValue = stone.isCasted() ? 0 : intent.runeValue(stone);
	result.emplace(stone, std::distance(stones.begin(), it),
	               mahjongStoneValue(stones, stone, trash, other) + strategicValue);
    }

    if(result.size())
    {
	auto itbeg = result.begin();
	auto itend = result.upper_bound(*itbeg);

	std::vector<StoneCost> rnd; rnd.reserve(8);
	std::copy(itbeg, itend, std::back_inserter(rnd));

	GameplayRng::shuffle(rnd.begin(), rnd.end());

	// first find casted
	auto itres = std::find_if(rnd.begin(), rnd.end(), [](const StoneCost & sc) { return sc.stone.isCasted(); });
	if(itres != rnd.end()) return (*itres).index;

	// after return normal
	return rnd.front().index;
    }

    // impossible ;)
    return GameplayRng::uniform(0, stones.size());
}

int AI::mahjongLuckChoice(const GameStones & stones, const VecStones & choices,
                          const VecStones & trash, const WinRules & other)
{
    return mahjongLuckChoice(stones, choices, trash, other, StrategicIntent());
}

int AI::mahjongLuckChoice(const GameStones & stones, const VecStones & choices,
                          const VecStones & trash, const WinRules & other,
                          const StrategicIntent & intent)
{
    int selected = 0;
    int bestValue = std::numeric_limits<int>::min();

    for(int index = 0; index < static_cast<int>(choices.size()); ++index)
    {
	GameStones hand = stones;
	const GameStone candidate(choices[index], false);
	hand.add(candidate);
	const int value = mahjongStoneValue(hand, candidate, trash, other) + intent.runeValue(candidate);
	if(bestValue < value)
	{
	    bestValue = value;
	    selected = index;
	}
    }

    return selected;
}

void AI::adventureMove(const RemotePlayer & player, ActionList & actions)
{
    const BehaviorProfile profile = behaviorProfile(player);

    // Claims can open another border, so recalculate after each successful deed.
    while(true)
    {
        const AdventureClaimPlan claim = chooseAdventureClaim(player, profile);
        if(!claim.isValid()) break;
        DEBUG("AI profile: " << behaviorProfileName(profile) << ", claim: " << claim.land.toString() <<
              ", score: " << claim.score);
        if(!GameData::client2Adventure(player.avatar, ClientLandClaim(claim.land), actions)) break;
    }

    const AdventureTurnPlan turnPlan = chooseAdventureTurn(player, profile);
    for(const AdventureThreat & threat : turnPlan.threats)
    {
        DEBUG("AI profile: " << behaviorProfileName(profile) << ", avatar: " << player.avatar.toString() <<
              ", predicted threat: " << threat.land.toString() << ", enemy origin: " <<
              threat.enemyOrigin.toString() << ", score: " << threat.score << ", strength: " <<
              threat.enemyStrength << "/" << threat.defenseStrength << ", capture: " <<
              threat.captureChance << "%" << ", survival: " << threat.attackerSurvival << "%");
    }

    for(const AdventurePartyOrder & order : turnPlan.orders)
    {
        if(order.hold)
        {
            DEBUG("AI profile: " << behaviorProfileName(profile) << ", avatar: " << player.avatar.toString() <<
                  ", hold: " << order.origin.toString() <<
                  (order.defend.isValid() ? ", reserve for: " + order.defend.toString() : ""));
            continue;
        }

        if(!order.move.isValid()) continue;
        DEBUG("AI profile: " << behaviorProfileName(profile) << ", avatar: " << player.avatar.toString() <<
              ", target: " << order.move.target.toString() << ", destination: " <<
              order.move.destination.toString() << ", score: " << order.score << ", strength: " <<
              order.move.attackStrength << "/" << order.move.defenseStrength << ", capture: " <<
              order.move.captureChance << "%" << ", survival: " << order.move.attackerSurvival << "%" <<
              (order.defend.isValid() ? ", reinforce: " + order.defend.toString() : ""));

        for(int unit : order.units)
        {
            if(player.army.findBattleUnitConst(unit))
                GameData::client2Adventure(player.avatar, ClientUnitMoved(unit, order.move.destination), actions);
        }
    }
}
