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
#include <sstream>

#include "gameplayrng.h"
#include "gamedata.h"
#include "matchtopology.h"
#include "runegameruleset.h"

namespace GameData
{
    extern int bonusStart;
}

namespace
{
    const char* legacyNamedClanId(const Clan & clan)
    {
        switch(clan())
        {
            case Clan::Red:    return "maitha";
            case Clan::Yellow: return "kartha";
            case Clan::Aqua:   return "iz";
            case Clan::Purple: return "marz";
            default: break;
        }
        return "none";
    }
}

/* Person */
JsonObject Person::toJsonObject(void) const
{
    JsonObject jo;
    jo.addString("avatar", avatar.toString());
    jo.addString("clan", clan.toString());
    jo.addString("wind", wind.toString());
    jo.addInteger("flags", flags());
    return jo;
}

Person Person::fromJsonObject(const JsonObject & jo)
{
    Person res;
    res.avatar = Avatar(jo.getString("avatar", "none"));
    res.clan = Clan(jo.getString("clan", "none"));
    res.wind = Wind(jo.getString("wind", "none"));
    res.flags = jo.getInteger("flags", 0);
    return res;
}

std::string Person::toString(void) const
{
    std::ostringstream os;
    os << "wind: " << wind.toString() << ", " << "avatar: " << name();
    return os.str();
}

std::string Person::name(void) const
{
    const AvatarInfo & info = GameData::avatarInfo(avatar);
    return info.name;
}

/* Persons */
Persons::Persons(const Person & person)
{
    reserve(4);
    push_back(person);

    // add others persons
    std::vector<Clan::clan_t> clans(clans_all);
    clans.erase(std::find(clans.begin(), clans.end(), person.clan.id()));

    while(! clans.empty())
    {
	Avatars avatars = GameData::avatarsOfClan(clans.back());
	auto it = std::remove_if(avatars.begin(), avatars.end(), [&](const Avatar & ava)
	{
	    return std::any_of(begin(), end(),
			[&](const Person & pers){ return pers.avatar == ava; });
	});
	avatars.erase(it, avatars.end());
        GameplayRng::shuffle(avatars.begin(), avatars.end());

	push_back(Person(avatars.front(), clans.back(), Wind()));
	clans.pop_back();
    }

    if(size() == 4)
    {
	// all ai
	for(auto & pers : *this)
	    pers.setAI(true);

	if(activeMatchTopology().id() == ClassicFreeForAllTopologyId)
	    GameplayRng::shuffle(begin(), end());
	else
	{
	    // Duel and Coalition use two readable island halves. Red/Purple are
	    // assigned to East/South and Yellow/Aqua to West/North; order within
	    // each pair remains random so winds do not become a clan alias.
	    std::vector<Person> westHalf;
	    std::vector<Person> eastHalf;
	    for(const Person & pers : *this)
	    {
		if(pers.clan == Clan(Clan::Red) || pers.clan == Clan(Clan::Purple))
		    westHalf.push_back(pers);
		else
		    eastHalf.push_back(pers);
	    }
	    GameplayRng::shuffle(westHalf.begin(), westHalf.end());
	    GameplayRng::shuffle(eastHalf.begin(), eastHalf.end());
	    clear();
	    insert(end(), westHalf.begin(), westHalf.end());
	    insert(end(), eastHalf.begin(), eastHalf.end());
	}

	at(0).wind = Wind(Wind::East);
	at(1).wind = Wind(Wind::South);
	at(2).wind = Wind(Wind::West);
	at(3).wind = Wind(Wind::North);

	auto it = std::find_if(begin(), end(),
			[&](const Person & pers){ return pers.isAvatar(person.avatar); });

	if(it != end())
	    (*it).setAI(false);
	else
	    ERROR("local person not found: " << person.avatar.toString());
    }
    else
	ERROR("incorrect size");
}

/* LandClaims */
int LandClaims::points(const Clan & clan) const
{
    return clan.isValid() && clan() < static_cast<int>(values.size()) ? values[clan()] : 0;
}

void LandClaims::add(const Clan & clan, int value)
{
    if(clan.isValid() && clan() < static_cast<int>(values.size()) && 0 < value)
        values[clan()] += value;
}

bool LandClaims::spend(const Clan & clan, int value)
{
    if(!clan.isValid() || clan() >= static_cast<int>(values.size()) || value < 0 || values[clan()] < value)
        return false;

    values[clan()] -= value;
    return true;
}

JsonObject LandClaims::toJsonObject(void) const
{
    JsonObject jo;
    for(auto clanId : clans_all)
    {
        const Clan clan(clanId);
        jo.addInteger(clan.toString(), points(clan));
    }
    return jo;
}

LandClaims LandClaims::fromJsonObject(const JsonObject & jo)
{
    LandClaims res;
    for(auto clanId : clans_all)
    {
        const Clan clan(clanId);
        int points = jo.getInteger(clan.toString(), -1);
        if(points < 0)
            points = jo.getInteger(legacyNamedClanId(clan), 0);
        res.values[clan()] = std::max(0, points);
    }
    return res;
}

/* RemotePlayer */
RemotePlayer::RemotePlayer()
{
    points = GameData::bonusStart;
}

RemotePlayer::RemotePlayer(const Person & pers) : Person(pers)
{
    points = GameData::bonusStart;
}

bool RemotePlayer::adventurePartDone(void) const
{
    return flags.check(Person::AdventurePartDone);
}

void RemotePlayer::setAdventurePartDone(void)
{
    flags.set(Person::AdventurePartDone);
}

void RemotePlayer::initAdventurePart(void)
{
    flags.reset(Person::AdventurePartDone);

    const AvatarInfo & avaInfo = GameData::avatarInfo(avatar);
    for(auto & bcr : army.toBattleCreatures())
	bcr->initAdventurePart(avaInfo.ability);
}

Lands RemotePlayer::lands(void) const
{
    return Lands::thisClan(clan);
}

BattleTargets RemotePlayer::toBattleTargets(void) const
{
    return army.toBattleTargets(clan);
}

bool RemotePlayer::mahjongApplySpell(const Spell & spell, const Avatar & source)
{
    switch(spell())
    {
        case Spell::DrawSkull:
        case Spell::DrawSword:
        case Spell::DrawNumber:
	//
        case Spell::RandomDiscard:
	//
        case Spell::ManaFog:
	    affected.insert(AffectedSpell(spell));
            return true;

        case Spell::ScryRunes:
	    affected.insert(AffectedSpell(spell, GameData::spellInfo(spell).duration(), source));
            return true;

        case Spell::Silence:
        {
            // check telepath
            const AvatarInfo & avaInfo = GameData::avatarInfo(avatar);
            if(avaInfo.ability() == Ability::Telepath)
            {
                DEBUG("ability Telepath found, silence skipping...");
                return false;
            }

	    affected.insert(AffectedSpell(spell));
            return true;
        }

        default: ERROR("unknown action" << ", " << "spell: " << spell.toString()); break;
    }

    return false;
}

bool RemotePlayer::isAffectedSpell(const Spell & spell) const
{
    return affected.isAffected(spell);
}

bool RemotePlayer::isAffectedSpell(const Spell & spell, const Avatar & source) const
{
    return affected.isAffected(spell, source);
}

bool RemotePlayer::isSilenced(void) const
{
    return isAffectedSpell(Spell::Silence) &&
           GameData::avatarInfo(avatar).ability() != Ability::Telepath;
}

void RemotePlayer::affectedSpellActivate(const Spell & spell)
{
    affected.spellAffected(spell);
}

JsonObject RemotePlayer::toJsonObject(void) const
{
    JsonObject jo = Person::toJsonObject();
    jo.addArray("rules", rules.toJsonArray());
    jo.addArray("army", army.toJsonArray());
    jo.addInteger("points", points);
    jo.addArray("affected", affected.toJsonArray());
    jo.addObject("landClaims", landClaims.toJsonObject());

    return jo;
}

RemotePlayer RemotePlayer::fromJsonObject(const JsonObject & jo)
{
    RemotePlayer res(Person::fromJsonObject(jo));
    const JsonArray* ja = nullptr;

    ja = jo.getArray("rules");
    if(ja) res.rules = WinRules::fromJsonArray(*ja);

    ja = jo.getArray("army");
    if(ja) res.army = BattleArmy::fromJsonArray(*ja);

    res.points = jo.getInteger("points", 0);

    ja = jo.getArray("affected");
    if(ja) res.affected = AffectedSpells::fromJsonArray(*ja);

    const JsonObject* claims = jo.getObject("landClaims");
    if(claims) res.landClaims = LandClaims::fromJsonObject(*claims);

    return res;
}

/* LocalPlayer */
JsonObject LocalPlayer::toJsonObject(void) const
{
    JsonObject jo = RemotePlayer::toJsonObject();
    jo.addArray("stones", stones.toJsonArray());
    jo.addObject("stone:new", newStone.toJsonObject());

    return jo;
}

LocalPlayer LocalPlayer::fromJsonObject(const JsonObject & jo)
{
    LocalPlayer res(RemotePlayer::fromJsonObject(jo));

    const JsonArray* ja = jo.getArray("stones");
    if(ja) res.stones = GameStones::fromJsonArray(*ja);

    const JsonObject* jj = jo.getObject("stone:new");
    if(jj) res.newStone = GameStone::fromJsonObject(*jj);

    return res;
}

bool LocalPlayer::newTurnEvent(CroupierSet & croupier, bool skipNewStone /* pung, kong, chao */)
{
    DEBUG(toString() << ", " << "stones: " <<  stones.toString() <<  ", " << "rules: " <<  rules.toString());

    setCasted(false);

    if(skipNewStone)
    {
	newStone = GameStone(Stone::None, true);
        DEBUG("new stone: " << "skipped");
    }
    else
    {
	const AvatarInfo & info = GameData::avatarInfo(avatar);
	const bool directedDraw = isAffectedSpell(Spell::DrawNumber) ||
	    isAffectedSpell(Spell::DrawSword) || isAffectedSpell(Spell::DrawSkull);
	if(info.ability() == Ability::Luck && !directedDraw && croupier.beginLuckDraw())
	{
	    newStone = GameStone(Stone::None, true);
	    DEBUG("luck draw: " << croupier.luckChoices().toString());
	    return true;
	}

	newStone = GameStone(croupier.get(*this), false);
	DEBUG("new stone: " << newStone() << ", " << "(" << newStone.toString() << ")");
    }

    return false;
}

void LocalPlayer::initMahjongPart(void)
{
    stones.clear();
    rules.clear();

    newStone.reset();
    flags.reset(Person::Casted);

    const AvatarInfo & avaInfo = GameData::avatarInfo(avatar);
    for(auto & bcr : army.toBattleCreatures())
	bcr->initMahjongPart(avaInfo.ability);
}

bool LocalPlayer::haveKong(void) const
{
    return stones.haveKong();
}

bool LocalPlayer::allowCastSpell(const Spell & spell) const
{
    if(isSilenced() || isAffectedSpell(Spell::ManaFog))
        return false;

    // check avatar info spells
    const AvatarInfo & avaInfo = GameData::avatarInfo(avatar);
    if(avaInfo.spells.end() == std::find(avaInfo.spells.begin(), avaInfo.spells.end(), spell))
    {
	const Spells & armySpells = army.allCastSpells();
	if(armySpells.end() == std::find(armySpells.begin(), armySpells.end(), spell))
	    return false;
    }

    const SpellInfo & spellInfo = GameData::spellInfo(spell);
    return stones.allowCast(spellInfo.stones, newStone);
}

bool LocalPlayer::isMahjongChao(const Wind & currentWind, const Stone & dropStone) const
{
    return isMahjongChao(currentWind, dropStone, classicRuneGameRuleset());
}

bool LocalPlayer::isMahjongChao(const Wind & currentWind, const Stone & dropStone,
                                const RuneGameRuleset & ruleset) const
{
    if(isSilenced())
        return false;

    return ruleset.allowsChao(wind == currentWind.next(),
                              dropStone.isValid() && !dropStone.isSpecial(),
                              static_cast<int>(stones.findChaoVariants(dropStone).size()));
}

bool LocalPlayer::isMahjongPung(const Wind & currentWind, const Stone & dropStone) const
{
    return isMahjongPung(currentWind, dropStone, classicRuneGameRuleset());
}

bool LocalPlayer::isMahjongPung(const Wind & currentWind, const Stone & dropStone,
                                const RuneGameRuleset & ruleset) const
{
    if(isSilenced())
        return false;

    return dropStone.isValid() &&
        ruleset.allowsPung(currentWind != wind, stones.countStone(dropStone));
}

bool LocalPlayer::isMahjongKong1(const Wind & currentWind, const Stone & dropStone) const
{
    return isMahjongKong1(currentWind, dropStone, classicRuneGameRuleset());
}

bool LocalPlayer::isMahjongKong1(const Wind & currentWind, const Stone & dropStone,
                                 const RuneGameRuleset & ruleset) const
{
    if(isSilenced())
        return false;

    return dropStone.isValid() &&
        ruleset.allowsExposedKong(currentWind != wind, stones.countStone(dropStone));
}

bool LocalPlayer::isMahjongKong2(const Wind & currentWind) const
{
    return isMahjongKong2(currentWind, classicRuneGameRuleset());
}

bool LocalPlayer::isMahjongKong2(const Wind & currentWind,
                                 const RuneGameRuleset & ruleset) const
{
    if(isSilenced())
        return false;

    const bool upgradesPung = newStone.isValid() &&
        std::any_of(rules.begin(), rules.end(),
            [&](const WinRule & rule){ return rule.isPungStone(newStone); });
    const int matchingRuneCount = newStone.isValid() ? stones.countStone(newStone) : 0;
    return ruleset.allowsSelfKong(currentWind == wind, newStone.isValid(),
                                  upgradesPung, matchingRuneCount);
}

bool LocalPlayer::isWinMahjong(const Wind & currentWind, const Wind & roundWind, const Stone & dropStone, WinResults* winResult) const
{
    return isWinMahjong(currentWind, roundWind, dropStone, winResult,
                        classicRuneGameRuleset());
}

bool LocalPlayer::isWinMahjong(const Wind & currentWind, const Wind & roundWind,
                               const Stone & dropStone, WinResults* winResult,
                               const RuneGameRuleset & ruleset) const
{
    if(isSilenced())
        return false;

    Stone winStone;

    if(!ruleset.allowsWinStone(newStone.isValid(), dropStone.isValid(), currentWind != wind))
        return false;

    if(newStone.isValid())
        winStone = newStone;
    else
    if(dropStone.isValid())
        winStone = dropStone;
    else
        return false;

    Stones stones2 = stones;
    stones2.push_back(winStone);

    Stones pairs = stones2.findPairs();
    if(pairs.empty()) return false;

    // wins: 4 rules and pair
    if(ruleset.isWinningStructure(static_cast<int>(rules.size()),
                                  static_cast<int>(pairs.size())))
    {
	DEBUG(toString() << ", " << "win stone: " << winStone() <<
		", " << "stones: " << stones.toString() << ", " << "rules: " << rules.toString());
        if(winResult) *winResult = WinResults(currentWind, wind, roundWind, rules, WinRules(), pairs[0], winStone);
        return true;
    }

    for(auto it1 = pairs.begin(); it1 != pairs.end(); ++it1)
    {
        Stones stones3 = stones2;

	bool r1 = stones3.removeStone(*it1);
	bool r2 = stones3.removeStone(*it1);

	if(!r1 || !r2)
	{
	    ERROR("pairs not found: " << (*it1).id() << ", " << "new stone: " << newStone() << ", " <<
		    "drop stone: " << dropStone() << ", " << "stones: " << stones3.toString());
	    return false;
	}

        WinRules rules2 = WinRules::fromStones(stones3);

        if(ruleset.isWinningStructure(static_cast<int>(rules.size() + rules2.size()), 1))
        {
	    DEBUG("wind: " << currentWind.toString() << ", " << "win stone: " << winStone() <<
		", " << "stones: " << stones.toString() << ", " << "rules: " << rules.toString() <<
		", " << "rules: " << rules2.toString());
	    if(winResult) *winResult = WinResults(currentWind, wind, roundWind, rules, rules2, *it1, winStone);
            return true;
        }
    }

    return false;
}

Stone LocalPlayer::setMahjongDrop(int indexDrop)
{
    return setMahjongDrop(indexDrop, classicRuneGameRuleset());
}

Stone LocalPlayer::setMahjongDrop(int indexDrop, const RuneGameRuleset & ruleset)
{

    if(stones.size() < indexDrop)
    {
	ERROR("index out of range");
	indexDrop = GameplayRng::uniform(0, stones.size() - 1);
    }

    Stone dropStone;

    if(indexDrop < stones.size())
    {
	dropStone = stones[indexDrop];
	stones.del(indexDrop);
	if(! isAI() && newStone.isValid()) stones.add(newStone);
    }
    else
    {
	dropStone = newStone;
    }

    DEBUG(toString() << ", " << "stones: " << stones.toString() << ", " << "drop stone: " << dropStone.id());

    if(isAffectedSpell(Spell::RandomDiscard))
    {
	affectedSpellActivate(Spell::RandomDiscard);
	stones.add(dropStone);
	indexDrop = GameplayRng::uniform(0, stones.size() - 1);
	dropStone = stones[indexDrop];
	stones.del(indexDrop);
	DEBUG("random discard affected" << ", " << "drop stone: " << dropStone.id());
    }

    newStone = GameStone(Stone::None, true);
    points += ruleset.spellPointAward(RuneGameSpellPointEvent::Discard);

    if(isAffectedSpell(Spell::Silence))
    {
	DEBUG("affected spell turn: " << "silence");
	affectedSpellActivate(Spell::Silence);
    }

    if(isAffectedSpell(Spell::ScryRunes))
    {
	DEBUG("affected spell turn: " << "scry runes");
	affectedSpellActivate(Spell::ScryRunes);
    }

    // The casting turn must not consume the first blocked turn.
    if(isAffectedSpell(Spell::ManaFog) && !isCasted())
    {
	DEBUG("affected spell turn: " << "mana fog");
	affectedSpellActivate(Spell::ManaFog);
    }

    return dropStone;
}

void LocalPlayer::setMahjongChao(const Stone & dropStone, int index)
{
    setMahjongChao(dropStone, index, classicRuneGameRuleset());
}

void LocalPlayer::setMahjongChao(const Stone & dropStone, int index,
                                  const RuneGameRuleset & ruleset)
{
    DEBUG(toString() << ", " << "stones: " << stones.toString() << ", " << "drop stone: " << dropStone.id() <<
	    ", " << "variant: " << index);

    Stones variants = stones.findChaoVariants(dropStone);

    // AI select variants
    if(variants.size() < index)
    {
        if(1 < variants.size())
            index = GameplayRng::uniform(0, variants.size() - 1);
        else
        if(variants.size())
            index = 0;
    }

    if(0 <= index && index < variants.size())
    {
	WinRule winRule(WinRule::Chao, variants[index], false);
	stones.add(dropStone);
	stones.remove(winRule);
	rules.push_back(winRule);
	points += ruleset.spellPointAward(RuneGameSpellPointEvent::Chao);
    }
    else
        ERROR("index out of range");

}

void LocalPlayer::setMahjongPung(const Stone & dropStone)
{
    setMahjongPung(dropStone, classicRuneGameRuleset());
}

void LocalPlayer::setMahjongPung(const Stone & dropStone,
                                  const RuneGameRuleset & ruleset)
{
    DEBUG(toString() << ", " << "stones: " << stones.toString() << ", " << "drop stone: " << dropStone.id());

    auto it = std::find(stones.begin(), stones.end(), dropStone);

    if(it != stones.end())
    {
        WinRule winRule(WinRule::Pung, dropStone, false);
        stones.add(dropStone);
        stones.remove(winRule);
        rules.push_back(winRule);
        points += ruleset.spellPointAward(RuneGameSpellPointEvent::Pung);
    }
    else
        ERROR("stone not found: " << dropStone.id());
}

void LocalPlayer::setMahjongKong1(const Stone & dropStone)
{
    setMahjongKong1(dropStone, classicRuneGameRuleset());
}

void LocalPlayer::setMahjongKong1(const Stone & dropStone,
                                   const RuneGameRuleset & ruleset)
{
    DEBUG(toString() << ", " << "stones: " << stones.toString() << ", " << "drop stone: " << dropStone.id());

    auto it = std::find(stones.begin(), stones.end(), dropStone);

    if(it != stones.end())
    {
        WinRule winRule(WinRule::Kong, dropStone, false);
        stones.add(dropStone);
        stones.remove(winRule);
        rules.push_back(winRule);
        points += ruleset.spellPointAward(RuneGameSpellPointEvent::Kong);
    }
    else
        ERROR("stone not found: " << dropStone.id());
}

void LocalPlayer::setMahjongGame(const WinResults & winResult)
{
    setMahjongGame(winResult, classicRuneGameRuleset());
}

void LocalPlayer::setMahjongGame(const WinResults & winResult,
                                  const RuneGameRuleset & ruleset)
{
    WinRules rules2 = winResult.winRulesConcealed();

    DEBUG(toString() << ", " << "stones: " << stones.toString() << ", " <<
	    ", " << "rules: " << rules.toString() << ", " << "win stone: " << winResult.lastStone.toString() <<
	    ", " << "win rules: " << rules2.toString());

    for(auto it = rules2.begin(); it != rules2.end(); ++it)
    {
	switch((*it).rule())
	{
	    case WinRule::Chao:
                points += ruleset.spellPointAward(RuneGameSpellPointEvent::Chao);
                break;
	    case WinRule::Pung:
                points += ruleset.spellPointAward(RuneGameSpellPointEvent::Pung);
                break;
	    case WinRule::Kong:
                points += ruleset.spellPointAward(RuneGameSpellPointEvent::Kong);
                break;
	    default: break;
	}
    }

    points += ruleset.spellPointAward(RuneGameSpellPointEvent::Win);
}

void LocalPlayer::setMahjongKong2(void)
{
    setMahjongKong2(classicRuneGameRuleset());
}

void LocalPlayer::setMahjongKong2(const RuneGameRuleset & ruleset)
{
    DEBUG(toString() << ", " << "stones: " << stones.toString() << ", " << "new stone: " << newStone.id());

    if(3 == stones.countStone(newStone))
    {
        WinRule winRule(WinRule::Kong, newStone, true);
        stones.add(newStone);
        newStone = GameStone(Stone::None, true);
        stones.remove(winRule);
        rules.push_back(winRule);
        points += ruleset.spellPointAward(RuneGameSpellPointEvent::Kong);
    }
    else
    {
	auto it = std::find_if(rules.begin(), rules.end(),
		[&](const WinRule & rule){ return rule.isPungStone(newStone); });

	if(it != rules.end())
	{
	    (*it).upgradeKong();
	    newStone = GameStone(Stone::None, true);
	    points += ruleset.spellPointAward(RuneGameSpellPointEvent::Kong);
	}
	else
	{
	    ERROR("stone not found: " << newStone.id());
	}
    }
}

/* LocalPlayers */
void LocalPlayers::setPersons(const Persons & persons)
{
    clear();

    for(auto & pers : persons)
	emplace_back(pers);
}

void LocalPlayers::distributeStones(CroupierSet & croupier)
{
    distributeStones(croupier, classicRuneGameRuleset());
}

void LocalPlayers::distributeStones(CroupierSet & croupier, const RuneGameRuleset & ruleset)
{
    for(int ii = 0; ii < ruleset.initialHandSize(); ++ii)
    {
        for(auto & player : *this)
	    player.stones.add(GameStone(croupier.get(player), false));
    }
}

LocalPlayer* LocalPlayers::playerOfClan(const Clan & clan)
{
    auto it = std::find_if(begin(), end(), [&](const LocalPlayer & lp){ return lp.isClan(clan); });
    return it != end() ? & (*it) : nullptr;
}

const LocalPlayer* LocalPlayers::playerOfClan(const Clan & clan) const
{
    auto it = std::find_if(begin(), end(), [&](const LocalPlayer & lp){ return lp.isClan(clan); });
    return it != end() ? & (*it) : nullptr;
}

LocalPlayer* LocalPlayers::playerOfWind(const Wind & wind)
{
    auto it = std::find_if(begin(), end(), [&](const LocalPlayer & lp){ return lp.isWind(wind); });
    return it != end() ? & (*it) : nullptr;
}

const LocalPlayer* LocalPlayers::playerOfWind(const Wind & wind) const
{
    auto it = std::find_if(begin(), end(), [&](const LocalPlayer & lp){ return lp.isWind(wind); });
    return it != end() ? & (*it) : nullptr;
}

LocalPlayer* LocalPlayers::playerOfAvatar(const Avatar & ava)
{
    auto it = std::find_if(begin(), end(), [&](const LocalPlayer & lp){ return lp.isAvatar(ava); });
    return it != end() ? & (*it) : nullptr;
}

const LocalPlayer* LocalPlayers::playerOfAvatar(const Avatar & ava) const
{
    auto it = std::find_if(begin(), end(), [&](const LocalPlayer & lp){ return lp.isAvatar(ava); });
    return it != end() ? & (*it) : nullptr;
}

void LocalPlayers::shiftWinds(void)
{
    for(auto & lp : *this)
	lp.shiftWind();
}

bool LocalPlayers::findKongs(void) const
{
    return std::any_of(begin(), end(), [](const LocalPlayer & lp){ return lp.haveKong(); });
}

JsonArray LocalPlayers::toJsonArray(void) const
{
    JsonArray ja;
    for(auto & lp : *this)
	ja.addObject(lp.toJsonObject());
    return ja;
}

LocalPlayers LocalPlayers::fromJsonArray(const JsonArray & ja)
{
    LocalPlayers res;
    for(int it = 0; it < ja.size(); ++it)
    {
	const JsonObject* jo = ja.getObject(it);
	if(jo) res.push_back(LocalPlayer::fromJsonObject(*jo));
    }
    return res;
}
