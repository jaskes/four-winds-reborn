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
#include <iterator>
#include <sstream>

#include "gamedata.h"
#include "matchtopology.h"
#include "runegameruleset.h"

/* WinResults */
enum { AllConcealedWithDiscard = 0x80000000 };

WinResults::WinResults(const Wind & wind1, const Wind & wind2, const Wind & wind3, const WinRules & winRules1, const WinRules & winRules2, const Stone & winPair, const Stone & winStone)
{
    dealWind = wind1;
    winWind = wind2;
    roundWind = wind3;

    if(winRules1.empty())
	flags.set(AllConcealedWithDiscard);

    for(auto & rule : winRules1)
	if(rules.size() < 4) rules.push_back(rule);

    for(auto & rule : winRules2)
	if(rules.size() < 4) rules.emplace_back(rule.rule(), rule.stone(), true);

    pairStone = winPair;
    lastStone = winStone;
}

WinResults WinResults::drawn(const Wind & deal, const Wind & round)
{
    WinResults res;
    res.dealWind = deal;
    res.roundWind = round;
    return res;
}

WinRules WinResults::winRulesConcealed(void) const
{
    WinRules res;
    for(auto & rule : rules)
	if(rule.isConcealed()) res.push_back(rule);
    return res;
}

std::string WinResults::toString(void) const
{
    std::ostringstream os;

    os <<"deal" << "(" << dealWind.toString() << ")" << ", " <<
	"win" << "(" << winWind.toString() << ")" << ", " <<
	"round" << "(" << roundWind.toString() << ")" << ", ";

    os << "rules: " << "[ ";
    for(auto it = rules.begin(); it != rules.end(); ++it)
	os << (*it).toString() << ", ";
    os << " ]" << ", ";

    os << "pair" << "(" << pairStone() << ")" << ", " << "last" << "(" << lastStone() << ")";

    return os.str();
}

JsonObject WinResults::toJsonObject(void) const
{
    JsonObject jo;
    jo.addString("wind:deal", dealWind.toString());
    jo.addString("wind:win", winWind.toString());
    jo.addString("wind:round", roundWind.toString());
    jo.addString("stone:pair", pairStone.toString());
    jo.addString("stone:last", lastStone.toString());
    jo.addInteger("flags", flags());
    jo.addArray("rules", rules.toJsonArray());
    return jo;
}

WinResults WinResults::fromJsonObject(const JsonObject & jo)
{
    WinResults res;
    res.dealWind = Wind(jo.getString("wind:deal", "none"));
    res.winWind = Wind(jo.getString("wind:win", "none"));
    res.roundWind = Wind(jo.getString("wind:round", "none"));
    res.pairStone = Stone(jo.getString("stone:pair", "none"));
    res.lastStone = Stone(jo.getString("stone:last", "none"));
    res.flags = jo.getInteger("flags", 0);

    const JsonArray* ja = nullptr;

    ja = jo.getArray("rules");
    if(ja) res.rules = WinRules::fromJsonArray(*ja);

    return res;
}

/* HandBonus */
std::string HandBonus::name(void) const
{
    switch(type())
    {
	case OneChance:			return _("One Chance");
	case SelfDrawn:			return _("Self Drawn");
	case AllConcealedWithDiscard:	return _("All Concealed, with Discard");
	default: break;
    }

    return "None";
}

/* DoubleBonus */
std::string DoubleBonus::name(void) const
{
    switch(type())
    {
	case LuckySets:			return _("Lucky Sets");
	case OneSuitHonors:		return _("One Suit plus Honors");
	case NoPoints:			return _("No Points");
	case FourTriplets:		return _("Four Triplets");
	case TerminalsHonors:		return _("Terminals and Honors");
	case TerminalHonorEachSet:	return _("Terminal, Honor in each set");
	case AllSimples:		return _("All Simples");
	case ThreeLittleDragons:	return _("Three Little Dragons");
	case OneSuitOnly:		return _("One Suit Only");
	case ThreeConsecutiveSequences:	return _("Three Consecutive Sequences");
	case AllConcealedSelfDrawn:	return _("All Concealed, Self Drawn");
	case LastTileWall:		return _("Last Tile");
	case ThreeConcealedTriplets:	return _("Three Concealed Triplets");
	case RobbedKong:		return _("Robbed a Kong");
	case SupplementalTile:		return _("Supplemental Tile");
	case ThreeBigDragons:		return _("Three Big Dragons");
	case BigFourWinds:		return _("Big Four Winds");
	case AllTerminals:		return _("All Terminals");
	case AllHonors:			return _("All Honors");
	case NineGates:			return _("Nine Gates");
	case HeavenlyHand:		return _("Heavenly Hand");
	case FourConcealedTriplets:	return _("Four Concealed Triplets");
	case ThirteenOrphans:		return _("Thirteen Orphans");
	case LittleFourWinds:		return _("Little Four Winds");
	case EarthlyHand:		return _("Earthly Hand");
	default: break;
    }

    return "None";
}

/* RuneBonus */
Stones RuneBonus::stones(void) const
{
    Stones res; res.reserve(5);
    if(isKong())
	res.assign(4, stone());
    else
    if(isPung())
	res.assign(3, stone());
    else
    if(isPair())
	res.assign(2, stone());
    else
    if(isChao())
	res << stone() << stone().next() << stone().next().next();
    return res;
}

RuneBonusList WinResults::bonusRunes(void) const
{
    RuneBonusList res;

    for(auto & winRule : rules)
    {
	const Stone & stone = winRule.stone();
	const int & rule = winRule.rule();
	const bool & concealed = winRule.isConcealed();
	int type = 0; int value = 0;

        switch(rule)
        {
	    case WinRule::Pung:
		if(stone.isHonor() || stone.isTerminal())
		{
		    type |= RuneBonus::RunePung | (stone.isHonor() ? RuneBonus::RuneHonor : RuneBonus::RuneTerminal);
		    value = concealed ? 8 : 4;
		}
		else
		{
		    type |= RuneBonus::RunePung | RuneBonus::RuneSimple;
		    value = concealed ? 4 : 2;
		}
		break;

	    case WinRule::Kong:
		if(stone.isHonor() || stone.isTerminal())
		{
		    type |= RuneBonus::RuneKong | (stone.isHonor() ? RuneBonus::RuneHonor : RuneBonus::RuneTerminal);
		    value = concealed ? 32 : 16;
		}
		else
		{
		    type |= RuneBonus::RuneKong | RuneBonus::RuneSimple;
		    value = concealed ? 16 : 8;
		}
		break;

	    case WinRule::Chao:
		    type |= RuneBonus::RuneChao | RuneBonus::RuneSimple;
		    value = 0;
		break;

	    default:
		break;
        }

	if(stone.isWind())
	{
	    if(stone.isWind(roundWind)) type |= RuneBonus::RuneLucky;
	    if(stone.isWind(winWind)) type |= RuneBonus::RuneLucky;
	}
	else
	if(stone.isDragon())
	{
	    type |= RuneBonus::RuneLucky;
	}

	if(concealed) type |= RuneBonus::RuneConcealed;
	res << RuneBonus(stone, type, value);
    }

    // add pair
    int type = RuneBonus::RunePair;
    if(lastStone != pairStone) type |= RuneBonus::RuneConcealed;

    res << RuneBonus(pairStone, type, pairBonus());

    return res;
}

Wind OpponentFine::wind(void) const
{
    switch(type())
    {
	case Wind::East:	return Wind::East;
	case Wind::West:	return Wind::West;
	case Wind::South:	return Wind::South;
	case Wind::North:	return Wind::North;
	default: break;
    }

    return Wind::None;
}

int WinResults::scoreRules(void) const
{
    int res = 0;
    RuneBonusList list = bonusRunes();
    for(auto it = list.begin(); it != list.end(); ++it) res += (*it).value();
    return res;
}

bool WinResults::noPoints(void) const
{
    return 0 == scoreRules();
}

int WinResults::baseScore(void) const
{
    return baseScore(classicRuneGameRuleset());
}

int WinResults::baseScore(const RuneGameRuleset & ruleset) const
{
    return ruleset.baseWinPoints();
}

int WinResults::pairBonus(void) const
{
    // pair bonus: double wind
    if(pairStone.isWind(roundWind) && pairStone.isWind(winWind))
        return 4;
    else
    // pair bonus: lucky runes
    if(pairStone.isDragon() || pairStone.isWind(roundWind) || pairStone.isWind(winWind))
        return 2;

    return 0;
}

HandBonusList WinResults::bonusHands(void) const
{
    HandBonusList res;

    // 2 points for "One Chance" and "Self Drawn" are not counted if the hand would otherwise be "No Points".
    if(! noPoints())
    {
	// other bonus: one chance hand
	if(pairStone == lastStone || pairStone.isLucky())
	    res << HandBonus(HandBonus::OneChance, 2);
	else
	{
	    if(std::any_of(rules.begin(), rules.end(),
		[&](const WinRule & rule){ return rule.isOneChance(pairStone); }))
		res << HandBonus(HandBonus::OneChance, 2);
	}

	// other bonus: self-drawn hand
	if(isSelfDrawn())
	    res << HandBonus(HandBonus::SelfDrawn, 2);
    }

    // other bonus: Concealed hand with	discarded tile
    if(flags.check(AllConcealedWithDiscard))
	res << HandBonus(HandBonus::AllConcealedWithDiscard, 10);

    return res;
}

int WinResults::totalPoints(void) const
{
    return totalPoints(classicRuneGameRuleset());
}

int WinResults::totalPoints(const RuneGameRuleset & ruleset) const
{
    int res = baseScore(ruleset);

    res += scoreRules();
    res += pairBonus();

    for(auto & bonus : bonusHands())
	res += bonus.value();

    return res;
}

DoubleBonusList WinResults::bonusDoubles(void) const
{
    DoubleBonusList res;

    // No Points: 1
    if(noPoints())
	res << DoubleBonus(DoubleBonus::NoPoints, 1);

    // Little three dragons: 1
    // (3 dragon sets, one being a pair)
    if(pairStone.isDragon())
    {
	if(2 == rules.countStoneType(StoneType::IsDragon))
	    res << DoubleBonus(DoubleBonus::ThreeLittleDragons, 1);
    }
    else
    // little four winds: 1
    if(pairStone.isWind())
    {
	if(3 == rules.countStoneType(StoneType::IsWind))
	    res << DoubleBonus(DoubleBonus::LittleFourWinds, 1);
    }

    // three big dragons
    if(3 == rules.countStoneType(StoneType::IsDragon))
    {
	res << DoubleBonus(DoubleBonus::ThreeBigDragons, 5);
    }
    // big four winds
    else
    if(4 == rules.countStoneType(StoneType::IsWind))
    {
	res << DoubleBonus(DoubleBonus::BigFourWinds, 5);
    }

    // Lucky Sets:
    {
	int value = 0;

	for(auto & rule : rules)
	{
	    // pung, kong only
	    if(! rule.isChao())
	    {
		// Double Wind triplet or foursome: 2
		if(rule.stone().isWind(roundWind) && rule.stone().isWind(winWind))
		    value += 2;
		else
		// Lucky triplet or foursome: 1
		if((rule.stone().isDragon() || rule.stone().isWind(roundWind) || rule.stone().isWind(winWind)))
		    value += 1;
	    }
	}

	if(value) res << DoubleBonus(DoubleBonus::LuckySets, value);
    }

    // Four triplets or foursomes:
    // 0, 1, or 2 concealed: 1
    // 3 concealed: 2
    // 4 concealed: Limit (5 - 500pt)
    int chaoCount = std::count_if(rules.begin(), rules.end(), [](const WinRule & rule){ return rule.isChao(); });

    if(0 == chaoCount)
    {
	int concealed = std::count_if(rules.begin(), rules.end(),
			[](const WinRule & rule){ return rule.isConcealed(); });

	if(3 < concealed)
	    res << DoubleBonus(DoubleBonus::FourConcealedTriplets, 5);
	else
	    res << DoubleBonus(DoubleBonus::FourTriplets, 2 < concealed ? 2 : 1);
    }

    // Three concealed triplets, one sequence: 1
    if(1 == chaoCount)
    {
	int concealed = std::count_if(rules.begin(), rules.end(),
			[](const WinRule & rule){ return ! rule.isChao() && rule.isConcealed(); });

	if(3 == concealed)
	    res << DoubleBonus(DoubleBonus::ThreeConcealedTriplets, 1);
    }

    // Three consecutive sequences: 1
    // (1,2,3 4,5,6 7,8,9 of same suit)
    {
	WinRules tmp = rules;
	auto itend = std::remove_if(tmp.begin(), tmp.end(),
		    [](const WinRule & rule){ return rule.stoneOrder(1) || rule.stoneOrder(4) || rule.stoneOrder(7); });

	if(3 <= std::distance(itend, tmp.end()))
	{
	    int type = 0;

	    if(3 == std::count_if(itend, tmp.end(), [](const WinRule & rule) { return rule.stoneType(StoneType::IsSkull); }))
		type = StoneType::IsSkull;
	    else
	    if(3 == std::count_if(itend, tmp.end(), [](const WinRule & rule) { return rule.stoneType(StoneType::IsNumber); }))
		type = StoneType::IsNumber;
	    else
	    if(3 == std::count_if(itend, tmp.end(), [](const WinRule & rule) { return rule.stoneType(StoneType::IsSword); }))
		type = StoneType::IsSword;

	    if(type)
		res << DoubleBonus(DoubleBonus::ThreeConsecutiveSequences, 1);
	}
    }

    // All Terminals and Honors: 1
    if(pairStone.isTerminal() || pairStone.isHonor())
    {
	int terminals = std::count_if(rules.begin(), rules.end(), [](const WinRule & rule) { return rule.stoneTerminal(); });
	int honors = std::count_if(rules.begin(), rules.end(), [](const WinRule & rule) { return rule.stoneHonor(); });

	if(4 == terminals && pairStone.isTerminal())
	    res << DoubleBonus(DoubleBonus::AllTerminals, 5);
	else
	if(4 == honors && pairStone.isHonor())
	    res << DoubleBonus(DoubleBonus::AllHonors, 5);
	else
	if(4 == terminals + honors)
	    res << DoubleBonus(DoubleBonus::TerminalsHonors, 1);
    }
    else
    // All Simples: 1
    {
	int terminals = std::count_if(rules.begin(), rules.end(), [](const WinRule & rule) { return rule.stoneTerminal(); });
	int honors = std::count_if(rules.begin(), rules.end(), [](const WinRule & rule) { return rule.stoneHonor(); });

	if(0 == terminals + honors)
	    res << DoubleBonus(DoubleBonus::AllSimples, 1);

    }

    // One suit only: 4
    if(std::all_of(rules.begin(), rules.end(), [&](const WinRule & rule) { return rule.stoneType(pairStone.stoneType()); }))
	res << DoubleBonus(DoubleBonus::OneSuitOnly, 4);

    // One suit with honors: 1
    {
	WinRules tmp = rules;
	auto itend = std::remove_if(tmp.begin(), tmp.end(), [](const WinRule & rule){ return rule.stoneHonor(); });
	int type = 0;

	if(! pairStone.isHonor())
	    type = pairStone.stoneType();
	else
	if(tmp.begin() != itend)
	    type = tmp.front().stone().stoneType();

	if(type && std::all_of(tmp.begin(), itend, [=](const WinRule & rule){ return rule.stoneType(type); }))
	    res << DoubleBonus(DoubleBonus::OneSuitHonors, 1);
    }

//    last:
//    TerminalHonorEachSet,
//    SupplementalTile, LastTileWall,

//    AllConcealedSelfDrawn, RobbedKong, HeavenlyHand, EarthlyHand

//Terminal or honor in each set           1 (only applies if the hand contains at least one sequence)
//
//Ways of Going Out
//     Supplemental Tile                  1
//     Last Tile of Wall                  1

    return res;
}

int WinResults::totalScore(void) const
{
    return totalScore(classicRuneGameRuleset());
}

int WinResults::totalScore(const RuneGameRuleset & ruleset) const
{
    int doubles = 0;

    DoubleBonusList list = bonusDoubles();
    for(auto it = list.begin(); it != list.end(); ++it) doubles += (*it).value();

    int res = totalPoints(ruleset) * ruleset.scoreMultiplier(doubles);
    if(res > ruleset.maximumWinScore()) res = ruleset.maximumWinScore();

    return res;
}

int WinResults::scoreMultiplier(int doubles)
{
    return classicRuneGameRuleset().scoreMultiplier(doubles);
}

OpponentFinesList WinResults::opponentFines(void) const
{
    OpponentFinesList res;

    // If dealer wins self-drawn:            Receives x2 from each player.
    // If dealer wins from discard:          Receives x6 from discarder.
    // If non-dealer wins self drawn:        Receives x2 from dealer, Receives x1 from other players.
    // If non-dealer wins from discard:      Receives x4 from discarder.

    Wind windWin = winWind;
    Wind windDeal(Wind::East);
    Wind windCurrent = dealWind;

    if(windWin == windDeal)
    {
        if(windWin == windCurrent)
        {
	    for(auto & id : activeMatchTopology().winds())
	    {
                if(windWin.id() != id)
                    res << OpponentFine(id, 2);
	    }
        }
        else
        {
            res << OpponentFine(windCurrent, 6);
        }
    }
    else
    {
        if(windWin == windCurrent)
        {
	    for(auto & id : activeMatchTopology().winds())
	    {
                if(windWin.id() != id)
                    res << OpponentFine(id, (windCurrent.id() == id ? 2 : 1));
	    }
        }
        else
        {
            res << OpponentFine(windCurrent, 4);
        }
    }

    return res;
}
