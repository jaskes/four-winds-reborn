#include <chrono>
#include <array>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "network/matchsession.h"
#include "network/clientview.h"
#include "network/securetransport.h"
#include "network/wirejson.h"
#include "contentpackage.h"
#include "matchtopology.h"
#include "recovery.h"
#include "replay.h"
#include "runegameruleset.h"

namespace GameData { extern Wind currentWind; extern Stone dropStone; extern CroupierSet croupier; }

namespace
{
    using namespace Multiplayer;
    using Clock = std::chrono::steady_clock;
    void require(bool condition, const std::string& error)
    { if(!condition) throw std::runtime_error(error); }

    void eventually(const std::function<bool()>& check, const std::string& error)
    {
        const auto deadline = Clock::now() + std::chrono::seconds(4);
        do
        {
            if(check()) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while(Clock::now() < deadline);
        throw std::runtime_error(error);
    }

    JsonObject packet(const char* kind)
    { JsonObject result; result.addString("kind", kind); return result; }

    void send(SecureConnection& peer, const JsonObject& message)
    {
        std::string error;
        require(peer.send(message.toString(), error), "send failed: " + error);
        peer.poll();
    }

    void read(SecureConnection& peer, std::vector<JsonObject>& messages, std::size_t maximum = 4096)
    {
        peer.poll();
        std::string wire, error;
        for(std::size_t count = 0; count < maximum && peer.receive(wire); ++count)
        {
            JsonObject message;
            require(parseWireObject(wire, message, error), error);
            messages.push_back(std::move(message));
        }
    }

    bool hasKind(const std::vector<JsonObject>& messages, const char* kind)
    {
        for(const auto& message : messages) if(message.getString("kind") == kind) return true;
        return false;
    }

    std::size_t countKind(const std::vector<JsonObject>& messages, const char* kind)
    {
        std::size_t count = 0;
        for(const auto& message : messages) if(message.getString("kind") == kind) ++count;
        return count;
    }

    void acknowledge(SecureConnection& peer, std::uint64_t revision)
    {
        auto seen = packet("seen");
        seen.addString("revision", std::to_string(revision));
        send(peer, seen);
    }

    void testHostConsumptionBarrier()
    {
        MatchSession host;
        HostOptions options;
        options.port = 0;
        std::string error;
        require(host.host(options, error), error);
        SecureConnection remote(host.roomCode());
        require(remote.connect("localhost", host.port(), error), error);
        eventually([&] { host.poll(); remote.poll(); return remote.connected(); }, "host transport connection");
        auto hello = packet("hello");
        hello.addInteger("protocol", 1);
        hello.addInteger("rulesContract", 1);
        const auto identity = contentPackageIdentity(activeContentPackageManifest());
        hello.addString("content", identity.id);
        hello.addInteger("contentVersion", identity.version);
        hello.addString("room", host.roomCode());
        hello.addString("name", "Protocol client");
        hello.addString("token", "");
        send(remote, hello);
        std::vector<JsonObject> messages;
        eventually([&] { host.poll(); read(remote, messages); return hasKind(messages, "welcome"); }, "host admission");
        remote.close();
        require(remote.connect("localhost", host.port(), error), error);
        eventually([&] { host.poll(); remote.poll(); return remote.connected(); }, "pregame replacement transport");
        send(remote, hello);
        eventually([&]
        {
            host.poll(); read(remote, messages);
            return countKind(messages, "welcome") == 2;
        }, "closed pregame seat was not freed before replacement admission");
        require(!hasKind(messages, "error") && host.canStart(), "pregame reconnect was permanently rejected as room full");
        for(const auto& message : messages)
            if(message.getString("kind") == "welcome") hello.addString("token", message.getString("token"));
        remote.close();
        require(remote.connect("localhost", host.port(), error), error);
        eventually([&] { host.poll(); remote.poll(); return remote.connected(); }, "known-token pregame transport");
        send(remote, hello);
        eventually([&]
        {
            host.poll(); read(remote, messages);
            return countKind(messages, "welcome") == 3;
        }, "known token did not reconnect its pregame seat");
        require(!hasKind(messages, "error") && host.canStart(), "known pregame token was revoked on disconnect");
        require(host.start(error), error);
        const auto firstRevision = host.revision();
        ActionList events;
        host.takeEvents(events);
        events.clear();
        acknowledge(remote, firstRevision);
        auto ready = packet("ready");
        ready.addString("sequence", "1");
        ready.addString("revision", std::to_string(firstRevision));
        ready.addInteger("phase", Menu::ShowPlayers);
        send(remote, ready);
        for(int i = 0; i < 8; ++i) { host.poll(); remote.poll(); }
        require(host.revision() == firstRevision, "Receiving events prematurely acknowledged host consumption");
        host.takeEvents(events);
        eventually([&] { host.poll(); read(remote, messages); return host.revision() > firstRevision; }, "remote ready stayed blocked after consumption");
        const auto remoteReadyRevision = host.revision();
        require(host.ready(), "queue host phase-ready intent");
        acknowledge(remote, remoteReadyRevision);
        host.takeEvents(events); events.clear(); host.takeEvents(events);
        eventually([&] { host.poll(); read(remote, messages); return host.phase() == Menu::MahjongPart; }, "host queued ready was lost");
        const auto handRevision = host.revision();
        host.takeEvents(events);
        require(!events.empty(), "new hand must produce presentation events");
        acknowledge(remote, handRevision);
        require(host.submit(ClientReady(), events) && host.submit(ClientReady(), events),
                "same-screen consecutive host commands must both queue");
        for(int i = 0; i < 8; ++i) { host.takeEvents(events); host.poll(); remote.poll(); }
        require(host.revision() == handRevision, "Host GameData changed while prior UI events were still queued");
        events.clear(); host.takeEvents(events);
        eventually([&] { host.poll(); read(remote, messages); return host.revision() > handRevision; }, "first queued host command never executed");
        const auto commandRevision = host.revision();
        require(commandRevision == handRevision + 1, "more than one host command ran through a single presentation barrier");
        acknowledge(remote, commandRevision);
        host.takeEvents(events);
        require(!events.empty(), "ClientReady should restore rune presentation events");
        for(int i = 0; i < 8; ++i) { host.poll(); remote.poll(); }
        require(host.revision() == commandRevision, "second host command bypassed the first command's animation");
        events.clear(); host.takeEvents(events);
        eventually([&] { host.poll(); read(remote, messages); return host.revision() > commandRevision; }, "second queued host command was lost");
        require(host.revision() == commandRevision + 1, "queued host command count changed");

        // Lose the socket after authoritative acceptance, before the client
        // can read either the acknowledgement or its resulting snapshot.
        const auto beforeLostAck = host.revision();
        acknowledge(remote, beforeLostAck);
        events.clear(); host.takeEvents(events); events.clear(); host.takeEvents(events);
        auto command = packet("command");
        command.addString("sequence", "2");
        command.addString("revision", std::to_string(beforeLostAck));
        command.addObject("action", ClientReady());
        send(remote, command);
        eventually([&] { host.poll(); return host.revision() > beforeLostAck; }, "lost-ack command not accepted");
        require(host.revision() == beforeLostAck + 1, "unexpected accepted-command revision delta");
        const auto acceptedRevision = host.revision();
        const auto acceptedHash = Replay::authoritativeStateHash();
        remote.close();
        eventually([&] { host.poll(); return host.paused(); }, "disconnect did not pause the match");
        for(int i = 0; i < 8; ++i) host.poll();
        require(host.revision() == acceptedRevision && Replay::authoritativeStateHash() == acceptedHash,
                "disconnected match continued to mutate");
        std::string token;
        for(const auto& message : messages)
            if(message.getString("kind") == "welcome") token = message.getString("token");
        require(token.size() == 64, "reconnect token missing");
        messages.clear();
        require(remote.connect("localhost", host.port(), error), error);
        eventually([&] { host.poll(); remote.poll(); return remote.connected(); }, "reconnect transport failed");
        hello.addString("token", token);
        send(remote, hello);
        eventually([&] { host.poll(); read(remote, messages); return hasKind(messages, "welcome") && hasKind(messages, "state"); }, "seat resume failed");
        send(remote, command);
        eventually([&]
        {
            host.poll(); read(remote, messages);
            for(const auto& response : messages)
                if(response.getString("kind") == "ack" && response.getString("sequence") == "2") return true;
            return false;
        }, "duplicate sequence did not receive cached acknowledgement");
        bool cachedAccepted = false;
        for(const auto& message : messages)
            if(message.getString("kind") == "ack" && message.getString("sequence") == "2")
                cachedAccepted = message.getBoolean("accepted");
        require(cachedAccepted && host.revision() == acceptedRevision &&
                Replay::authoritativeStateHash() == acceptedHash,
                "lost-ack command was applied twice or rejected instead of deduplicated");

        // ClientReady above resumes presentation; it does not draw the first
        // rune. Let the normal authority tick reach a real human choice, and
        // consume its published view before testing command non-mutation.
        // Otherwise a slow peer can acknowledge the pre-draw revision, trigger
        // a legitimate draw, and leave every later command behind that unseen
        // presentation barrier.
        std::size_t consumedMessages = 0;
        std::uint64_t consumedRevision = 0;
        eventually([&]
        {
            host.poll(); read(remote, messages, 1);
            while(consumedMessages < messages.size())
            {
                const auto& message = messages[consumedMessages++];
                if(message.getString("kind") != "state") continue;
                consumedRevision = std::stoull(message.getString("revision"));
                acknowledge(remote, consumedRevision);
            }
            events.clear(); host.takeEvents(events); events.clear(); host.takeEvents(events);
            const auto* current = GameData::players().playerOfWind(GameData::currentWind);
            return current && !current->isAI() &&
                (current->newStone.isValid() || GameData::croupier.hasLuckDraw()) &&
                consumedRevision == host.revision();
        }, "first-draw view never reached a consumed human-choice state");

        const JsonObject cachedRequest = command;
        auto rejectedWithoutMutation = [&](JsonObject request, const char* description, bool stale = false)
        {
            const auto baselineRevision = host.revision();
            const auto baselineHash = Replay::authoritativeStateHash();
            const auto sequence = request.getString("sequence");
            request.addString("revision", std::to_string(baselineRevision - (stale ? 1 : 0)));
            // Cross the 100 ms authority cadence with no incoming command.
            // A human-choice state must remain unchanged regardless of host
            // speed or the delay before the next TLS message is dispatched.
            std::this_thread::sleep_for(std::chrono::milliseconds(120));
            for(int i = 0; i < 8; ++i) { host.poll(); remote.poll(); }
            require(host.revision() == baselineRevision && Replay::authoritativeStateHash() == baselineHash,
                    "rejection fixture was not parked at a stable human choice");
            messages.clear();
            // Deliberately interleave an old accepted acknowledgement with the
            // new rejected one. Dequeue one message at a time so ack and resume
            // cannot accidentally appear atomic on fast loopback transports.
            send(remote, cachedRequest);
            send(remote, request);
            bool rejected = false;
            eventually([&]
            {
                host.poll(); read(remote, messages, 1);
                bool receivedAck = false;
                for(const auto& response : messages)
                {
                    if(response.getString("kind") == "ack" && response.getString("sequence") == sequence)
                    {
                        receivedAck = true;
                        rejected = !response.getBoolean("accepted");
                        require(rejected, description);
                    }
                    if(receivedAck && response.getString("kind") == "state" && response.getBoolean("resume") &&
                       response.getString("revision") == std::to_string(baselineRevision))
                        return true;
                }
                return false;
            }, std::string("timed out awaiting rejection and resume for sequence ") + sequence + ": " + description);
            require(rejected && host.revision() == baselineRevision &&
                    Replay::authoritativeStateHash() == baselineHash, description);
            acknowledge(remote, baselineRevision);
        };
        command.addString("sequence", "3");
        command.addString("avatar", host.localAvatar().toString());
        command.addObject("action", ClientDropIndex(0));
        rejectedWithoutMutation(command, "forged host actor bypassed connection seat binding");
        command = packet("command");
        command.addString("sequence", "4");
        command.addObject("action", ClientSummonCreature(Creature::SkeletonHorde, Land::Maithaius, true));
        rejectedWithoutMutation(command, "forced action mutated authority state");
        command.addString("sequence", "5");
        command.addObject("action", ClientReady());
        rejectedWithoutMutation(command, "stale revision mutated authority state", true);
        host.leave();
    }

    struct FakeHost
    {
        SecureListener listener{std::string(32, 'a')};
        std::unique_ptr<SecureConnection> connection;
        MatchSession client;
        JsonObject view;
        std::vector<JsonObject> received;
        std::string error;

        FakeHost()
        {
            selectActiveMatchTopology(DuelTopologyId, DuelTopologyVersion);
            selectActiveRuneGameRuleset(QuickRuneGameRulesetId, QuickRuneGameRulesetVersion);
            Persons roster;
            roster.emplace_back(Avatar::Nucrus, Clan::Red, Wind::East);
            roster.emplace_back(Avatar::Lakkho, Clan::Yellow, Wind::West);
            require(GameData::initPersons(roster) && GameData::initMahjong(), "client wire fixture initialization");
            GameData::currentWind = Wind::West;
            view = buildClientView(Avatar::Lakkho);
            require(listener.listen(0, error), error);
            require(client.join("localhost", listener.port(), std::string(32, 'a'), "Protocol client", error), error);
            eventually([&]
            {
                client.poll();
                if(!connection) connection = listener.accept();
                if(connection) read(*connection, received);
                return hasKind(received, "hello");
            }, "client hello timeout");
            auto welcome = packet("welcome");
            welcome.addString("token", std::string(64, 'a'));
            welcome.addString("avatar", Avatar(Avatar::Lakkho).toString());
            welcome.addInteger("required", 2);
            welcome.addString("mode", "duel");
            welcome.addString("ruleset", "quick");
            send(*connection, welcome);
            state(10, false);
            ActionList events;
            eventually([&] { poll(); client.takeEvents(events); events.clear(); return client.started(); }, "client initial view timeout");
            client.takeEvents(events);
            poll();
            require(client.localAvatar() == Avatar(Avatar::Lakkho), "client assigned seat mismatch");
        }

        void poll() { client.poll(); read(*connection, received); }
        void state(std::uint64_t revision, bool resume, const ActionList& events = ActionList())
        {
            auto message = packet("state");
            message.addString("revision", std::to_string(revision));
            message.addInteger("phase", view.getInteger("phase"));
            message.addObject("view", view);
            JsonArray serialized;
            for(const auto& event : events) serialized.addObject(event);
            message.addArray("events", serialized);
            message.addBoolean("resume", resume);
            send(*connection, message);
        }
        void ack(std::uint64_t sequence, bool accepted)
        {
            auto message = packet("ack");
            message.addString("sequence", std::to_string(sequence));
            message.addBoolean("accepted", accepted);
            message.addInteger("reason", 0);
            send(*connection, message);
        }
    };

    void testClientCommandFifo()
    {
        FakeHost fake;
        ActionList events;
        require(fake.client.submit(ClientUnitMoved(101, Land::Maithaius), events) &&
                fake.client.submit(ClientUnitMoved(102, Land::Maithaius), events) &&
                fake.client.submit(ClientUnitMoved(103, Land::Maithaius), events),
                "multiple movement commands from one click must queue");
        eventually([&] { fake.poll(); return countKind(fake.received, "command") == 1; }, "first client intent not dispatched");
        for(int i = 0; i < 8; ++i) fake.poll();
        require(countKind(fake.received, "command") == 1, "client sent a second unacknowledged command");
        fake.ack(1, true);
        fake.state(11, false);
        eventually([&] { fake.poll(); fake.client.takeEvents(events); return fake.client.revision() == 11; }, "next state not applied");
        for(int i = 0; i < 8; ++i) fake.poll();
        require(countKind(fake.received, "command") == 1, "second command bypassed state-consumed callback");
        fake.client.takeEvents(events);
        eventually([&] { fake.poll(); return countKind(fake.received, "command") == 2; }, "second queued command was lost");
        std::vector<JsonObject> commands;
        for(const auto& message : fake.received)
            if(message.getString("kind") == "command") commands.push_back(message);
        require(commands[0].getString("revision") == "10" && commands[1].getString("revision") == "11",
                "queued command revision was captured too early");
        require(commands[0].getObject("action")->getInteger("unit") == 101 &&
                commands[1].getObject("action")->getInteger("unit") == 102, "movement FIFO order changed");
        fake.ack(2, false);
        fake.state(11, true);
        for(int i = 0; i < 20; ++i)
        { fake.poll(); fake.client.takeEvents(events); events.clear(); }
        require(countKind(fake.received, "command") == 2, "rejected command did not cancel remaining intent queue");
        require(fake.client.submit(ClientUnitMoved(104, Land::Maithaius), events), "enqueue command before changed turn");
        fake.view.addString("wind:current", Wind(Wind::East).toString());
        fake.state(12, false);
        eventually([&] { fake.poll(); fake.client.takeEvents(events); return fake.client.revision() == 12; }, "changed-turn view not applied");
        for(int i = 0; i < 8; ++i) { fake.client.takeEvents(events); fake.poll(); }
        require(countKind(fake.received, "command") == 2, "queued movement crossed into a different turn");
    }

    void testLateReadyRejectionKeepsNewPhaseCommands()
    {
        FakeHost fake;
        ActionList events;
        fake.view.addInteger("phase", Menu::MahjongSummaryPart);
        fake.view.addObject("winresult", WinResults().toJsonObject());
        fake.state(11, false);
        eventually([&]
        {
            fake.poll(); fake.client.takeEvents(events); events.clear();
            return fake.client.phase() == Menu::MahjongSummaryPart;
        }, "summary phase fixture was not applied");
        fake.client.takeEvents(events);
        require(fake.client.ready(), "summary ready intent was not queued");
        eventually([&] { fake.poll(); return countKind(fake.received, "ready") == 1; },
                   "summary ready intent was not dispatched");

        // Another player's ready completes the phase before our stale ready
        // request is rejected. The new screen can already enqueue its action.
        fake.view.addInteger("phase", Menu::AdventurePart);
        fake.state(12, false);
        eventually([&]
        {
            fake.poll(); fake.client.takeEvents(events); events.clear();
            return fake.client.phase() == Menu::AdventurePart;
        }, "adventure phase fixture was not applied");
        require(fake.client.submit(ClientUnitMoved(201, Land::Maithaius), events),
                "new adventure command was not queued behind old ready acknowledgement");
        fake.ack(1, false);
        fake.state(12, true);
        eventually([&]
        {
            fake.poll(); fake.client.takeEvents(events); events.clear();
            return countKind(fake.received, "command") == 1;
        }, "late summary rejection discarded a command from the new adventure phase");
        for(const auto& message : fake.received)
            if(message.getString("kind") == "command")
                require(message.getString("revision") == "12" &&
                        message.getObject("action")->getInteger("unit") == 201,
                        "new-phase command lost its current revision or selected unit");
    }

    void testClientStateOrdering()
    {
        FakeHost fake;
        ActionList events;
        fake.state(12, false);
        fake.state(11, false);
        // Deliver both packets before the UI applies either one. Comparing
        // against only the applied revision would enqueue both and roll back.
        for(int i = 0; i < 8; ++i) fake.poll();
        for(int i = 0; i < 8; ++i)
        { fake.client.takeEvents(events); events.clear(); fake.poll(); }
        require(fake.client.revision() == 12, "queued older state rolled back the client revision");

        ActionList announcement;
        announcement.push_back(MahjongInfo(Wind::West, "one presentation only"));
        fake.state(13, false, announcement);
        fake.state(13, false, announcement);
        std::size_t delivered = 0;
        for(int i = 0; i < 20; ++i)
        {
            fake.poll(); fake.client.takeEvents(events);
            delivered += events.size(); events.clear();
        }
        require(fake.client.revision() == 13 && delivered == 1,
                "duplicate regular state replayed its presentation events");
        fake.state(12, true);
        for(int i = 0; i < 8; ++i)
        { fake.poll(); fake.client.takeEvents(events); events.clear(); }
        require(fake.client.revision() == 13, "stale resume rolled back an applied state");
    }

    void testEventActorMustBelongToRoster()
    {
        FakeHost fake;
        const auto before = buildClientView(fake.client.localAvatar()).toString();
        ActionList forged;
        forged.push_back(MahjongTurn(Wind::South, Stone(), false, false));
        fake.state(11, false, forged);
        ActionList delivered;
        eventually([&]
        {
            fake.poll(); fake.client.takeEvents(delivered);
            return !fake.client.connected();
        }, "event for an absent Duel seat was delivered to roster-indexing UI code");
        require(delivered.empty() && fake.client.revision() == 10 &&
                buildClientView(fake.client.localAvatar()).toString() == before,
                "invalid event actor mutated the client projection or escaped validation");
    }

    void testConcurrentReadyFairness()
    {
        MatchSession host;
        HostOptions options;
        options.mode = "classic-ffa";
        options.humanSeats = 4;
        options.port = 0;
        options.seed = 606099;
        std::string error;
        require(host.host(options, error), error);
        std::array<SecureConnection, 3> peers{{SecureConnection(host.roomCode()),
            SecureConnection(host.roomCode()), SecureConnection(host.roomCode())}};
        std::array<std::vector<JsonObject>, 3> responses;
        std::array<std::uint64_t, 3> latest{};
        std::array<std::uint64_t, 3> sequence{{1, 1, 1}};
        std::array<bool, 3> inFlight{};
        const auto identity = contentPackageIdentity(activeContentPackageManifest());
        for(std::size_t index = 0; index < peers.size(); ++index)
        {
            require(peers[index].connect("localhost", host.port(), error), error);
            eventually([&] { host.poll(); peers[index].poll(); return peers[index].connected(); },
                       "ready fairness transport connection");
            auto hello = packet("hello");
            hello.addInteger("protocol", 1); hello.addInteger("rulesContract", 1);
            hello.addString("content", identity.id); hello.addInteger("contentVersion", identity.version);
            hello.addString("room", host.roomCode()); hello.addString("name", "Ready retry player");
            hello.addString("token", "");
            send(peers[index], hello);
            eventually([&]
            {
                host.poll(); read(peers[index], responses[index]);
                return hasKind(responses[index], "welcome");
            }, "ready fairness admission");
        }
        require(host.start(error), error);
        const auto initialRevision = host.revision();
        ActionList events;
        auto pump = [&]
        {
            host.poll();
            for(std::size_t index = 0; index < peers.size(); ++index)
            {
                std::vector<JsonObject> batch;
                read(peers[index], batch);
                for(const auto& response : batch)
                {
                    responses[index].push_back(response);
                    if(response.getString("kind") == "state")
                    {
                        latest[index] = std::stoull(response.getString("revision"));
                        acknowledge(peers[index], latest[index]);
                    }
                    if(response.getString("kind") == "ack") inFlight[index] = false;
                    if(response.getString("kind") == "ping") send(peers[index], packet("pong"));
                }
            }
            host.takeEvents(events); events.clear(); host.takeEvents(events); events.clear();
        };
        auto ready = [&](std::size_t index)
        {
            auto request = packet("ready");
            request.addString("sequence", std::to_string(sequence[index]++));
            request.addString("revision", std::to_string(latest[index]));
            request.addInteger("phase", Menu::ShowPlayers);
            send(peers[index], request);
            inFlight[index] = true;
        };
        eventually([&]
        {
            pump();
            return latest[0] == initialRevision && latest[1] == initialRevision && latest[2] == initialRevision;
        }, "ready fairness initial state delivery");
        ready(0);
        eventually([&]
        {
            pump();
            return !inFlight[0] && latest[0] == initialRevision + 1 &&
                latest[1] == initialRevision + 1 && latest[2] == initialRevision + 1;
        }, "first ready vote was not acknowledged");

        std::array<std::size_t, 3> published{};
        for(std::size_t index = 0; index < peers.size(); ++index)
            published[index] = countKind(responses[index], "state");
        ready(0); // Same player's fresh sequence, already-ready phase.
        eventually([&] { pump(); return !inFlight[0]; }, "idempotent ready retry was not acknowledged");
        const auto quietUntil = Clock::now() + std::chrono::milliseconds(100);
        while(Clock::now() < quietUntil)
        {
            pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        require(host.revision() == initialRevision + 1, "duplicate readiness changed match revision");
        for(std::size_t index = 0; index < peers.size(); ++index)
            require(countKind(responses[index], "state") == published[index],
                    "unchanged ready retry republished snapshots and reopened presentation barriers");

        // A low-index client continuously retries its acknowledged vote while
        // both later seats and the host try to become ready. Each peer has only
        // one request in flight, matching the real client command FIFO.
        eventually([&]
        {
            pump();
            if(host.phase() == Menu::MahjongPart) return true;
            for(std::size_t index = 0; index < peers.size(); ++index)
                if(!inFlight[index]) ready(index);
            require(host.ready(), "host ready retry was not accepted for queuing");
            return false;
        }, "repeated early-seat readiness starved later players or host");
        require(host.revision() == initialRevision + 4,
                "four ready votes must initialize exactly one hand without duplicate transitions");
    }

    void testConcurrentDiscardResponses()
    {
        MatchSession host;
        HostOptions options;
        options.mode = "classic-ffa";
        options.humanSeats = 4;
        options.port = 0;
        options.seed = 606098;
        std::string error;
        require(host.host(options, error), error);
        std::array<SecureConnection, 3> peers{{SecureConnection(host.roomCode()),
            SecureConnection(host.roomCode()), SecureConnection(host.roomCode())}};
        std::array<std::vector<JsonObject>, 3> responses;
        std::array<std::uint64_t, 3> latest{};
        const auto identity = contentPackageIdentity(activeContentPackageManifest());
        for(std::size_t index = 0; index < peers.size(); ++index)
        {
            auto& peer = peers[index];
            require(peer.connect("localhost", host.port(), error), error);
            eventually([&] { host.poll(); peer.poll(); return peer.connected(); }, "four-player connection");
            auto hello = packet("hello");
            hello.addInteger("protocol", 1); hello.addInteger("rulesContract", 1);
            hello.addString("content", identity.id); hello.addInteger("contentVersion", identity.version);
            hello.addString("room", host.roomCode()); hello.addString("name", "Concurrent player");
            hello.addString("token", "");
            send(peer, hello);
            eventually([&] { host.poll(); read(peer, responses[index]); return hasKind(responses[index], "welcome"); }, "four-player admission");
        }
        require(host.start(error), error);
        ActionList events;
        auto receiveStates = [&](bool acknowledgeStates)
        {
            for(std::size_t index = 0; index < peers.size(); ++index)
            {
                std::vector<JsonObject> batch;
                read(peers[index], batch);
                for(const auto& response : batch)
                {
                    responses[index].push_back(response);
                    if(response.getString("kind") == "state")
                    {
                        latest[index] = std::stoull(response.getString("revision"));
                        if(acknowledgeStates) acknowledge(peers[index], latest[index]);
                    }
                    if(response.getString("kind") == "ping") send(peers[index], packet("pong"));
                }
            }
        };
        auto consumeHost = [&]
        {
            host.takeEvents(events); events.clear(); host.takeEvents(events); events.clear();
        };
        auto pump = [&]
        {
            host.poll(); receiveStates(true); consumeHost();
        };
        // Ready votes intentionally use sequential current revisions; the
        // concurrency under test is three claims for one shared discard.
        for(std::size_t index = 0; index < peers.size(); ++index)
        {
            eventually([&] { pump(); return latest[index] == host.revision(); }, "ready-state delivery");
            const auto before = host.revision();
            auto ready = packet("ready");
            ready.addString("sequence", "1"); ready.addString("revision", std::to_string(before));
            ready.addInteger("phase", Menu::ShowPlayers);
            send(peers[index], ready);
            eventually([&] { pump(); return host.revision() > before; }, "four-player ready vote");
        }
        require(host.ready(), "queue four-player host ready");
        eventually([&] { pump(); return host.phase() == Menu::MahjongPart; }, "four-player hand initialization");
        eventually([&]
        {
            pump();
            const auto local = GameData::toLocalData(host.localAvatar());
            return local.myPlayer().newStone.isValid() || GameData::croupier.hasLuckDraw();
        }, "host initial draw");
        if(GameData::croupier.hasLuckDraw())
        {
            require(host.submit(ClientLuckChoice(0), events), "initial Luck selection");
            eventually([&] { pump(); return !GameData::croupier.hasLuckDraw(); }, "Luck selection completion");
        }
        require(GameData::currentWind == Wind(Wind::East), "fixture must discard from host East seat");
        require(host.submit(ClientDropIndex(0), events), "queue shared discard");
        eventually([&] { pump(); return GameData::dropStone.isValid(); }, "shared discard publication");
        const auto discardRevision = host.revision();
        const auto discardWind = GameData::currentWind;
        for(std::size_t index = 0; index < peers.size(); ++index)
        {
            auto response = packet("command");
            response.addString("sequence", "2");
            response.addString("revision", std::to_string(discardRevision));
            response.addObject("action", ClientButtonPass());
            send(peers[index], response);
        }
        auto acceptedReplies = [&]
        {
            int count = 0;
            for(const auto& log : responses)
                for(const auto& response : log)
                    if(response.getString("kind") == "ack" && response.getString("sequence") == "2")
                    {
                        require(response.getBoolean("accepted"), "simultaneous response was spuriously rejected as stale");
                        ++count;
                    }
            return count;
        };
        eventually([&]
        {
            host.poll(); receiveStates(true);
            // Hold the final presentation so the next draw cannot conceal an
            // extra unintended advancement in the revision assertion below.
            if(GameData::dropStone.isValid()) consumeHost();
            return acceptedReplies() == 3;
        }, "concurrent same-revision discard responses did not all complete");
        require(!GameData::dropStone.isValid() && GameData::currentWind != discardWind &&
                host.revision() == discardRevision + 3,
                "all-pass resolution must advance the discard once, accepting each vote once");
        const auto resolvedRevision = host.revision();
        const auto resolvedHash = Replay::authoritativeStateHash();
        auto delayed = packet("command");
        delayed.addString("sequence", "3");
        delayed.addString("revision", std::to_string(discardRevision));
        delayed.addObject("action", ClientButtonPass());
        send(peers[0], delayed);
        consumeHost();
        bool rejected = false;
        eventually([&]
        {
            host.poll(); receiveStates(false);
            for(const auto& response : responses[0])
                if(response.getString("kind") == "ack" && response.getString("sequence") == "3")
                    rejected = !response.getBoolean("accepted");
            return rejected;
        }, "delayed response from resolved discard was not rejected");
        require(host.revision() == resolvedRevision && Replay::authoritativeStateHash() == resolvedHash,
                "delayed old-discard response mutated the next turn");
    }
}

int runSessionProtocolTests()
{
    try
    {
        Recovery::setEnabled(false);
        testHostConsumptionBarrier();
        testClientCommandFifo();
        testLateReadyRejectionKeepsNewPhaseCommands();
        testClientStateOrdering();
        testEventActorMustBelongToRoster();
        testConcurrentReadyFairness();
        testConcurrentDiscardResponses();
        std::cout << "Session presentation barriers and command FIFO regressions: ok\n";
        return 0;
    }
    catch(const std::exception& exception)
    {
        std::cerr << "Session protocol regression failed: " << exception.what() << '\n';
        return 1;
    }
}
