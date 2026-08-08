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

#include <cstring>
#include <sstream>
#include <algorithm>
#include <set>

#include "adventurecommands.h"
#include "aiprofile.h"
#include "aiturn.h"
#include "actions.h"
#include "battle.h"
#include "battlesession.h"
#include "crashreport.h"
#include "gamedata.h"
#include "matchtopology.h"
#include "replay.h"
#include "runegameruleset.h"

std::string SpellInfo::effectDescription(void) const
{
    switch(id())
    {
	case Spell::LightningBolt:
	case Spell::MassPanic:
	case Spell::HellBlast:		return StringFormat(_("-%1 Loyalty")).arg(std::abs(effect.loyalty));
	case Spell::DemonicCompulsion:	return StringFormat(_("+%1 Loyalty")).arg(std::abs(effect.loyalty));

	case Spell::BattleFury:		return StringFormat(_("+%1 Attack")).arg(std::abs(effect.attack));
	case Spell::DustCloud:		return StringFormat(_("-%1 Attack")).arg(std::abs(effect.attack));

	case Spell::Guidance:		return StringFormat(_("+%1 Missile")).arg(std::abs(effect.ranger));
	case Spell::Smoke:		return StringFormat(_("-%1 Missile")).arg(std::abs(effect.ranger));

	case Spell::MagicalAura:	return StringFormat(_("+%1 Defense")).arg(std::abs(effect.defense));
	case Spell::BrilliantLights:	return StringFormat(_("-%1 Defense")).arg(std::abs(effect.defense));

	case Spell::Heroism:		return StringFormat(_("+%1 Attack, +%2 Max Loyalty")).arg(std::abs(effect.attack)).arg(std::abs(effect.loyalty));
	case Spell::MysticalFountain:	return StringFormat(_("+%1 Attack or +%2 Missile or +%3 Max Loyalty")).arg(std::abs(effect.attack)).arg(std::abs(effect.ranger)).arg(std::abs(effect.loyalty));
	case Spell::BlindAmbition:	return StringFormat(_("+%1 Attack, +%2 Missile, -%3 Max Loyalty")).arg(std::abs(effect.attack)).arg(std::abs(effect.ranger)).arg(std::abs(effect.loyalty));
	case Spell::Healing:		return StringFormat(_("+%1 Loyalty, up to max")).arg(std::abs(effect.loyalty));
	case Spell::Reduction:		return StringFormat(_("-%1 Defense, -%2 Attack")).arg(std::abs(effect.defense)).arg(std::abs(effect.attack));
	case Spell::ForceShield:	return StringFormat(_("Missile attacks do %1 less damage")).arg(value);

	case Spell::Paralyze:		return _("Freeze creature");
	case Spell::Teleport:		return _("Move one unit to any friendly territory");
	case Spell::DispelMagic:
	case Spell::MassDispel:		return _("Remove enchantments");

	default:		break;
    }

    return name;
}

std::string AvatarInfo::toStringClans(void) const
{
    StringList res;

    for(auto & id : clans)
    {
	const ClanInfo & clan = GameData::clanInfo(id);
	res << clan.name;
    }

    return res.join(", ");
}

namespace GameData
{
    std::vector<StoneInfo>		stonesInfo;
    std::vector<WindInfo>		windsInfo;
    std::vector<ClanInfo>		clansInfo;
    std::vector<CreatureInfo>		creaturesInfo;
    std::vector<SpellInfo>		spellsInfo;
    std::vector<SpecialityInfo>		specialsInfo;
    std::vector<AbilityInfo>		abilitiesInfo;
    std::vector<AvatarInfo>		avatarsInfo;
    std::vector<LandInfo>		landsInfo;
    std::vector<Clan>			initialLandOwners;

    int					bonusStart;
    int					bonusGame;
    int					bonusKong;
    int					bonusPung;
    int					bonusChao;
    int					bonusPass;

    template<typename T>
    bool loadJson(const char* json, std::vector<T> & v)
    {
	JsonContent jc = GameTheme::jsonResource(json);
	if(jc.isArray())
	{
	    auto list = jc.toArray().toStdList<T>();
	    v.resize(list.size() + 1); // nul reserve

	    for(auto & val : list)
		v[val.id.index()] = val;

	    return true;
	}

	ERROR("incorrect json array: " << json);
	return false;
    }

    bool 		loadIndexes(const JsonObject &);

    LocalPlayer &	playerOfClan(const Clan &);
    LocalPlayer &	playerOfAvatar(const Avatar &);
    LocalPlayer &	playerOfWind(const Wind &);
}

bool GameData::init(const JsonObject & jo)
{
    if(! loadIndexes(jo))
	return false;

    bonusStart = jo.getInteger("bonus:start", 250);
    bonusGame = jo.getInteger("bonus:game", 50);
    bonusPass = jo.getInteger("bonus:pass", 10);
    bonusChao = jo.getInteger("bonus:chao", 20);
    bonusPung = jo.getInteger("bonus:pung", 30);
    bonusKong = jo.getInteger("bonus:kong", 40);

    // load game jsons
    if(! loadJson<StoneInfo>("stones.json", stonesInfo))
	return false;

    if(! loadJson<WindInfo>("winds.json", windsInfo))
	return false;

    if(! loadJson<ClanInfo>("clans.json", clansInfo))
	return false;

    if(! loadJson<CreatureInfo>("creatures.json", creaturesInfo))
	return false;

    if(! loadJson<SpellInfo>("spells.json", spellsInfo))
	return false;

    if(! loadJson<SpecialityInfo>("specials.json", specialsInfo))
	return false;

    if(! loadJson<AbilityInfo>("abilities.json", abilitiesInfo))
	return false;

    if(! loadJson<AvatarInfo>("avatars.json", avatarsInfo))
	return false;

    if(! loadJson<LandInfo>("lands.json", landsInfo))
	return false;

    retranslateThemeData();

    initialLandOwners.resize(landsInfo.size());
    for(std::size_t index = 0; index < landsInfo.size(); ++index)
        initialLandOwners[index] = landsInfo[index].clan;

    // check path valid
    for(auto & info1 : landsInfo)
    {
        const Land & land1 = info1.id;
        for(auto & land2 : info1.borders)
        {
            const LandInfo & info2 = landInfo(land2);

            if(std::none_of(info2.borders.begin(), info2.borders.end(), [&](const Land & land){ return land == land1; }))
                ERROR("land path error: " << land1.toString() << "<->" << land2.toString());
        }
    }

    return true;
}

const LandInfo & GameData::landInfo(const Land & landId)
{
    return landsInfo[landId()];
}

const AvatarInfo & GameData::avatarInfo(const Avatar & avatarId)
{
    return avatarsInfo[avatarId()];
}

const AbilityInfo & GameData::abilityInfo(const Ability & abilityId)
{
    return abilitiesInfo[abilityId()];
}

const SpecialityInfo & GameData::specialityInfo(const Speciality & speciality)
{
    return specialsInfo[speciality.index()];
}

const CreatureInfo & GameData::creatureInfo(const Creature & creatureId)
{
    return creaturesInfo[creatureId()];
}

const SpellInfo & GameData::spellInfo(const Spell & spellId)
{
    return spellsInfo[spellId()];
}

const StoneInfo & GameData::stoneInfo(const Stone & stone)
{
    return stonesInfo[stone.index()];
}

const WindInfo & GameData::windInfo(const Wind & windId)
{
    return windsInfo[windId()];
}

const ClanInfo & GameData::clanInfo(const Clan & clanId)
{
    return clansInfo[clanId()];
}

Avatars GameData::avatarsOfClan(const Clan & clanId)
{
    Avatars res;

    for(auto id : avatars_all)
    {
	const AvatarInfo & info = GameData::avatarInfo(id);
	if(std::any_of(info.clans.begin(), info.clans.end(), [&](const Clan & id){ return id == clanId; }))
	    res.emplace_back(id);
    }

    return res;
}

const RemotePlayer & LocalData::playerCurrentWind(void) const
{
    return playerOfWind(currentWind);
}

LocalPlayer & LocalData::myPlayer(void)
{
    return players[3];
}

const LocalPlayer & LocalData::myPlayer(void) const
{
    return players[3];
}

const RemotePlayer & LocalData::playerOfWind(const Wind & wind) const
{
    auto it = std::find_if(players.begin(), players.end(),
			    [&](const Person & pers){ return pers.isWind(wind); });

    if(it == players.end())
    {
	ERROR("player not found" << ", " << "wind: " << wind.toString());
	Engine::except(__FUNCTION__, "exit");
    }

    return *it;
}

const RemotePlayer & LocalData::playerOfClan(const Clan & clan) const
{
    auto it = std::find_if(players.begin(), players.end(),
			    [&](const Person & pers){ return pers.isClan(clan); });

    if(it == players.end())
    {
	ERROR("player not found" << ", " << "clan: " << clan.canonicalName());
	Engine::except(__FUNCTION__, "exit");
    }

    return *it;
}

const RemotePlayer & LocalData::playerOfAvatar(const Avatar & avatar) const
{
    auto it = std::find_if(players.begin(), players.end(),
			    [&](const Person & pers){ return pers.isAvatar(avatar); });

    if(it == players.end())
    {
	ERROR("player not found" << ", " << "avatar: " << avatar.toString());
	Engine::except(__FUNCTION__, "exit");
    }

    return *it;
}

const BattleCreature* LocalData::findBattleUnitConst(int unit) const
{
    if(0 < unit)
    {
	for(const auto & player : players)
	{
	    const BattleCreature* creature = player.army.findBattleUnitConst(unit);
	    if(creature) return creature;
	}
    }

    return nullptr;
}

Persons LocalData::toPersons(void) const
{
    Persons res;
    res.assign(players.begin(), players.end());
    return res;
}

namespace GameData
{
    // remote data
    Person				person;
    LocalPlayers			gamers;
    Wind				currentWind;
    Wind				roundWind;
    Wind				partWind;
    CroupierSet				croupier;
    int					stoneLastCount = 0;
    Stone				dropStone;
    WinResults				winResult;
    std::list<BattleLegend>		battleHistory;
    bool				skipRepeatSay = false;
    bool				skipNewStone = false;
    bool				skipNewTurn = false;
    int					gamePart = 0;
    int					battleUnitId = 1;
    AI::Difficulty                      difficulty = AI::Difficulty::Normal;
    bool                                assistedByDeveloper = false;
#ifdef BUILD_DEBUG
    Avatar                              developerAutoplayAvatar;
#endif
    JsonObject				stateGUI;

    void grantAiDifficultyIncome(bool initial)
    {
        const AI::DifficultyRules & rules = AI::difficultyRules(difficulty);
        const int spellPoints = initial ? rules.initialAiSpellPointBonus :
                                          rules.mahjongPartAiSpellPointBonus;

        for(LocalPlayer & player : gamers)
        {
            // Developer autoplay still represents the human seat. Only actual AI
            // opponents receive the deliberately asymmetric Unfair economy.
            if(!usesAI(player)) continue;

            player.points += spellPoints;
            if(!initial && rules.mahjongPartAiLandClaimBonus > 0)
            {
                for(const auto clanId : clans_all)
                {
                    const Clan clan(clanId);
                    if(!allied(clan, player.clan))
                        player.addLandClaimPoints(clan, rules.mahjongPartAiLandClaimBonus);
                }
            }
        }
    }

    Wind                                prevWindCompass(const Wind &);
    Wind                                nextWindCompass(const Wind &);
    Wind                                oppositeWindCompass(const Wind &);

    void				dumpOrderPersons(void);
    void				validateMahjongSummary(void);

}

int GameData::nextBattleUnitId(void)
{
    return battleUnitId++;
}

bool GameData::isGameOver(void)
{
    return isGameOver(activeRuneGameRuleset());
}

bool GameData::isGameOver(const RuneGameRuleset & ruleset)
{
    return ruleset.advanceRound(roundWind(), partWind()).complete;
}

std::list<BattleLegend> GameData::getBattleHistoryFor(const Avatar & avatar)
{
    std::list<BattleLegend> res;

    for(auto & legend : battleHistory)
	if(legend.attacker == avatar) res.push_back(legend);

    return res;
}

const JsonObject & GameData::jsonGUI(void)
{
    return stateGUI;
}

void GameData::setGamePart(int v)
{
    gamePart = v;

    if(v == Menu::AdventurePart)
    {
	for(auto & player : gamers)
            player.initAdventurePart();
    }
}

int GameData::loadedGamePart(void)
{
    return gamePart;
}

const Person & GameData::myPerson(void)
{
    return person;
}

const Person & GameData::currentPerson(void)
{
    return playerOfWind(currentWind);
}

const LocalPlayers & GameData::players(void)
{
    return gamers;
}

Avatar GameData::localMahjongAvatar(void)
{
    const LocalPlayer & current = playerOfWind(currentWind);

    // During an owned turn the controller must see and act with that hand,
    // regardless of which of its two Duel seats was selected initially.
    if(!dropStone.isValid() && isLocallyControlled(current) && !usesAI(current))
        return current.avatar;

    // Once a rune has been discarded, select the controller-owned opponent
    // that can answer it.  There is only one such opponent in Duel, but the
    // ordered scan also makes this deterministic for future topologies.
    if(dropStone.isValid())
    {
        const RuneGameRuleset & ruleset = activeRuneGameRuleset();
        const auto claimPriority = [&](const LocalPlayer & player) {
            WinResults result;
            if(player.isWinMahjong(currentWind, roundWind, dropStone, &result, ruleset)) return 4;
            if(player.isMahjongKong1(currentWind, dropStone, ruleset)) return 3;
            if(player.isMahjongPung(currentWind, dropStone, ruleset)) return 2;
            if(player.isMahjongChao(currentWind, dropStone, ruleset)) return 1;
            return 0;
        };

        const LocalPlayer* selected = nullptr;
        int selectedPriority = -1;
        for(const LocalPlayer & player : gamers)
        {
            if(player.wind == currentWind || !isLocallyControlled(player) || usesAI(player))
                continue;
            const int priority = claimPriority(player);
            if(selectedPriority < priority)
            {
                selected = &player;
                selectedPriority = priority;
            }
        }
        if(selected) return selected->avatar;
    }

    return person.avatar;
}

Avatar GameData::localAdventureAvatar(void)
{
    const LocalPlayer & current = playerOfWind(currentWind);
    return isLocallyControlled(current) && !usesAI(current) ? current.avatar : person.avatar;
}

AI::Difficulty GameData::aiDifficulty(void)
{
    return difficulty;
}

void GameData::setAIDifficulty(AI::Difficulty value)
{
    difficulty = value;
}

bool GameData::usesAI(const Person & player)
{
    if(isLocallyControlled(player))
    {
#ifdef BUILD_DEBUG
        return developerAutoplayAvatar.isValid() && player.avatar == developerAutoplayAvatar;
#else
        return false;
#endif
    }
#ifdef BUILD_DEBUG
    return player.isAI() || (developerAutoplayAvatar.isValid() &&
                             player.avatar == developerAutoplayAvatar);
#else
    return player.isAI();
#endif
}

int GameData::localControllerId(void)
{
    // Headless/simulation games intentionally store an AI seat as the local
    // reference person.  It must not turn that controller (and, in Duel, its
    // partner hand) into a human-controlled seat.
    return !person.isAI() && person.clan.isValid() ?
        activeMatchTopology().controllerForClan(person.clan()) : -1;
}

bool GameData::isLocallyControlled(const Person & player)
{
    const int local = localControllerId();
    return 0 <= local && player.clan.isValid() &&
           activeMatchTopology().controllerForClan(player.clan()) == local;
}

bool GameData::allied(const Person & first, const Person & second)
{
    return first.clan.isValid() && second.clan.isValid() &&
           activeMatchTopology().alliedByClan(first.clan(), second.clan());
}

bool GameData::allied(const Clan & first, const Clan & second)
{
    if(!first.isValid() || !second.isValid()) return false;
    if(first == second) return true;

    const LocalPlayer* firstPlayer = gamers.playerOfClan(first);
    const LocalPlayer* secondPlayer = gamers.playerOfClan(second);
    return firstPlayer && secondPlayer && allied(*firstPlayer, *secondPlayer);
}

bool GameData::developerAssisted(void)
{
    return assistedByDeveloper;
}

#ifdef BUILD_DEBUG
bool GameData::developerAutoplay(const Avatar & avatar)
{
    return developerAutoplayAvatar.isValid() && developerAutoplayAvatar == avatar;
}

void GameData::setDeveloperAutoplay(const Avatar & avatar, bool enabled)
{
    developerAutoplayAvatar = enabled ? avatar : Avatar();
    if(enabled) assistedByDeveloper = true;
    CrashReport::breadcrumb(std::string("Developer autoplay avatar=")
        .append(enabled ? avatar.toString() : "none"));
}
#endif

void GameData::retranslateThemeData(void)
{
    const auto translated = [](const std::string & source) {
        return source.empty() ? source : std::string(_(source));
    };

    for(auto & info : stonesInfo)
        info.name = translated(info.sourceName);
    for(auto & info : windsInfo)
        info.name = translated(info.sourceName);
    for(auto & info : clansInfo)
        info.name = translated(info.sourceName);
    for(auto & info : creaturesInfo)
    {
        info.name = translated(info.sourceName);
        info.description = translated(info.sourceDescription);
    }
    for(auto & info : spellsInfo)
    {
        info.name = translated(info.sourceName);
        info.description = translated(info.sourceDescription);
    }
    for(auto & info : specialsInfo)
    {
        info.name = translated(info.sourceName);
        info.description = translated(info.sourceDescription);
    }
    for(auto & info : abilitiesInfo)
    {
        info.name = translated(info.sourceName);
        info.description = translated(info.sourceDescription);
    }
    for(auto & info : avatarsInfo)
    {
        info.name = translated(info.sourceName);
        info.dignity = translated(info.sourceDignity);
        info.description = translated(info.sourceDescription);
    }
    for(auto & info : landsInfo)
        info.name = translated(info.sourceName);
}

LocalData GameData::toLocalData(const Avatar & ava)
{
    LocalPlayer* lp = gamers.playerOfAvatar(ava);

    if(! lp)
    {
        ERROR("player not found" << ", " << "avatar: " << ava.toString());
        Engine::except(__FUNCTION__, "exit");
    }

    const Wind & wind = lp->wind;
    const AvatarInfo & avaInfo = avatarInfo(ava);

    LocalData ld;
    ld.trashSet = croupier.trash;

    ld.roundWind = roundWind;
    ld.partWind = partWind;
    ld.currentWind = currentWind;
    ld.compass = WindCompass(wind);
    ld.dropStone = dropStone;
    ld.stoneLastCount = stoneLastCount;
    ld.winResult = winResult;


    lp = gamers.playerOfWind(ld.compass.left());
    if(lp) ld.players[0] = *lp;
    else ERROR("player not found" << ", wind: " << ld.compass.left().toString());

    lp = gamers.playerOfWind(ld.compass.right());
    if(lp) ld.players[1] = *lp;
    else ERROR("player not found" << ", wind: " << ld.compass.right().toString());

    lp = gamers.playerOfWind(ld.compass.top());
    if(lp) ld.players[2] = *lp;
    else ERROR("player not found" << ", wind: " << ld.compass.top().toString());

    lp = gamers.playerOfWind(ld.compass.bottom());
    if(lp) ld.players[3] = *lp;
    else ERROR("player not found" << ", wind: " << ld.compass.bottom().toString());

    // Only the caster can see the hand marked by Scry Runes.
    for(int it = 0; it < 3; ++it)
    {
	if(! ld.players[it].isAffectedSpell(Spell::ScryRunes, ava))
	{
	    // hide: private game info
	    ld.players[it].stones.clear();
	    ld.players[it].newStone.reset();
	}
    }

    // check: Ability Monocle
    if(avaInfo.ability() != Ability::Monacle)
    {
	ld.players[0].army.applyInvisibility(ld.players[3].clan);
	ld.players[1].army.applyInvisibility(ld.players[3].clan);
	ld.players[2].army.applyInvisibility(ld.players[3].clan);
    }

    return ld;
}

BattleArmy & GameData::getBattleArmy(const Clan & clan)
{
    return playerOfClan(clan).army;
}

bool GameData::findCreatureUnique(const Creature & cr)
{
    for(auto & player : gamers)
	if(player.army.findCreature(cr)) return true;

    return false;
}

BattleParty* GameData::getBattleParty(int unit)
{
    if(0 < unit)
    {
	for(auto & player : gamers)
	{
	    for(auto & party : player.army)
		if(party.findBattleUnitConst(unit)) return & party;
	}
    }

    ERROR("battle unit not found" << ", " << "id: " << String::hex(unit));
    Engine::except(__FUNCTION__, "exit");

    return nullptr;
}

BattleCreature* GameData::findBattleCreature(int unit)
{
    if(0 < unit)
    {
	for(auto & player : gamers)
	{
	    auto bcr = player.army.findBattleUnit(unit);
	    if(bcr) return bcr;
	}
    }

    return nullptr;
}

BattleCreature* GameData::getBattleCreature(int unit)
{
    BattleCreature* creature = findBattleCreature(unit);
    if(creature) return creature;

    ERROR("battle unit not found" << ", " << "id: " << String::hex(unit));
    Engine::except(__FUNCTION__, "exit");

    return nullptr;
}

RemotePlayer* GameData::getBattleArmyOwner(const BattleArmy & army)
{
    for(auto & player : gamers)
	if(& player.army == & army) return & player;

    ERROR("battle army not found");
    Engine::except(__FUNCTION__, "exit");

    return nullptr;
}

Wind GameData::oppositeWindCompass(const Wind & wind)
{
    return wind.isValid() ? WindCompass(wind).top() : Wind(Wind::South);
}

Wind GameData::nextWindCompass(const Wind & wind)
{
    return wind.isValid() ? WindCompass(wind).right() : Wind(Wind::East);
}

Wind GameData::prevWindCompass(const Wind & wind)
{
    return wind.isValid() ? WindCompass(wind).left() : Wind(Wind::North);
}

void GameData::initPersons(const Person & cur)
{
    Replay::clearActionJournal();
    Persons persons(cur);
    gamers.setPersons(persons);
    const LocalPlayer* selected = gamers.playerOfAvatar(cur.avatar);
    person = selected ? static_cast<const Person &>(*selected) : cur;
    grantAiDifficultyIncome(true);
    roundWind = Wind(Wind::None);
    partWind = Wind(Wind::None);
    currentWind = Wind(Wind::None);
    pendingBattle = PendingBattle();
    assistedByDeveloper = false;
#ifdef BUILD_DEBUG
    developerAutoplayAvatar = Avatar();
#endif
}

bool GameData::initPersons(const Persons & configured)
{
    if(configured.size() != winds_all.size()) return false;

    std::set<int> avatars;
    std::set<int> clans;
    std::set<int> winds;
    Persons persons;

    for(const Person & configuredPerson : configured)
    {
        if(!configuredPerson.avatar.isValid() || configuredPerson.avatar == Avatar(Avatar::Random) ||
           !configuredPerson.clan.isValid() || !configuredPerson.wind.isValid() ||
           !avatars.insert(configuredPerson.avatar()).second ||
           !clans.insert(configuredPerson.clan()).second ||
           !winds.insert(configuredPerson.wind()).second)
            return false;

        const AvatarInfo & info = avatarInfo(configuredPerson.avatar);
        if(std::find(info.clans.begin(), info.clans.end(), configuredPerson.clan) == info.clans.end())
            return false;

        Person clean(configuredPerson.avatar, configuredPerson.clan, configuredPerson.wind);
        clean.setAI(configuredPerson.isAI());
        persons.push_back(clean);
    }

    Replay::clearActionJournal();
    gamers.setPersons(persons);
    person = persons.front();
    grantAiDifficultyIncome(true);
    roundWind = Wind(Wind::None);
    partWind = Wind(Wind::None);
    currentWind = Wind(Wind::None);
    stoneLastCount = 0;
    dropStone = Stone(Stone::None);
    winResult = WinResults();
    battleHistory.clear();
    skipRepeatSay = false;
    skipNewStone = false;
    skipNewTurn = false;
    gamePart = 0;
    battleUnitId = 1;
    assistedByDeveloper = false;
#ifdef BUILD_DEBUG
    developerAutoplayAvatar = Avatar();
#endif
    stateGUI.clear();
    resetAdventureCommandState();
    pendingBattle = PendingBattle();

    if(initialLandOwners.size() == landsInfo.size())
    {
        for(std::size_t index = 0; index < landsInfo.size(); ++index)
            landsInfo[index].clan = initialLandOwners[index];
    }

    return true;
}

bool GameData::initMahjong(void)
{
    return initMahjong(activeRuneGameRuleset());
}

bool GameData::initMahjong(const RuneGameRuleset & ruleset)
{
    pendingBattle = PendingBattle();
    do
    {
	for(auto & lp : gamers)
	    lp.initMahjongPart();

	croupier.reset(ruleset);
	gamers.distributeStones(croupier, ruleset);
    }
    // fix kong startup
    while(gamers.findKongs());

    stoneLastCount = GAME_STONE_MAX;
    skipRepeatSay = false;
    gamePart = Menu::MahjongPart;
    skipNewStone = false;
    skipNewTurn = false;
    stateGUI.clear();

    const RuneGameRoundAdvance advance = ruleset.advanceRound(roundWind(), partWind());
    if(advance.complete)
        return false;

    if(advance.rotatePlayerWinds)
        gamers.shiftWinds();

    roundWind = Wind(static_cast<Wind::wind_t>(advance.roundWindId));
    partWind = Wind(static_cast<Wind::wind_t>(advance.partWindId));
    currentWind = Wind(static_cast<Wind::wind_t>(ruleset.firstTurnWindId()));
    dropStone = Stone(Stone::None);
    winResult = WinResults();

    battleHistory.clear();

    grantAiDifficultyIncome(false);

    VERBOSE("wind round: " << roundWind.toString());
    VERBOSE("wind part: " << partWind.toString());

    dumpOrderPersons();
    return true;
}

void GameData::dumpOrderPersons(void)
{
    DEBUG("players: ");
    for(auto & id : winds_all)
    {
	LocalPlayer & player = playerOfWind(id);

	DEBUG("wind: " << player.wind.toString() << ", " << "player: " << player.name() << ", " <<
		(! player.isAI() ? " (*)" : "") << ", " << "clan: " << player.clan.canonicalName() << ", " <<
		"stones: " <<  player.stones.toString() << ", " << "new stone: " << player.newStone.toString());
    }
}

LocalPlayer & GameData::playerOfAvatar(const Avatar & avatar)
{
    LocalPlayer* res = gamers.playerOfAvatar(avatar);

    if(! res)
    {
	ERROR("unknown avatar id: " << avatar());
	Engine::except(__FUNCTION__, "exit");
    }

    return *res;
}

LocalPlayer & GameData::playerOfClan(const Clan & clan)
{
    LocalPlayer* res = gamers.playerOfClan(clan);

    if(! res)
    {
	ERROR("unknown clan id: " << clan());
	Engine::except(__FUNCTION__, "exit");
    }

    return *res;
}

LocalPlayer & GameData::playerOfWind(const Wind & wind)
{
    LocalPlayer* res = gamers.playerOfWind(wind);

    if(! res)
    {
	ERROR("unknown wind: " << wind());
	Engine::except(__FUNCTION__, "exit");
    }

    return *res;
}

bool GameData::mahjong2Client(const Avatar & avatar, ActionList & actions)
{
    LocalPlayer & current = playerOfWind(currentWind);
    const RuneGameRuleset & ruleset = activeRuneGameRuleset();
    const auto runAutomatedTurn = [&](bool showGame, bool showKong)
    {
	const WinRules & left = playerOfWind(prevWindCompass(currentWind)).rules;
	const WinRules & right = playerOfWind(nextWindCompass(currentWind)).rules;
	const WinRules & top = playerOfWind(oppositeWindCompass(currentWind)).rules;
	return AI::mahjongTurn(currentWind, current.avatar, croupier.trash,
	                       left, right, top, showGame, showKong, actions);
    };

    if(croupier.hasLuckDraw())
    {
	if(GameData::usesAI(current))
	{
	    WinRules other;
	    const WinRules & left = playerOfWind(prevWindCompass(currentWind)).rules;
	    const WinRules & right = playerOfWind(nextWindCompass(currentWind)).rules;
	    const WinRules & top = playerOfWind(oppositeWindCompass(currentWind)).rules;
	    other.reserve(left.size() + right.size() + top.size());
	    other.insert(other.end(), left.begin(), left.end());
	    other.insert(other.end(), right.begin(), right.end());
	    other.insert(other.end(), top.begin(), top.end());

	    const AI::StrategicIntent intent = AI::chooseStrategicIntent(
	        AI::observePlayer(current.avatar), AI::behaviorProfile(current), aiDifficulty());
	    const int selected = AI::mahjongLuckChoice(current.stones, croupier.luckChoices(),
	                                                  croupier.trash, other, intent);
	    current.newStone = GameStone(croupier.resolveLuckDraw(selected), false);
	    if(0 < stoneLastCount) --stoneLastCount;
	    return runAutomatedTurn(
	        current.isWinMahjong(currentWind, roundWind, dropStone, &winResult, ruleset),
	        current.isMahjongKong2(currentWind, ruleset));
	}

	if(current.avatar == avatar)
	{
	    actions.push_back(MahjongLuckChoice(currentWind, croupier.luckChoices()));
	    return true;
	}
	return false;
    }

    if(current.newStone.isValid() || skipNewTurn)
    {
	if(GameData::usesAI(current))
	    return runAutomatedTurn(
	        current.isWinMahjong(currentWind, roundWind, dropStone, &winResult, ruleset),
	        current.isMahjongKong2(currentWind, ruleset));

	//DEBUG("wind: " << currentWind.toString() << ", " << "person: " << current.name() << ", " <<
	//	"new stone: " << current.newStone() << ", " << "wait action");
	return false;
    }
    else
    if(dropStone.isValid())
    {
	if(skipRepeatSay)
	{
	    const bool allAI = std::all_of(gamers.begin(), gamers.end(),
	        [](const LocalPlayer & player){ return GameData::usesAI(player); });
	    return allAI ? client2Mahjong(current.avatar, ClientButtonPass(), actions) : false;
	}

	skipRepeatSay = true;

	if(AI::mahjongGameKongPungChao(currentWind, roundWind, dropStone, winResult,
	                                 actions, true, ruleset))
	    return true;

	DEBUG("wait player pass" << ", " << "current: " << current.toString());
	return false;
    }

    DEBUG("new turn: " << "last count: " << stoneLastCount);

    if(0 == stoneLastCount)
    {
	winResult = WinResults::drawn(currentWind, roundWind);
	actions.push_back(MahjongEnd(currentWind));
	validateMahjongSummary();
	gamePart = Menu::MahjongSummaryPart;
	return true;
    }

    bool showGame2 = false;
    bool showKong2 = false;

    const bool luckDraw = current.newTurnEvent(croupier, skipNewStone);

    if(! skipNewStone)
    {
	if(luckDraw)
	{
	    if(!GameData::usesAI(current))
	    {
		actions.push_back(MahjongLuckChoice(currentWind, croupier.luckChoices()));
		return true;
	    }

	    WinRules other;
	    const WinRules & left = playerOfWind(prevWindCompass(currentWind)).rules;
	    const WinRules & right = playerOfWind(nextWindCompass(currentWind)).rules;
	    const WinRules & top = playerOfWind(oppositeWindCompass(currentWind)).rules;
	    other.reserve(left.size() + right.size() + top.size());
	    other.insert(other.end(), left.begin(), left.end());
	    other.insert(other.end(), right.begin(), right.end());
	    other.insert(other.end(), top.begin(), top.end());

	    const AI::StrategicIntent intent = AI::chooseStrategicIntent(
	        AI::observePlayer(current.avatar), AI::behaviorProfile(current), aiDifficulty());
	    const int selected = AI::mahjongLuckChoice(current.stones, croupier.luckChoices(),
	                                                  croupier.trash, other, intent);
	    current.newStone = GameStone(croupier.resolveLuckDraw(selected), false);
	}

	stoneLastCount--;

	showGame2 = current.isWinMahjong(currentWind, roundWind, dropStone, & winResult, ruleset);
	showKong2 = current.isMahjongKong2(currentWind, ruleset);
    }
    else
    {
	skipNewTurn = true;
    }

    if(GameData::usesAI(current))
    {
	runAutomatedTurn(showGame2, showKong2);
    }
    else
    {
	actions.push_back(MahjongTurn(currentWind, current.newStone, showKong2, showGame2));
	actions.push_back(MahjongData(currentWind));
    }

    return true;
}


void GameData::validateMahjongSummary(void)
{
    int total = 0;

    for(auto & id : winds_all)
    {
	const LocalPlayer & player = GameData::playerOfWind(id);

        DEBUG(player.toString() << ", " <<
                "stones: " <<  player.stones.toString() << ", " << "rules: " << player.rules.toString());

	total += player.stones.size();
	total += player.rules.count();

	if(player.newStone.isValid())
	{
	    DEBUG("new stone: " << player.newStone());
	    total += 1;
	}
    }

    if(dropStone.isValid())
    {
	DEBUG("drop stone: " << dropStone());
	total += 1;
    }

    DEBUG("croupier trash: " << croupier.trash.toString());
    DEBUG("croupier bank: " << croupier.bank.toString());
    total += croupier.trash.size() + croupier.bank.size();
    total += croupier.luckDraw.size();

    DEBUG("game total: " << total << ", " << "(" << (total != 136 ? "FALSE" : "TRUE") << ")");
}
