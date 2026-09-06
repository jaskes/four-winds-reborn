#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "gamedata.h"
#include "aiprofile.h"
#include "gameplayrng.h"
#include "matchscore.h"
#include "matchtopology.h"
#include "recovery.h"
#include "replay.h"
#include "runegameruleset.h"
#include "savegames.h"
#include "simulation.h"

namespace GameData
{
    extern LocalPlayers gamers;
    extern JsonObject stateGUI;
}

namespace
{
    struct EditableObject : JsonObject
    {
        explicit EditableObject(const JsonObject & source) : JsonObject(source) {}
        void erase(const std::string & key) { content.erase(key); }
    };
}

int runStateIntegrityTests()
{
    if(!std::getenv("FOUR_WINDS_SAVE_DIR"))
    {
        std::cerr << "FAIL: isolated save directory is required\n";
        return 1;
    }
    int failures = 0;
    const auto check = [&](bool valid, const std::string & message) {
        if(!valid) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
    };
    Recovery::setEnabled(false);
    selectActiveRuneGameRuleset(QuickRuneGameRulesetId, QuickRuneGameRulesetVersion);
    selectActiveMatchTopology(ClassicFreeForAllTopologyId, ClassicFreeForAllTopologyVersion);
    GameplayRng::seed(605001);
    GameData::initPersons(Person(Avatar::Nucrus, Clan::Red, Wind::East));
    check(GameData::initMahjong(), "initialize source save");
    const JsonObject source = GameData::authoritativeState();

    JsonObject aliasSave = source;
    JsonObject aliasPerson = *source.getObject("myperson");
    aliasPerson.addString("clan", "Maitha");
    aliasSave.addObject("myperson", aliasPerson);
    JsonArray aliasPlayers;
    for(std::size_t index = 0; index < source.getArray("players")->size(); ++index)
    {
        JsonObject player = *source.getArray("players")->getObject(index);
        if(player.getString("clan") == "red") player.addString("clan", "Maitha");
        aliasPlayers.addObject(player);
    }
    aliasSave.addArray("players", aliasPlayers);
    check(Recovery::validateSaveState(aliasSave) && GameData::restoreState(aliasSave),
          "legacy clan aliases remain compatible with canonical island ownership");

    EditableObject duplicateSave(source);
    duplicateSave.erase("landOwners");
    JsonArray duplicatePlayers;
    for(std::size_t index = 0; index < source.getArray("players")->size(); ++index)
    {
        JsonObject player = *source.getArray("players")->getObject(index);
        if(player.getString("clan") == "purple") player.addString("clan", "Maitha");
        duplicatePlayers.addObject(player);
    }
    duplicateSave.addArray("players", duplicatePlayers);
    check(!Recovery::validateSaveState(duplicateSave), "clan aliases cannot hide duplicate players");

    selectActiveMatchTopology(DuelTopologyId, DuelTopologyVersion);
    GameplayRng::seed(605002);
    GameData::initPersons(Person(Avatar::Lakkho, Clan::Yellow, Wind::East));
    check(GameData::initMahjong(), "initialize live Duel");
    const JsonObject live = GameData::authoritativeState();

    for(const char* key : {"seed", "state", "draws"})
    {
        for(const char* invalid : {"-1", "+1", " 1", "1x", "18446744073709551616"})
        {
            JsonObject rng = GameplayRng::toJsonObject();
            const std::string before = Recovery::stateHash(rng);
            rng.addString(key, invalid);
            check(!GameplayRng::isValidState(rng) && !GameplayRng::fromJsonObject(rng),
                  std::string("reject invalid unsigned RNG value: ") + key + "=" + invalid);
            check(Recovery::stateHash(GameplayRng::toJsonObject()) == before,
                  "rejected RNG restore is non-mutating");
        }
    }

    // A rejected load must not change the existing roster, map, UI or RNG.
    for(const char* key : {"croupier", "winresult", "history", "battleSession", "gameplayRng"})
    {
        check(GameData::restoreState(live), "restore live fixture");
        GameData::stateGUI.addString("type", "IntegrityLiveScreen");
        GameData::stateGUI.addInteger("stoneSelected", 5);
        const std::string guiBefore = Recovery::stateHash(GameData::stateGUI);
        const std::string before = Recovery::stateHash(GameData::authoritativeState());
        JsonObject corrupt = source;
        if(std::string(key) == "gameplayRng" || std::string(key) == "battleSession")
        {
            JsonObject invalid;
            invalid.addString("algorithm", "unsupported");
            corrupt.addObject(key, invalid);
        }
        else corrupt.addNull(key);
        std::string reason;
        check(!Recovery::validateSaveState(corrupt, &reason) && !reason.empty(),
              std::string("inspection rejects invalid ") + key);
        check(!GameData::restoreState(corrupt), std::string("load rejects invalid ") + key);
        check(Recovery::stateHash(GameData::authoritativeState()) == before,
              std::string("failed load preserves live state: ") + key);
        check(Recovery::stateHash(GameData::stateGUI) == guiBefore,
              std::string("failed load preserves GUI state: ") + key);
    }

    // Invalid autosave replacement must preserve the existing valid save.
    const std::string directory = SaveGames::defaultDirectory();
    std::filesystem::create_directories(directory);
    const std::string path = (std::filesystem::path(directory) / "integrity.sav").string();
    check(SaveGames::writeAutosave(path, source), "write valid autosave fixture");
    std::string saved;
    check(Systems::readFile2String(path, saved), "read valid autosave fixture");
    JsonObject corrupt = source;
    corrupt.addNull("croupier");
    check(!SaveGames::writeAutosave(path, corrupt), "refuse damaged autosave replacement");
    std::string remaining;
    check(Systems::readFile2String(path, remaining) && remaining == saved,
          "damaged replacement preserves previous autosave");

    // Pre-ownership saves use original theme ownership, never the previous match.
    check(GameData::restoreState(live), "restore Duel before legacy load");
    EditableObject legacy(source);
    legacy.erase("landOwners");
    check(GameData::restoreState(legacy), "load legacy save without owners");
    check(Recovery::stateHash(*GameData::authoritativeState().getObject("landOwners")) ==
          Recovery::stateHash(*source.getObject("landOwners")),
          "legacy load restores original four-clan island");

    selectActiveMatchTopology(DuelTopologyId, DuelTopologyVersion);
    GameData::setAIDifficulty(AI::Difficulty::Unfair);
    GameData::initPersons(Person(Avatar::Nucrus, Clan::Red, Wind::East));
    check(GameData::initMahjong(), "initialize Unfair Duel");
    for(LocalPlayer & player : GameData::gamers)
    {
        if(!GameData::usesAI(player)) continue;
        check(player.landClaimPoints(Clan::Purple) == 0,
              "Unfair Duel grants no claims against absent Purple clan");
        player.addLandClaimPoints(Clan::Purple, 123);
        const auto score = MatchScore::observe(player);
        check(score.scores[MatchScore::index(MatchScore::Category::LandClaims)] ==
              player.landClaimPoints(Clan::Red), "Duel score ignores absent clans");
    }

    check(GameData::initMahjong(), "initialize final Quick Duel hand");
    const std::string completed = Recovery::stateHash(GameData::authoritativeState());
    check(!GameData::initMahjong(), "Quick Duel refuses a third hand");
    check(Recovery::stateHash(GameData::authoritativeState()) == completed,
          "completed match retains hands, score and RNG when next hand is refused");

#ifdef BUILD_DEBUG
    for(const auto* topology : {&classicFreeForAllTopology(), &duelTopology(), &coalitionTopology()})
    {
        for(const auto* ruleset : {&classicRuneGameRuleset(), &quickRuneGameRuleset()})
        {
            selectActiveMatchTopology(topology->id(), topology->version());
            selectActiveRuneGameRuleset(ruleset->id(), ruleset->version());
            GameData::initPersons(Person(Avatar::Nucrus, Clan::Red, Wind::East));
            check(GameData::initMahjong(), "initialize developer fixture base");
            check(GameData::initDeveloperFinalRuneFixture(Avatar::Nucrus) &&
                  Recovery::validateSaveState(GameData::authoritativeState()),
                  "developer final Rune fixture respects match rules");
            const JsonObject finalHand = GameData::authoritativeState();
            check(finalHand.getString("wind:part") == Wind(topology->winds().back()).toString() &&
                  finalHand.getString("wind:round") == (ruleset->id() == QuickRuneGameRulesetId ? "east" : "north"),
                  "developer fixture deals the last active hand of the last allowed round");
            check(GameData::initDeveloperFinalAdventureFixture(Avatar::Nucrus) &&
                  Recovery::validateSaveState(GameData::authoritativeState()),
                  "developer final Adventure fixture respects match rules");
        }
    }
#endif

    if(!failures) std::cout << "state integrity: ok\n";
    return failures ? 1 : 0;
}

int runMatchModeMatrixTests()
{
    int failures = 0;
    int matches = 0;
    const auto check = [&](bool valid, const std::string & message) {
        if(!valid) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
    };
    Recovery::setEnabled(false);
    for(const auto* topology : {&classicFreeForAllTopology(), &legacyDuelTopology(),
                               &duelTopology(), &coalitionTopology()})
    {
        for(const auto difficulty : {AI::Difficulty::Training, AI::Difficulty::Easy,
                                     AI::Difficulty::Normal, AI::Difficulty::Hard,
                                     AI::Difficulty::Unfair})
        {
            Simulation::MatchConfig config;
            config.seed = 605020 + matches;
            config.difficulty = difficulty;
            config.matchTopologyId = topology->id();
            config.matchTopologyVersion = topology->version();
            config.runeGameRulesetId = QuickRuneGameRulesetId;
            config.runeGameRulesetVersion = QuickRuneGameRulesetVersion;
            config.maximumTicks = 20000;
            config.captureFullReplay = true;
            config.persons.push_back(Person(Avatar::Nucrus, Clan::Red, Wind::East));
            if(topology->seatCount() == 4)
            {
                config.persons.push_back(Person(Avatar::Dayla, Clan::Purple, Wind::South));
                config.persons.push_back(Person(Avatar::Ziag, Clan::Aqua, Wind::North));
            }
            config.persons.push_back(Person(Avatar::Lakkho, Clan::Yellow, Wind::West));
            for(Person & player : config.persons) player.setAI(true);
            selectActiveMatchTopology(config.matchTopologyId, config.matchTopologyVersion);
            selectActiveRuneGameRuleset(config.runeGameRulesetId, config.runeGameRulesetVersion);
            const std::string label = topology->id() + "@" + std::to_string(topology->version()) +
                "/" + AI::difficultyName(difficulty);
            const auto result = Simulation::runMatch(config);
            check(result.completed(), label + " completes: " + result.error);
            if(!result.completed()) continue;
            ++matches;
            check(result.mahjongHands == config.persons.size() &&
                  result.adventurePhases == config.persons.size(), label + " has one East round");
            check(result.score.size() == config.persons.size(), label + " scores active players");
            check(!MatchScore::winnerIndices(result.score).empty(), label + " has a winner");
            for(const auto & player : result.score)
                for(const auto & other : result.score)
                    if(player.teamId == other.teamId)
                        check(player.teamScore == other.teamScore && player.teamRank == other.teamRank,
                              label + " teammates share final score and rank");

            std::string error;
            const bool replayed = Replay::run(result.actionReplay, &error);
            check(replayed, label + " replay: " + error);
            const std::string hash = Replay::authoritativeStateHash();
            const JsonObject save = GameData::authoritativeState();
            const bool loaded = Recovery::validateSaveState(save, &error) && GameData::restoreState(save);
            check(loaded, label + " final state is loadable: " + error);
            check(Replay::authoritativeStateHash() == hash, label + " save round trip retains state");
            std::cout << "mode matrix: " << label << " seed=" << config.seed << '\n';
        }
    }
    check(matches == 20, "all 20 mode/difficulty combinations complete");
    if(!failures) std::cout << "match mode matrix: 20 matches and replays ok\n";
    return failures ? 1 : 0;
}
