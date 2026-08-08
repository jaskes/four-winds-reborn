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

#ifndef _RWNA_GAMEOBJECTS_
#define _RWNA_GAMEOBJECTS_

#include <array>
#include <set>

#include "libswe.h"
using namespace SWE;

#define GAME_SET_COUNT  13
#define GAME_STONE_MAX  70

class RuneGameRuleset;

struct Enum
{
    int	val;

    enum type_t { TypeWind, TypeClan, TypeAvatar, TypeSpell, TypeSpellTarget, TypeCreature, TypeStone, TypeLand, TypeAbility, TypeSpeciality };

    Enum(int v) : val(v) {}
    virtual ~Enum() {}

    int				operator() (void) const { return id(); };
    bool			operator< (const Enum & v) const { return id() < v.id(); }
    bool			operator== (const Enum & v) const { return v.id() == id(); }
    bool			operator!= (const Enum & v) const { return v.id() != id(); }

    void			set(const Enum & v) { val = v.id(); }
    void			reset(void) { val = 0; }
    int				mask(void) const { return static_cast<int>(1) << index(); }

    virtual int			id(void) const { return val; }
    virtual bool		isValid(void) const { return id() != 0; }
    virtual int			index(void) const { return id(); }

    virtual type_t		baseType(void) const = 0;
    virtual std::string		toString(void) const = 0;

    bool			isWind(void) const { return baseType() == TypeWind; }
    bool			isClan(void) const { return baseType() == TypeClan; }
    bool			isAvatar(void) const { return baseType() == TypeAvatar; }
    bool			isSpell(void) const { return baseType() == TypeSpell; }
    bool			isSpellTarget(void) const { return baseType() == TypeSpellTarget; }
    bool			isCreature(void) const { return baseType() == TypeCreature; }
    bool			isStone(void) const { return baseType() == TypeStone; }
    bool			isLand(void) const { return baseType() == TypeLand; }
    bool			isAbility(void) const { return baseType() == TypeAbility; }
    bool			isSpeciality(void) const { return baseType() == TypeSpeciality; }
};

struct Wind : Enum
{
    enum wind_t { None = 0, East, South, West, North };

    Wind(wind_t v = None) : Enum(v) {}
    Wind(const std::string &);

    Wind 			prev(void) const;
    Wind 			next(void) const;

    void 			shift(void) { val = next().id(); }
    std::string			toString(void) const override;
    type_t			baseType(void) const override { return TypeWind; }
};

struct Clans;

struct Clan : Enum
{
    enum clan_t { None = 0, Red, Yellow, Aqua, Purple };

    Clan(clan_t v = None) : Enum(v) {}
    Clan(const std::string &);

    Clan 			prev(void) const;
    Clan 			next(void) const;

    std::string			toString(void) const override;
    std::string			canonicalName(void) const;
    type_t			baseType(void) const override { return TypeClan; }

    static Clan			random(void);
};

struct Clans : std::vector<Clan>
{
    Clans() {}
    Clans(size_t sz, const Clan & clan) : std::vector<Clan>(sz, clan) {}
    Clans(const std::vector<Clan> & v) : std::vector<Clan>(v) {}

};

struct Ability : Enum
{
    enum ability_t { None, Monacle, Catasrophic, Bard, Luck, Telepath };

    Ability(ability_t v = None) : Enum(v) {}
    Ability(const std::string &);

    std::string			toString(void) const override;
    type_t			baseType(void) const override { return TypeAbility; }

};

struct Spell;

struct Speciality : Enum
{
    enum speciality_t { None = 0,
	    Swarm, Merge, Invisibility, Regeneration, CastHellblast, MagicResistence,
	    MightyBlow, Gate, FirstStrike, SeeInvisible, IgnoreMissiles, Devotion,
	    FireShield, CastDrawNumber, CastDrawSword, CastDrawSkull, CastRandomDiscard,
	    CastSilence, CastScryRunes, CastManaFog, RangerAttack };

    Speciality(speciality_t v = None) : Enum(v) {}
    Speciality(const std::string &);

    std::string			toString(void) const override;
    type_t			baseType(void) const override { return TypeSpeciality; }
    int				index(void) const override;
    Spell			toSpell(void) const;

    static Speciality		fromIndex(int);
};

struct SpecialityDevotion : Speciality
{
    SpecialityDevotion() : Speciality(Speciality::Devotion) {}
    SpecialityDevotion(const Speciality & spec) : Speciality(spec) {}

    int restore(void) const;
};

struct SpecialityMagicResistence : Speciality
{
    SpecialityMagicResistence() : Speciality(Speciality::MagicResistence) {}
    SpecialityMagicResistence(const Speciality & spec) : Speciality(spec) {}

    int chance(int creatureId) const;
};

struct SpecialityMightyBlow : Speciality
{
    SpecialityMightyBlow() : Speciality(Speciality::MightyBlow) {}
    SpecialityMightyBlow(const Speciality & spec) : Speciality(spec) {}

    int chance(void) const;
    int strength(void) const;
};

struct Specials : std::bitset<32>
{
    Specials(unsigned long v = 0) : std::bitset<32>(v) {}
    Specials(const StringList &);

    void		set(const Speciality &);
    void		reset(const Speciality &);
    bool		check(const Speciality &) const;

    Specials & operator<< (const Speciality &);
    std::list<Speciality> toList(void) const;

    static Specials allCastSpells(void);
};

struct Avatars;

struct Avatar : Enum
{
    enum avatar_t { None, Orachi, Lakkho, Dayla, Ziag, Niana, Kierac, Logun, Nucrus, Javed, Random };

    Avatar(avatar_t v = None) : Enum(v) {}
    Avatar(const std::string &);

    std::string			toString(void) const override;
    type_t			baseType(void) const override { return TypeAvatar; }

    static Avatar		random(void);
};

struct Avatars : std::vector<Avatar>
{
    Avatars() {}
    Avatars(size_t sz, const Avatar & avatar) : std::vector<Avatar>(sz, avatar) {}
    Avatars(const std::vector<Avatar> & v) : std::vector<Avatar>(v) {}
};

struct Spell : Enum
{
    enum spell_t { None, Smoke, DemonicCompulsion, MassPanic, Paralyze, Reduction, BattleFury, Guidance, ForceShield, DustCloud, Heroism,
	    MysticalFountain, MysticalFountainAttack, MysticalFountainRange, MysticalFountainLoyalty,
	    BlindAmbition, BrilliantLights, MagicalAura, Teleport, DispelMagic, LightningBolt, Healing, HellBlast, DrawSkull, DrawSword, DrawNumber,
	    RandomDiscard, ScryRunes, Silence, ManaFog, MassDispel };

    Spell(spell_t v = None) : Enum(v) {}
    Spell(const std::string &);

    std::string			toString(void) const override;
    type_t			baseType(void) const override { return TypeSpell; }
};

struct Spells : std::vector<Spell>
{
    Spells() {}
    Spells(const std::vector<Spell> & v) : std::vector<Spell>(v) {}
};

struct SpellTarget : Enum
{
    enum { None, Friendly = 0x01, Enemy = 0x02, Any = Friendly | Enemy, Party = 0x04, Land = Any | Party, MyPlayer = 0x10, OtherPlayer = 0x20, AllPlayers = MyPlayer | OtherPlayer };

    SpellTarget(int v = None) : Enum(v) {}
    SpellTarget(const std::string &);

    int				index(void) const override;
    std::string			toString(void) const override;
    type_t			baseType(void) const override { return TypeSpellTarget; }
};

struct Creature : Enum
{
    enum creature_t { None, SkeletonHorde, Shadow, Durlock, KilorCelsbane, Wraith, MazRa, KnightTemplar, GreatCarol, Minotaur, AdventureParty, FireGiant, KingDrago, SandWraith, StoneGolem, LearraSiren,
	    ThunderBird, KiLin, Shanahan, WhiteDragon, GreenDragon, RedDragon, FireElemental, EarthElemental, AirElemental, WaterElemental, Juggernaut, Tornado, Griffon, Chameleon };

    Creature(creature_t v = None) : Enum(v) {}
    Creature(const std::string &);

    std::string			toString(void) const override;
    type_t			baseType(void) const override { return TypeCreature; }
};

struct Creatures : std::vector<Creature>
{
    Creatures() {}
    Creatures(const std::vector<Creature> & v) : std::vector<Creature>(v) {}
};

struct Stone;
struct WinRule;

struct VecStones : std::vector<Stone>
{
    std::string			toString(void) const;
    JsonArray                   toJsonArray(void) const;
    static VecStones            fromJsonArray(const JsonArray &);
};

struct Stones : std::vector<Stone>
{
    Stones &			operator<< (const Stone & st) { push_back(st); return *this; }

    Stones			toUnique(void) const;
    Stones			findPairs(void) const;
    Stones			findChaoVariants(const Stone &) const;
    bool			findStone(const Stone &) const;
    bool			findWinRule(const WinRule &) const;
    bool			removeWinRule(const WinRule &);
    int				countStone(const Stone &) const;
    bool			removeStone(const Stone &);
    bool			isChaoStone(const Stone &, bool) const;
    bool			haveKong(void) const;

    std::string			toString(void) const;
    JsonArray			toJsonArray(void) const;
    static Stones		fromJsonArray(const JsonArray &);
};

namespace StoneType
{
    enum { None = 0, IsSkull = 0x01, IsSword = 0x02, IsNumber = 0x04, IsWind = 0x08, IsDragon = 0x10,
		IsSpecial = IsWind | IsDragon, IsLucky = IsDragon, IsHonor = IsSpecial, IsTerminal = 0x80 };
}

struct Stone : Enum
{
    BitFlags			flag;

    enum stone_t { None = 0,
	Skull1 = 11, Skull2 = 12, Skull3 = 13, Skull4 = 14, Skull5 = 15, Skull6 = 16, Skull7 = 17, Skull8 = 18, Skull9 = 19,
	Sword1 = 21, Sword2 = 22, Sword3 = 23, Sword4 = 24, Sword5 = 25, Sword6 = 26, Sword7 = 27, Sword8 = 28, Sword9 = 29,
	Number1 = 31, Number2 = 32, Number3 = 33, Number4 = 34, Number5 = 35, Number6 = 36, Number7 = 37, Number8 = 38, Number9 = 39,
	Wind1 = 41, Wind2 = 42, Wind3 = 43, Wind4 = 44, WindEast = Wind1, WindSouth = Wind2, WindWest = Wind3, WindNorth = Wind4,
	Dragon1 = 51, Dragon2 = 52, Dragon3 = 53, WhiteDragon = Dragon1, GreenDragon = Dragon2, RedDragon = Dragon3 };

    enum { IsNew = 0x80000000, IsCasted = 0x40000000 };

    Stone(stone_t v = None);
    Stone(const std::string &);

    std::string			toString(void) const override;
    type_t			baseType(void) const override { return TypeStone; }

    int				index(void) const override;
    int				order(void) const { return id() % 10; }
    int				stoneType(void) const;

    Stone			prev(void) const;
    Stone			next(void) const;

    bool			isSkull(void) const { return StoneType::IsSkull & stoneType(); }
    bool			isSword(void) const { return StoneType::IsSword & stoneType(); }
    bool			isNumber(void) const { return StoneType::IsNumber & stoneType(); }
    bool			isWind(void) const { return StoneType::IsWind & stoneType(); }
    bool			isWind(const Wind &) const;
    bool			isDragon(void) const { return Dragon1 <= id() && id() <= Dragon3; }
    bool			isSpecial(void) const { return StoneType::IsSpecial & stoneType(); }
    bool			isLucky(void) const { return StoneType::IsLucky & stoneType(); }
    bool			isHonor(void) const { return StoneType::IsHonor & stoneType(); }
    bool			isTerminal(void) const { return StoneType::IsTerminal & stoneType(); }
};

struct GameStone : Stone
{
    GameStone() {}
    GameStone(const Stone & v) : Stone(v) {}
    GameStone(const Stone & v, bool cast) : Stone(v) { setCasted(cast); }

    bool			operator< (const GameStone & gs) const { return id() == gs.id() ? isCasted() > gs.isCasted() : id() < gs.id(); }
    bool			operator==(const GameStone & gs) const { return id() == gs.id() && isCasted() == gs.isCasted(); }
    bool			operator==(const Stone & st) const { return id() == st.id(); }

    void			setNewStone(bool f) { flag.set(IsNew, f); }
    void			setCasted(bool f) { flag.set(IsCasted, f); }

    bool			isNewStone(void) const { return flag.check(IsNew); }
    bool			isCasted(void) const { return flag.check(IsCasted); }
    static bool			isCasted(const Stone & st) { return GameStone(st).isCasted(); }

    JsonObject			toJsonObject(void) const;
    static GameStone		fromJsonObject(const JsonObject &);
};

struct GameStones : Stones
{
    void			push_back(const GameStone & stone) { Stones::push_back(stone); }

public:
    GameStones() { reserve(68); }

    void			add(const GameStone &); // sorted push_back
    Stone			del(int);
    bool			find(const WinRule &) const;
    void			remove(const WinRule &);

    bool			allowCast(const Stones &, const GameStone &) const;
    bool			allowCast(const Stones &) const;
    void			setCasted(const Stones &, GameStone &);

    std::string			toString(void) const;

    JsonArray			toJsonArray(void) const;
    static GameStones		fromJsonArray(const JsonArray &);
};

struct Land : Enum
{
    enum land_t { None, TowerOf4Winds, Maithaius, Baliphon, Vermille, Sulanthia, Trojensek, Talon, Siramak, Ronzinol, Corzen, Greenbaw, Zubrus,
	Corimar, Inkartha, Hexan, Firland, Vesna, Kern, RegencyPeaks, Knighthaven, Rikter, Gorok, Suntura, Bertram, Mastenbroek, Reisse, Cirrus,
	Grosek, Chahrr, Saugrezz, Mahnjeet, Trassk, Bechnarr, Azuria, PyramusReach, SerenityPlains, Sunspot, CharmersExpanse, GambitsRun,
	AshPoint, OrchidBay, Mocklebury, CelestialWood, TortoiseIsle, SiphonsChute };

    Land(land_t v = None) : Enum(v) {}
    Land(const std::string &);

    std::string			toString(void) const override;
    type_t			baseType(void) const override { return TypeLand; }

    bool			isPower(void) const;
    bool                        isTowerWinds(void) const;
};

struct Lands : std::vector<Land>
{
    Lands() { reserve(50); }
    Lands(const std::vector<Land> & v) : std::vector<Land>(v) {}

    Lands &			operator<< (const Land & st) { push_back(st); return *this; }
    Lands			powerOnly(void) const;

    static Lands 		pathfind(const Land &, const Land &);
    static Lands		thisClan(const Clan &);
    static Lands		enemyAroundOnly(const Clan &);

    std::string			toString(void) const;
};

struct BaseStat
{
    int				attack;
    int				ranger;
    int				defense;
    int				loyalty;

    BaseStat(int a, int r, int d, int l) : attack(a), ranger(r), defense(d), loyalty(l) {}
    BaseStat() : attack(0), ranger(0), defense(0), loyalty(0) {}

    BaseStat &			operator+= (const BaseStat &);
    BaseStat &			operator-= (const BaseStat &);

    std::string			toString(void) const;
};

struct CreatureStat : BaseStat
{
    int				move;

    CreatureStat() : move(0) {}
    CreatureStat(const BaseStat & bs) : BaseStat(bs), move(0) {}
};

struct TownStat : BaseStat
{
    int				point;
    bool			power;

    TownStat() : point(0), power(false) {}
    TownStat(const BaseStat & bs) : BaseStat(bs), point(0), power(false) {}
};

struct BattleStat : std::pair<int, int>
{
    BattleStat() {}
    BattleStat(int v1, int v2) : std::pair<int, int>(v1, v2) {}

    int 			base(void) const { return first; }
    int 			current(void) const { return second; }

    void			set(int v) { first = v; second = v; }
    void			reset(void) { second = first; }

    BattleStat &		operator+ (int v) { second += v; return *this; }
    BattleStat &		operator- (int v) { second -= v; return *this; }

    BattleStat &		operator+= (int v) { second += v; return *this; }
    BattleStat &		operator-= (int v) { second -= v; return *this; }

    std::string			toString(void) const;

    JsonArray			toJsonArray(void) const;
    static BattleStat		fromJsonArray(const JsonArray &);
};

class BattleUnit
{
protected:
    BattleStat			stat1; /* melee */
    BattleStat			stat2; /* ranged */
    BattleStat			stat3; /* defense */
    BattleStat			stat4; /* loyalty */

public:
    BattleUnit() {}
    BattleUnit(const BaseStat & bs) : stat1(bs.attack, bs.attack), stat2(bs.ranger, bs.ranger), stat3(bs.defense, bs.defense), stat4(bs.loyalty, bs.loyalty) {}

    bool			operator< (const BattleUnit & bu) const { return battleUnit() < bu.battleUnit(); }
    bool			operator> (const BattleUnit & bu) const { return battleUnit() > bu.battleUnit(); }
    bool			operator== (const BattleUnit & bu) const { return battleUnit() == bu.battleUnit(); }
    bool			operator!= (const BattleUnit & bu) const { return battleUnit() != bu.battleUnit(); }

    int				baseAttack(void) const { return stat1.base(); }
    int				baseRanger(void) const { return stat2.base(); }
    int				baseDefense(void) const { return stat3.base(); }
    int				baseLoyalty(void) const { return stat4.base(); }

    virtual int			attack(void) const { return stat1.current(); }
    virtual int			ranger(void) const { return stat2.current(); }
    virtual int			defense(void) const { return stat3.current(); }
    virtual int			loyalty(void) const { return stat4.current(); }

    virtual bool		haveSpeciality(const Speciality &) const { return false; }
    virtual bool		isTown(void) const { return false; }
    virtual bool		isCreature(void) const { return false; }
    virtual bool		isValid(void) const { return false; }
    virtual std::string		name(void) const { return "unknown"; }
    virtual bool		applySpell(const Spell &) { return false; }
    virtual int			battleUnit(void) const { return -1; }
    virtual Clan		clan(void) const { return Clan(); }

    bool			isBattleUnit(int id) const { return id == battleUnit(); }
    bool			isAlive(void) const { return 0 < loyalty(); }
    bool			isRanger(void) const { return 0 < ranger(); }
    void			applyDamage(int value) { if(0 < value) stat4 -= value; }
    void			applyStats(const BaseStat &);

    std::string			toString(void) const;

    JsonObject			toJsonObject(void) const;
    static BattleUnit		fromJsonObject(const JsonObject &);
};

struct AffectedSpell : Spell
{
    static constexpr int	Permanent = -1;

    int				duration;
    Avatar			source;

    AffectedSpell() : duration(0) {}
    AffectedSpell(const Spell &);
    AffectedSpell(const Spell & sp, int duration);
    AffectedSpell(const Spell & sp, int duration, const Avatar & source);

    void			actionReduceDuration(void);

    bool			operator== (const Spell & sp) const { return id() == sp(); }
    bool			isPermanent(void) const { return Permanent == duration; }
    bool			isValid(void) const { return 0 != duration; }

    JsonObject			toJsonObject(void) const;
    static AffectedSpell	fromJsonObject(const JsonObject &);
};

struct AffectedSpells : std::vector<AffectedSpell>
{
    AffectedSpells() { reserve(10); }

    bool			isAffected(const Spell &) const;
    bool			isAffected(const Spell &, const Avatar &) const;

    int				attack(void) const;
    int				ranger(void) const;
    int				defense(void) const;
    int				loyalty(void) const;

    void			advanceAdventureRound(void);
    void			insert(const AffectedSpell &);
    void			spellAffected(const Spell &);

    JsonArray			toJsonArray(void) const;
    static AffectedSpells	fromJsonArray(const JsonArray &);
};

class CreatureSkill : public BattleUnit
{
protected:
    BattleStat			stat5; /* move */

    int				move(void) const { return stat5.current(); }

public:
    CreatureSkill() {}
    CreatureSkill(const BattleUnit & bs) : BattleUnit(bs) {}
    CreatureSkill(const CreatureStat & cs) : BattleUnit(cs), stat5(cs.move, cs.move) {}

    int				baseMove(void) const { return stat5.base(); }
    int				freeMovePoint(void) const { return move(); }

    bool			canMove(int steps) const { return 0 < steps && steps <= freeMovePoint(); }
    void			moved(int steps);

    void			initMahjongPart(const Ability &, const Specials &, const AffectedSpells &);
    void			initAdventurePart(const Ability &, const Specials &, const AffectedSpells &);

    bool			isTown(void) const override { return false; }
    bool			isCreature(void) const override { return true; }
    std::string			name(void) const override { return "skill"; }
    int				battleUnit(void) const override { return -1; }

    std::string			toString(void) const;
    JsonObject			toJsonObject(void) const;
    static CreatureSkill	fromJsonObject(const JsonObject &);
};

class BattleTown : public BattleUnit
{
protected:
    Clan			previous;
    Land			territory;

protected:
    Clan			clan(void) const override { return currentClan(); }

public:
    BattleTown() {}
    BattleTown(const BattleUnit & bs, const Clan & clan, const Land & land) : BattleUnit(bs), previous(clan), territory(land) {}
    BattleTown(const Land &);

    bool			isTown(void) const override { return true; }
    bool			isCreature(void) const override { return false; }
    bool			isValid(void) const override { return territory.isValid(); }
    std::string			name(void) const override;
    int				battleUnit(void) const override { return territory() << 16; }
    //bool			applySpell(const Spell &) override;

    const Land &		land(void) const { return territory; }
    const Clan &		currentClan(void) const;
    const Clan &		previousClan(void) const;

    std::string			toString(void) const;
    JsonObject			toJsonObject(void) const;
    static BattleTown		fromJsonObject(const JsonObject &);
};

class BattleParty;

class BattleCreature : public Creature, public CreatureSkill
{
protected:
    int				buid;
    bool			selected;
    AffectedSpells		affected;
    Clan			owner;

public:
    BattleCreature() : buid(-1), selected(false) {  }
    BattleCreature(const Clan &, const Creature &, const CreatureSkill &, int battleUid = -1);
    BattleCreature(const Clan &, const Creature &, int battleUid);
    virtual ~BattleCreature() {}

    bool			operator== (const BattleCreature & other) const { return battleUnit() == other.battleUnit(); }
    bool			operator== (const Creature & cr) const { return cr.id() == id(); }

    bool			isSelected(void) const { return selected; }
    void			setSelected(bool f) { selected = f; }
    void			switchSelected(void) { selected = selected ? false : true; }
    bool			canMoveSelected(void) const;
    const AffectedSpells &	affectedSpells(void) const { return affected; }

    bool			isAffectedSpell(const Spell &) const;
    bool			canReceiveSpell(const Spell &) const;

    int				attack(void) const override;
    int				ranger(void) const override;
    int				defense(void) const override;
    int				loyalty(void) const override;
    bool			haveSpeciality(const Speciality &) const override;
    std::string			name(void) const override;
    bool			applySpell(const Spell &) override;

    int				battleUnit(void) const override { return buid; }
    Clan 			clan(void) const override { return owner; }

    void			initMahjongPart(const Ability &);
    void			initAdventurePart(const Ability &);
    int				freeMovePoint(void) const;
    bool			isValid(void) const override { return 0 < battleUnit() && Creature::isValid(); }

    std::string			toString(void) const override;
    JsonObject			toJsonObject(void) const;
    static BattleCreature	fromJsonObject(const JsonObject &);
};

struct BattleCreatures : std::vector<BattleCreature*>
{
    BattleCreatures() { reserve(6); }

    BattleCreatures &		operator<< (const BattleCreatures &);
    int				validSize(void) const;
};

struct CreatureMoved : std::pair<int /* battle creature uid */, Land /* innvalid: unit remove */>
{
    CreatureMoved() {}
    CreatureMoved(int uid, const Land & dst) : std::pair<int, Land>(uid, dst) {}
};

struct BattleTargets : std::vector<BattleUnit*>
{
    BattleTargets() {}
    BattleTargets(const BattleCreatures &);

    BattleTargets &		operator<< (const BattleUnit*);
    BattleTargets &		operator<< (const BattleTargets &);

    JsonArray			toJsonArray(void) const;
    static BattleTargets	fromJsonArray(const JsonArray &);
};

class BattleArmy;

class BattleParty : protected std::vector<BattleCreature>
{
    Land			position;
    Land			target;
    Clan			owner;

public:
    BattleParty() { resize(3); }
    BattleParty(const Clan &, const Land &);

    BattleCreatures		toBattleCreatures(const Specials &, bool filter) const;
    BattleCreatures		toBattleCreatures(void) const;
    BaseStat			toBaseStatSummary(void) const;

    std::string			toString(void) const;
    bool			isPosition(const Land & land) const { return land == position; }
    bool			canJoin(void) const;
    bool			join(const Creature &);
    bool			join(const BattleCreature &);
    bool			remove(const BattleCreature &);
    void			removeUnloyalty(void);
    int				count(void) const;
    int				movePoint(void) const;
    bool			isEmpty(void) const;
    void			dismiss(void);

    bool			findCreature(const Creature &) const;
    BattleCreature*		findBattleUnit(int);
    const BattleCreature*	findBattleUnitConst(int) const;
    const BattleCreature*	index(int) const;

    const Land &		land(void) const { return position; }
    const Clan &		clan(void) const { return owner; }

    void			setMoveTarget(const Land & land) { target = land; }
    const Land &		moveTarget(void) const { return target; }

    JsonObject			toJsonObject(void) const;
    static BattleParty		fromJsonObject(const JsonObject &);
};

struct Person;

class BattleArmy : public std::list<BattleParty>
{
public:
    BattleArmy() {}

    BattleParty*		findParty(const Land &);
    const BattleParty*		findPartyConst(const Land &) const;

    bool			findCreature(const Creature &) const;
    BattleCreature*		findBattleUnit(int);
    const BattleCreature*	findBattleUnitConst(int) const;

	    BattleCreatures		partySelected(const Land &) const;
	    void			partySetAllSelected(const Land &);
	    void			setAllSelected(void);

	    bool			canGateParty(const Land &, const Land &) const;
	    bool			canMoveCreature(const BattleCreature &, const Land &, const Lands &) const;
	    bool			moveCreature(const BattleCreature &, const Land &);
    std::vector<int>		moveSelectedCreatures(const Land &, const Land &);
    bool			teleportCreature(const BattleCreature &, const Land &);
    bool			isMaximumSummoning(void) const;

    BattleCreatures		toBattleCreatures(void) const;
    BattleCreatures		toBattleCreatures(const Specials &, bool filter) const;
    BattleTargets		toBattleTargets(const Clan &) const;

    bool			canJoin(const Land &) const;
    bool			join(const Creature &, const Land &);
    void			remove(const BattleCreature &);
    void			removeUnloyalty(void);
    void			applyInvisibility(const Clan & observer);
    void			shrinkEmpty(void);
    bool			isFullHouse(void) const;
    std::string			toString(void) const;

    Spells			allCastSpells(void) const;

    JsonArray			toJsonArray(void) const;
    static BattleArmy		fromJsonArray(const JsonArray &);
};

struct BattleStrike
{
    enum { None, Melee, Ranger, FireShield };

    int                 unit1;
    bool                is_creature1;
    int                 unit2;
    bool                is_creature2;
    int                 damage;
    int			type;

    BattleStrike() : unit1(0), is_creature1(false), unit2(0), is_creature2(false), damage(0), type(None) {}
    BattleStrike(const BattleUnit &, int, const BattleUnit &, int);

    JsonObject		toJsonObject(void) const;
    static BattleStrike	fromJsonObject(const JsonObject &);
};

struct BattleStrikes : std::list<BattleStrike>
{
    BattleStrikes() {}

    BattleStrikes & operator<< (const BattleStrike & strike) { push_back(strike); return *this; }
    BattleStrikes & operator<< (const BattleStrikes & strikes) { insert(end(), strikes.begin(), strikes.end()); return *this; }

    std::string			toString(void) const;

    JsonArray			toJsonArray(void) const;
    static BattleStrikes	fromJsonArray(const JsonArray &);
};

struct BattleLegend
{
    Avatar		attacker;
    BattleParty         attackers;
    Avatar		defender;
    BattleParty         defenders;
    BattleTown		town;
    bool		wins;

    BattleLegend() : wins(false) {}
    BattleLegend(const Avatar & h1, const BattleParty & p1, const Avatar & h2, const BattleParty & p2, const BattleTown & t, bool w)
	: attacker(h1), attackers(p1), defender(h2), defenders(p2), town(t), wins(w) {}

    const Land &	land(void) const { return town.land(); }
    std::string		toString(void) const;

    JsonObject		toJsonObject(void) const;
    static BattleLegend	fromJsonObject(const JsonObject &);
};

struct WinRule : std::pair<int, Stone>
{
    BitFlags			flags;

    enum { None, Chao, Pung, Kong, Game, Concealed = 0x40000000, Hidden = 0x80000000 };

    WinRule() : std::pair<int, Stone>(None, Stone::None) {}
    WinRule(int v, const Stone & s, bool concealed) : std::pair<int, Stone>(v, s) { if(concealed) flags.set(Concealed); }

    int				rule(void) const { return first; }
    const Stone &		stone(void) const { return second; }
    int				count(void) const { return isKong() ? 4 : 3; }

    bool			operator== (const Stone &) const;
    bool			isConcealed(void) const { return flags.check(Concealed); }
    bool			isHidden(void) const { return flags.check(Hidden); }
    bool			isOneChance(const Stone & st) const { return isConcealed() && isChao() && stone() == st; }

    std::string			toString(void) const;
    void			upgradeKong(void) { first = Kong; }

    bool			stoneType(int v) const { return stone().stoneType() & v; }
    bool			stoneOrder(int v) const { return stone().order() == v; }
    bool			stoneTerminal(void) const { return stone().isTerminal() && rule() != Chao; }
    bool			stoneHonor(void) const { return stone().isHonor(); }

    bool			isChaoStone(const Stone & st) const { return st == stone() && Chao == rule(); }
    bool			isPungStone(const Stone & st) const { return st == stone() && Pung == rule(); }
    bool			isKongStone(const Stone & st) const { return st == stone() && Kong == rule(); }

    bool			isChao(void) const { return Chao == rule(); }
    bool			isPung(void) const { return Pung == rule(); }
    bool			isKong(void) const { return Kong == rule(); }

    JsonObject                  toJsonObject(void) const;
    static WinRule		fromJsonObject(const JsonObject &);
};

struct WinRules : std::vector<WinRule>
{
    WinRules() { reserve(4); }

    std::string			toString(void) const;
    WinRules &                  operator<< (const WinRule & st) { push_back(st); return *this; }
    WinRules &                  operator<< (const WinRules & st) { insert(end(), st.begin(), st.end()); return *this; }

    int				count(void) const;
    int				countStoneType(int) const;

    WinRules			copy(int /* WinRule type */) const;
    static WinRules		fromStones(const Stones &);

    JsonArray			toJsonArray(void) const;
    static WinRules		fromJsonArray(const JsonArray &);
};

struct WindCompass
{
    Wind			wind;

    WindCompass() {}
    WindCompass(const Wind & w) : wind(w) {}

    Wind			right(void) const { return wind.next(); }
    Wind			top(void) const { return right().next(); }
    Wind			left(void) const { return top().next(); }
    Wind			bottom(void) const { return wind; }
};

struct RemotePlayer;

struct CroupierSet
{
    VecStones                   bank;
    VecStones                   trash;
    VecStones                   luckDraw;
    int                         last;

    CroupierSet();

    Stone                       get(RemotePlayer &);
    bool                        beginLuckDraw(void);
    bool                        hasLuckDraw(void) const { return luckDraw.size() == 2; }
    const VecStones &           luckChoices(void) const { return luckDraw; }
    Stone                       resolveLuckDraw(int);
    void                        reset(void);
    void                        reset(const RuneGameRuleset &);
    bool                        valid(void) const;
    void                        put(const Stone &);

    JsonObject			toJsonObject(void) const;
    static CroupierSet		fromJsonObject(const JsonObject &);
};

struct TypeValue : std::pair<int, int>
{
    TypeValue() {}
    TypeValue(int type, int value) : std::pair<int, int>(type, value) {}
    virtual ~TypeValue() {}

    int				type(void) const { return first; }
    int				value(void) const { return second; }

    bool			isType(int v) const { return type() == v; }
    virtual std::string		name(void) const { return ""; }
};

struct HandBonus : TypeValue
{
    enum { None, OneChance, SelfDrawn, AllConcealedWithDiscard };

    HandBonus() {}
    HandBonus(int type, int value) : TypeValue(type, value) {}

    std::string			name(void) const;
};

struct DoubleBonus : TypeValue
{
    enum { None, LuckySets, OneSuitHonors, NoPoints, FourTriplets, TerminalsHonors, TerminalHonorEachSet, AllSimples,
	    ThreeLittleDragons, OneSuitOnly, ThreeConsecutiveSequences, AllConcealedSelfDrawn, LastTileWall, ThreeConcealedTriplets,
	    RobbedKong, SupplementalTile, ThreeBigDragons, BigFourWinds, AllTerminals, AllHonors, NineGates, HeavenlyHand, FourConcealedTriplets,
	    ThirteenOrphans, LittleFourWinds, EarthlyHand };

    DoubleBonus() {}
    DoubleBonus(int type, int value) : TypeValue(type, value) {}

    std::string			name(void) const;
};

struct RuneBonus : TypeValue
{
    enum type_t { None, RunePung = 0x0001, RuneKong = 0x0002, RuneChao = 0x0004, RunePair = 0x0008, RuneSimple = 0x0010, RuneHonor = 0x0020, RuneTerminal = 0x0040, RuneLucky = 0x0080, RuneConcealed = 0x8000 };

    RuneBonus() {}
    RuneBonus(const Stone & st, int type, int value) : TypeValue(st.id() << 16 | type, value) {}

    Stones			stones(void) const;
    Stone			stone(void) const { return static_cast<Stone::stone_t>(type() >> 16); }

    bool			isPair(void) const { return type() & RunePair; }
    bool			isChao(void) const { return type() & RuneChao; }
    bool			isPung(void) const { return type() & RunePung; }
    bool			isKong(void) const { return type() & RuneKong; }
    bool			isHonor(void) const { return type() & RuneHonor; }
    bool			isLucky(void) const { return type() & RuneLucky; }
    bool			isTerminal(void) const { return type() & RuneTerminal; }
    bool			isSimple(void) const { return type() & RuneSimple; }
    bool			isConcealed(void) const { return type() & RuneConcealed; }
};

struct OpponentFine : TypeValue
{
    OpponentFine() {}
    OpponentFine(const Wind & wind, int value) : TypeValue(wind.id(), value) {}

    Wind			wind(void) const;
};

struct RuneBonusList : std::list<RuneBonus>
{
    RuneBonusList() {}
    RuneBonusList &             operator<< (const RuneBonus & st) { push_back(st); return *this; }
};

struct HandBonusList : std::list<HandBonus>
{
    HandBonusList() {}
    HandBonusList &             operator<< (const HandBonus & st) { push_back(st); return *this; }
};

struct DoubleBonusList : std::list<DoubleBonus>
{
    DoubleBonusList() {}
    DoubleBonusList &           operator<< (const DoubleBonus & st) { push_back(st); return *this; }
};

struct OpponentFinesList : std::list<OpponentFine>
{
    OpponentFinesList() {}
    OpponentFinesList &         operator<< (const OpponentFine & st) { push_back(st); return *this; }
};

struct WinResults
{
    Wind			dealWind;
    Wind			winWind;
    Wind			roundWind;
    Stone			pairStone;
    Stone			lastStone;
    BitFlags			flags;
    WinRules			rules;

    WinResults() {}
    WinResults(const Wind &, const Wind &, const Wind &, const WinRules &, const WinRules &, const Stone &, const Stone &);
    static WinResults		drawn(const Wind & dealWind, const Wind & roundWind);

    bool			isValid(void) const { return dealWind.isValid(); }
    bool			isDrawn(void) const { return ! lastStone.isValid(); }
    bool			isSelfDrawn(void) const { return winWind == dealWind; }
    bool                        noPoints(void) const;

    WinRules			winRulesConcealed(void) const;

    RuneBonusList		bonusRunes(void) const;
    HandBonusList		bonusHands(void) const;
    DoubleBonusList		bonusDoubles(void) const;
    OpponentFinesList		opponentFines(void) const;

    int				totalPoints(void) const;
    int                         totalPoints(const RuneGameRuleset &) const;
    int				scoreRules(void) const;
    int				totalScore(void) const;
    int                         totalScore(const RuneGameRuleset &) const;
    static int			scoreMultiplier(int doubles);

    int                         baseScore(void) const;
    int                         baseScore(const RuneGameRuleset &) const;
    int                         pairBonus(void) const;

    std::string			toString(void) const;

    JsonObject			toJsonObject(void) const;
    static WinResults		fromJsonObject(const JsonObject &);
};

struct Person
{
    enum { None = 0, AI = 0x0001, Casted = 0x0002, AdventurePartDone = 0x0004 };

    Avatar			avatar;
    Clan			clan;
    Wind			wind;
    BitFlags			flags;

    Person() {}
    Person(const Avatar & h, const Clan & c, const Wind & w) : avatar(h), clan(c), wind(w) {}

    bool			isWind(const Wind & w) const { return wind == w; }
    bool			isAvatar(const Avatar & h) const { return avatar == h; }
    bool			isClan(const Clan & c) const { return clan == c; }

    void			shiftWind(void) { wind.shift(); }

    bool			isCasted(void) const { return flags.check(Casted); }
    void			setCasted(bool f) { flags.set(Casted, f); }

    bool			isAI(void) const { return flags.check(AI); }
    void			setAI(bool f) { flags.set(AI, f); }

    std::string			name(void) const;
    std::string			toString(void) const;

    JsonObject			toJsonObject(void) const;
    static Person		fromJsonObject(const JsonObject &);
};

struct Persons : public std::vector<Person>
{
    Persons() { reserve(4); }
    Persons(const Person &);
};

struct LandClaims
{
    std::array<int, Clan::Purple + 1> values{};

    int                         points(const Clan &) const;
    void                        add(const Clan &, int);
    bool                        spend(const Clan &, int);

    JsonObject                  toJsonObject(void) const;
    static LandClaims           fromJsonObject(const JsonObject &);
};

struct RemotePlayer : public Person
{
    WinRules			rules;
    BattleArmy			army;
    AffectedSpells		affected;
    LandClaims                 landClaims;
    int				points;

    RemotePlayer();
    RemotePlayer(const Person &);

    Lands			lands(void) const;
    int                         landClaimPoints(const Clan & clan) const { return landClaims.points(clan); }
    void                        addLandClaimPoints(const Clan & clan, int value) { landClaims.add(clan, value); }
    bool                        spendLandClaimPoints(const Clan & clan, int value) { return landClaims.spend(clan, value); }
    void			initAdventurePart(void);
    bool			isAffectedSpell(const Spell &) const;
    bool			isAffectedSpell(const Spell &, const Avatar &) const;
    bool                        isSilenced(void) const;
    void			affectedSpellActivate(const Spell &);

    void			setAdventurePartDone(void);
    bool			adventurePartDone(void) const;

    bool			mahjongApplySpell(const Spell &, const Avatar & source = Avatar());
    BattleTargets		toBattleTargets(void) const;

    JsonObject			toJsonObject(void) const;
    static RemotePlayer		fromJsonObject(const JsonObject &);
};

struct LocalPlayer : public RemotePlayer
{
    GameStones			stones;
    GameStone                   newStone;

    LocalPlayer(const RemotePlayer & rp) : RemotePlayer(rp) {}
    LocalPlayer() {}

    Stone			setMahjongDrop(int);
    Stone                       setMahjongDrop(int, const RuneGameRuleset &);
    void			setMahjongChao(const Stone &, int);
    void                        setMahjongChao(const Stone &, int, const RuneGameRuleset &);
    void			setMahjongPung(const Stone &);
    void                        setMahjongPung(const Stone &, const RuneGameRuleset &);
    void			setMahjongKong1(const Stone &);
    void                        setMahjongKong1(const Stone &, const RuneGameRuleset &);
    void			setMahjongKong2(void);
    void                        setMahjongKong2(const RuneGameRuleset &);
    void			setMahjongGame(const WinResults &);
    void                        setMahjongGame(const WinResults &, const RuneGameRuleset &);

    bool			haveKong(void) const;
    bool			allowCastSpell(const Spell &) const;

    void			initMahjongPart(void);
    bool			newTurnEvent(CroupierSet &, bool skipnewStone);

    bool			isMahjongChao(const Wind &, const Stone &) const;
    bool                        isMahjongChao(const Wind &, const Stone &, const RuneGameRuleset &) const;
    bool			isMahjongPung(const Wind &, const Stone &) const;
    bool                        isMahjongPung(const Wind &, const Stone &, const RuneGameRuleset &) const;
    bool			isMahjongKong1(const Wind &, const Stone &) const;
    bool                        isMahjongKong1(const Wind &, const Stone &, const RuneGameRuleset &) const;
    bool			isMahjongKong2(const Wind &) const;
    bool                        isMahjongKong2(const Wind &, const RuneGameRuleset &) const;
    bool			isWinMahjong(const Wind &, const Wind &, const Stone &, WinResults* = nullptr) const;
    bool                        isWinMahjong(const Wind &, const Wind &, const Stone &,
                                             WinResults*, const RuneGameRuleset &) const;

    JsonObject			toJsonObject(void) const;
    static LocalPlayer		fromJsonObject(const JsonObject &);
};

struct LocalPlayers : public std::vector<LocalPlayer>
{
    void			setPersons(const Persons &);
    void			distributeStones(CroupierSet &);
    void                        distributeStones(CroupierSet &, const RuneGameRuleset &);
    void			shiftWinds(void);

    bool			findKongs(void) const;

    LocalPlayer*		playerOfAvatar(const Avatar &);
    LocalPlayer*		playerOfClan(const Clan &);
    LocalPlayer*		playerOfWind(const Wind &);
    const LocalPlayer*          playerOfAvatar(const Avatar &) const;
    const LocalPlayer*          playerOfClan(const Clan &) const;
    const LocalPlayer*          playerOfWind(const Wind &) const;

    JsonArray			toJsonArray(void) const;
    static LocalPlayers		fromJsonArray(const JsonArray &);
};


extern std::initializer_list<Clan::clan_t> clans_all;
extern std::initializer_list<Wind::wind_t> winds_all;
extern std::initializer_list<Land::land_t> lands_all;
extern std::initializer_list<Avatar::avatar_t> avatars_all;

#endif
