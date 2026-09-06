#include <iostream>
#include <string>

#include "network/clientview.h"
#include "gameplayrng.h"
#include "matchscore.h"
#include "matchtopology.h"
#include "recovery.h"
#include "runegameruleset.h"

namespace GameData
{
    extern LocalPlayers gamers;
    extern CroupierSet croupier;
    extern WinResults winResult;
    extern std::list<BattleLegend> battleHistory;
}

namespace
{
    const JsonObject* playerView(const JsonObject & view, const Avatar & avatar)
    {
        const auto* players = view.getArray("players");
        if(players)
            for(std::size_t i = 0; i < players->size(); ++i)
                if(players->getObject(i)->getString("avatar") == avatar.toString()) return players->getObject(i);
        return nullptr;
    }

    JsonObject replacePlayer(const JsonObject & view, const Avatar & avatar, const JsonObject & replacement)
    {
        JsonObject result = view;
        JsonArray players;
        for(std::size_t i = 0; i < view.getArray("players")->size(); ++i)
        {
            const auto & player = *view.getArray("players")->getObject(i);
            players.addObject(player.getString("avatar") == avatar.toString() ? replacement : player);
        }
        result.addArray("players", players);
        return result;
    }
}

int runClientViewTests()
{
    int failures = 0;
    const auto check = [&](bool valid, const std::string & message) {
        if(!valid) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
    };
    Recovery::setEnabled(false);
    selectActiveMatchTopology(DuelTopologyId, DuelTopologyVersion);
    selectActiveRuneGameRuleset(QuickRuneGameRulesetId, QuickRuneGameRulesetVersion);
    Persons roster;
    roster.emplace_back(Avatar::Nucrus, Clan::Red, Wind::East);
    roster.emplace_back(Avatar::Lakkho, Clan::Yellow, Wind::West);
    GameplayRng::seed(605600);
    check(GameData::initPersons(roster), "initialize two human network participants");
    GameData::setGamePart(Menu::ShowPlayers);
    const JsonObject beforeHand = Multiplayer::buildClientView(Avatar::Lakkho);
    check(beforeHand.getString("recipient") == Avatar(Avatar::Lakkho).toString(), "lobby view targets invited player");
    std::string error;
    check(Multiplayer::applyClientView(beforeHand, &error), "pre-hand projection hydrates: " + error);
    check(GameData::myPerson().avatar == Avatar(Avatar::Lakkho), "invited player remains the local reference");
    check(GameData::initPersons(roster) && GameData::initMahjong(), "initialize authority rune state");
    GameData::setGamePart(Menu::MahjongPart);
    auto & red = *GameData::gamers.playerOfAvatar(Avatar::Nucrus);
    auto & yellow = *GameData::gamers.playerOfAvatar(Avatar::Lakkho);
    red.newStone = GameStone(Stone::Sword9);
    yellow.newStone = GameStone(Stone::Number9);
    GameData::winResult = WinResults(Wind::East, Wind::East, Wind::East,
        WinRules(), WinRules(), Stone::Skull7, Stone::Sword9);
    const JsonObject authority = GameData::authoritativeState();
    const JsonObject redView = Multiplayer::buildClientView(Avatar::Nucrus);
    const JsonObject yellowView = Multiplayer::buildClientView(Avatar::Lakkho);
    check(!redView.hasKey("gameplayRng") && !redView.hasKey("croupier") && !redView.hasKey("bank") &&
          !redView.hasKey("battleSession") && !redView.hasKey("winresult"),
          "active projection excludes RNG, wall, pending battle and speculative win result");
    check(playerView(redView, Avatar::Nucrus)->getArray("stones")->size() == red.stones.size() &&
          playerView(redView, Avatar::Nucrus)->getObject("stone:new")->getString("stone") == Stone(Stone::Sword9).toString(),
          "projection retains the recipient's hand and drawn rune");
    check(playerView(redView, Avatar::Lakkho)->getArray("stones")->size() == 0 &&
          playerView(redView, Avatar::Lakkho)->getObject("stone:new")->getString("stone") == Stone().toString(),
          "opponent hand and drawn rune are absent from wire");
    check(playerView(yellowView, Avatar::Nucrus)->getArray("stones")->size() == 0 &&
          playerView(yellowView, Avatar::Lakkho)->getArray("stones")->size() == yellow.stones.size(),
          "opposite recipient receives the opposite privacy projection");
    check(!Multiplayer::buildClientView(Avatar::Ziag).isValid(), "outsider receives no snapshot");

    const MahjongTurn turn(Wind::East, Stone::Sword9, true, true);
    const auto ownTurn = Multiplayer::eventToWire(turn, Avatar::Nucrus);
    const auto hiddenTurn = Multiplayer::eventToWire(turn, Avatar::Lakkho);
    check(ownTurn.getString("newStone") == Stone(Stone::Sword9).toString() && ownTurn.getBoolean("showGame"),
          "turn actor receives draw and legal self-call flags");
    check(hiddenTurn.getString("newStone") == Stone().toString() &&
          !hiddenTurn.getBoolean("showKong") && !hiddenTurn.getBoolean("showGame"),
          "observer turn event reveals neither rune nor winning/kong structure");
    VecStones choices;
    choices.emplace_back(Stone::Skull8);
    choices.emplace_back(Stone::Dragon3);
    MahjongLuckChoice luck(Wind::East, choices);
    check(Multiplayer::eventToWire(luck, Avatar::Nucrus).isValid() &&
          !Multiplayer::eventToWire(luck, Avatar::Lakkho).isValid(), "Luck choices belong only to their actor");
    BattleLegend legend;
    legend.attacker = Avatar::Nucrus;
    legend.defender = Avatar::Lakkho;
    legend.attackers = BattleParty(Clan::Red, Land::Baliphon);
    legend.attackers.join(BattleCreature(Clan::Red, Creature::SkeletonHorde, 60601));
    legend.town = BattleTown(Land::Baliphon);
    legend.wins = false;
    BattleStrikes strikes;
    strikes.emplace_back(*legend.attackers.findBattleUnitConst(60601), 1, legend.town, BattleStrike::Melee);
    AdventureBattleChoice battle(Wind::East, legend, "attacker_melee", strikes,
        {60601}, {legend.town.battleUnit()}, {60601, legend.town.battleUnit()}, 1, 1);
    check(Multiplayer::eventToWire(battle, Avatar::Nucrus).isValid() &&
          !Multiplayer::eventToWire(battle, Avatar::Lakkho).isValid(), "battle choice belongs only to attacker");
    ActionMessage roundtrip(Action::None);
    check(Multiplayer::eventFromWire(hiddenTurn, roundtrip, &error) && roundtrip.type() == Action::MahjongTurn,
          "sanitized turn survives event codec");
    const auto unchangedEvent = roundtrip.toString();
    JsonObject forged = hiddenTurn;
    forged.addArray("bank", choices.toJsonArray());
    check(!Multiplayer::eventFromWire(forged, roundtrip) && roundtrip.toString() == unchangedEvent,
          "unknown event fields fail without changing output");
    forged = hiddenTurn;
    forged.addString("type", std::to_string(Action::MahjongTurn));
    check(!Multiplayer::eventFromWire(forged, roundtrip), "event type coercion is rejected");
    ActionList mixed;
    mixed.push_back(turn); mixed.push_back(luck); mixed.push_back(battle);
    mixed.push_back(MahjongDrop(Wind::East, Stone::Skull1));
    const auto filtered = Multiplayer::filterEvents(mixed, Avatar::Lakkho);
    check(filtered.size() == 2 && filtered.back().type() == Action::MahjongDrop,
          "fanout suppresses private prompts while preserving public event order");

    const auto rngBefore = GameplayRng::toJsonObject().toString();
    check(Multiplayer::applyClientView(yellowView, &error), "recipient snapshot hydrates: " + error);
    check(GameplayRng::toJsonObject().toString() == rngBefore, "projection hydration does not advance or replace RNG");
    check(GameData::myPerson().avatar == Avatar(Avatar::Lakkho) && GameData::players().size() == 2 &&
          GameData::croupier.bank.empty() && GameData::croupier.luckDraw.empty(),
          "hydration installs assigned seat and clears stale authoritative secrets");
    check(GameData::players().playerOfAvatar(Avatar::Nucrus)->stones.empty() &&
          !GameData::toLocalData(Avatar::Lakkho).winResult.isValid(),
          "previous hand and speculative score cannot survive hydration");
    const auto appliedHash = Recovery::stateHash(GameData::authoritativeState());
    const auto rejectsAtomically = [&](const JsonObject & bad, const char* reason) {
        check(!Multiplayer::applyClientView(bad, &error) &&
              Recovery::stateHash(GameData::authoritativeState()) == appliedHash, reason);
    };
    JsonObject bad = yellowView;
    bad.addObject("gameplayRng", GameplayRng::toJsonObject());
    rejectsAtomically(bad, "secret save fields rejected atomically");
    bad = yellowView; bad.addInteger("phase", Menu::SettingsMenu);
    rejectsAtomically(bad, "UI-only phase rejected atomically");
    bad = yellowView; bad.addString("wind:current", "south");
    rejectsAtomically(bad, "inactive Duel wind rejected atomically");
    bad = yellowView; bad.addString("lastcount", "70");
    rejectsAtomically(bad, "counter type coercion rejected atomically");
    JsonObject invalidPlayer = *playerView(yellowView, Avatar::Nucrus);
    invalidPlayer.addString("avatar", "random");
    rejectsAtomically(replacePlayer(yellowView, Avatar::Nucrus, invalidPlayer), "random roster identity rejected atomically");
    invalidPlayer = *playerView(redView, Avatar::Nucrus);
    rejectsAtomically(replacePlayer(yellowView, Avatar::Nucrus, invalidPlayer), "unauthorized disclosed opponent hand rejected");
    invalidPlayer = *playerView(yellowView, Avatar::Nucrus);
    invalidPlayer.addString("points", "250");
    rejectsAtomically(replacePlayer(yellowView, Avatar::Nucrus, invalidPlayer), "nested player scalar coercion rejected");

    check(GameData::restoreState(authority), "restore local authority fixture for Scry tests");
    GameData::gamers.playerOfAvatar(Avatar::Lakkho)->affected.insert(AffectedSpell(Spell::ScryRunes, 2, Avatar::Nucrus));
    const auto scryView = Multiplayer::buildClientView(Avatar::Nucrus);
    check(playerView(scryView, Avatar::Lakkho)->getArray("stones")->size() > 0 &&
          Multiplayer::applyClientView(scryView, &error), "authorized Scry hand crosses wire and hydrates: " + error);
    check(GameData::restoreState(authority), "restore authority after Scry");
    GameData::setGamePart(Menu::MahjongSummaryPart);
    const auto summary = Multiplayer::buildClientView(Avatar::Lakkho);
    check(summary.isObject("winresult") && Multiplayer::applyClientView(summary, &error) &&
          GameData::toLocalData(Avatar::Lakkho).winResult.pairStone == Stone(Stone::Skull7),
          "completed hand reveals its score context: " + error);

    check(GameData::restoreState(authority), "restore authority for final standings");
    BattleParty hidden(Clan::Red, Land::Maithaius);
    hidden.join(BattleCreature(Clan::Red, Creature::Shadow, 60602));
    GameData::gamers.playerOfAvatar(Avatar::Nucrus)->army.push_back(hidden);
    const AdventureMoves hiddenMove(Wind::East, 60602, Land::Maithaius);
    check(!Multiplayer::eventToWire(hiddenMove, Avatar::Lakkho).isValid() &&
          Multiplayer::eventToWire(hiddenMove, Avatar::Nucrus).isValid(),
          "movement event cannot disclose an invisible opponent's position");
    GameData::battleHistory.push_back(legend);
    GameData::setGamePart(Menu::GameSummaryPart);
    const auto expectedScores = MatchScore::current();
    const auto finalView = Multiplayer::buildClientView(Avatar::Lakkho);
    check(Multiplayer::applyClientView(finalView, &error), "terminal public armies and battle history hydrate: " + error);
    const auto receivedScores = MatchScore::current();
    check(receivedScores.size() == expectedScores.size(), "terminal score participant count agrees");
    for(std::size_t i = 0; i < std::min(receivedScores.size(), expectedScores.size()); ++i)
        check(receivedScores[i].totalScore == expectedScores[i].totalScore &&
              receivedScores[i].categories[MatchScore::index(MatchScore::Category::Units)].score ==
                  expectedScores[i].categories[MatchScore::index(MatchScore::Category::Units)].score,
              "terminal unit score includes formerly invisible creatures");
    check(GameData::getBattleHistoryFor(Avatar::Nucrus).size() == 1, "battle summary history replaces stale state");
    if(!failures) std::cout << "client view privacy and codec regressions: ok\n";
    return failures ? 1 : 0;
}
