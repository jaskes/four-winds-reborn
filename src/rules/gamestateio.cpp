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

#include <ctime>
#include <set>

#include "settings.h"
#include "adventurecommands.h"
#include "aiprofile.h"
#include "battlesession.h"
#include "contentpackage.h"
#include "crashreport.h"
#include "gamedata.h"
#include "matchsession.h"
#include "gameplayrng.h"
#include "matchtopology.h"
#include "recovery.h"
#include "replayfiles.h"
#include "savegames.h"
#include "replay.h"
#include "runegameruleset.h"

#ifndef FOUR_WINDS_VERSION
#define FOUR_WINDS_VERSION "unknown"
#endif
#ifndef FOUR_WINDS_GAME_REVISION
#define FOUR_WINDS_GAME_REVISION "unknown"
#endif
#ifndef FOUR_WINDS_ENGINE_REVISION
#define FOUR_WINDS_ENGINE_REVISION "unknown"
#endif

namespace GameData
{
    extern Person                       person;
    extern LocalPlayers                 gamers;
    extern Wind                         currentWind;
    extern Wind                         roundWind;
    extern Wind                         partWind;
    extern CroupierSet                  croupier;
    extern int                          stoneLastCount;
    extern Stone                        dropStone;
    extern WinResults                   winResult;
    extern std::list<BattleLegend>       battleHistory;
    extern bool                         skipRepeatSay;
    extern bool                         skipNewStone;
    extern bool                         skipNewTurn;
    extern int                          gamePart;
    extern int                          battleUnitId;
    extern AI::Difficulty               difficulty;
    extern bool                         assistedByDeveloper;
#ifdef BUILD_DEBUG
    extern Avatar                       developerAutoplayAvatar;
#endif

    bool archiveCurrentReplay(std::string* savedFile, std::string* error)
    {
        if(Replay::actionJournalSize() == 0)
        {
            if(error) *error = "replay journal is empty";
            return false;
        }

        const JsonObject journal = Replay::actionJournal(GameData::authoritativeState());
        if(!journal.getBoolean("contiguousToCheckpoint"))
        {
            if(error) *error = "replay journal is not contiguous to the current state";
            return false;
        }

        std::string localError;
        std::string path;
        if(!ReplayFiles::writeAutomatic(journal, &path, &localError))
        {
            if(error) *error = localError;
            ERROR("automatic replay archive failed: " << localError);
            return false;
        }

        if(savedFile) *savedFile = path;
        if(error) error->clear();
        VERBOSE("automatic replay archived: " << path);
        return true;
    }
    extern JsonObject                   stateGUI;
    extern std::vector<LandInfo>        landsInfo;
    extern std::vector<Clan>            initialLandOwners;

    const char* recoveryPlatform(void)
    {
#if defined(__ANDROID__) || defined(ANDROID)
        return "android";
#elif defined(_WIN32)
        return "windows";
#elif defined(__APPLE__)
        return "macos";
#elif defined(__linux__)
        return "linux";
#else
        return "unknown";
#endif
    }

    JsonObject toJsonObject(const JsonObject & gui)
    {
        JsonObject jo;
        jo.addInteger("version", FORMAT_VERSION_CURRENT);
        jo.addObject(RuneGameRulesetIdentityKey,
                     runeGameRulesetIdentityJson(activeRuneGameRuleset()));
        jo.addObject(MatchTopologyIdentityKey,
                     matchTopologyIdentityJson(activeMatchTopology()));
        jo.addObject(ContentPackageIdentityKey,
                     contentPackageIdentityJson(activeContentPackageManifest()));
        jo.addString("wind:round", roundWind.toString());
        jo.addString("wind:part", partWind.toString());
        jo.addString("wind:current", currentWind.toString());
        jo.addInteger("lastcount", stoneLastCount);
        jo.addString("stone:drop", dropStone.toString());
        jo.addBoolean("skiprepeat", skipRepeatSay);
        jo.addBoolean("skipnew", skipNewStone);
        jo.addBoolean("skipturn", skipNewTurn);
        jo.addInteger("gamepart", gamePart);
        jo.addInteger("nextBattleUnitId", battleUnitId);
        jo.addString("ai:difficulty", AI::difficultyName(difficulty));
        jo.addBoolean("developerAssisted", assistedByDeveloper);
        jo.addObject("gameplayRng", GameplayRng::toJsonObject());

        jo.addObject("myperson", person.toJsonObject());
        jo.addObject("croupier", croupier.toJsonObject());
        jo.addObject("winresult", winResult.toJsonObject());
        jo.addArray("players", gamers.toJsonArray());

        JsonObject landOwners;
        for(auto landId : lands_all)
        {
            const Land land(landId);
            landOwners.addString(land.toString(), landInfo(land).clan.toString());
        }
        jo.addObject("landOwners", landOwners);

        JsonArray ja;
        for(auto & legend : battleHistory)
            ja.addObject(legend.toJsonObject());
        jo.addArray("history", ja);

        if(pendingBattle.isValid())
            jo.addObject("battleSession", pendingBattle.toJsonObject());

        if(gui.isValid()) jo.addObject("gui", gui);

        return jo;
    }

    JsonObject authoritativeState(void)
    {
        return toJsonObject(JsonObject());
    }

    bool fromJsonObject(const JsonObject & jo)
    {
        int version = jo.getInteger("version");

        if(version < FORMAT_VERSION_LAST || version > FORMAT_VERSION_CURRENT)
        {
            ERROR("unknown version: " << version << ", " <<
                "supported release: " << FORMAT_VERSION_LAST);
            return false;
        }

        std::string validationError;
        if(!Recovery::validateSaveState(jo, &validationError))
        {
            ERROR("invalid saved game: " << validationError);
            return false;
        }

        RuneGameRulesetIdentity loadedRuleset;
        if(!resolveRuneGameRulesetIdentity(jo, loadedRuleset, true, &validationError))
        {
            ERROR("invalid saved game: " << validationError);
            return false;
        }

        MatchTopologyIdentity loadedTopology;
        if(!resolveMatchTopologyIdentity(jo, loadedTopology, true, &validationError))
        {
            ERROR("invalid saved game: " << validationError);
            return false;
        }

        const JsonObject* loadedPersonObject = jo.getObject("myperson");
        const JsonArray* loadedPlayersArray = jo.getArray("players");
        const Person loadedPerson = Person::fromJsonObject(*loadedPersonObject);
        LocalPlayers loadedGamers = LocalPlayers::fromJsonArray(*loadedPlayersArray);

        std::set<int> loadedAvatars;
        std::set<int> loadedClans;
        std::set<int> loadedWinds;
        bool rosterIsValid = loadedPerson.avatar.isValid() &&
            loadedPerson.avatar != Avatar(Avatar::Random) && loadedPerson.clan.isValid();
        bool matchingLocalPlayer = false;
        for(const LocalPlayer & player : loadedGamers)
        {
            const bool validIdentity = player.avatar.isValid() &&
                player.avatar != Avatar(Avatar::Random) && player.clan.isValid() &&
                player.wind.isValid();
            if(!validIdentity || !loadedAvatars.insert(player.avatar()).second ||
               !loadedClans.insert(player.clan()).second ||
               !loadedWinds.insert(player.wind()).second)
            {
                rosterIsValid = false;
                break;
            }

            if(player.avatar == loadedPerson.avatar && player.clan == loadedPerson.clan)
                matchingLocalPlayer = true;
        }

        if(loadedGamers.size() != static_cast<std::size_t>(
               findMatchTopology(loadedTopology.id, loadedTopology.version)->seatCount()) ||
           !rosterIsValid || !matchingLocalPlayer)
        {
            ERROR("invalid saved game: player roster is incomplete or inconsistent");
            return false;
        }

        // Decode everything before committing. A failed load must leave the
        // previous game and its command/undo state usable.
        CroupierSet loadedCroupier = CroupierSet::fromJsonObject(*jo.getObject("croupier"));
        WinResults loadedWinResult = WinResults::fromJsonObject(*jo.getObject("winresult"));
        std::list<BattleLegend> loadedHistory;
        const JsonArray* history = jo.getArray("history");
        for(std::size_t index = 0; index < history->size(); ++index)
            loadedHistory.push_back(BattleLegend::fromJsonObject(*history->getObject(index)));

        PendingBattle loadedBattle;
        if(const JsonObject* battle = jo.getObject("battleSession"))
            loadedBattle = PendingBattle::fromJsonObject(*battle);

        JsonObject loadedGUI;
        if(const JsonObject* gui = jo.getObject("gui")) loadedGUI = *gui;

        // Saves predating persisted captures start from the theme's map, not
        // ownership left behind by a previous match (possibly a Duel).
        std::vector<Clan> loadedOwners = initialLandOwners;
        if(const JsonObject* owners = jo.getObject("landOwners"))
        {
            for(const auto landId : lands_all)
            {
                const Land land(landId);
                if(!land.isTowerWinds() && owners->hasKey(land.toString()))
                    loadedOwners[land()] = Clan(owners->getString(land.toString()));
            }
        }

        // All possible validation failures precede the first live mutation.
        if(const JsonObject* rng = jo.getObject("gameplayRng"))
        {
            if(!GameplayRng::fromJsonObject(*rng)) return false;
        }
        else GameplayRng::seedFromEntropy();

        selectActiveRuneGameRuleset(loadedRuleset.id, loadedRuleset.version);
        selectActiveMatchTopology(loadedTopology.id, loadedTopology.version);
        VERBOSE("load gamedata, version: " << version);

        stoneLastCount = jo.getInteger("lastcount");
        roundWind = Wind(jo.getString("wind:round"));
        partWind = Wind(jo.getString("wind:part"));
        currentWind = Wind(jo.getString("wind:current"));
        dropStone = Stone(jo.getString("stone:drop"));
        skipNewStone = jo.getBoolean("skipnew");
        skipNewTurn = jo.getBoolean("skipturn");
        skipRepeatSay = false; // initial say need! jo.getBoolean("skiprepeat");
        gamePart = jo.getInteger("gamepart");
        battleUnitId = jo.getInteger("nextBattleUnitId");
        difficulty = AI::difficultyFromString(jo.getString("ai:difficulty", "normal"));
        assistedByDeveloper = jo.getBoolean("developerAssisted", false);
#ifdef BUILD_DEBUG
        developerAutoplayAvatar = Avatar();
#endif

        person = loadedPerson;
        croupier = std::move(loadedCroupier);
        winResult = std::move(loadedWinResult);
        gamers = std::move(loadedGamers);
        resetAdventureCommandState();
        pendingBattle = std::move(loadedBattle);
        for(std::size_t index = 0; index < loadedOwners.size(); ++index)
            landsInfo[index].clan = loadedOwners[index];
        battleHistory = std::move(loadedHistory);
        stateGUI = std::move(loadedGUI);

        return true;
    }

    bool restoreState(const JsonObject & state)
    {
        return fromJsonObject(state);
    }

    bool saveGame(const JsonObject & gui)
    {
        if(Multiplayer::session().active()) return true;
        const std::string & share = Settings::shareDir();
        if(!Systems::isDirectory(share)) Systems::makeDirectory(share);
        Display::renderScreenshot(Settings::fileSave("game.png"));
        std::string error;
        const bool saved = SaveGames::writeAutosave(Settings::fileSaveGame(),
                                                    GameData::toJsonObject(gui), &error);
        if(!saved) {
            ERROR("autosave failed: " << error);
        }
        else {
            archiveCurrentReplay();
        }
        return saved;
    }

    bool saveNamedGame(const JsonObject & gui, const std::string & name,
                       bool overwrite, std::string* error)
    {
        if(Multiplayer::session().active())
        {
            if(error) *error = "A network match cannot be saved as a local single-player game.";
            return false;
        }
        std::string savedFile;
        if(!SaveGames::writeManual(GameData::toJsonObject(gui), name, overwrite, &savedFile, error))
            return false;

        if(4 < savedFile.size() && savedFile.substr(savedFile.size() - 4) == ".sav")
            Display::renderScreenshot(savedFile.substr(0, savedFile.size() - 4) + ".png");
        archiveCurrentReplay();
        return true;
    }

    bool saveRecovery(const JsonObject & gui, const std::string & reason)
    {
        if(Multiplayer::session().active()) return true;
        if(!Recovery::enabled()) return true;
        if(gamers.empty()) return false;

        CrashReport::breadcrumb(std::string("Recovery stage=begin reason=").append(reason));
        const JsonObject saveState = GameData::toJsonObject(gui);
        const std::string saveData = saveState.toString();

        JsonObject metadata;
        metadata.addInteger("schema", 1);
        metadata.addInteger("saveFormat", FORMAT_VERSION_CURRENT);
        metadata.addObject(RuneGameRulesetIdentityKey,
                           runeGameRulesetIdentityJson(activeRuneGameRuleset()));
        metadata.addObject(MatchTopologyIdentityKey,
                           matchTopologyIdentityJson(activeMatchTopology()));
        metadata.addObject(ContentPackageIdentityKey,
                           contentPackageIdentityJson(activeContentPackageManifest()));
        metadata.addString("savedAtEpoch", std::to_string(static_cast<long long>(std::time(nullptr))));
        metadata.addString("reason", reason);
        metadata.addString("platform", recoveryPlatform());
        metadata.addString("gameVersion", FOUR_WINDS_VERSION);
        metadata.addString("gameRevision", FOUR_WINDS_GAME_REVISION);
        metadata.addString("engineRevision", FOUR_WINDS_ENGINE_REVISION);
        metadata.addString("fileHashFNV1a64", Recovery::stateHash(saveData));
        metadata.addString("stateHashFNV1a64", Recovery::stateHash(saveState));
        metadata.addObject("gameplayRng", GameplayRng::toJsonObject());
        metadata.addObject("replay", Replay::actionJournal(GameData::authoritativeState()));
        metadata.addInteger("stateBytes", static_cast<int>(saveData.size()));
        metadata.addInteger("gamePart", gamePart);
        metadata.addString("roundWind", roundWind.toString());
        metadata.addString("partWind", partWind.toString());
        metadata.addString("currentWind", currentWind.toString());
        metadata.addString("aiDifficulty", AI::difficultyName(difficulty));
        metadata.addString("breadcrumbSequence", std::to_string(CrashReport::breadcrumbSequence()));
        metadata.addString("crashReport", CrashReport::filePath());

        JsonArray breadcrumbs;
        for(const std::string & entry : CrashReport::recentBreadcrumbs())
            breadcrumbs.addString(entry);
        metadata.addArray("recentBreadcrumbs", breadcrumbs);

        const std::string directory = Recovery::defaultDirectory();
        const bool saved = Recovery::writeCheckpoint(directory, saveData, metadata);
        CrashReport::breadcrumb(std::string("Recovery stage=end reason=").append(reason)
            .append(" status=").append(saved ? "ok" : "failed")
            .append(" directory=").append(directory));
        return saved;
    }

    bool loadGame(void)
    {
        return loadGame(Settings::fileSaveGame());
    }

    bool loadGame(const std::string & fn)
    {
        std::string str;
        std::string validationError;

        if(Recovery::validateSaveFile(fn, &validationError, &str))
        {
            const bool loaded = fromJsonObject(JsonContentString(str).toObject());
            if(loaded) Replay::clearActionJournal();
            return loaded;
        }

        ERROR("saved game rejected: " << validationError);
        return false;
    }
}
