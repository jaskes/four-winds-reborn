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

        const JsonObject* jo2 = nullptr;

        person = loadedPerson;

        jo2 = jo.getObject("croupier");
        if(! jo2)
        {
            ERROR("json parse: " << "croupier");
            return false;
        }
        croupier = CroupierSet::fromJsonObject(*jo2);

        jo2 = jo.getObject("winresult");
        if(! jo2)
        {
            ERROR("json parse: " << "winresult");
            return false;
        }
        winResult = WinResults::fromJsonObject(*jo2);

        const JsonArray* ja2 = nullptr;

        gamers = std::move(loadedGamers);
        resetAdventureCommandState();
        pendingBattle = PendingBattle();

        // Older saves did not persist captured territory owners and keep theme defaults.
        jo2 = jo.getObject("landOwners");
        if(jo2)
        {
            for(auto landId : lands_all)
            {
                const Land land(landId);
                const Clan owner(jo2->getString(land.toString(), landInfo(land).clan.toString()));
                if(!land.isTowerWinds() && owner.isValid()) landsInfo[land()].clan = owner;
            }
        }

        battleHistory.clear();

        ja2 = jo.getArray("history");
        if(! ja2)
        {
            ERROR("json parse: " << "history");
            return false;
        }

        for(int it = 0; it < ja2->size(); ++it)
        {
            jo2 = ja2->getObject(it);
            if(jo2) battleHistory.push_back(BattleLegend::fromJsonObject(*jo2));
        }

        jo2 = jo.getObject("battleSession");
        if(jo2)
        {
            pendingBattle = PendingBattle::fromJsonObject(*jo2);
            if(!pendingBattle.isValid())
            {
                ERROR("invalid pending battle session");
                return false;
            }
        }

        stateGUI.clear();

        jo2 = jo.getObject("gui");
        if(jo2) stateGUI = *jo2;

        const JsonObject* rng = jo.getObject("gameplayRng");
        if(rng)
        {
            if(!GameplayRng::fromJsonObject(*rng))
            {
                ERROR("unsupported or invalid gameplay RNG state");
                return false;
            }
        }
        else
        {
            // Compatibility for saves created before deterministic gameplay RNG.
            GameplayRng::seedFromEntropy();
        }

        if(!selectActiveRuneGameRuleset(loadedRuleset.id, loadedRuleset.version,
                                        &validationError))
        {
            ERROR("invalid saved game: " << validationError);
            return false;
        }
        if(!selectActiveMatchTopology(loadedTopology.id, loadedTopology.version,
                                      &validationError))
        {
            ERROR("invalid saved game: " << validationError);
            return false;
        }

        return true;
    }

    bool restoreState(const JsonObject & state)
    {
        return fromJsonObject(state);
    }

    bool saveGame(const JsonObject & gui)
    {
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
