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
 ***************************************************************************/

#include <algorithm>
#include <sstream>
#include <utility>

#include "gameplayrng.h"
#include "gamedata.h"

BattleCreature::BattleCreature(const Clan & clan, const Creature & cr, int uid)
    : Creature(cr), CreatureSkill(GameData::creatureInfo(cr).stat), buid(uid), selected(true), owner(clan)
{
}
BattleCreature::BattleCreature(const Clan & clan, const Creature & cr, const CreatureSkill & cs, int uid)
    : Creature(cr), CreatureSkill(cs), buid(uid), selected(false), owner(clan)
{
}

int BattleCreature::attack(void) const
{
    return BattleUnit::attack() + affected.attack();
}

int BattleCreature::ranger(void) const
{
    return BattleUnit::ranger() + affected.ranger();
}

int BattleCreature::defense(void) const
{
    return BattleUnit::defense() + affected.defense();
}

int BattleCreature::loyalty(void) const
{
    return BattleUnit::loyalty() + affected.loyalty();
}

int BattleCreature::freeMovePoint(void) const
{
    bool isParalyze = isAffectedSpell(Spell::Paralyze);
    return isParalyze ? 0 : CreatureSkill::freeMovePoint();
}

bool BattleCreature::haveSpeciality(const Speciality & spec) const
{
    const CreatureInfo & info = GameData::creatureInfo(*this);
    return info.specials.check(spec);
}

bool BattleCreature::isAffectedSpell(const Spell & spell) const
{
    return affected.isAffected(spell);
}

bool BattleCreature::canReceiveSpell(const Spell & spell) const
{
    // Guidance improves an existing ranged attack; it does not grant one.
    return spell() != Spell::Guidance || 0 < baseRanger();
}

bool BattleCreature::applySpell(const Spell & spell)
{
    if(haveSpeciality(Speciality::MagicResistence))
    {
	int chance = SpecialityMagicResistence().chance(Creature::id());

	if(chance >= GameplayRng::uniform(1, 100))
	{
	    VERBOSE("Speciality: " << "Magic Resistence!");
	    return false;
	}
    }

    switch(spell())
    {
	case Spell::Paralyze:
	    affected.insert(AffectedSpell(spell));
	    return true;

	case Spell::Smoke:
	case Spell::DemonicCompulsion:
	case Spell::MassPanic:
	case Spell::Reduction:
	case Spell::BattleFury:
	case Spell::Guidance:
	case Spell::ForceShield:
	case Spell::DustCloud:
	case Spell::Heroism:
	case Spell::BlindAmbition:
	case Spell::BrilliantLights:
	case Spell::MagicalAura:
	    affected.insert(spell);
	    return true;

	case Spell::MysticalFountain:
	{
	    auto rnd = static_cast<Spell::spell_t>(Spell::MysticalFountain + GameplayRng::uniform(1, 3));
	    affected.insert(Spell(rnd));
	}
	    return true;


	case Spell::Healing:
	    applyStats(GameData::spellInfo(spell).effect);
	    if(stat4.current() > stat4.base()) stat4.reset();
	    return true;

	case Spell::LightningBolt:
	case Spell::HellBlast:
	    applyStats(GameData::spellInfo(spell).effect);
	    return true;

	case Spell::DispelMagic:
	case Spell::MassDispel:
	    affected.clear();
	    return true;

	case Spell::Teleport:
	case Spell::DrawSkull:
	case Spell::DrawSword:
	case Spell::DrawNumber:
	case Spell::RandomDiscard:
	case Spell::ScryRunes:
	case Spell::Silence:
	case Spell::ManaFog:
	    break;

	default: ERROR("unknown spell: " << spell()); break;
    }

    return false;
}

void BattleCreature::initMahjongPart(const Ability & ability)
{
    // The previous Adventure phase consumes one round of timed enchantments.
    affected.advanceAdventureRound();

    // skills turn
    const CreatureInfo & info = GameData::creatureInfo(*this);
    CreatureSkill::initMahjongPart(ability, info.specials, affected);
    setSelected(true);
}

void BattleCreature::initAdventurePart(const Ability & ability)
{
    const CreatureInfo & info = GameData::creatureInfo(*this);
    CreatureSkill::initAdventurePart(ability, info.specials, affected);
}

bool BattleCreature::canMoveSelected(void) const
{
    bool isParalyze = isAffectedSpell(Spell::Paralyze);
    return 0 < freeMovePoint() && ! isParalyze && isSelected();
}

std::string BattleCreature::name(void) const
{
    std::ostringstream os;
    os << "creature" << "(" << Creature::toString() << ", " << "battleUnit: " << String::hex(battleUnit(), 8) << ")";
    return os.str();
}

std::string BattleCreature::toString(void) const
{
    std::ostringstream os;
    os << Creature::toString() << "(" << CreatureSkill::toString() << ", " << "clan: " << owner.toString() << ", " << "battleUnit: " << String::hex(battleUnit(), 8) << ")";
    return os.str();
}

JsonObject BattleCreature::toJsonObject(void) const
{
    JsonObject jo = CreatureSkill::toJsonObject();
    jo.addInteger("battleUnit", battleUnit());
    jo.addString("creature", Creature::toString());
    jo.addString("owner", owner.toString());

    jo.addArray("affected", affected.toJsonArray());

    return jo;
}

BattleCreature BattleCreature::fromJsonObject(const JsonObject & jo)
{
    Creature cr(jo.getString("creature", "none"));
    CreatureSkill bs = CreatureSkill::fromJsonObject(jo);
    BattleCreature res(Clan(jo.getString("owner", "none")), cr, bs, jo.getInteger("battleUnit", 0));
    res.selected = true;

    const JsonArray* ja = jo.getArray("affected");
    if(ja) res.affected = AffectedSpells::fromJsonArray(*ja);

    return res;
}

/* BattleCreatures */
BattleCreatures & BattleCreatures::operator<< (const BattleCreatures & bcrs)
{
    insert(end(), bcrs.begin(), bcrs.end());
    return *this;
}

int BattleCreatures::validSize(void) const
{
    int res = 0;
    for(auto it = begin(); it != end(); ++it)
	if(*it && (*it)->isValid()) ++res;
    return res;
}

/* BattleTargets */
BattleTargets::BattleTargets(const BattleCreatures & bcrs)
{
    for(auto it = bcrs.begin(); it != bcrs.end(); ++it)
	if(*it && (*it)->isValid()) push_back(*it);
}

BattleTargets & BattleTargets::operator<< (const BattleUnit* bu)
{
    if(bu && bu->isValid()) push_back(const_cast<BattleUnit*>(bu));
    return *this;
}

BattleTargets & BattleTargets::operator<< (const BattleTargets & btrs)
{
    insert(end(), btrs.begin(), btrs.end());
    return *this;
}

JsonArray BattleTargets::toJsonArray(void) const
{
    JsonArray ja;
    for(auto it = begin(); it != end(); ++it)
        if(*it) ja.addInteger((*it)->battleUnit());
    return ja;
}

BattleTargets BattleTargets::fromJsonArray(const JsonArray & ja)
{
    BattleTargets res;
    for(int it = 0; it < ja.size(); ++it)
    {
	int battleUnit = ja.getInteger(it);
	BattleCreature* creature = GameData::findBattleCreature(battleUnit);
	if(creature) res.push_back(creature);
    }
    return res;
}

/* BattleTown */
BattleTown::BattleTown(const Land & land) : BattleUnit(GameData::landInfo(land).stat), territory(land)
{
    previous = GameData::landInfo(land).clan;
}

JsonObject BattleTown::toJsonObject(void) const
{
    JsonObject jo = BattleUnit::toJsonObject();

    jo.addString("previous", previous.toString());
    jo.addString("territory", land().toString());

    return jo;
}

BattleTown BattleTown::fromJsonObject(const JsonObject & jo)
{
    const Clan & previous = Clan(jo.getString("previous", "none"));
    const Land & territory = Land(jo.getString("territory", "none"));

    return BattleTown(BattleUnit::fromJsonObject(jo), previous, territory);
}

const Clan & BattleTown::previousClan(void) const
{
    return previous;
}

const Clan & BattleTown::currentClan(void) const
{
    return GameData::landInfo(land()).clan;
}

std::string BattleTown::toString(void) const
{
    std::ostringstream os;
    os << land().toString() << "(" << BattleUnit::toString() << ", " << "battleUnit: " << String::hex(battleUnit(), 8) << ")";
    return os.str();
}

std::string BattleTown::name(void) const
{
    std::ostringstream os;
    os << "tower" << "(" << land().toString() << ")";
    return os.str();
}

/* BattleParty */
BattleParty::BattleParty(const Clan & clan, const Land & land) : position(land), target(Land::None), owner(clan)
{
    resize(3);
}

std::string BattleParty::toString(void) const
{
    std::ostringstream os;

    os << "party: " <<
	"clan(" << owner.toString() << ")" << ", " <<
	"land(" << position.toString() << ")" << ", ";

    if(isEmpty())
	os << "empty";
    else
    {
	for(auto it = begin(); it != end(); ++it)
	    if((*it).isValid()) os << (*it).toString() << ", ";
    }

    return os.str();
}

JsonObject BattleParty::toJsonObject(void) const
{
    JsonObject jo;
    jo.addString("position", position.toString());
    jo.addString("target", target.toString());
    jo.addString("owner", owner.toString());

    JsonArray ja;
    for(auto it = begin(); it != end(); ++it)
    {
	ja.addObject((*it).toJsonObject());
    }
    jo.addArray("creatures", ja);

    return jo;
}

BattleParty BattleParty::fromJsonObject(const JsonObject & jo)
{
    BattleParty res;
    res.position = Land(jo.getString("position", "none"));
    res.target = Land(jo.getString("target", "none"));
    res.owner = Clan(jo.getString("owner", "none"));

    const JsonArray* ja = jo.getArray("creatures");
    if(ja)
    {
	res.resize(ja->size());

	for(int it = 0; it < ja->size(); ++it)
	{
	    const JsonObject* jo = ja->getObject(it);
	    if(jo && jo->isValid())
	    {
		res[it] = BattleCreature::fromJsonObject(*jo);
	    }
	}
    }

    return res;
}

BattleCreatures BattleParty::toBattleCreatures(const Specials & specials, bool filter) const
{
    BattleCreatures res;

    for(auto & bcr : *this)
	if(bcr.isValid())
    {
	const CreatureInfo & info = GameData::creatureInfo(bcr);
	bool push = false;

	if(filter)
	{
	    if(specials.any())
	    {
		push = info.specials.to_ulong() & specials.to_ulong();
	    }
	    else
	    {
		push = info.specials.none();
	    }
	}
	else
	{
	    if(specials.any())
	    {
		push = info.specials.to_ulong() & ~specials.to_ulong();
	    }
	    else
	    {
		push = info.specials.any();
	    }
	}

	if(push)
	    res.push_back(const_cast<BattleCreature*>(& bcr));
    }

    return res;
}

BattleCreatures BattleParty::toBattleCreatures(void) const
{
    BattleCreatures res;
    for(auto it = begin(); it != end(); ++it)
	if((*it).isValid()) res.push_back(const_cast<BattleCreature*>(& (*it)));

    return res;
}

int BattleParty::count(void) const
{
    return size() - std::count_if(begin(), end(), [](const BattleCreature & bcr){ return ! bcr.isValid(); });
}

bool BattleParty::isEmpty(void) const
{
    return count() == 0;
}

void BattleParty::dismiss(void)
{
    clear();
}

bool BattleParty::canJoin(void) const
{
    return std::any_of(begin(), end(), [](const BattleCreature & bcr){ return ! bcr.isValid(); });
}

bool BattleParty::join(const Creature & cr)
{
    // first invalid
    auto it = std::find_if(begin(), end(), [](const BattleCreature & bcr){ return ! bcr.isValid(); });
    if(it == end()){ ERROR("party: is full"); return false; }

    *it = BattleCreature(owner, cr, GameData::nextBattleUnitId());
    DEBUG((*it).toString());

    return true;
}

bool BattleParty::join(const BattleCreature & bcr)
{
    if(! bcr.isValid() || bcr.clan() != owner)
    {
	ERROR("invalid creature for party");
	return false;
    }

    auto it = std::find_if(begin(), end(), [](const BattleCreature & current){ return ! current.isValid(); });
    if(it == end())
    {
	ERROR("party: is full");
	return false;
    }

    *it = bcr;
    return true;
}

bool BattleParty::remove(const BattleCreature & bcr)
{
    auto it = std::find(begin(), end(), bcr);
    if(it != end())
    {
	*it = BattleCreature();
	return true;
    }
    return false;
}

void BattleParty::removeUnloyalty(void)
{
     for(auto it = begin(); it != end(); ++it)
	if((*it).isValid() && ! (*it).isAlive())
	*it = BattleCreature();
}

const BattleCreature* BattleParty::index(int index) const
{
    return 0 <= index && index < size() ? & at(index) : nullptr;
}

BattleCreature* BattleParty::findBattleUnit(int uid)
{
    return const_cast<BattleCreature*>(findBattleUnitConst(uid));
}

const BattleCreature* BattleParty::findBattleUnitConst(int uid) const
{
    auto it = std::find_if(begin(), end(),[=](const BattleCreature & bcr){ return bcr.isBattleUnit(uid); });
    return it != end() ? & (*it) : nullptr;
}

bool BattleParty::findCreature(const Creature & cr) const
{
    return end() != std::find(begin(), end(), cr);
}

int BattleParty::movePoint(void) const
{
    int res = 0;

    for(auto it = begin(); it != end(); ++it)
    {
	if(! (*it).isValid()) continue;

	const int move = (*it).freeMovePoint();
	res = res ? std::min(res, move) : move;
	if(! res) break;
    }

    return res;
}

BaseStat BattleParty::toBaseStatSummary(void) const
{
    BaseStat res;

    for(auto it = begin(); it != end(); ++it)
    {
        res.attack += (*it).attack();
        res.defense += (*it).defense();
        res.ranger += (*it).ranger();
        res.loyalty += (*it).loyalty();
    }

    return res;
}

/*BattleArmy */
BattleParty* BattleArmy::findParty(const Land & land)
{
    return const_cast<BattleParty*>(findPartyConst(land));
}

const BattleParty* BattleArmy::findPartyConst(const Land & land) const
{
    auto it = std::find_if(begin(), end(), [&](const BattleParty & party){ return party.isPosition(land); });
    return it != end() ? & (*it) : nullptr;
}

BattleCreatures BattleArmy::partySelected(const Land & land) const
{
    BattleCreatures res;
    const BattleParty* party = findPartyConst(land);
    if(party)
    {
	res = party->toBattleCreatures();
	auto itend = std::remove_if(res.begin(), res.end(), [](auto & bcr){ return ! bcr->canMoveSelected(); });
	res.erase(itend, res.end());
    }
    return res;
}

void BattleArmy::partySetAllSelected(const Land & land)
{
    BattleParty* party = findParty(land);
    if(party)
    {
	for(auto & bcr : party->toBattleCreatures())
	    bcr->setSelected(true);
    }
}

BattleCreature* BattleArmy::findBattleUnit(int uid)
{
    return const_cast<BattleCreature*>(findBattleUnitConst(uid));
}

const BattleCreature* BattleArmy::findBattleUnitConst(int uid) const
{
    for(auto & party : *this)
    {
	const BattleCreature* res = party.findBattleUnitConst(uid);
	if(res) return res;
    }
    return nullptr;
}

bool BattleArmy::findCreature(const Creature & cr) const
{
    return std::any_of(begin(), end(), [&](const BattleParty & party){ return party.findCreature(cr); });
}

BattleTargets BattleArmy::toBattleTargets(const Clan & clan) const
{
    BattleTargets res;

    for(auto it = begin(); it != end(); ++it)
	res << BattleTargets((*it).toBattleCreatures());

    return res;
}

BattleCreatures BattleArmy::toBattleCreatures(const Specials & specials, bool filter) const
{
    BattleCreatures res;

    for(auto it = begin(); it != end(); ++it)
	res << (*it).toBattleCreatures(specials, filter);

    return res;
}

BattleCreatures BattleArmy::toBattleCreatures(void) const
{
    BattleCreatures res;

    for(auto it = begin(); it != end(); ++it)
	res << (*it).toBattleCreatures();

    return res;
}

JsonArray BattleArmy::toJsonArray(void) const
{
    JsonArray ja;
    for(auto it = begin(); it != end(); ++it)
	ja.addObject((*it).toJsonObject());
    return ja;
}

BattleArmy BattleArmy::fromJsonArray(const JsonArray & ja)
{
    BattleArmy res;
    for(int it = 0; it < ja.size(); ++it)
    {
	const JsonObject* jo = ja.getObject(it);
	if(jo) res.push_back(BattleParty::fromJsonObject(*jo));
    }
    return res;
}

void BattleArmy::setAllSelected(void)
{
    BattleCreatures creatures = toBattleCreatures();

    for(auto it = creatures.begin(); it != creatures.end(); ++it)
	if(*it) (*it)->setSelected(true);
}

bool BattleArmy::isFullHouse(void) const
{
    int count = 0;

    for(auto it = begin(); it != end(); ++it)
	count += (*it).count();

    return count >= 6;
}

Spells BattleArmy::allCastSpells(void) const
{
    Spells spells;
    Specials specialsCast = Specials::allCastSpells();
    BattleCreatures bcrs = toBattleCreatures(specialsCast, true);

    for(auto it = bcrs.begin(); it != bcrs.end(); ++it) if(*it && (*it)->isValid())
    {
        const CreatureInfo & info = GameData::creatureInfo(**it);
        for(auto & spec : Specials(info.specials.to_ulong() & specialsCast.to_ulong()).toList())
        {
            const Spell spell = spec.toSpell();
            if(spell.isValid())
		spells.push_back(spell);
        }
    }

    return spells;
}

bool BattleArmy::canJoin(const Land & land) const
{
    if(isFullHouse())
	return false;

    const BattleParty* party = findPartyConst(land);
    return party ? party->canJoin() : false;
}

bool BattleArmy::join(const Creature & creature, const Land & land)
{
    if(! isFullHouse())
    {
	BattleParty* party = findParty(land);

	if(! party)
	{
	    auto remote = GameData::getBattleArmyOwner(*this);
	    if(remote)
	    {
		push_back(BattleParty(remote->clan, land));
		party = & back();
	    }
	}

	return party->join(creature);
    }

    ERROR("army: is full");
    return false;
}

bool BattleArmy::isMaximumSummoning(void) const
{
    return 4 < toBattleCreatures().size();
}

void BattleArmy::remove(const BattleCreature & bcr)
{
    for(auto & party : *this)
	party.remove(bcr);
    shrinkEmpty();
}

void BattleArmy::removeUnloyalty(void)
{
    for(auto & party : *this)
	party.removeUnloyalty();

    shrinkEmpty();
}

void BattleArmy::applyInvisibility(const Clan & observer)
{
    const BattleArmy & observerArmy = GameData::getBattleArmy(observer);

    auto observerHasAdjacentDetector = [&observerArmy](const std::vector<Land> & lands) -> bool
    {
	for(auto & land : lands)
	{
	    const BattleParty* party = observerArmy.findPartyConst(land);
	    if(party && party->toBattleCreatures(Specials() << Speciality::SeeInvisible, true).size())
		return true;
	}
	return false;
    };

    // remove if Speciality::Invisibility
    for(auto & party : *this)
    {
	BattleCreatures bcrs = party.toBattleCreatures(Specials() << Speciality::Invisibility, true);
        bool remove = true;

        // check Speciality::SeeInvisible on borders
        const LandInfo & positionInfo = GameData::landInfo(party.land());

	// skip if position: TowerOf4Winds
	if(positionInfo.id.isTowerWinds())
	    remove = false;
	else
	{
	    if(observerHasAdjacentDetector(positionInfo.borders))
	    {
		DEBUG("found Speciality::SeeInvisible" << ", " << "observer: " << observer.toString() << ", " << "land: " << party.land().toString());
                remove = false;
            }
	}

        if(remove)
        {
	    for(auto & bcr : bcrs)
	    {
		DEBUG("remove Speciality::Invisibility" << ", " << "land: " << party.land().toString() << ", " << "creature: " << bcr->toString());
		party.remove(*bcr);
	    }
	}
    }

    shrinkEmpty();
}

void BattleArmy::shrinkEmpty(void)
{
    remove_if([](const BattleParty & party){ return party.isEmpty(); });
}

std::string BattleArmy::toString(void) const
{
    std::ostringstream os;

    for(auto it = begin(); it != end(); ++it)
	os << (*it).toString() << ", ";

    return os.str();
}


bool BattleArmy::canGateParty(const Land & fromLand, const Land & toLand) const
{
    if(!fromLand.isValid() || !toLand.isValid() || fromLand == toLand) return false;

    const BattleParty* source = findPartyConst(fromLand);
    if(!source || source->isEmpty()) return false;

    const LandInfo & destination = GameData::landInfo(toLand);
    if(!toLand.isTowerWinds() && (!destination.stat.power || destination.clan != source->clan()))
	return false;

    const BattleCreatures creatures = source->toBattleCreatures();
    return std::any_of(creatures.begin(), creatures.end(), [](const BattleCreature* creature)
    {
	return creature && creature->haveSpeciality(Speciality::Gate);
    });
}

bool BattleArmy::canMoveCreature(const BattleCreature & bcr, const Land & fromLand, const Lands & path) const
{
    if(path.empty())
    {
        DEBUG("creature can't move: empty path from " << fromLand.toString());
	return false;
    }

    const Land & toLand = path.back();

    // Allied territory is safe to cross, but the classic combat model stores
    // one defending army per owner. Do not create co-located allied garrisons
    // until multi-army battles are introduced explicitly.
    if(!toLand.isTowerWinds())
    {
        const Clan destinationOwner = GameData::landInfo(toLand).clan;
        if(destinationOwner.isValid() && destinationOwner != bcr.clan() &&
           GameData::allied(destinationOwner, bcr.clan()))
            return false;
    }

    const bool gate = canGateParty(fromLand, toLand);
    const int movementCost = gate ? 1 : static_cast<int>(path.size());

    // check move point
    if(! bcr.canMove(movementCost))
    {
	DEBUG("creature can't move: " << "move cost: " << movementCost << ", " << bcr.toString() << " " << "point: " << bcr.freeMovePoint() << ", " <<
	    "from: " << fromLand.toString() << ", " << "to: " << toLand.toString());
	return false;
    }

    // long jump
    if(!gate && 1 < path.size())
    {
	const CreatureInfo & info = GameData::creatureInfo(bcr);

	if(! info.fly)
	{
	    // check owner: skip dest
	    for(int it = 0; it < path.size() - 1; ++it)
	    {
		const LandInfo & land1 = GameData::landInfo(fromLand);
		const LandInfo & land2 = GameData::landInfo(path[it]);
		if(!GameData::allied(land1.clan, land2.clan)) return false;
	    }
	}
    }

    return true;
}

std::vector<int> BattleArmy::moveSelectedCreatures(const Land & fromLand, const Land & toLand)
{
    std::vector<int> units;
    if(!fromLand.isValid() || !toLand.isValid() || fromLand == toLand) return units;

    const BattleCreatures selected = partySelected(fromLand);
    if(selected.empty()) return units;

    const BattleParty* destination = findPartyConst(toLand);
    const int occupied = destination ? destination->count() : 0;
    if(3 < occupied + static_cast<int>(selected.size())) return units;

    const bool gate = canGateParty(fromLand, toLand);
    Lands path;
    if(gate) path << toLand;
    else path = Lands::pathfind(fromLand, toLand);
    if(path.empty()) return units;

    BattleCreatures ordered = selected;
    if(gate)
    {
	// Keep the portal open until every selected companion has moved.
	std::stable_sort(ordered.begin(), ordered.end(), [](const BattleCreature* left, const BattleCreature* right)
	{
	    const bool leftGate = left && left->haveSpeciality(Speciality::Gate);
	    const bool rightGate = right && right->haveSpeciality(Speciality::Gate);
	    return leftGate < rightGate;
	});
    }

    units.reserve(selected.size());
    for(const BattleCreature* creature : ordered)
    {
	if(!creature || !canMoveCreature(*creature, fromLand, path))
	{
	    units.clear();
	    return units;
	}
	units.push_back(creature->battleUnit());
    }

    BattleArmy planned(*this);
    for(const int unit : units)
    {
	BattleCreature* creature = planned.findBattleUnit(unit);
	if(!creature || !planned.moveCreature(*creature, toLand))
	{
	    units.clear();
	    return units;
	}
    }

    *this = std::move(planned);
    return units;
}

bool BattleArmy::moveCreature(const BattleCreature & bcr, const Land & toLand)
{
    auto it = std::find_if(begin(), end(), [&](BattleParty & party){ return party.findBattleUnit(bcr.battleUnit()); });
    if(it == end())
    {
	ERROR("creature not found, unit: " << bcr.battleUnit());
	return false;
    }

    BattleParty & fromParty = *it;
    BattleParty* toParty = findParty(toLand);
    const Land & fromLand = fromParty.land();

    if(fromLand == toLand)
	return false;

    DEBUG(bcr.toString() << ", from land: " << fromParty.land().toString() << ", to land: " << toLand.toString());

    if(toParty && ! toParty->canJoin())
    {
	ERROR("land can't join: " << toLand.toString());
	return false;
    }

    const bool gate = canGateParty(fromLand, toLand);

    // Gate ignores map distance but still costs one movement point.
    Lands path;
    if(gate) path << toLand;
    else path = Lands::pathfind(fromLand, toLand);
    if(path.empty())
    {
	ERROR("path not found: " << fromLand.toString() << ", " << toLand.toString());
	return false;
    }

    if(! canMoveCreature(bcr, fromLand, path))
    {
        ERROR("creature can't move, path: " << path.toString());
        return false;
    }

    // move
    if(! toParty)
    {
	push_back(BattleParty(fromParty.clan(), toLand));
	toParty = & back();
    }

    BattleCreature* battle = findBattleUnit(bcr.battleUnit());

    if(battle)
    {
	const int battleUnit = battle->battleUnit();
	if(! toParty->join(*battle)) return false;

	BattleCreature* moved = toParty->findBattleUnit(battleUnit);
	if(moved) moved->moved(gate ? 1 : path.size());
    }

    if(! battle) return false;

    // remove
    fromParty.remove(bcr);
    remove_if([](const BattleParty & party){ return party.isEmpty(); });

    return true;
}

bool BattleArmy::teleportCreature(const BattleCreature & bcr, const Land & toLand)
{
    auto it = std::find_if(begin(), end(), [&](BattleParty & party){ return party.findBattleUnit(bcr.battleUnit()); });
    if(it == end() || it->land() == toLand)
	return false;

    BattleParty & fromParty = *it;
    BattleParty* toParty = findParty(toLand);

    if(toParty && ! toParty->canJoin())
	return false;

    if(! toParty)
    {
	push_back(BattleParty(fromParty.clan(), toLand));
	toParty = & back();
    }

    BattleCreature* battle = findBattleUnit(bcr.battleUnit());
    if(! battle || ! toParty->join(*battle))
    {
	shrinkEmpty();
	return false;
    }

    fromParty.remove(bcr);
    shrinkEmpty();
    return true;
}
