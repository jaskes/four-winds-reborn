#include <chrono>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <thread>

#include "network/matchsession.h"
#include "matchscore.h"
#include "recovery.h"

namespace GameData
{
    extern LocalPlayers gamers;
}

namespace
{
    using TestClock = std::chrono::steady_clock;

    void report(const JsonObject & message)
    {
        std::cout << "FWR_MP " << message.toString() << std::endl;
    }

    JsonObject record(const char* kind, const Multiplayer::MatchSession & session)
    {
        JsonObject result;
        result.addString("kind", kind);
        result.addString("peer", session.isHost() ? "host" : "client");
        result.addString("avatar", session.localAvatar().toString());
        result.addInteger("phase", session.phase());
        result.addString("revision", std::to_string(session.revision()));
        return result;
    }

    JsonArray scores()
    {
        JsonArray result;
        for(const auto & player : MatchScore::current())
        {
            JsonObject item;
            item.addString("avatar", player.person.avatar.toString());
            item.addString("clan", player.person.clan.toString());
            item.addInteger("total", player.totalScore);
            item.addInteger("rank", player.finalRank);
            item.addInteger("team", player.teamId);
            item.addInteger("teamScore", player.teamScore);
            item.addInteger("teamRank", player.teamRank);
            JsonArray categories;
            for(const auto & category : player.categories)
            {
                JsonObject score;
                score.addInteger("score", category.score);
                score.addInteger("rank", category.rank);
                score.addInteger("points", category.standingPoints);
                categories.addObject(score);
            }
            item.addArray("categories", categories);
            result.addObject(item);
        }
        return result;
    }

    bool combatFixture()
    {
        // Test-only, host-only initial position. Keep the real recipes, map
        // ownership and battle rules; all subsequent actions use the session.
        if(GameData::players().size() != 2) return false;
        auto & first = GameData::gamers[0];
        auto & second = GameData::gamers[1];
        Land home, enemy;
        for(const auto id : lands_all)
        {
            const Land candidate(id);
            if(candidate.isTowerWinds() || GameData::landInfo(candidate).clan != first.clan) continue;
            for(const Land adjacent : GameData::landInfo(candidate).borders)
                if(!adjacent.isTowerWinds() && GameData::landInfo(adjacent).clan == second.clan)
                { home = candidate; enemy = adjacent; break; }
            if(home.isValid()) break;
        }
        if(!home.isValid()) return false;
        first.points = second.points = 2000;
        first.army.clear(); second.army.clear();
        BattleParty attackers(first.clan, home), defenders(second.clan, enemy);
        if(!attackers.join(BattleCreature(first.clan, Creature::StoneGolem, GameData::nextBattleUnitId())) ||
           !defenders.join(BattleCreature(second.clan, Creature::KnightTemplar, GameData::nextBattleUnitId()))) return false;
        first.army.push_back(attackers);
        second.army.push_back(defenders);
        return true;
    }

    std::unique_ptr<ClientMessage> summon(const LocalData & view)
    {
        const auto & mine = view.myPlayer();
        if(mine.isCasted() || mine.isSilenced() || mine.isAffectedSpell(Spell::ManaFog) ||
           mine.army.isMaximumSummoning()) return nullptr;
        for(const Creature creature : GameData::avatarInfo(mine.avatar).creatures)
        {
            const auto & recipe = GameData::creatureInfo(creature);
            // Nonunique choices avoid inferring globally hidden unique units.
            if(recipe.unique || recipe.cost > mine.points || recipe.stones.empty() ||
               !mine.stones.allowCast(recipe.stones, mine.newStone)) continue;
            for(const auto id : lands_all)
            {
                const Land land(id);
                const auto & info = GameData::landInfo(land);
                const auto* party = mine.army.findPartyConst(land);
                if(info.stat.power && info.clan == mine.clan && (!party || party->canJoin()))
                    return std::make_unique<ClientSummonCreature>(creature, land);
            }
        }
        return nullptr;
    }

    std::unique_ptr<ClientMessage> invade(const LocalData & view, const std::set<int> & moved)
    {
        const auto & mine = view.myPlayer();
        for(const auto & party : mine.army)
        {
            for(const auto* unit : party.toBattleCreatures())
            {
                if(!unit || moved.count(unit->battleUnit()) || unit->freeMovePoint() <= 0) continue;
                for(const Land destination : GameData::landInfo(party.land()).borders)
                {
                    const auto & info = GameData::landInfo(destination);
                    const auto* destinationParty = mine.army.findPartyConst(destination);
                    if(destination.isTowerWinds() || GameData::allied(mine.clan, info.clan) ||
                       (destinationParty && !destinationParty->canJoin())) continue;
                    Lands path;
                    path.push_back(destination);
                    if(mine.army.canMoveCreature(*unit, party.land(), path))
                        return std::make_unique<ClientUnitMoved>(unit->battleUnit(), destination);
                }
            }
        }
        return nullptr;
    }
}

// Runs after the regression executable's ordinary theme/data bootstrap. The
// decision policy observes only the recipient's LocalData and delivered events;
// it never reads the authority, the wall, enemy hands, seeds or hidden flags.
int runMultiplayerProcessPeer(int argc, char** argv)
{
    if(argc < 3 || (std::string(argv[2]) != "host" && std::string(argv[2]) != "client"))
    {
        std::cerr << "multiplayer process peer requires host or client\n";
        return 2;
    }
    const bool hosting = std::string(argv[2]) == "host";
    const bool combat = argc > 5 && std::string(argv[5]) == "combat";
    Recovery::setEnabled(false);
    auto & session = Multiplayer::session();
    std::string error;
    if(hosting)
    {
        Multiplayer::HostOptions options;
        options.port = 0;
        options.name = "Process host";
        options.mode = argc > 3 ? argv[3] : "duel";
        options.ruleset = "quick";
        options.humanSeats = 2;
        if(combat) options.seed = 606005;
        if(argc > 4)
        {
            try { options.humanSeats = std::stoi(argv[4]); }
            catch(const std::exception&) { return 2; }
        }
        if(!session.host(options, error)) { std::cerr << error << '\n'; return 2; }
        auto room = record("room", session);
        room.addInteger("port", session.port());
        room.addString("code", session.roomCode());
        room.addString("mode", options.mode);
        room.addInteger("humans", options.humanSeats);
        report(room);
    }
    else
    {
        if(argc != 5 && argc != 6) { std::cerr << "client requires port and room code\n"; return 2; }
        int port = 0;
        try { port = std::stoi(argv[3]); }
        catch(const std::exception&) { return 2; }
        if(port <= 0 || port > 65535 || !session.join("127.0.0.1", static_cast<std::uint16_t>(port),
            argv[4], "Process client", error)) { std::cerr << error << '\n'; return 2; }
    }

    const auto deadline = TestClock::now() + std::chrono::seconds(220);
    auto nextReport = TestClock::now() + std::chrono::seconds(5);
    auto nextDecision = TestClock::now();
    auto rejectionRetry = TestClock::now();
    TestClock::time_point completed;
    bool finished = false;
    int previousPhase = -1;
    std::uint64_t previousRevision = 0;
    std::uint64_t lastCommandRevision = 0;
    int lastAction = Action::None;
    std::set<int> visited;
    int discards = 0, passes = 0, mapTurns = 0;
    int summons = 0, moves = 0, combats = 0, battleChoices = 0;
    bool luckPrompt = false, battlePrompt = false;
    std::string passedDiscard;
    std::string summonAttempt;
    std::set<int> movedUnits;
    std::set<std::string> completedMapTurns;
    ActionList events;

    while(TestClock::now() < deadline)
    {
        session.poll();
        session.takeEvents(events);
        const auto now = TestClock::now();
        if(!session.active() || session.status().find("Invalid server") != std::string::npos ||
           session.status().find("Network session error") != std::string::npos)
        {
            auto failed = record("error", session);
            failed.addString("message", session.status()); report(failed);
            session.leave();
            return 1;
        }
        if(hosting && session.canStart())
        {
            if(!session.start(error)) { std::cerr << "start failed: " << error << '\n'; return 1; }
            if(combat && !combatFixture()) { std::cerr << "combat fixture failed\n"; return 1; }
            session.takeEvents(events);
        }
        if(session.started())
        {
            const int phase = session.phase();
            if(phase != previousPhase)
            {
                visited.insert(phase);
                report(record("phase", session));
                previousPhase = phase;
                luckPrompt = battlePrompt = false;
                passedDiscard.clear();
                summonAttempt.clear();
                movedUnits.clear();
                lastAction = Action::None;
                nextDecision = now;
            }
            const auto currentView = GameData::toLocalData(session.localAvatar());
            if((phase == Menu::AdventurePart || phase == Menu::BattleSummaryPart) &&
               currentView.myPlayer().adventurePartDone())
            {
                completedMapTurns.insert(currentView.roundWind.toString() + ":" + currentView.partWind.toString());
                mapTurns = static_cast<int>(completedMapTurns.size());
            }
            const auto discardKey = currentView.dropStone.isValid() ?
                currentView.roundWind.toString() + ":" + currentView.partWind.toString() + ":" +
                currentView.currentWind.toString() + ":" + std::to_string(currentView.trashSet.size()) + ":" +
                currentView.dropStone.toString() : std::string();
            for(const auto & event : events)
            {
                if(event.type() == Action::MahjongLuckChoice) luckPrompt = true;
                if(event.type() == Action::AdventureBattleChoice)
                {
                    const auto* legend = event.getObject("legend");
                    const auto* actors = event.getArray("actors");
                    if(phase != Menu::AdventurePart || !legend ||
                       legend->getString("attacker") != session.localAvatar().toString() ||
                       !actors || actors->size() == 0)
                    {
                        auto failure = record("error", session);
                        failure.addString("message", "private battle choice reached the wrong player or phase");
                        report(failure);
                        session.leave();
                        return 1;
                    }
                    battlePrompt = true;
                    ++battleChoices;
                }
                if(event.type() == Action::AdventureCombat) ++combats;
                const bool actor = Wind(event.getString("currentWind")) == currentView.myPlayer().wind;
                if(event.type() == Action::MahjongDrop && actor) ++discards;
                if(event.type() == Action::MahjongPass && actor) ++passes;
                if(event.type() == Action::MahjongSummon && actor) ++summons;
                if(event.type() == Action::AdventureMoves && actor)
                { ++moves; movedUnits.insert(event.getInteger("unit")); }
                if(event.type() == Action::MahjongPass && !discardKey.empty() &&
                   Wind(event.getString("currentWind")) == currentView.myPlayer().wind)
                    passedDiscard = discardKey;
            }
            events.clear();
            if(phase == Menu::GameSummaryPart && !finished)
            {
                auto final = record("result", session);
                final.addArray("scores", scores());
                JsonArray phases;
                for(int value : visited) phases.addInteger(value);
                final.addArray("visited", phases);
                final.addInteger("discards", discards);
                final.addInteger("passes", passes);
                final.addInteger("mapTurns", mapTurns);
                final.addInteger("summons", summons);
                final.addInteger("moves", moves);
                final.addInteger("combats", combats);
                final.addInteger("battleChoices", battleChoices);
                report(final);
                finished = true;
                completed = now;
            }
            if(finished)
            {
                // Let queued final-state bytes and acknowledgements leave both
                // processes before either process closes its sockets.
                if(now - completed >= std::chrono::seconds(2)) { session.leave(); return 0; }
            }
            else if(!session.paused())
            {
                if(previousRevision != session.revision()) nextDecision = now;
                previousRevision = session.revision();
                if(now >= nextDecision)
                {
                    // Decide from the current public view. Successful intents
                    // wait for a new revision; explicit rejections permit a
                    // delayed retry without flooding the command FIFO.
                    nextDecision = now + std::chrono::milliseconds(25);
                    if(phase == Menu::ShowPlayers || phase == Menu::MahjongSummaryPart ||
                       phase == Menu::BattleSummaryPart)
                    {
                        session.ready();
                    }
                    else
                    {
                        const LocalData view = GameData::toLocalData(session.localAvatar());
                        const LocalPlayer & mine = view.myPlayer();
                        std::unique_ptr<ClientMessage> command;
                        if(phase == Menu::MahjongPart)
                        {
                            if(luckPrompt) command = std::make_unique<ClientLuckChoice>(0);
                            else if(view.dropStone.isValid() && view.currentWind != mine.wind &&
                                    passedDiscard != discardKey)
                                command = std::make_unique<ClientButtonPass>();
                            else if(!view.dropStone.isValid() && view.yourTurn() && mine.newStone.isValid())
                            {
                                const auto turn = view.roundWind.toString() + ":" + view.partWind.toString() + ":" +
                                    std::to_string(view.stoneLastCount);
                                if(combat && summonAttempt != turn)
                                {
                                    command = summon(view);
                                    summonAttempt = turn;
                                }
                                if(!command) command = std::make_unique<ClientDropIndex>(static_cast<int>(mine.stones.size()));
                            }
                        }
                        else if(phase == Menu::AdventurePart)
                        {
                            if(battlePrompt) command = std::make_unique<ClientBattleChoice>(-1, -1, true);
                            else if(view.yourTurn() && !mine.adventurePartDone())
                            {
                                if(combat) command = invade(view, movedUnits);
                                if(!command) command = std::make_unique<ClientBattleReady>();
                            }
                        }
                        const bool retryRejected = now >= rejectionRetry &&
                            session.status() == "State changed; choose again";
                        if(command && (lastCommandRevision != session.revision() || retryRejected))
                        {
                            lastAction = command->type();
                            if(session.submit(*command, events))
                            {
                                lastCommandRevision = session.revision();
                                rejectionRetry = now + std::chrono::milliseconds(250);
                                if(command->type() == Action::ClientLuckChoice) luckPrompt = false;
                                if(command->type() == Action::ClientBattleChoice) battlePrompt = false;
                            }
                        }
                    }
                }
            }
        }
        if(now >= nextReport)
        {
            auto progress = record("progress", session);
            progress.addBoolean("connected", session.connected());
            progress.addString("status", session.status());
            progress.addInteger("lastAction", lastAction);
            progress.addInteger("discards", discards);
            report(progress);
            nextReport = now + std::chrono::seconds(5);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    auto failure = record("error", session);
    failure.addString("message", "process peer timed out: " + session.status());
    failure.addInteger("lastAction", lastAction);
    report(failure);
    session.leave();
    return 1;
}
