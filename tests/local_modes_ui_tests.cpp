#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <set>

#include "gametheme.h"
#include "adventurepart.h"
#include "gameplayrng.h"
#include "mahjongpart.h"
#include "matchtopology.h"
#include "recovery.h"
#include "runewars.h"
#include "settings.h"
#include "showplayers.h"
#include "settingsmenu.h"
#include "gamesummarypart.h"
#include "mahjongsummarypart.h"
#include "battlesummarypart.h"
#include "simulation.h"
#include "replay.h"
#include "runegameruleset.h"
#include "dialogs.h"

namespace GameData
{
    extern LocalPlayers gamers;
    extern Wind currentWind;
    extern Stone dropStone;
    extern bool skipRepeatSay;
    extern WinResults winResult;
    extern Wind roundWind;
    extern Wind partWind;
}

namespace
{
    class RuneScreen : public MahjongPartScreen
    {
    public:
        using MahjongPartScreen::fromJsonObject;
        using MahjongPartScreen::toJsonObject;
        using MahjongPartScreen::tickEvent;
        using MahjongPartScreen::userEvent;
    };

    class MapScreen : public AdventurePartScreen
    {
    public:
        using AdventurePartScreen::AdventurePartScreen;
        using AdventurePartScreen::tickEvent;
        using AdventurePartScreen::userEvent;
        bool doneEnabled() { return !buttons.findIds("but_done")->isDisabled(); }
    };

    class ResultsScreen : public GameSummaryScreen
    {
    public:
        using GameSummaryScreen::userEvent;
        using GameSummaryScreen::tickEvent;
        void details() { for(int i = 0; i < 4; ++i) { userEvent(Action::ButtonDone, nullptr); tickEvent(1000 + i); } }
    };

    void snapshot(const char* name)
    {
        DisplayScene::sceneRedraw(true);
        if(const char* directory = std::getenv("FOUR_WINDS_UI_SNAPSHOT_DIR"))
        {
            std::filesystem::create_directories(directory);
            Display::renderScreenshot((std::filesystem::path(directory) / name).string());
        }
    }
}

int runLocalModesUiTests(const char*)
{
    int failures = 0;
    const auto check = [&](bool condition, const char* message) {
        if(!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
    };
    Recovery::setEnabled(false);
    const std::string resourceProgram = (std::filesystem::path(FOUR_WINDS_SOURCE_DIR) /
        "four-winds-reborn.exe").make_preferred().string();
    const char* testTheme = std::getenv("FOUR_WINDS_UI_TEST_THEME");
    if(!Engine::init() || !GameTheme::init(Application(resourceProgram.c_str(), false, Size(), testTheme ? testTheme : "classic")))
        return 1;
    if(const char* language = std::getenv("FOUR_WINDS_UI_TEST_LANGUAGE"))
    {
        Settings::setLanguage(language);
        Translation::setStripContext('|');
        check(Translation::bindDomain(Application::domain(), GameTheme::readResource(std::string(language) + ".mo")), "load test language");
        Translation::setLanguage(language);
        Translation::setDomain(Application::domain());
        GameData::retranslateThemeData();
    }
    Settings::setMusic(false);
    Settings::setSound(false);
    Settings::setVoiceVolume(0);
    Settings::setSoundGuardianRules(false);
    Settings::setGameSpeed("fast");

    selectActiveMatchTopology(DuelTopologyId, LegacyDuelTopologyVersion);
    Persons persons;
    persons.push_back(Person(Avatar::Nucrus, Clan::Red, Wind::East));
    persons.push_back(Person(Avatar::Dayla, Clan::Purple, Wind::South));
    persons.push_back(Person(Avatar::Lakkho, Clan::Yellow, Wind::West));
    persons.push_back(Person(Avatar::Ziag, Clan::Aqua, Wind::North));
    for(std::size_t i = 1; i < persons.size(); ++i) persons[i].setAI(true);
    GameplayRng::seed(150050);
    check(GameData::initPersons(persons) && GameData::initMahjong(), "initialize local Duel");

    // A recovered second hand already has a drawn rune: no new MahjongTurn
    // will arrive to repair a lost selection or a hidden self-draw Game button.
    GameData::currentWind = Wind::South;
    ActionList drawActions;
    GameData::mahjong2Client(Avatar::Dayla, drawActions);
    JsonObject gui;
    gui.addString("type", "MahjongPartScreen");
    gui.addBoolean("playerReady", true);
    gui.addInteger("stoneSelected", GameData::gamers[1].stones.size());
    gui.addInteger("variantSelected", -1);
    JsonObject hidden;
    hidden.addBoolean("visible", false);
    gui.addObject("buttonLocalReady", hidden);
    {
        RuneScreen screen;
        screen.fromJsonObject(gui);
        screen.tickEvent(1000);
        check(screen.toJsonObject().getInteger("stoneSelected", -1) >= 0,
              "Continue on Duel's second hand must retain a selectable rune");
    }

    // Red discards a rune that Purple can claim. The GUI must not queue Pass
    // on behalf of both hands before Purple is offered the discard.
    check(GameData::initPersons(persons) && GameData::initMahjong(), "reset local Duel");
    GameData::gamers[0].newStone = GameStone(Stone::Skull5, false);
    GameData::gamers[1].stones.clear();
    GameData::gamers[1].stones.add(GameStone(Stone::Skull5, false));
    GameData::gamers[1].stones.add(GameStone(Stone::Skull5, false));
    gui.addInteger("stoneSelected", GameData::gamers[0].stones.size());
    {
        RuneScreen screen;
        screen.fromJsonObject(gui);
        screen.userEvent(Action::MahjongDropSelected, nullptr);
        const JsonObject state = screen.toJsonObject();
        const JsonObject* pass = state.getObject("buttonPass");
        check(pass && !pass->getBoolean("pressed"),
              "Duel discard must not automatically press Pass for the partner hand");
        screen.tickEvent(2000);
        screen.tickEvent(3000);
        screen.userEvent(Action::ButtonPung, nullptr);
        check(GameData::currentWind == Wind(Wind::South) && !GameData::dropStone.isValid(),
              "the partner must be able to claim the local discard through the GUI");
    }

    check(GameData::initPersons(persons) && GameData::initMahjong() && GameData::initAdventure(),
          "initialize Duel adventure");
    for(LocalPlayer & player : GameData::gamers) player.army.clear();
    {
        MapScreen screen(Avatar::Nucrus);
        screen.tickEvent(1000);
        screen.userEvent(Action::ButtonDone, nullptr);
        screen.tickEvent(2000);
        screen.tickEvent(3000);
        check(GameData::currentWind == Wind(Wind::South) && screen.doneEnabled(),
              "Duel must enable Done when control reaches the second clan");
    }

    selectActiveMatchTopology(DuelTopologyId, DuelTopologyVersion);
    for(const auto clan : clans_all)
    {
        const Avatar avatar = GameData::avatarsOfClan(clan).front();
        GameData::initPersons(Person(avatar, clan, Wind::East));
        check(GameData::players().size() == 2, "new Duel must contain exactly two players");
        check(GameData::myPerson().avatar == avatar &&
              GameData::isLocallyControlled(GameData::myPerson()), "selected Duel wizard remains human on either side");
        check(GameData::initMahjong(), "start two-player Rune Game");
        check(GameData::toLocalData(avatar).toPersons().size() == 2, "Duel view must expose only two participants");
        check(GameData::players()[0].lands().size() == 22 && GameData::players()[1].lands().size() == 22,
              "each Duel player starts with exactly 22 towns");
        for(const auto land : lands_all)
            if(!Land(land).isTowerWinds())
                check(GameData::players().playerOfClan(GameData::landInfo(land).clan) != nullptr,
                      "every Duel town belongs to a participating wizard");
        const JsonObject save = GameData::authoritativeState();
        std::string error;
        check(Recovery::validateSaveState(save, &error), error.c_str());
        if(const char* directory = std::getenv("FOUR_WINDS_RECOVERY_DIR"))
        {
            Recovery::setEnabled(true);
            check(GameData::saveRecovery(JsonObject(), "two-player-ui-test"), "persist two-player recovery checkpoint");
            Recovery::setEnabled(false);
            check(Recovery::inspectCheckpoint(directory, 0).valid, "two-player recovery metadata and state agree");
        }
        JsonObject corrupt = save;
        corrupt.addString("wind:current", "south");
        check(!Recovery::validateSaveState(corrupt, &error), "Duel cannot restore an absent current seat");
        corrupt.addString("wind:current", "none");
        check(!Recovery::validateSaveState(corrupt, &error), "a started Duel requires a current seat");
        corrupt = save;
        corrupt.addObject("landOwners", JsonObject());
        check(!Recovery::validateSaveState(corrupt, &error), "Duel cannot restore a partial island");
        selectActiveMatchTopology(ClassicFreeForAllTopologyId, ClassicFreeForAllTopologyVersion);
        check(GameData::restoreState(save) && activeMatchTopology().version() == DuelTopologyVersion,
              "Continue must restore two-player Duel independently of settings");
    }

    GameData::initPersons(Person(Avatar::Nucrus, Clan::Red, Wind::East));
    LocalPlayer chao = *GameData::players().playerOfWind(Wind::West);
    chao.stones.clear();
    chao.stones.add(GameStone(Stone::Number2, false));
    chao.stones.add(GameStone(Stone::Number3, false));
    check(chao.isMahjongChao(Wind::East, Stone::Number1), "Duel's opposite seat is the next player for Chao");
    selectActiveRuneGameRuleset(QuickRuneGameRulesetId, QuickRuneGameRulesetVersion);
    check(GameData::initMahjong() && GameData::partWind == Wind(Wind::East), "Quick Duel starts with East deal");
    {
        ShowPlayersScreen screen;
        snapshot("duel-roster.png");
    }
    {
        RuneScreen screen;
        snapshot("duel-table.png");
        const LocalData data = GameData::toLocalData(GameData::myPerson().avatar);
        MapStatusDialog status(data, screen);
        snapshot("duel-status.png");
    }
    GameData::winResult = WinResults::drawn(GameData::currentWind, GameData::roundWind);
    {
        MahjongSummaryPartScreen screen;
        snapshot("duel-rune-summary.png");
    }
    check(GameData::initMahjong() && GameData::partWind == Wind(Wind::West), "Quick Duel rotates directly to West deal");
    check(!GameData::initMahjong(), "Quick Duel ends after two hands");
    GameData::initAdventure();
    {
        MapScreen screen(GameData::myPerson().avatar);
        snapshot("duel-map.png");
    }
    {
        BattleSummaryScreen screen;
        snapshot("duel-battle-summary.png");
    }

    Simulation::MatchConfig duel;
    duel.seed = 150051;
    duel.matchTopologyId = DuelTopologyId;
    duel.matchTopologyVersion = DuelTopologyVersion;
    duel.runeGameRulesetId = QuickRuneGameRulesetId;
    duel.runeGameRulesetVersion = QuickRuneGameRulesetVersion;
    duel.persons.push_back(Person(Avatar::Nucrus, Clan::Red, Wind::East));
    duel.persons.push_back(Person(Avatar::Lakkho, Clan::Yellow, Wind::West));
    for(Person & player : duel.persons) player.setAI(true);
    duel.captureFullReplay = true;
    duel.maximumTicks = 5000;
    const Simulation::MatchResult result = Simulation::runMatch(duel);
    check(result.completed() && result.score.size() == 2, ("two-player simulation: " + result.error).c_str());
    check(result.mahjongHands == 2 && result.adventurePhases == 2, "Quick Duel completes exactly two hands and island phases");
    std::string replayError;
    check(Replay::run(result.actionReplay, &replayError), ("two-player replay: " + replayError).c_str());
    {
        ResultsScreen screen;
        snapshot("duel-victory.png");
        screen.details();
        snapshot("duel-scores.png");
    }
    duel.runeGameRulesetId = ClassicRuneGameRulesetId;
    duel.captureFullReplay = false;
    const Simulation::MatchResult classicDuel = Simulation::runMatch(duel);
    check(classicDuel.completed() && classicDuel.mahjongHands == 8 && classicDuel.adventurePhases == 8,
          "Classic Duel completes four rounds of two hands");

    selectActiveMatchTopology(CoalitionTopologyId, CoalitionTopologyVersion);
    GameData::initPersons(Person(Avatar::Nucrus, Clan::Red, Wind::East));
    std::set<int> islandOwners;
    for(const auto land : lands_all)
        if(!Land(land).isTowerWinds()) islandOwners.insert(GameData::landInfo(land).clan());
    check(islandOwners.size() == 4, "new Coalition restores the four clans after Duel");
    GameData::initMahjong();
    {
        ShowPlayersScreen screen;
        snapshot("coalition-roster.png");
    }
    {
        RuneScreen screen;
        snapshot("coalition-table.png");
    }
    GameData::initAdventure();
    {
        MapScreen screen(GameData::myPerson().avatar);
        snapshot("coalition-map.png");
    }
    {
        ResultsScreen screen;
        screen.details();
        snapshot("coalition-scores.png");
    }
    {
        SettingsMenuScreen screen(resourceProgram);
        snapshot("settings.png");
    }
    GameTheme::clear();
    Engine::quit();
    if(!failures) std::cout << "local modes UI: ok\n";
    return failures ? 1 : 0;
}
