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
#include <set>

#include "actionvalidation.h"
#include "aiturn.h"
#include "crashreport.h"
#include "gamedata.h"
#include "replay.h"
#include "runegameruleset.h"

namespace GameData
{
    extern LocalPlayers gamers;
    extern Wind currentWind;
    extern Wind roundWind;
    extern Wind partWind;
    extern CroupierSet croupier;
    extern int stoneLastCount;
    extern Stone dropStone;
    extern WinResults winResult;
    extern bool skipRepeatSay;
    extern bool skipNewStone;
    extern bool skipNewTurn;
    extern int gamePart;

    LocalPlayer & playerOfAvatar(const Avatar &);
    LocalPlayer & playerOfWind(const Wind &);
    LocalPlayer & playerOfClan(const Clan &);
    void validateMahjongSummary(void);

    bool clientLuckChoice(const Avatar &, const ClientMessage &, ActionList &);
    bool clientReady(const Avatar &, const ClientMessage &, ActionList &);
    bool clientSayGame(const Avatar &, const ClientMessage &, ActionList &);
    bool clientSayChao(const Avatar &, const ClientMessage &, ActionList &);
    bool clientSayPung(const Avatar &, const ClientMessage &, ActionList &);
    bool clientSayKong(const Avatar &, const ClientMessage &, ActionList &);
    bool clientButtonGame(const Avatar &, const ClientMessage &, ActionList &);
    bool clientButtonPass(const Avatar &, const ClientMessage &, ActionList &);
    bool clientButtonPung(const Avatar &, const ClientMessage &, ActionList &);
    bool clientButtonKong1(const Avatar &, const ClientMessage &, ActionList &);
    bool clientButtonKong2(const Avatar &, const ClientMessage &, ActionList &);
    bool clientChaoVariant(const Avatar &, const ClientMessage &, ActionList &);
    bool clientDropIndex(const Avatar &, const ClientMessage &, ActionList &);
    bool clientSummonCreature(const Avatar &, const ClientMessage &, ActionList &);
    bool clientCastSpell(const Avatar &, const ClientMessage &, ActionList &);
}

bool GameData::clientLuckChoice(const Avatar & avatar, const ClientMessage & act, ActionList & actions)
{
    LocalPlayer & client = playerOfAvatar(avatar);
    if(client.wind != currentWind)
    {
	ERROR("invalid luck choice outside current turn: " << client.toString());
	return rejectAction(ActionRejectReason::WrongTurn);
    }

    if(GameData::usesAI(client) || client.newStone.isValid() || !croupier.hasLuckDraw())
    {
	ERROR("invalid luck choice: " << client.toString());
	return rejectAction(ActionRejectReason::StaleSelection);
    }

    const auto choice = static_cast<const ClientLuckChoice &>(act);
    const Stone selected = croupier.resolveLuckDraw(choice.index());
    if(!selected.isValid()) return rejectAction(ActionRejectReason::InvalidRuneChoice);

    client.newStone = GameStone(selected, false);
    if(0 < stoneLastCount) stoneLastCount--;

    const RuneGameRuleset & ruleset = activeRuneGameRuleset();
    const bool showGame = client.isWinMahjong(currentWind, roundWind, dropStone,
                                              &winResult, ruleset);
    const bool showKong = client.isMahjongKong2(currentWind, ruleset);
    actions.push_back(MahjongTurn(currentWind, client.newStone, showKong, showGame));
    actions.push_back(MahjongData(currentWind));
    return true;
}

bool GameData::clientReady(const Avatar & avatar, const ClientMessage & act, ActionList & actions)
{
    LocalPlayer & client = playerOfAvatar(avatar);
    DEBUG(client.toString());

    actions.push_back(MahjongBegin(currentWind, roundWind, partWind == Wind(Wind::East)));

    return true;
}

bool GameData::clientSayGame(const Avatar & avatar, const ClientMessage & act, ActionList & actions)
{
    LocalPlayer & client = playerOfAvatar(avatar);

    if(client.isSilenced())
    {
	ERROR("player silence mode: " << client.name());
	return rejectAction(ActionRejectReason::Silenced);
    }

    // need fill winResult
    if(client.isWinMahjong(currentWind, roundWind, dropStone, & winResult,
                           activeRuneGameRuleset()))
    {
	DEBUG(client.toString());

	actions.push_back(MahjongSayGame(client.wind));
	AI::mahjongOtherPass(currentWind, actions, client.wind);
	return true;
    }

    ERROR("isWinMahjong: false");
    return rejectAction(ActionRejectReason::IllegalMahjongCall);
}

bool GameData::clientSayChao(const Avatar & avatar, const ClientMessage & act, ActionList & actions)
{
    LocalPlayer & client = playerOfAvatar(avatar);

    DEBUG(client.toString());

    if(client.isSilenced())
    {
	ERROR("player silence mode: " << client.name());
	return rejectAction(ActionRejectReason::Silenced);
    }

    if(client.isMahjongChao(currentWind, dropStone, activeRuneGameRuleset()))
    {
	actions.push_back(MahjongSayChao(client.wind));
	AI::mahjongOtherPass(currentWind, actions, client.wind);
	return true;
    }

    ERROR("isMahjongChao: false");
    return rejectAction(ActionRejectReason::IllegalMahjongCall);
}

bool GameData::clientSayPung(const Avatar & avatar, const ClientMessage & act, ActionList & actions)
{
    LocalPlayer & client = playerOfAvatar(avatar);

    DEBUG(client.toString());

    if(client.isSilenced())
    {
	ERROR("player silence mode: " << client.name());
	return rejectAction(ActionRejectReason::Silenced);
    }

    if(client.isMahjongPung(currentWind, dropStone, activeRuneGameRuleset()))
    {
	actions.push_back(MahjongSayPung(client.wind));
	AI::mahjongOtherPass(currentWind, actions, client.wind);
	return true;
    }

    ERROR("isMahjongPung: false");
    return rejectAction(ActionRejectReason::IllegalMahjongCall);
}

bool GameData::clientSayKong(const Avatar & avatar, const ClientMessage & act, ActionList & actions)
{
    LocalPlayer & client = playerOfAvatar(avatar);
    auto action = static_cast<const ClientSayKong &>(act);

    DEBUG(client.toString());

    if(client.isSilenced())
    {
	ERROR("player silence mode: " << client.name());
	return rejectAction(ActionRejectReason::Silenced);
    }

    if(1 == action.kongType() &&
       client.isMahjongKong1(currentWind, dropStone, activeRuneGameRuleset()))
    {
	actions.push_back(MahjongSayKong(client.wind));
	AI::mahjongOtherPass(currentWind, actions, client.wind);
	return true;
    }
    else
    if(2 == action.kongType() && client.isMahjongKong2(currentWind, activeRuneGameRuleset()))
    {
	actions.push_back(MahjongSayKong(client.wind));
	return true;
    }

    ERROR("isMahjongKong: false");
    return rejectAction(ActionRejectReason::IllegalMahjongCall);
}

bool GameData::clientButtonGame(const Avatar & avatar, const ClientMessage & act, ActionList & actions)
{
    LocalPlayer & client = playerOfAvatar(avatar);

    DEBUG(client.toString());

    WinResults validatedResult;
    if(!client.isWinMahjong(currentWind, roundWind, dropStone, &validatedResult,
                            activeRuneGameRuleset()))
    {
	ERROR("invalid Mahjong Game claim: " << client.toString());
	return rejectAction(client.isSilenced() ? ActionRejectReason::Silenced :
	                    ActionRejectReason::IllegalMahjongCall);
    }

    winResult = validatedResult;
    const RuneGameRuleset & ruleset = activeRuneGameRuleset();

    client.setMahjongGame(winResult, ruleset);

    const int score = winResult.totalScore(ruleset);
    if(0 < score)
    {
        for(const auto & fine : winResult.opponentFines())
        {
            const LocalPlayer & opponent = playerOfWind(fine.wind());
            client.addLandClaimPoints(opponent.clan, score * fine.value());
        }
    }

    actions.push_back(MahjongGame(client.wind));
    AI::mahjongOtherPass(currentWind, actions, client.wind);
    actions.push_back(MahjongData(currentWind));

    actions.push_back(MahjongEnd(currentWind));
    validateMahjongSummary();
    gamePart = Menu::MahjongSummaryPart;

    return true;
}

bool GameData::clientButtonPass(const Avatar & avatar, const ClientMessage & act, ActionList & actions)
{
    LocalPlayer & client = playerOfAvatar(avatar);

    DEBUG(client.toString());

    if(AI::mahjongGameKongPungChao(currentWind, roundWind, dropStone, winResult,
                                   actions, false, activeRuneGameRuleset()))
	return true;

	const bool allAI = std::all_of(gamers.begin(), gamers.end(),
	    [](const LocalPlayer & player){ return GameData::usesAI(player); });
	if(!GameData::usesAI(client) || allAI)
	{
	    currentWind.shift();
	    croupier.put(dropStone);
	    dropStone = Stone(Stone::None);
    }

    actions.push_back(MahjongData(currentWind));
    skipRepeatSay = false;
    return true;
}

bool GameData::clientButtonPung(const Avatar & avatar, const ClientMessage & act, ActionList & actions)
{
    LocalPlayer & client = playerOfAvatar(avatar);

    DEBUG(client.toString());

    if(client.isSilenced()) return rejectAction(ActionRejectReason::Silenced);
    if(!client.isMahjongPung(currentWind, dropStone, activeRuneGameRuleset()))
	return rejectAction(ActionRejectReason::IllegalMahjongCall);

    actions.push_back(MahjongPung(client.wind, dropStone));
    client.setMahjongPung(dropStone, activeRuneGameRuleset());
    dropStone.reset();
    currentWind = client.wind;
    actions.push_back(MahjongData(currentWind));
    skipNewStone = true;

    return true;
}

bool GameData::clientButtonKong1(const Avatar & avatar, const ClientMessage & act, ActionList & actions)
{
    LocalPlayer & client = playerOfAvatar(avatar);

    DEBUG(client.toString());

    if(client.isSilenced()) return rejectAction(ActionRejectReason::Silenced);
    if(!client.isMahjongKong1(currentWind, dropStone, activeRuneGameRuleset()))
	return rejectAction(ActionRejectReason::IllegalMahjongCall);

    actions.push_back(MahjongKong1(client.wind, dropStone));
    client.setMahjongKong1(dropStone, activeRuneGameRuleset());
    dropStone.reset();
    currentWind = client.wind;
    actions.push_back(MahjongData(currentWind));
    // An exposed Kong, unlike Pung or Chao, receives a replacement draw.
    // Skipping it leaves the caller one rune short and can eventually empty
    // the concealed hand before the part is over.
    skipNewStone = false;

    return true;
}

bool GameData::clientButtonKong2(const Avatar & avatar, const ClientMessage & act, ActionList & actions)
{
    LocalPlayer & client = playerOfAvatar(avatar);

    DEBUG(client.toString());

    if(client.isSilenced()) return rejectAction(ActionRejectReason::Silenced);
    if(!client.isMahjongKong2(currentWind, activeRuneGameRuleset()))
	return rejectAction(ActionRejectReason::IllegalMahjongCall);

    actions.push_back(MahjongKong2(client.wind));
    client.setMahjongKong2(activeRuneGameRuleset());
    actions.push_back(MahjongData(currentWind));

    return true;
}

bool GameData::clientChaoVariant(const Avatar & avatar, const ClientMessage & act, ActionList & actions)
{
    LocalPlayer & client = playerOfAvatar(avatar);

    auto ca = static_cast<const ClientChaoVariant &>(act);

    DEBUG(client.toString() << ", " << "variant: " << ca.chaoVariant());

    if(client.isSilenced()) return rejectAction(ActionRejectReason::Silenced);
    const Stones variants = client.stones.findChaoVariants(dropStone);
    if(!client.isMahjongChao(currentWind, dropStone, activeRuneGameRuleset()) ||
       ca.chaoVariant() < 0 || variants.size() <= ca.chaoVariant())
	return rejectAction(ActionRejectReason::IllegalMahjongCall);

    actions.push_back(MahjongChao(client.wind, dropStone));
    client.setMahjongChao(dropStone, ca.chaoVariant(), activeRuneGameRuleset());
    dropStone.reset();
    currentWind = client.wind;
    actions.push_back(MahjongData(currentWind));
    skipNewStone = true;

    return true;
}

bool GameData::clientDropIndex(const Avatar & avatar, const ClientMessage & act, ActionList & actions)
{
    LocalPlayer & client = playerOfAvatar(avatar);

    auto ca = static_cast<const ClientDropIndex &>(act);

    DEBUG(client.toString() << ", " << "index: " << ca.dropIndex() << ", " << "stones: " << client.stones.toString());

    if(currentWind != client.wind) return rejectAction(ActionRejectReason::WrongTurn);

    if(dropStone.isValid())
    {
	ERROR("drop stone: " << dropStone() << ", " << "(" << dropStone.toString() << ")");
	return rejectAction(ActionRejectReason::StaleSelection);
    }

    if(ca.dropIndex() < 0 || client.stones.size() < ca.dropIndex() ||
       (client.stones.size() == ca.dropIndex() && !client.newStone.isValid()))
	return rejectAction(ActionRejectReason::InvalidRuneChoice);

    dropStone = client.setMahjongDrop(ca.dropIndex(), activeRuneGameRuleset());
    actions.push_back(MahjongDrop(currentWind, dropStone));
    actions.push_back(MahjongData(currentWind));

    DEBUG("drop stone: " << dropStone() << ", " << "(" << dropStone.toString() << ")");

    AI::mahjongGameKongPungChao(currentWind, roundWind, dropStone, winResult,
                                actions, true, activeRuneGameRuleset());
    skipRepeatSay = true;
    skipNewStone = false;
    skipNewTurn = false;

    return true;
}

bool GameData::clientSummonCreature(const Avatar & avatar, const ClientMessage & act, ActionList & actions)
{
    LocalPlayer & client = playerOfAvatar(avatar);

    if(currentWind != client.wind)
    {
	ERROR("wind not current: " << client.wind.toString());
	return rejectAction(ActionRejectReason::WrongTurn);
    }

    auto ca = static_cast<const ClientSummonCreature &>(act);

    if(client.isSilenced())
    {
	ERROR("player silence mode: " << client.name());
	return rejectAction(ActionRejectReason::Silenced);
    }

    if(client.isAffectedSpell(Spell::ManaFog) && !ca.isForce())
    {
	ERROR("player mana fog mode: " << client.name());
	return rejectAction(ActionRejectReason::ManaFog);
    }

    if(client.isCasted() && !ca.isForce())
    {
	ERROR("player casted: " << client.name());
	return rejectAction(ActionRejectReason::AlreadyCast);
    }

    if(client.army.isMaximumSummoning())
    {
	ERROR("player summmoning maximum: " << client.name());
	return rejectAction(ActionRejectReason::ArmyFull);
    }

    Creature creature = ca.creature();
    Land land = ca.land();

    if(! land.isValid())
    {
	ERROR("land invalid: " << land.toString());
	return rejectAction(ActionRejectReason::InvalidLand);
    }

    const LandInfo & landInfo = GameData::landInfo(land);

    if(! landInfo.stat.power || (! land.isTowerWinds() && landInfo.clan != client.clan))
    {
	ERROR("land incorrect: " << land.toString());
	return rejectAction(ActionRejectReason::InvalidLand);
    }

    const CreatureInfo & creatureInfo = GameData::creatureInfo(creature);
    if(! client.stones.allowCast(creatureInfo.stones, client.newStone) && !ca.isForce())
    {
	ERROR("player can not cast rule: " << creatureInfo.stones.toString());
	return rejectAction(ActionRejectReason::MissingRunes);
    }

    if(creatureInfo.unique && GameData::findCreatureUnique(creature))
    {
	ERROR("unique creature found on map, there can be only one!");
	return rejectAction(ActionRejectReason::UniqueCreatureExists);
    }

    if(client.points < creatureInfo.cost && !ca.isForce())
    {
	ERROR("points error: " << client.points << ", " << creatureInfo.cost);
	return rejectAction(ActionRejectReason::InsufficientPoints, client.points, creatureInfo.cost);
    }

    if(! client.army.join(creature, land))
	return rejectAction(ActionRejectReason::PartyFull);

    if(!ca.isForce())
    {
	client.points -= creatureInfo.cost;
	client.stones.setCasted(creatureInfo.stones, client.newStone);
	client.setCasted(true);
    }

    actions.push_back(MahjongSummon(currentWind, creature, land));
    actions.push_back(MahjongData(currentWind));

    DEBUG(client.toString() << ", " << "creature: " << creature.toString() << ", " << "land: " << land.toString());
    return true;
}

bool GameData::clientCastSpell(const Avatar & avatar, const ClientMessage & act, ActionList & actions)
{
    LocalPlayer & client = playerOfAvatar(avatar);

    if(currentWind != client.wind)
    {
	ERROR("wind not current: " << client.wind.toString());
	return rejectAction(ActionRejectReason::WrongTurn);
    }

    auto ca = static_cast<const ClientCastSpell &>(act);

    if(client.isSilenced())
    {
	ERROR("player silence mode: " << client.name());
	return rejectAction(ActionRejectReason::Silenced);
    }

    if(client.isAffectedSpell(Spell::ManaFog) && !ca.isForce())
    {
	ERROR("player mana fog mode: " << client.name());
	return rejectAction(ActionRejectReason::ManaFog);
    }

    if(client.isCasted() && !ca.isForce())
    {
	ERROR("player already casted: " << client.name());
	return rejectAction(ActionRejectReason::AlreadyCast);
    }

    Spell spell = ca.spell();

    const SpellInfo & spellInfo = GameData::spellInfo(spell);

    if(! client.allowCastSpell(spell) && ! ca.isForce())
    {
	if(spellInfo.stones.size())
	{
	    ERROR("player can not cast rule: " << spellInfo.stones.toString());
	}

	ERROR("player can not cast spell: " << spell.toString());
	return rejectAction(spellInfo.stones.size() ? ActionRejectReason::MissingRunes :
	                    ActionRejectReason::InvalidTarget);
    }

    if(client.points < spellInfo.cost)
    {
	ERROR("points error: " << client.points << ", " << spellInfo.cost);
	return rejectAction(ActionRejectReason::InsufficientPoints, client.points, spellInfo.cost);
    }

    if(spellInfo.target() == SpellTarget::AllPlayers)
    {
	DEBUG(client.toString() << ", " << "spell: " << spell.toString());

	actions.push_back(MahjongCast(currentWind, spell));
	for(auto & player : gamers)
	    player.mahjongApplySpell(spell, client.avatar);
    }
    else
    if(spellInfo.target() == SpellTarget::MyPlayer)
    {
	DEBUG(client.toString() << ", " << "spell: " << spell.toString());

	actions.push_back(MahjongCast(currentWind, spell));
	client.mahjongApplySpell(spell, client.avatar);
    }
    else
    if(spellInfo.target() == SpellTarget::OtherPlayer)
    {
	Avatar target = ca.target();
	DEBUG(client.toString() << ", " << "spell: " << spell.toString() << ", " << "target: " << target.toString());
	LocalPlayer* targetPlayer = gamers.playerOfAvatar(target);

	if(! targetPlayer || target == client.avatar || GameData::allied(*targetPlayer, client))
	{
	    ERROR("other player target invalid: " << target.toString());
	    return rejectAction(ActionRejectReason::InvalidTarget);
	}

	actions.push_back(MahjongCast(currentWind, spell, target));
	targetPlayer->mahjongApplySpell(spell, client.avatar);
    }
    else
    {
	Land land = ca.land();
	int unit = ca.unit();

	DEBUG(client.toString() << ", " << "spell: " << spell.toString() << ", " <<
	    "land: " << land.toString() << ", " << "unit: " << String::hex(unit, 8) << ", " << "spell effect: " << spellInfo.effect.toString());

	BattleTargets targets;
	targets.reserve(10);

	if(spell() == Spell::Teleport)
	{
	    BattleCreature* target = client.army.findBattleUnit(unit);
	    const bool friendlyLand = land.isValid() &&
		(land.isTowerWinds() || GameData::landInfo(land).clan == client.clan);

	    if(! target || ! friendlyLand || ! client.army.teleportCreature(*target, land))
	    {
		ERROR("teleport target error" << ", " << "unit: " << String::hex(unit, 8) << ", " << "land: " << land.toString());
		return rejectAction(!target ? ActionRejectReason::UnitNotFound :
		                    (!friendlyLand ? ActionRejectReason::InvalidLand :
		                                     ActionRejectReason::PartyFull));
	    }

	    targets << client.army.findBattleUnit(unit);
	}
	else
	if(spellInfo.target() == SpellTarget::Land)
	{
	    for(auto & lp : gamers)
	    {
		auto party = lp.army.findPartyConst(land);
		if(party) targets << BattleTargets(party->toBattleCreatures());
	    }
	}
	else
	if(spellInfo.target() & SpellTarget::Party)
	{
	    BattleParty* party = 0 < unit ? getBattleParty(unit) : nullptr;
	    const bool friendly = party && GameData::allied(party->clan(), client.clan);
	    const bool allowed = party && party->land() == land &&
		(((spellInfo.target() & SpellTarget::Friendly) && friendly) ||
		 ((spellInfo.target() & SpellTarget::Enemy) && !friendly));

	    if(! allowed)
	    {
		ERROR("party spell target invalid" << ", " << "unit: " << String::hex(unit, 8) << ", " << "land: " << land.toString());
		return rejectAction(ActionRejectReason::InvalidTarget);
	    }

	    targets << BattleTargets(party->toBattleCreatures());
	}
	else
	if(unit)
	{
	    BattleCreature* bcr = getBattleCreature(unit);
	    BattleParty* party = getBattleParty(unit);
	    const bool friendly = bcr && GameData::allied(bcr->clan(), client.clan);
	    const bool allowed = bcr && party && party->land() == land && bcr->canReceiveSpell(spell) &&
		(((spellInfo.target() & SpellTarget::Friendly) && friendly) ||
		 ((spellInfo.target() & SpellTarget::Enemy) && !friendly));

	    if(! allowed)
	    {
		ERROR("unit spell target invalid" << ", " << "unit: " << String::hex(unit, 8) << ", " << "land: " << land.toString());
		return rejectAction(bcr ? ActionRejectReason::InvalidTarget : ActionRejectReason::UnitNotFound);
	    }

	    targets.push_back(bcr);
	}

	if(targets.empty())
	{
	    ERROR("spell has no valid targets: " << spell.toString());
	    return rejectAction(ActionRejectReason::InvalidTarget);
	}

	std::vector<int> resistence;
	resistence.reserve(5);

	if(spell() != Spell::Teleport)
	{
	    for(auto & tgt : targets)
	    {
		if(! tgt->applySpell(spell))
		    resistence.push_back(tgt->battleUnit());
	    }

	    // Serialize target ids before dead creatures are removed from their parties.
	    actions.push_back(MahjongCast(currentWind, spell, land, targets, resistence));

	    std::set<Clan> checkArmy;

	    // check dead creatures
	    for(auto & tgt : targets)
	    {
		if(0 >= tgt->loyalty())
		{
		    const LocalPlayer & other = playerOfClan(tgt->clan());
		    const std::string info = StringFormat(_("%1's %2 was vanquished"))
		        .arg(other.name())
		        .arg(GameData::creatureInfo(static_cast<const BattleCreature &>(*tgt)).name);
		    actions.push_back(MahjongInfo(currentWind, info));
		    checkArmy.insert(tgt->clan());
		}
	    }

	    for(auto & clan : checkArmy)
	    {
		LocalPlayer & other = playerOfClan(clan);
		other.army.removeUnloyalty();
	    }
	}
	else
	    actions.push_back(MahjongCast(currentWind, spell, land, targets, resistence));
    }

    // client apply
    client.points -= spellInfo.cost;
    client.stones.setCasted(spellInfo.stones, client.newStone);
    client.setCasted(true);

    // copy all data
    actions.push_back(MahjongData(currentWind));
    return true;
}

bool GameData::client2Mahjong(const Avatar & avatar, const ClientMessage & act, ActionList & actions,
                              ActionRejection* rejection)
{
    ScopedActionRejection rejectionScope(rejection);
    const std::size_t actionsBefore = actions.size();
    const bool recordAction = Replay::actionRecordingEnabled();
    const JsonObject beforeState = recordAction ? authoritativeState() : JsonObject();
    CrashReport::breadcrumb(std::string("GameData phase=Mahjong stage=before avatar=")
        .append(avatar.toString()).append(" type=").append(String::number(act.type()))
        .append(" payload=").append(act.toString()));

    bool accepted = false;
    bool recognized = true;
    switch(act.type())
    {
	case Action::ClientReady:	accepted = clientReady(avatar, act, actions); break;
	case Action::ClientSayGame:	accepted = clientSayGame(avatar, act, actions); break;
	case Action::ClientSayChao:	accepted = clientSayChao(avatar, act, actions); break;
	case Action::ClientSayPung:	accepted = clientSayPung(avatar, act, actions); break;
	case Action::ClientSayKong:	accepted = clientSayKong(avatar, act, actions); break;
	case Action::ClientButtonGame:	accepted = clientButtonGame(avatar, act, actions); break;
	case Action::ClientButtonPass:	accepted = clientButtonPass(avatar, act, actions); break;
	case Action::ClientButtonPung:	accepted = clientButtonPung(avatar, act, actions); break;
	case Action::ClientButtonKong1:	accepted = clientButtonKong1(avatar, act, actions); break;
	case Action::ClientButtonKong2:	accepted = clientButtonKong2(avatar, act, actions); break;
	case Action::ClientChaoVariant:	accepted = clientChaoVariant(avatar, act, actions); break;
	case Action::ClientDropIndex:	accepted = clientDropIndex(avatar, act, actions); break;
	case Action::ClientLuckChoice:	accepted = clientLuckChoice(avatar, act, actions); break;
	case Action::ClientSummonCreature: accepted = clientSummonCreature(avatar, act, actions); break;
	case Action::ClientCastSpell:	accepted = clientCastSpell(avatar, act, actions); break;

	default: recognized = false; rejectAction(ActionRejectReason::UnknownAction); break;
    }

    if(recognized && !accepted && (!rejection || !rejection->isValid()))
        rejectAction(ActionRejectReason::UnknownAction);

    if(recognized && accepted && recordAction)
        Replay::recordAcceptedAction(beforeState, avatar, act, authoritativeState());

    CrashReport::breadcrumb(std::string("GameData phase=Mahjong stage=after avatar=")
        .append(avatar.toString()).append(" type=").append(String::number(act.type()))
        .append(" recognized=").append(recognized ? "true" : "false")
        .append(" accepted=").append(accepted ? "true" : "false")
        .append(" rejection=").append(rejection ? actionRejectReasonName(rejection->reason) : "not_requested")
        .append(" emitted=").append(String::number(static_cast<int>(actions.size() - actionsBefore))));
    return accepted;
}
