#include "matchsession.h"
#include "matchauthority.h"
#include "clientview.h"
#include "commandwire.h"
#include "securetransport.h"
#include "networkaddress.h"
#include "wirejson.h"
#include "contentpackage.h"
#include "matchtopology.h"
#include "runegameruleset.h"
#include "settings.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <deque>
#include <limits>
#include <stdexcept>

namespace GameData { extern Wind currentWind; extern Stone dropStone; }

namespace Multiplayer
{
namespace
{
using Clock = std::chrono::steady_clock;
constexpr int ProtocolVersion = 1;
constexpr int RulesContractVersion = 1;

std::string nonce(std::size_t bytes)
{
    std::string result;
    std::string error;
    if(!secureRandomHex(bytes, result, error)) throw std::runtime_error(error);
    return result;
}

JsonObject envelope(const char* kind)
{
    JsonObject result;
    result.addString("kind", kind);
    return result;
}

bool counter(const JsonObject& obj, const char* key, std::uint64_t& result)
{
    if(!obj.isString(key)) return false;
    const std::string value = obj.getString(key);
    if(value.empty() || value.size() > 20 || (value.size() > 1 && value[0] == '0')) return false;
    result = 0;
    for(char digit : value)
    {
        if(digit < '0' || digit > '9') return false;
        const auto next = static_cast<unsigned>(digit - '0');
        if(result > (std::numeric_limits<std::uint64_t>::max() - next) / 10) return false;
        result = result * 10 + next;
    }
    return true;
}

bool cleanName(const std::string& value)
{
    return !value.empty() && value.size() <= 48 &&
        std::none_of(value.begin(), value.end(), [](unsigned char c) { return c < 32 || c == 127; });
}

bool hexToken(const JsonObject& packet, const char* key, std::size_t length)
{
    if(!packet.isString(key)) return false;
    const auto value = packet.getString(key);
    return value.size() == length && std::all_of(value.begin(), value.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

MatchConfig configFor(const HostOptions& options)
{
    MatchConfig config;
    config.seed = options.seed;
    config.topologyId = options.mode == "duel" ? DuelTopologyId :
        options.mode == "coalition" ? CoalitionTopologyId : ClassicFreeForAllTopologyId;
    config.topologyVersion = options.mode == "duel" ? DuelTopologyVersion : 1;
    config.rulesetId = options.ruleset == "quick" ? QuickRuneGameRulesetId : ClassicRuneGameRulesetId;
    config.rulesetVersion = 1;
    const std::array<Clan, 4> clans{Clan(Clan::Red), Clan(Clan::Yellow), Clan(Clan::Aqua), Clan(Clan::Purple)};
    const std::array<Avatar, 4> avatars{Avatar("nucrus"), Avatar("lakkho"), Avatar("ziag"), Avatar("dayla")};
    const auto* topology = findMatchTopology(config.topologyId, config.topologyVersion);
    for(std::size_t i = 0; i < topology->winds().size(); ++i)
    {
        Person person(avatars[i], clans[i], Wind(topology->winds()[i]));
        person.setAI(static_cast<int>(i) >= options.humanSeats);
        config.players.push_back(person);
    }
    return config;
}
}

struct MatchSession::Impl
{
    struct Seat
    {
        std::string name;
        std::string token;
        std::unique_ptr<SecureConnection> connection;
        Clock::time_point received = Clock::now();
        std::uint64_t consumed = 0;
        std::uint64_t lastSequence = 0;
        std::deque<std::pair<std::uint64_t, JsonObject>> acknowledgements;
        JsonObject request;
    };
    struct Incoming
    {
        std::unique_ptr<SecureConnection> connection;
        Clock::time_point accepted = Clock::now();
    };
    struct Intent
    {
        JsonObject action;
        int phase = Menu::MainMenu;
        int wind = Wind::None;
        int battleChoice = -1;
        std::uint64_t discardRevision = 0;
        bool ready = false;
    };
    bool enabled = false;
    bool hosting = false;
    bool begun = false;
    bool welcomed = false;
    bool serverPaused = false;
    bool helloSent = false;
    bool polling = false;
    HostOptions options;
    MatchConfig config;
    MatchAuthority authority;
    std::unique_ptr<SecureListener> listener;
    std::vector<Incoming> incoming;
    std::array<Seat, 4> seats;
    std::vector<std::string> names;
    std::unique_ptr<SecureConnection> server;
    std::string address, code, name, token, message;
    std::uint16_t remotePort = 0;
    Avatar avatar;
    int currentPhase = Menu::ShowPlayers;
    int occupied = 1;
    int required = 2;
    std::uint64_t applied = 0;
    std::uint64_t nextSequence = 1;
    std::uint64_t sentSequence = 0;
    int nextSeat = 0;
    JsonObject pending;
    Intent pendingIntent;
    std::deque<Intent> commands;
    std::deque<JsonObject> updates;
    ActionList localEvents;
    bool localDeliveryPending = false;
    bool awaitingConsumption = false;
    std::uint64_t deliveredRevision = 0;
    std::uint64_t clientConsumed = 0;
    int battleChoice = -1;
    Clock::time_point lastReceive = Clock::now();
    Clock::time_point lastPing = Clock::now();
    Clock::time_point nextConnect = Clock::now();
    Clock::time_point nextTick = Clock::now();

    bool allConnected() const
    {
        for(int i = 1; i < required; ++i)
            if(!seats[i].connection || !seats[i].connection->connected()) return false;
        return true;
    }
    bool allConsumed() const
    {
        for(int i = 0; i < required; ++i)
            if(seats[i].consumed < authority.revision()) return false;
        return true;
    }
    bool sameContext(const Intent& intent) const
    {
        return intent.phase == currentPhase && (intent.ready ||
            (intent.wind == GameData::currentWind() &&
             (intent.action.getInteger("type") != Action::ClientBattleChoice ||
              intent.battleChoice == battleChoice)));
    }
    void cancelCommands()
    {
        if(!commands.empty()) message = "State changed; choose again";
        commands.clear();
    }
    void observeEvents(const ActionList& events)
    {
        for(const auto& event : events)
            if(event.type() == Action::AdventureBattleChoice)
                battleChoice = event.getInteger("choiceNumber", -1);
    }
    void send(SecureConnection& connection, const JsonObject& packet)
    {
        std::string error;
        if(!connection.send(packet.toString(), error)) connection.close();
    }
    void lobby()
    {
        occupied = 1;
        for(int i = 1; i < required; ++i)
            if(seats[i].connection && seats[i].connection->connected()) ++occupied;
        JsonObject packet = envelope("lobby");
        packet.addInteger("occupied", occupied);
        packet.addInteger("required", required);
        packet.addString("mode", options.mode);
        packet.addString("ruleset", options.ruleset);
        packet.addBoolean("paused", begun && !allConnected());
        JsonArray roster;
        for(int i = 0; i < required; ++i)
            roster.addString(i == 0 || seats[i].connection ? seats[i].name : "");
        packet.addArray("names", roster);
        for(int i = 1; i < required; ++i)
            if(seats[i].connection) send(*seats[i].connection, packet);
    }
    JsonObject state(int index, bool resume)
    {
        const Avatar recipient = config.players[index].avatar;
        auto packet = envelope("state");
        packet.addString("revision", std::to_string(authority.revision()));
        packet.addInteger("phase", authority.phase());
        packet.addObject("view", buildClientView(recipient));
        packet.addBoolean("resume", resume);
        JsonArray events;
        // A resume supersedes old undelivered events, rather than replaying
        // them again in the next regular publication.
        if(resume) authority.takeEvents(recipient);
        const auto emitted = resume ? authority.resumeEvents(recipient) : authority.takeEvents(recipient);
        for(const auto& event : emitted)
        {
            const auto wire = eventToWire(event, recipient);
            if(wire.size()) events.addObject(wire);
        }
        packet.addArray("events", events);
        return packet;
    }
    void publish()
    {
        currentPhase = authority.phase();
        auto emitted = authority.takeEvents(avatar);
        observeEvents(emitted);
        localEvents.splice(localEvents.end(), emitted);
        localDeliveryPending = true;
        seats[0].consumed = 0;
        for(int i = 1; i < required; ++i)
            if(seats[i].connection)
            {
                seats[i].consumed = 0;
                send(*seats[i].connection, state(i, false));
            }
        if(!commands.empty() && !sameContext(commands.front())) cancelCommands();
    }
    void welcome(int index)
    {
        auto packet = envelope("welcome");
        packet.addString("token", seats[index].token);
        packet.addString("avatar", config.players[index].avatar.toString());
        packet.addInteger("required", required);
        packet.addString("mode", options.mode);
        packet.addString("ruleset", options.ruleset);
        send(*seats[index].connection, packet);
        if(begun)
        {
            seats[index].consumed = 0;
            send(*seats[index].connection, state(index, true));
        }
        lobby();
    }
    void reject(SecureConnection& connection, const std::string& reason)
    {
        auto packet = envelope("error");
        packet.addString("message", reason);
        send(connection, packet);
        connection.poll();
        connection.close();
    }
    bool admit(Incoming& peer, const JsonObject& packet)
    {
        const auto identity = contentPackageIdentity(activeContentPackageManifest());
        if(!packet.isString("kind") || packet.getString("kind") != "hello" || !packet.isInteger("protocol") ||
           packet.getInteger("protocol") != ProtocolVersion || !packet.isInteger("rulesContract") ||
           packet.getInteger("rulesContract") != RulesContractVersion ||
           !packet.isString("content") || packet.getString("content") != identity.id || !packet.isInteger("contentVersion") ||
           packet.getInteger("contentVersion") != identity.version ||
           !hexToken(packet, "room", code.size()) || packet.getString("room") != code ||
           !packet.isString("name") || !cleanName(packet.getString("name")) ||
           !packet.isString("token") ||
           (!packet.getString("token").empty() && !hexToken(packet, "token", 64)))
        {
            reject(*peer.connection, "Room code or game version does not match");
            return false;
        }
        int chosen = -1;
        const auto reconnect = packet.getString("token");
        for(int i = 1; i < required; ++i)
        {
            if(!reconnect.empty() && seats[i].token == reconnect) { chosen = i; break; }
            if(reconnect.empty() && !begun && !seats[i].connection && chosen < 0) chosen = i;
        }
        if(chosen < 0)
        {
            reject(*peer.connection, "Room is full or reconnect token is invalid");
            return false;
        }
        auto& seat = seats[chosen];
        if(seat.connection) seat.connection->close();
        seat.connection = std::move(peer.connection);
        seat.name = packet.getString("name");
        // A known token may resume its disconnected pregame seat. A newcomer
        // can reuse that seat, receiving a fresh token that revokes the old one.
        if(reconnect.empty()) seat.token = nonce(32);
        seat.received = Clock::now();
        welcome(chosen);
        return true;
    }
    void executeRequest(int index, const JsonObject& packet)
    {
        const auto before = authority.revision();
        auto& seat = seats[index];
        const auto kind = packet.getString("kind");
        std::uint64_t sequence = 0, revision = 0;
        counter(packet, "sequence", sequence);
        counter(packet, "revision", revision);
        auto ack = envelope("ack");
        ack.addString("sequence", std::to_string(sequence));
        bool accepted = false;
        ActionRejection rejection;
        if(begun)
        {
            if(kind == "ready")
                accepted = revision == authority.revision() && packet.isInteger("phase") && packet.getInteger("phase") == authority.phase()
                    && authority.ready(config.players[index].avatar, &rejection);
            else if(const auto* body = packet.getObject("action"))
            {
                std::string error;
                auto command = commandFromWire(*body, error);
                if(command && (revision == authority.revision() ||
                    authority.acceptsClaimRevision(config.players[index].avatar, *command, revision)))
                    accepted = authority.submit(config.players[index].avatar, *command, &rejection);
            }
        }
        ack.addBoolean("accepted", accepted);
        ack.addInteger("reason", static_cast<int>(rejection.reason));
        seat.lastSequence = sequence;
        seat.acknowledgements.emplace_back(sequence, ack);
        if(seat.acknowledgements.size() > 64) seat.acknowledgements.pop_front();
        send(*seat.connection, ack);
        if(accepted && authority.revision() != before) publish();
        else if(!accepted && begun)
        {
            seat.consumed = 0;
            send(*seat.connection, state(index, true));
        }
    }
    void hostPacket(int index, const JsonObject& packet)
    {
        auto& seat = seats[index];
        const auto kind = packet.getString("kind");
        if(kind == "ping") { send(*seat.connection, envelope("pong")); return; }
        if(kind == "pong") return;
        if(kind == "seen")
        {
            std::uint64_t revision = 0;
            if(counter(packet, "revision", revision) && revision <= authority.revision())
                seat.consumed = std::max(seat.consumed, revision);
            else seat.connection->close();
            return;
        }
        std::uint64_t sequence = 0, revision = 0;
        if((kind != "command" && kind != "ready") || !counter(packet, "sequence", sequence) ||
           sequence == 0 || !counter(packet, "revision", revision))
        { seat.connection->close(); return; }
        if(sequence <= seat.lastSequence)
        {
            for(const auto& saved : seat.acknowledgements)
                if(saved.first == sequence) { send(*seat.connection, saved.second); return; }
            reject(*seat.connection, "Command sequence expired"); return;
        }
        if(sequence != seat.lastSequence + 1) { seat.connection->close(); return; }
        if(seat.request.size())
        {
            // A reconnect may retry the one in-flight command before its
            // presentation barrier opens. Never enqueue that command twice.
            if(seat.request.toString() != packet.toString()) seat.connection->close();
            return;
        }
        seat.request = packet;
    }
    void executeLocal()
    {
        if(commands.empty()) return;
        if(!sameContext(commands.front())) { cancelCommands(); return; }
        const auto intent = std::move(commands.front());
        commands.pop_front();
        const auto before = authority.revision();
        bool accepted = false;
        if(intent.ready) accepted = authority.ready(avatar);
        else
        {
            std::string error;
            auto command = commandFromWire(intent.action, error);
            if(command && (!intent.discardRevision || intent.discardRevision == authority.revision() ||
                authority.acceptsClaimRevision(avatar, *command, intent.discardRevision)))
                accepted = authority.submit(avatar, *command);
        }
        if(accepted && authority.revision() != before) publish();
        else if(!accepted)
        {
            cancelCommands();
            message = "State changed; choose again";
            auto resumed = authority.resumeEvents(avatar);
            observeEvents(resumed);
            localEvents.splice(localEvents.end(), resumed);
            localDeliveryPending = true;
            seats[0].consumed = 0;
        }
    }
    bool sweepSeats(Clock::time_point now)
    {
        bool changed = false;
        for(int i = 1; i < required; ++i)
        {
            auto& seat = seats[i];
            if(!seat.connection) continue;
            seat.connection->poll();
            if(now - seat.received > std::chrono::seconds(15)) seat.connection->close();
            if(!seat.connection->closed()) continue;
            seat.connection.reset();
            if(!begun)
            {
                seat.request.clear();
                seat.acknowledgements.clear(); seat.lastSequence = seat.consumed = 0;
            }
            changed = true;
        }
        return changed;
    }
    void hostPoll()
    {
        const auto now = Clock::now();
        // Free closed lobby seats before processing a replacement hello.
        bool changed = sweepSeats(now);
        for(int count = 0; count < 8; ++count)
        {
            auto accepted = listener->accept();
            if(!accepted) break;
            if(incoming.size() >= 8) accepted->close();
            else incoming.push_back(Incoming{std::move(accepted), now});
        }
        for(auto& peer : incoming)
        {
            peer.connection->poll();
            std::string wire;
            if(peer.connection->receive(wire))
            {
                JsonObject packet;
                std::string error;
                if(wire.size() > 4096 || !parseWireObject(wire, packet, error)) peer.connection->close();
                else admit(peer, packet);
            }
            if(peer.connection && now - peer.accepted > std::chrono::seconds(5)) peer.connection->close();
        }
        incoming.erase(std::remove_if(incoming.begin(), incoming.end(),
            [](const Incoming& peer) { return !peer.connection || peer.connection->closed(); }), incoming.end());
        for(int i = 1; i < required; ++i)
        {
            auto& seat = seats[i];
            if(!seat.connection) continue;
            seat.connection->poll();
            std::string wire;
            for(int count = 0; count < 32 && seat.connection->receive(wire); ++count)
            {
                JsonObject packet;
                std::string error;
                if(wire.size() > 16384 || !parseWireObject(wire, packet, error))
                { seat.connection->close(); break; }
                seat.received = now;
                hostPacket(i, packet);
            }
        }
        changed = sweepSeats(now) || changed;
        if(changed) lobby();
        if(now - lastPing > std::chrono::seconds(2))
        {
            for(int i = 1; i < required; ++i)
                if(seats[i].connection) send(*seats[i].connection, envelope("ping"));
            lastPing = now;
        }
        if(begun && allConnected() && allConsumed())
        {
            // Incoming commands remain queued while any screen is consuming
            // an earlier revision. The same rule covers host button clicks.
            bool executed = false;
            // Rotate across humans, including the host. Repeated ready votes
            // from an earlier seat cannot starve another player's first vote.
            for(int offset = 0; offset < required && !executed; ++offset)
            {
                const int i = (nextSeat + offset) % required;
                if(i == 0 && !commands.empty())
                {
                    executeLocal();
                    nextSeat = 1;
                    executed = true;
                }
                else if(i != 0 && seats[i].request.size())
                {
                    auto request = std::move(seats[i].request);
                    seats[i].request.clear();
                    executeRequest(i, request);
                    nextSeat = (i + 1) % required;
                    executed = true;
                }
            }
            if(!executed && now >= nextTick)
            {
                nextTick = now + std::chrono::milliseconds(100);
                if(authority.tick()) publish();
            }
        }
        message = begun ? (allConnected() ? "Connected" : "Waiting for disconnected player") :
            std::to_string(occupied) + " / " + std::to_string(required) + " players connected";
    }
    void clientPacket(const JsonObject& packet)
    {
        const auto kind = packet.getString("kind");
        if(kind == "ping") { send(*server, envelope("pong")); return; }
        if(kind == "pong") return;
        if(kind == "welcome")
        {
            const auto assigned = Avatar(packet.getString("avatar"));
            if(!packet.isString("avatar") || !assigned.isValid() || !hexToken(packet, "token", 64) ||
               !packet.isInteger("required") || packet.getInteger("required") < 2 || packet.getInteger("required") > 4)
            { server->close(); return; }
            if(!packet.isString("mode") || !packet.isString("ruleset") ||
               (packet.getString("mode") != "duel" && packet.getString("mode") != "classic-ffa" && packet.getString("mode") != "coalition") ||
               (packet.getString("ruleset") != "quick" && packet.getString("ruleset") != "classic"))
            { server->close(); return; }
            avatar = assigned;
            token = packet.getString("token");
            required = packet.getInteger("required");
            options.mode = packet.getString("mode");
            options.ruleset = packet.getString("ruleset");
            welcomed = true;
            awaitingConsumption = false;
            clientConsumed = 0;
            message = "Connected; waiting for host";
            if(pending.size()) send(*server, pending);
            return;
        }
        if(kind == "lobby" && welcomed)
        {
            const auto* roster = packet.getArray("names");
            if(!packet.isInteger("occupied") || packet.getInteger("occupied") < 1 ||
               packet.getInteger("occupied") > required || !packet.isBoolean("paused") ||
               !roster || roster->size() != static_cast<std::size_t>(required))
            { server->close(); return; }
            std::vector<std::string> nextNames;
            for(std::size_t i = 0; i < roster->size(); ++i)
            {
                if(!roster->getValue(i)->isString() ||
                   (!roster->getString(i).empty() && !cleanName(roster->getString(i))))
                { server->close(); return; }
                nextNames.push_back(roster->getString(i));
            }
            names = std::move(nextNames);
            occupied = packet.getInteger("occupied");
            serverPaused = packet.getBoolean("paused");
            return;
        }
        if(kind == "state" && welcomed)
        {
            std::uint64_t revision = 0;
            if(!counter(packet, "revision", revision) || !packet.isObject("view") ||
               !packet.isArray("events") || !packet.isInteger("phase") || !packet.isBoolean("resume") ||
               packet.getArray("events")->size() > 128 || updates.size() >= 32)
            { server->close(); return; }
            std::uint64_t newest = applied;
            if(!updates.empty()) counter(updates.back(), "revision", newest);
            // Account for packets already waiting on UI presentation. A
            // duplicate regular state must not replay its events, and even a
            // resume must never replace a newer queued or applied revision.
            if(revision < newest || (revision == newest && !packet.getBoolean("resume"))) return;
            if(packet.getBoolean("resume")) updates.clear();
            updates.push_back(packet);
            return;
        }
        if(kind == "ack" && welcomed)
        {
            std::uint64_t sequence = 0;
            if(!counter(packet, "sequence", sequence) || !packet.isBoolean("accepted"))
            { server->close(); return; }
            if(sequence == sentSequence && pending.size())
            {
                // A ready request can become stale because another seat
                // completed that phase. Its delayed rejection must not cancel
                // commands already selected on the next screen or turn.
                const bool rejectedCurrentContext = !packet.getBoolean("accepted") && sameContext(pendingIntent);
                pending.clear();
                if(rejectedCurrentContext) cancelCommands();
                message = rejectedCurrentContext ? "State changed; choose again" : "Connected";
            }
            return;
        }
        if(kind == "error")
        {
            message = packet.getString("message");
            server->close();
            // Admission failure needs an explicit corrected join, not retry flooding.
            if(token.empty()) enabled = false;
            return;
        }
        server->close();
    }
    void dispatchClient()
    {
        if(!begun || !welcomed || !server || !server->connected() || serverPaused ||
           pending.size() || commands.empty() || awaitingConsumption || !updates.empty() ||
           clientConsumed != applied) return;
        if(!sameContext(commands.front())) { cancelCommands(); return; }
        const auto intent = std::move(commands.front());
        commands.pop_front();
        auto packet = envelope(intent.ready ? "ready" : "command");
        sentSequence = nextSequence++;
        packet.addString("sequence", std::to_string(sentSequence));
        // Bind the revision at dispatch, after the previous queued command's
        // acknowledgement and resulting presentation have both been consumed.
        packet.addString("revision", std::to_string(applied));
        if(intent.ready) packet.addInteger("phase", currentPhase);
        else packet.addObject("action", intent.action);
        pending = packet;
        pendingIntent = intent;
        send(*server, packet);
    }
    void clientPoll()
    {
        const auto now = Clock::now();
        if(!server || server->closed())
        {
            welcomed = false;
            helloSent = false;
            awaitingConsumption = false;
            clientConsumed = 0;
            if(now < nextConnect) return;
            nextConnect = now + std::chrono::seconds(1);
            server = std::make_unique<SecureConnection>(code);
            std::string error;
            if(!server->connect(address, remotePort, error))
            {
                message = token.empty() ? "Could not join room; check address and invitation" : "Reconnecting...";
                if(token.empty()) enabled = false;
                return;
            }
            message = token.empty() ? "Connecting..." : "Reconnecting...";
            lastReceive = now;
        }
        server->poll();
        if(server->connected() && !helloSent)
        {
            const auto identity = contentPackageIdentity(activeContentPackageManifest());
            auto hello = envelope("hello");
            hello.addInteger("protocol", ProtocolVersion);
            hello.addInteger("rulesContract", RulesContractVersion);
            hello.addString("content", identity.id);
            hello.addInteger("contentVersion", identity.version);
            hello.addString("room", code);
            hello.addString("name", name);
            hello.addString("token", token);
            send(*server, hello);
            helloSent = true;
        }
        std::string wire;
        for(int count = 0; count < 32 && server->receive(wire); ++count)
        {
            JsonObject packet;
            std::string error;
            if(!parseWireObject(wire, packet, error)) { message = error; server->close(); break; }
            lastReceive = now;
            clientPacket(packet);
        }
        if(enabled && token.empty() && server->closed())
        {
            message = "Could not join room; check address and invitation";
            enabled = false;
            return;
        }
        if(now - lastReceive > std::chrono::seconds(15)) server->close();
        if(welcomed && now - lastPing > std::chrono::seconds(2))
        { send(*server, envelope("ping")); lastPing = now; }
        dispatchClient();
    }
};

MatchSession::MatchSession() : impl(std::make_unique<Impl>()) {}
MatchSession::~MatchSession() = default;
void MatchSession::leave() { impl = std::make_unique<Impl>(); }

bool MatchSession::host(const HostOptions& options, std::string& error)
{
    if((options.mode != "duel" && options.mode != "classic-ffa" && options.mode != "coalition") ||
       (options.ruleset != "quick" && options.ruleset != "classic") || !cleanName(options.name) ||
       options.humanSeats < 2 || options.humanSeats > (options.mode == "duel" ? 2 : 4))
    { error = "Invalid room settings"; return false; }
    leave();
    impl->code = nonce(16);
    impl->listener = std::make_unique<SecureListener>(impl->code);
    if(!impl->listener->listen(options.port, error)) return false;
    impl->enabled = impl->hosting = true;
    impl->options = options;
    impl->required = options.humanSeats;
    impl->config = configFor(options);
    impl->avatar = impl->config.players.front().avatar;
    impl->seats[0].name = options.name;
    impl->lobby();
    return true;
}

bool MatchSession::join(const std::string& address, std::uint16_t port,
                        const std::string& code, const std::string& name, std::string& error)
{
    JsonObject secret;
    secret.addString("room", code);
    if((address != "localhost" && !isNumericIPv4(address)) || !port ||
       (code.size() != 32 && code.size() != 64) || !hexToken(secret, "room", code.size()) || !cleanName(name))
    { error = "Enter host IPv4 address, port and room code"; return false; }
    leave();
    impl->enabled = true;
    impl->address = address;
    impl->remotePort = port;
    impl->code = code;
    impl->name = name;
    impl->clientPoll();
    return true;
}

bool MatchSession::start(std::string& error)
{
    if(!canStart()) { error = "Wait for all players to connect"; return false; }
    GameData::setAIDifficulty(Settings::aiDifficulty());
    if(!impl->authority.start(impl->config, &error)) return false;
    impl->begun = true;
    impl->publish();
    return true;
}

void MatchSession::poll()
{
    if(!impl->enabled || impl->polling) return;
    impl->polling = true;
    try { if(impl->hosting) impl->hostPoll(); else impl->clientPoll(); }
    catch(const std::exception& error)
    {
        impl->message = std::string("Network session error: ") + error.what();
        if(impl->server) impl->server->close();
    }
    impl->polling = false;
}

bool MatchSession::active() const { return impl->enabled; }
bool MatchSession::isHost() const { return impl->hosting; }
bool MatchSession::started() const { return impl->begun; }
bool MatchSession::connected() const
{ return impl->hosting ? impl->allConnected() : impl->welcomed && impl->server && impl->server->connected(); }
bool MatchSession::paused() const { return !connected() || (!impl->hosting && impl->serverPaused); }
bool MatchSession::canStart() const { return active() && isHost() && !started() && connected(); }
int MatchSession::occupiedSeats() const { return impl->occupied; }
int MatchSession::requiredSeats() const { return impl->required; }
int MatchSession::phase() const { return impl->currentPhase; }
std::uint64_t MatchSession::revision() const { return impl->hosting ? impl->authority.revision() : impl->applied; }
std::uint16_t MatchSession::port() const { return impl->hosting ? impl->listener->port() : impl->remotePort; }
Avatar MatchSession::localAvatar() const { return impl->avatar; }
const std::string& MatchSession::roomCode() const { return impl->code; }
const std::string& MatchSession::mode() const { return impl->options.mode; }
const std::string& MatchSession::ruleset() const { return impl->options.ruleset; }
const std::string& MatchSession::status() const { return impl->message; }
std::vector<std::string> MatchSession::playerNames() const
{
    if(!impl->hosting) return impl->names;
    std::vector<std::string> names;
    for(int i = 0; i < impl->required; ++i)
        names.push_back(i == 0 || impl->seats[i].connection ? impl->seats[i].name : "");
    return names;
}

bool MatchSession::submit(const ClientMessage& action, ActionList& events, ActionRejection* rejection)
{
    (void)events;
    if(rejection) *rejection = ActionRejection();
    if(!active() || !started() || paused()) return false;
    std::string error;
    const auto validated = commandFromWire(action, error);
    if(!validated || impl->commands.size() >= 64)
    {
        if(rejection) rejection->reason = ActionRejectReason::StaleSelection;
        return false;
    }
    Impl::Intent intent;
    intent.action = *validated;
    intent.phase = impl->currentPhase;
    intent.wind = GameData::currentWind();
    intent.battleChoice = impl->battleChoice;
    if(impl->hosting && impl->currentPhase == Menu::MahjongPart && GameData::dropStone.isValid())
        intent.discardRevision = impl->authority.revision();
    impl->commands.push_back(std::move(intent));
    return true;
}

bool MatchSession::ready()
{
    if(!active() || !started() || paused()) return false;
    if(impl->currentPhase != Menu::ShowPlayers && impl->currentPhase != Menu::MahjongSummaryPart &&
       impl->currentPhase != Menu::BattleSummaryPart) return false;
    if(impl->hosting && impl->authority.hasReady(impl->avatar)) return true;
    // Phase-wait screens retry periodically; collapse retries into one intent.
    if(impl->pending.getString("kind") == "ready" ||
       std::any_of(impl->commands.begin(), impl->commands.end(), [](const Impl::Intent& intent) { return intent.ready; }))
        return true;
    if(!impl->commands.empty() || impl->pending.size()) return false;
    Impl::Intent intent;
    intent.ready = true;
    intent.phase = impl->currentPhase;
    impl->commands.push_back(std::move(intent));
    return true;
}

void MatchSession::takeEvents(ActionList& events)
{
    // A nonempty destination still belongs to the previous UI animation.
    // Merely delivering events is not proof that the player has consumed them.
    if(!events.empty()) return;
    if(impl->awaitingConsumption)
    {
        if(impl->hosting) impl->seats[0].consumed = impl->deliveredRevision;
        else if(connected())
        {
            auto seen = envelope("seen");
            seen.addString("revision", std::to_string(impl->deliveredRevision));
            impl->send(*impl->server, seen);
            impl->clientConsumed = impl->deliveredRevision;
        }
        impl->awaitingConsumption = false;
    }
    if(impl->hosting)
    {
        if(!impl->localDeliveryPending) return;
        events.splice(events.end(), impl->localEvents);
        impl->localDeliveryPending = false;
        impl->deliveredRevision = impl->authority.revision();
        impl->awaitingConsumption = true;
        return;
    }
    if(impl->updates.empty()) return;
    const auto packet = std::move(impl->updates.front());
    impl->updates.pop_front();
    std::string error;
    const auto* view = packet.getObject("view");
    if(!view->isString("recipient") || view->getString("recipient") != impl->avatar.toString() ||
       !view->isInteger("phase") || view->getInteger("phase") != packet.getInteger("phase"))
    { impl->message = "Server state belongs to a different seat or phase"; impl->server->close(); return; }
    ActionList decoded;
    const auto* array = packet.getArray("events");
    const auto* roster = view->getArray("players");
    for(std::size_t i = 0; i < array->size(); ++i)
    {
        const auto* object = array->getObject(i);
        ActionMessage event(Action::None);
        if(!object || !eventFromWire(*object, event, &error))
        { impl->message = "Invalid server event: " + error; impl->server->close(); return; }
        bool participant = false;
        if(roster)
            for(std::size_t seat = 0; seat < roster->size(); ++seat)
                if(const auto* player = roster->getObject(seat))
                    participant = participant || (player->isString("wind") &&
                        player->getString("wind") == event.getString("currentWind"));
        // A valid compass enum can still name an absent Duel seat. Screens
        // look up event actors in the roster, so reject this before hydration.
        if(!participant)
        { impl->message = "Server event belongs to an absent participant"; impl->server->close(); return; }
        decoded.push_back(event);
    }
    if(!applyClientView(*packet.getObject("view"), &error))
    { impl->message = "Invalid server state: " + error; impl->server->close(); return; }
    counter(packet, "revision", impl->applied);
    impl->currentPhase = packet.getInteger("phase");
    impl->begun = true;
    impl->observeEvents(decoded);
    if(!impl->commands.empty() && !impl->sameContext(impl->commands.front())) impl->cancelCommands();
    events.splice(events.end(), decoded);
    impl->deliveredRevision = impl->applied;
    impl->awaitingConsumption = true;
}

MatchSession& session() { static MatchSession instance; return instance; }
bool runeCommand(const Avatar& avatar, const ClientMessage& command, ActionList& events, ActionRejection* rejection)
{ return session().active() ? session().submit(command, events, rejection) : GameData::client2Mahjong(avatar, command, events, rejection); }
bool adventureCommand(const Avatar& avatar, const ClientMessage& command, ActionList& events, ActionRejection* rejection)
{ return session().active() ? session().submit(command, events, rejection) : GameData::client2Adventure(avatar, command, events, rejection); }
void runeEvents(const Avatar& avatar, ActionList& events)
{ if(session().active()) session().takeEvents(events); else GameData::mahjong2Client(avatar, events); }
void adventureEvents(const Avatar& avatar, ActionList& events)
{ if(session().active()) session().takeEvents(events); else GameData::adventure2Client(avatar, events); }
}
