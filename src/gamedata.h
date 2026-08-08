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

#ifndef _RWNA_GAMEDATA_
#define _RWNA_GAMEDATA_

#include <list>
#include <array>

#include "actions.h"

namespace AI
{
    enum class Difficulty;
}

class RuneGameRuleset;

namespace Menu
{
    // Keep serialized gameplay part values stable; UI-only states belong at the end.
    enum
    {
        GameExit = 0,
        SelectPerson = 1,
        ShowPlayers = 2,
        MahjongPart = 3,
        MahjongSummaryPart = 4,
        AdventurePart = 5,
        BattleSummaryPart = 6,
        GameSummaryPart = 7,
        MahjongInitPart = 8,
        GameLoadPart = 9,
        MainMenu = 10,
        LoadRecovery = 11,
        SettingsMenu = 12,
        Encyclopedia = 13,
        ReplayLibrary = 14
    };
}

struct FileInfo
{
    std::string			id;
    std::string 		file;
    std::string                 fileEn;
    std::string                 fileRu;

    FileInfo() {}
};

struct FontInfo : public FileInfo
{
    int				size;
    CharRender                  blend;
    int                         style;
    CharHinting                 hinting;

    FontInfo() : size(0), blend(RenderBlended), style(StyleNormal), hinting(HintingNormal) {   }
};

struct ImageInfo : public FileInfo
{
    std::string 		colorkey;
    Rect			crop;

    ImageInfo() {}
    ImageInfo(const FileInfo & fi) : FileInfo(fi) {}
};

struct StoneInfo
{
    Stone			id;
    std::string 		name;
    std::string                 sourceName;
    std::string 		large;
    std::string 		medium;
    std::string 		small;

    StoneInfo() {}
};

struct WindInfo
{
    Wind			id;
    std::string 		name;
    std::string                 sourceName;
    std::string 		image;

    WindInfo() {}
};

struct ClanInfo
{
    Clan			id;
    std::string 		name;
    std::string                 sourceName;
    std::string 		image;
    std::string 		flag1;
    std::string 		flag2;
    std::string 		town;
    std::string 		button;
    std::string 		townflag1;
    std::string 		townflag2;

    ClanInfo() {}
};

struct SpecialityInfo
{
    Speciality			id;
    std::string			name;
    std::string 		description;
    std::string                 sourceName;
    std::string                 sourceDescription;

    SpecialityInfo() {}
};

struct CreatureInfo
{
    Creature			id;
    CreatureStat		stat;
    bool         		unique;
    bool         		fly;
    int				cost;
    Specials			specials;
    std::string 		name;
    std::string 		image1;
    std::string 		image2;
    std::string			description;
    std::string                 sourceName;
    std::string                 sourceDescription;
    std::string 		sound1;
    Stones			stones;

    CreatureInfo() : unique(false), fly(false), cost(0) {}
};

struct SpellInfo
{
    Spell			id;
    SpellTarget 		target;
    std::string 		name;
    std::string 		image;
    std::string 		description;
    std::string                 sourceName;
    std::string                 sourceDescription;
    std::string 		sound;
    Stones			stones;
    BaseStat			effect;
    bool         		persistent;
    int				cost;
    int				extval;
    int				value;

    SpellInfo() :  persistent(false), cost(0), extval(0), value(0) {}

    bool			isCurse(void) const { return persistent && (SpellTarget::Enemy & target()); }
    int				duration(void) const { return extval; }

    std::string			effectDescription(void) const;
};

struct AbilityInfo
{
    Ability			id;
    std::string 		name;
    std::string 		description;
    std::string                 sourceName;
    std::string                 sourceDescription;

    AbilityInfo() {}
};

struct AvatarInfo
{
    Avatar			id;
    std::string 		name;
    std::string 		dignity;
    std::string 		description;
    std::string                 sourceName;
    std::string                 sourceDignity;
    std::string                 sourceDescription;
    std::string 		portrait;
    std::string 		image;
    std::string                 aiProfile;
    Ability			ability;
    Clans			clans;
    Spells			spells;
    Creatures			creatures;

    AvatarInfo() : aiProfile("balanced") {}
    std::string			toStringClans(void) const;
};

struct LandInfo
{
    Land			id;
    Clan			clan;
    TownStat			stat;
    Point			center;
    Rect			area;
    Rect			iconrt;
    std::string 		name;
    std::string                 sourceName;
    std::vector<Land>		borders;
    Points			points;

    LandInfo() {}
};

struct LocalData
{
    VecStones			trashSet;
    std::array<LocalPlayer, 4>	players; // left, right, top, bottom
    int				stoneLastCount;

    Wind			roundWind;
    Wind			partWind;
    Wind			currentWind;
    WindCompass			compass;

    Stone			dropStone;
    WinResults			winResult;

    bool			newRound(void) const { return partWind() == Wind::East && stoneLastCount == GAME_STONE_MAX; }
    bool			yourTurn(void) const { return currentWind == compass.bottom(); }

    LocalPlayer &		myPlayer(void);
    const LocalPlayer &		myPlayer(void) const;

    const RemotePlayer &	playerCurrentWind(void) const;
    const RemotePlayer &	playerOfWind(const Wind &) const;
    const RemotePlayer &	playerOfClan(const Clan &) const;
    const RemotePlayer &	playerOfAvatar(const Avatar &) const;
    const BattleCreature*	findBattleUnitConst(int) const;

    const LocalPlayer &		remoteLeft(void) const { return players[0]; }
    const LocalPlayer &		remoteRight(void) const { return players[1]; }
    const LocalPlayer &		remoteTop(void) const { return players[2]; }
    const LocalPlayer &		remoteBottom(void) const { return players[3]; }

    Persons			toPersons(void) const;
};

struct ActionList : std::list<ActionMessage>
{
};

enum class ActionRejectReason
{
    None,
    UnknownAction,
    WrongPhase,
    WrongTurn,
    StaleSelection,
    Silenced,
    ManaFog,
    AlreadyCast,
    IllegalMahjongCall,
    InvalidRuneChoice,
    InvalidLand,
    InvalidTarget,
    MissingRunes,
    InsufficientPoints,
    InsufficientClaimPoints,
    ArmyFull,
    UniqueCreatureExists,
    PartyFull,
    UnitNotFound,
    IllegalMovement,
    IllegalLandClaim,
    NothingToUndo,
    NoPendingBattle,
    InvalidBattleChoice
};

struct ActionRejection
{
    ActionRejectReason reason = ActionRejectReason::None;
    int available = -1;
    int required = -1;

    bool isValid(void) const { return reason != ActionRejectReason::None; }
};

namespace GameData
{
    bool			init(const JsonObject &);
    void                        retranslateThemeData(void);

    LocalData                   toLocalData(const Avatar &);

    void			dumpOrderPersons(void);

    const StoneInfo &		stoneInfo(const Stone &);
    const WindInfo &		windInfo(const Wind &);
    const LandInfo &		landInfo(const Land &);
    const AvatarInfo &		avatarInfo(const Avatar &);
    const ClanInfo &		clanInfo(const Clan &);
    const AbilityInfo &		abilityInfo(const Ability &);
    const SpecialityInfo &	specialityInfo(const Speciality &);
    const CreatureInfo &	creatureInfo(const Creature &);
    const SpellInfo &		spellInfo(const Spell &);
    Avatars			avatarsOfClan(const Clan &);

    void			initPersons(const Person &);
    bool                        initPersons(const Persons &);

    bool			initMahjong(void);
    bool                        initMahjong(const RuneGameRuleset &);
    bool			mahjong2Client(const Avatar &, ActionList &);
    bool			client2Mahjong(const Avatar &, const ClientMessage &, ActionList &,
                                       ActionRejection* rejection = nullptr);

    bool			initAdventure(void);
    bool			adventure2Client(const Avatar &, ActionList &);
    bool			client2Adventure(const Avatar &, const ClientMessage &, ActionList &,
                                         ActionRejection* rejection = nullptr);
    const char*                 actionRejectReasonName(ActionRejectReason);
    std::string                 actionRejectionMessage(const ActionRejection &);
    bool                        canClaimLand(const RemotePlayer &, const Land &);
    Lands                       claimableLands(const RemotePlayer &);

    const Person &		myPerson(void);
    const Person &              currentPerson(void);
    const LocalPlayers &        players(void);
    Avatar                      localMahjongAvatar(void);
    Avatar                      localAdventureAvatar(void);

    AI::Difficulty              aiDifficulty(void);
    void                        setAIDifficulty(AI::Difficulty);
    bool                        usesAI(const Person &);
    bool                        isLocallyControlled(const Person &);
    bool                        allied(const Person &, const Person &);
    bool                        allied(const Clan &, const Clan &);
    int                         localControllerId(void);
    bool                        developerAssisted(void);

#ifdef BUILD_DEBUG
    bool                        developerAutoplay(const Avatar &);
    void                        setDeveloperAutoplay(const Avatar &, bool);
    bool                        initDeveloperBattleFixture(const Avatar &, ActionList &,
                                                           std::string* error = nullptr);
    bool                        initDeveloperFinalRuneFixture(const Avatar &,
                                                              std::string* error = nullptr);
    bool                        initDeveloperFinalAdventureFixture(const Avatar &,
                                                                   std::string* error = nullptr);
#endif

    bool			saveGame(const JsonObject &);
    bool                        saveNamedGame(const JsonObject &, const std::string &,
                                             bool overwrite, std::string* error = nullptr);
    bool                        archiveCurrentReplay(std::string* savedFile = nullptr,
                                                     std::string* error = nullptr);
    bool                        saveRecovery(const JsonObject &, const std::string & reason);
    JsonObject                  authoritativeState(void);
    bool                        restoreState(const JsonObject &);
    bool			loadGame(void);
    bool			loadGame(const std::string &);
    bool			isGameOver(void);
    bool                        isGameOver(const RuneGameRuleset &);

    int				loadedGamePart(void);
    void                        setGamePart(int);
    int				nextBattleUnitId(void);

    const JsonObject &		jsonGUI(void);

    bool			findCreatureUnique(const Creature &);
    BattleParty*		getBattleParty(int unit);
    BattleCreature*		findBattleCreature(int unit);
    BattleCreature*		getBattleCreature(int unit);
    RemotePlayer*		getBattleArmyOwner(const BattleArmy &);
    BattleArmy &		getBattleArmy(const Clan &);
    std::list<BattleLegend>	getBattleHistoryFor(const Avatar &);
};

#endif
