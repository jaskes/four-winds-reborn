#include <algorithm>
#include <iostream>
#include <set>
#include <string>

#include "battlesession.h"
#include "clientview.h"
#include "gameplayrng.h"
#include "matchauthority.h"
#include "matchscore.h"
#include "matchtopology.h"
#include "recovery.h"
#include "replay.h"
#include "runegameruleset.h"

namespace GameData
{
    extern LocalPlayers gamers;
    extern CroupierSet croupier;
    extern Stone dropStone;
    extern Wind currentWind;
    extern Wind roundWind;
    extern WinResults winResult;
    extern std::vector<LandInfo> landsInfo;
}

namespace
{
    Multiplayer::MatchConfig duelConfig(std::uint64_t seed)
    {
        Multiplayer::MatchConfig config;
        config.seed = seed;
        config.players.push_back(Person(Avatar::Nucrus, Clan::Red, Wind::East));
        config.players.push_back(Person(Avatar::Lakkho, Clan::Yellow, Wind::West));
        return config;
    }

    Multiplayer::MatchConfig fourPlayerConfig(std::uint64_t seed)
    {
        Multiplayer::MatchConfig config;
        config.topologyId = ClassicFreeForAllTopologyId;
        config.topologyVersion = ClassicFreeForAllTopologyVersion;
        config.seed = seed;
        std::set<int> used;
        for(std::size_t index = 0; index < clans_all.size(); ++index)
        {
            const Clan clan(*std::next(clans_all.begin(), index));
            const Avatars candidates = GameData::avatarsOfClan(clan);
            const auto selected = std::find_if(candidates.begin(), candidates.end(), [&](const Avatar& avatar) {
                return !used.count(avatar());
            });
            if(selected == candidates.end()) continue;
            used.insert((*selected)());
            config.players.push_back(Person(*selected, clan, Wind(*std::next(winds_all.begin(), index))));
        }
        return config;
    }

    void setHand(LocalPlayer& player, std::initializer_list<Stone::stone_t> stones)
    {
        player.stones.clear();
        player.rules.clear();
        player.newStone.reset();
        for(const Stone::stone_t stone : stones) player.stones.add(GameStone(stone));
    }

    bool hasEvent(const ActionList& events, int type)
    {
        return std::any_of(events.begin(), events.end(), [type](const ActionMessage& message) {
            return message.type() == type;
        });
    }
}

int runMatchAuthorityTests()
{
    Recovery::setEnabled(false);
    int failures = 0;
    const auto check = [&](bool valid, const std::string& message) {
        if(!valid) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
    };
    Multiplayer::MatchAuthority authority;
    const Multiplayer::MatchConfig config = duelConfig(606001);
    std::string error;
    check(authority.start(config, &error), "start two-human authority: " + error);
    if(!authority.active()) return 1;
    check(authority.phase() == Menu::ShowPlayers && !authority.tick(), "roster waits for both humans");
    const auto initialRevision = authority.revision();
    ActionRejection rejection;
    check(!authority.ready(Avatar(), &rejection) && authority.revision() == initialRevision,
          "unbound identity cannot acknowledge or mutate roster");
    check(authority.ready(config.players[0].avatar) && authority.phase() == Menu::ShowPlayers,
          "first human acknowledgement leaves roster barrier closed");
    const auto readyRevision = authority.revision();
    check(authority.ready(config.players[0].avatar) && authority.revision() == readyRevision,
          "duplicate phase acknowledgement is idempotent");
    check(authority.ready(config.players[1].avatar) && authority.phase() == Menu::MahjongPart,
          "second human starts the Rune phase");
    check(!GameData::usesAI(GameData::players().front()) && !GameData::usesAI(GameData::players().back()),
          "both network participants remain human");
    for(const Person& person : config.players) authority.takeEvents(person.avatar);
    check(authority.tick(), "authority draws the first rune once");
    const Avatar first = GameData::currentPerson().avatar;
    const Avatar other = first == config.players[0].avatar ? config.players[1].avatar : config.players[0].avatar;
    for(const ActionMessage& action : authority.takeEvents(other))
    {
        if(action.type() == Action::MahjongTurn)
            check(!Stone(action.getString("newStone")).isValid() && !action.getBoolean("showGame") &&
                  !action.getBoolean("showKong"), "opponent receives turn timing without private draw or legal calls");
        check(action.type() != Action::MahjongLuckChoice, "opponent cannot see Luck choices");
    }
    if(GameData::croupier.hasLuckDraw()) check(authority.submit(first, ClientLuckChoice(0)), "resolve first Luck draw");
    const auto beforeWrong = authority.revision();
    check(!authority.submit(other, ClientDropIndex(0), &rejection) && authority.revision() == beforeWrong,
          "opponent cannot discard another participant's hand");
    check(!authority.submit(first, ClientSummonCreature(Creature::SkeletonHorde, Land::TowerOf4Winds, true), &rejection) &&
          authority.revision() == beforeWrong, "network commands cannot request developer force mode");
    check(!authority.submit(first, ClientCastSpell(Spell()), &rejection) && authority.revision() == beforeWrong,
          "invalid spell identity is rejected before core spell lookup");
    const LocalPlayer* firstPlayer = GameData::players().playerOfAvatar(first);
    check(authority.submit(first, ClientDropIndex(static_cast<int>(firstPlayer->stones.size()))), "current human discards draw");
    check(authority.awaitingClaim(other), "opponent owns the discard response window");
    const auto beforeBadPass = authority.revision();
    const std::string beforeBadPassState = Replay::authoritativeStateHash();
    check(!authority.submit(first, ClientButtonPass(), &rejection) && authority.revision() == beforeBadPass &&
          Replay::authoritativeStateHash() == beforeBadPassState, "discarder's automatic Pass cannot steal opponent claim window");
    check(authority.submit(other, ClientButtonPass()) && !GameData::dropStone.isValid(), "opponent Pass advances discard once");
    const auto passedRevision = authority.revision();
    check(!authority.submit(other, ClientButtonPass(), &rejection) && authority.revision() == passedRevision,
          "duplicate Pass cannot advance a second turn");

    // Both humans finish complete real deals through the same authority API
    // used by sockets; no client calls a game initializer or the game pump.
    int runePhases = 1;
    int adventurePhases = 0;
    int previousPhase = authority.phase();
    for(int step = 0; step < 6000 && authority.phase() != Menu::GameSummaryPart; ++step)
    {
        const int phase = authority.phase();
        if(phase == Menu::MahjongSummaryPart || phase == Menu::BattleSummaryPart)
        {
            const Avatar firstReady = config.players[step % 2].avatar;
            const Avatar secondReady = config.players[(step + 1) % 2].avatar;
            check(authority.ready(firstReady), "first human acknowledges phase summary");
            check(authority.phase() == phase && !authority.tick(), "slow human keeps summary visible for both clients");
            check(authority.ready(secondReady), "second human releases phase summary barrier");
        }
        else if(phase == Menu::MahjongPart)
        {
            authority.tick();
            if(authority.phase() != Menu::MahjongPart) continue;
            if(GameData::dropStone.isValid())
            {
                for(const Person& person : config.players)
                    if(authority.awaitingClaim(person.avatar))
                        check(authority.submit(person.avatar, ClientButtonPass()), "human passes discard during full match");
            }
            else
            {
                const LocalPlayer* current = GameData::players().playerOfAvatar(GameData::currentPerson().avatar);
                if(GameData::croupier.hasLuckDraw())
                    check(authority.submit(current->avatar, ClientLuckChoice(0)), "human chooses Luck rune during full match");
                else if(current->newStone.isValid())
                    check(authority.submit(current->avatar, ClientDropIndex(static_cast<int>(current->stones.size()))),
                          "human discards during full match");
            }
        }
        else if(phase == Menu::AdventurePart)
        {
            authority.tick();
            if(authority.phase() == Menu::AdventurePart)
            {
                const LocalPlayer* current = GameData::players().playerOfAvatar(GameData::currentPerson().avatar);
                if(!current->adventurePartDone()) check(authority.submit(current->avatar, ClientBattleReady()), "human finishes island turn");
            }
        }
        const int now = authority.phase();
        if(now != previousPhase)
        {
            if(now == Menu::MahjongPart) ++runePhases;
            if(now == Menu::AdventurePart) ++adventurePhases;
            previousPhase = now;
        }
        for(const Person& person : config.players) authority.takeEvents(person.avatar);
    }
    check(authority.phase() == Menu::GameSummaryPart && runePhases == 2 && adventurePhases == 2,
          "Quick human Duel reaches final score after two Rune and two island phases");
    authority.stop();

    // The host must collect competing claims. Arrival order cannot turn a
    // lower-priority Pung into a win over another human's Game declaration.
    for(int gameFirst = 0; gameFirst < 2; ++gameFirst)
    {
        const auto four = fourPlayerConfig(606010 + gameFirst);
        check(authority.start(four, &error), "start four-human claim fixture: " + error);
        if(!authority.active()) break;
        for(const Person& person : four.players) check(authority.ready(person.avatar), "ready four-human participant");
        LocalPlayer& discarder = *GameData::gamers.playerOfWind(Wind::East);
        LocalPlayer& pung = *GameData::gamers.playerOfWind(Wind::South);
        LocalPlayer& winner = *GameData::gamers.playerOfWind(Wind::West);
        LocalPlayer& passer = *GameData::gamers.playerOfWind(Wind::North);
        setHand(discarder, {Stone::Skull1, Stone::Skull2, Stone::Skull4, Stone::Skull5, Stone::Skull7,
            Stone::Sword2, Stone::Sword4, Stone::Sword6, Stone::Sword8, Stone::Number2, Stone::Number4,
            Stone::Number6, Stone::Number8});
        discarder.newStone = GameStone(Stone::Number1);
        setHand(pung, {Stone::Number1, Stone::Number1, Stone::Skull1, Stone::Skull3, Stone::Skull5,
            Stone::Skull7, Stone::Sword1, Stone::Sword3, Stone::Sword5, Stone::Sword7, Stone::Number3,
            Stone::Number5, Stone::Number7});
        setHand(winner, {Stone::Sword1, Stone::Sword2, Stone::Sword3, Stone::Sword4, Stone::Sword5,
            Stone::Sword6, Stone::Number4, Stone::Number5, Stone::Number6, Stone::Skull7,
            Stone::Skull8, Stone::Skull9, Stone::Number1});
        check(authority.submit(discarder.avatar, ClientDropIndex(13)), "open competing claim fixture");
        check(authority.submit(gameFirst ? winner.avatar : pung.avatar,
                               gameFirst ? static_cast<const ClientMessage&>(ClientButtonGame()) :
                                           static_cast<const ClientMessage&>(ClientButtonPung())), "queue first human claim");
        check(authority.phase() == Menu::MahjongPart && GameData::dropStone.isValid(),
              "first human claim waits for other responses");
        check(authority.submit(passer.avatar, ClientButtonPass()), "third opponent passes claim window");
        check(authority.submit(gameFirst ? pung.avatar : winner.avatar,
                               gameFirst ? static_cast<const ClientMessage&>(ClientButtonPung()) :
                                           static_cast<const ClientMessage&>(ClientButtonGame())), "queue remaining human claim");
        check(authority.phase() == Menu::MahjongSummaryPart && GameData::winResult.winWind == winner.wind,
              "Game outranks Pung independently of packet arrival order");
        const ActionList announced = authority.takeEvents(passer.avatar);
        const auto said = std::find_if(announced.begin(), announced.end(), [](const ActionMessage& action) {
            return action.type() == Action::MahjongGame;
        });
        check(said != announced.end() && said->getBoolean("sayOnly") &&
              std::next(said) != announced.end() && std::next(said)->type() == Action::MahjongGame &&
              !std::next(said)->getBoolean("sayOnly"),
              "committed Game arrives with voice announcement before its guardian animation");
        authority.stop();
    }

    // A real Battle::Session remains owned by the host; only its attacker
    // receives and can answer a pending manual combat choice.
    check(authority.start(duelConfig(606020), &error), "start manual battle fixture: " + error);
    if(authority.active())
    {
        for(const Person& person : config.players) authority.ready(person.avatar);
        LocalPlayer& attacker = *GameData::gamers.playerOfWind(Wind::East);
        LocalPlayer& defender = *GameData::gamers.playerOfWind(Wind::West);
        const Land battlefield(Land::Baliphon);
        GameData::landsInfo[battlefield()].clan = defender.clan;
        for(LocalPlayer& player : GameData::gamers) player.army.clear();
        BattleParty attackParty(attacker.clan, battlefield);
        for(const Creature::creature_t creature : {Creature::AdventureParty, Creature::GreatCarol, Creature::FireElemental})
            attackParty.join(BattleCreature(attacker.clan, creature, GameData::nextBattleUnitId()));
        BattleParty defenseParty(defender.clan, battlefield);
        for(const Creature::creature_t creature : {Creature::Durlock, Creature::StoneGolem})
            defenseParty.join(BattleCreature(defender.clan, creature, GameData::nextBattleUnitId()));
        attacker.army.push_back(attackParty);
        defender.army.push_back(defenseParty);
        GameData::initAdventure();
        authority.tick();
        for(const Person& person : config.players) authority.takeEvents(person.avatar);
        check(authority.submit(attacker.avatar, ClientBattleReady()) && authority.tick(), "start manual battle on host");
        check(GameData::pendingBattle.isValid() && GameData::pendingBattle.session.awaitsChoice(), "real battle waits for manual choice");
        check(hasEvent(authority.resumeEvents(attacker.avatar), Action::AdventureBattleChoice) &&
              !hasEvent(authority.resumeEvents(defender.avatar), Action::AdventureBattleChoice),
              "reconnect restores battle prompt only for attacker");
        const auto battleRevision = authority.revision();
        check(!authority.submit(defender.avatar, ClientBattleChoice(-1, -1, true), &rejection) &&
              authority.revision() == battleRevision, "defender cannot act as attacker during manual battle");
        const std::string beforeInvalidChoice = Replay::authoritativeStateHash();
        check(!authority.submit(attacker.avatar, ClientBattleChoice(2147483647, 2147483647), &rejection) &&
              authority.revision() == battleRevision && Replay::authoritativeStateHash() == beforeInvalidChoice,
              "invalid manual combat choice leaves battle and RNG unchanged");
        if(GameData::pendingBattle.isValid() && GameData::pendingBattle.session.awaitsChoice())
        {
            const auto recommendation = GameData::pendingBattle.session.recommendedChoice();
            check(authority.submit(attacker.avatar, ClientBattleChoice(recommendation.first, recommendation.second)),
                  "attacker submits actual manual actor and target");
            if(GameData::pendingBattle.isValid())
                check(authority.submit(attacker.avatar, ClientBattleChoice(-1, -1, true)), "attacker resolves remaining battle choices");
            check(!GameData::pendingBattle.isValid(), "manual battle completes on authority");
        }
        authority.stop();
    }
    std::cout << "match authority: " << (failures ? "FAILED" : "ok") << '\n';
    return failures ? 1 : 0;
}
