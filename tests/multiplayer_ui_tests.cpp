#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>

#include "gametheme.h"
#include "adventurepart.h"
#include "adventureuievents.h"
#include "contentpackage.h"
#include "mahjongpart.h"
#include "matchsession.h"
#include "multiplayerlobby.h"
#include "recovery.h"
#include "runewars.h"
#include "settings.h"
#include "securetransport.h"
#include "wirejson.h"

namespace GameData
{
    extern LocalPlayers gamers;
    extern CroupierSet croupier;
    extern Wind currentWind;
    extern Stone dropStone;
}

namespace
{
    class LobbyScreen : public MultiplayerLobbyScreen
    {
    public:
        using MultiplayerLobbyScreen::tickEvent;
        using MultiplayerLobbyScreen::keyPressEvent;
        using MultiplayerLobbyScreen::textInputEvent;
        using MultiplayerLobbyScreen::mouseClickEvent;
    };
    class RuneScreen : public MahjongPartScreen
    {
    public:
        using MahjongPartScreen::tickEvent;
        using MahjongPartScreen::userEvent;
        using MahjongPartScreen::toJsonObject;
    };
    class MapScreen : public AdventurePartScreen
    {
    public:
        using AdventurePartScreen::AdventurePartScreen;
        using AdventurePartScreen::tickEvent;
        using AdventurePartScreen::userEvent;
        bool doneEnabled() { return !buttons.findIds("but_done")->isDisabled(); }
        bool unitAt(int id, Land land) const
        {
            const BattleParty* party = ld.myPlayer().army.findPartyConst(land);
            return party && party->findBattleUnitConst(id);
        }
    };

    // The peer owns only a TLS connection and wire messages. It never loads
    // snapshots into the host's process-wide GameData.
    struct ScriptedPeer
    {
        Multiplayer::SecureConnection connection;
        std::uint64_t revision = 0;
        std::uint64_t sequence = 0;
        std::uint64_t acknowledged = 0;
        int phase = Menu::ShowPlayers;
        bool accepted = false;
        std::string error;

        explicit ScriptedPeer(const std::string& roomSecret) : connection(roomSecret) {}

        void send(JsonObject packet) { connection.send(packet.toString(), error); }
        void poll()
        {
            connection.poll();
            std::string bytes;
            while(connection.receive(bytes))
            {
                JsonObject packet;
                if(!Multiplayer::parseWireObject(bytes, packet, error)) continue;
                const std::string kind = packet.getString("kind");
                if(kind == "state")
                {
                    revision = std::stoull(packet.getString("revision"));
                    phase = packet.getInteger("phase");
                    JsonObject seen;
                    seen.addString("kind", "seen");
                    seen.addString("revision", std::to_string(revision));
                    send(seen);
                }
                else if(kind == "ack")
                {
                    acknowledged = std::stoull(packet.getString("sequence"));
                    accepted = packet.getBoolean("accepted");
                }
                else if(kind == "ping")
                {
                    JsonObject pong;
                    pong.addString("kind", "pong");
                    send(pong);
                }
            }
        }
        void request(const ClientMessage* action = nullptr)
        {
            JsonObject packet;
            packet.addString("kind", action ? "command" : "ready");
            packet.addString("revision", std::to_string(revision));
            packet.addString("sequence", std::to_string(++sequence));
            if(action) packet.addObject("action", *action);
            else packet.addInteger("phase", phase);
            send(packet);
        }
    };

    bool visible(const JsonObject& state, const char* button)
    {
        const JsonObject* encoded = state.getObject(button);
        return encoded && encoded->getBoolean("visible");
    }

    void snapshot(const char* filename)
    {
        DisplayScene::sceneRedraw(true);
        if(const char* directory = std::getenv("FOUR_WINDS_UI_SNAPSHOT_DIR"))
        {
            std::filesystem::create_directories(directory);
            Display::renderScreenshot((std::filesystem::path(directory) / filename).string());
        }
    }
}

int runMultiplayerUiTests(const char*)
{
    int failures = 0;
    const auto check = [&](bool valid, const std::string& message) {
        if(!valid) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
    };
    Recovery::setEnabled(false);
    const std::string program = (std::filesystem::path(FOUR_WINDS_SOURCE_DIR) / "four-winds-reborn.exe").string();
    const char* theme = std::getenv("FOUR_WINDS_UI_TEST_THEME");
    if(!Engine::init() || !GameTheme::init(Application(program.c_str(), false, Size(), theme ? theme : "classic"))) return 1;
    Settings::setMusic(false);
    Settings::setSound(false);
    Settings::setVoiceVolume(0);
    Settings::setSoundGuardianRules(false);
    Settings::setGameSpeed("fast");
    if(const char* language = std::getenv("FOUR_WINDS_UI_TEST_LANGUAGE"))
    {
        Settings::setLanguage(language);
        Translation::setStripContext('|');
        if(std::string(language) != "en")
            check(Translation::bindDomain(Application::domain(), GameTheme::readResource(std::string(language) + ".mo")),
                  "load network UI test language");
        Translation::setLanguage(language);
        Translation::setDomain(Application::domain());
        GameData::retranslateThemeData();
    }
    auto& match = Multiplayer::session();
    {
        // Render both the ordinary lobby and the longest accepted invitation.
        // Then join another actual session through the clipboard/button path,
        // proving that endpoint, port, secret and host mode all reach the UI.
        LobbyScreen lobby;
        snapshot("multiplayer-lobby.png");
        lobby.textInputEvent(Multiplayer::formatInvite({"192.168.100.200", 65535, std::string(64, 'f')}));
        snapshot("multiplayer-invitation-long.png");
        Multiplayer::MatchSession invitationHost;
        Multiplayer::HostOptions inviteOptions;
        inviteOptions.port = 0;
        inviteOptions.mode = "coalition";
        inviteOptions.ruleset = "classic";
        inviteOptions.humanSeats = 3;
        std::string invitationError;
        check(invitationHost.host(inviteOptions, invitationError), "host invitation fixture");
        const std::string invite = Multiplayer::formatInvite({"127.0.0.1", invitationHost.port(), invitationHost.roomCode()});
        check(SDL_SetClipboardText(invite.c_str()) == 0, "populate invitation clipboard");
        const Point paste(512, 574);
        lobby.mouseClickEvent(ButtonsEvent(ButtonLeft, paste, paste));
        const Point join(690, 510);
        lobby.mouseClickEvent(ButtonsEvent(ButtonLeft, join, join));
        check(match.active() && !match.isHost() && match.port() == invitationHost.port() &&
            match.roomCode() == invitationHost.roomCode(), "paste invitation sets actual join endpoint, port and secret");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        do
        {
            invitationHost.poll();
            match.poll();
            lobby.tickEvent(SDL_GetTicks());
            Tools::delay(5);
        } while((!match.connected() || match.playerNames().size() != 3) && std::chrono::steady_clock::now() < deadline);
        check(match.connected() && match.mode() == "coalition" && match.ruleset() == "classic" && match.requiredSeats() == 3,
            "joined lobby receives host-selected mode, rules and player count");
        const auto names = match.playerNames();
        check(names.size() == 3 && names[0] == "Host" && names[1] == "Player" && names[2].empty(),
            "lobby roster shows admitted player names and its remaining seat");
        snapshot("multiplayer-joined-coalition.png");
        match.leave();
        lobby.tickEvent(SDL_GetTicks());
        std::string wrongSecret = invitationHost.roomCode();
        wrongSecret.front() = wrongSecret.front() == '0' ? '1' : '0';
        lobby.textInputEvent(Multiplayer::formatInvite({"127.0.0.1", invitationHost.port(), wrongSecret}));
        lobby.mouseClickEvent(ButtonsEvent(ButtonLeft, join, join));
        const auto failureDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        do
        {
            invitationHost.poll();
            match.poll();
            lobby.tickEvent(SDL_GetTicks());
            Tools::delay(5);
        } while(match.active() && std::chrono::steady_clock::now() < failureDeadline);
        check(!match.active() && SDL_IsTextInputActive(), "wrong invitation releases join and restores editable input");
        snapshot("multiplayer-invalid-invitation.png");
        match.leave();
        invitationHost.leave();
    }

    Multiplayer::HostOptions options;
    options.port = 0;
    options.name = "UI host";
    std::string error;
    check(match.host(options, error), "host UI fixture: " + error);
    {
        LobbyScreen lobby;
        Multiplayer::LocalIPv4Discovery discovery;
        std::vector<std::string> addresses;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        do
        {
            lobby.tickEvent(SDL_GetTicks());
            Tools::delay(5);
        } while(!discovery.poll(addresses) && std::chrono::steady_clock::now() < deadline);
        // CI machines may have no active interface beyond loopback.
        if(!addresses.empty())
        {
            bool copied = false;
            do
            {
                lobby.tickEvent(SDL_GetTicks());
                lobby.keyPressEvent(KeySym(Key::c, KMOD_CTRL));
                char* clipboard = SDL_GetClipboardText();
                Multiplayer::InviteAddress invitation;
                copied = clipboard && Multiplayer::parseInvite(clipboard, invitation) && invitation.port == match.port() &&
                    invitation.room == match.roomCode() && invitation.address.compare(0, 4, "127.") != 0;
                if(clipboard) SDL_free(clipboard);
                Tools::delay(5);
            } while(!copied && std::chrono::steady_clock::now() < deadline);
            check(copied, "host copy invitation uses a discovered LAN address and actual listening port");
        }
        snapshot("multiplayer-host-invitation.png");
    }
    ScriptedPeer peer(match.roomCode());
    check(peer.connection.connect("127.0.0.1", match.port(), error), "connect real socket peer: " + error);
    JsonObject hello;
    hello.addString("kind", "hello");
    hello.addInteger("protocol", 2);
    hello.addInteger("rulesContract", 1);
    const auto identity = contentPackageIdentity(activeContentPackageManifest());
    hello.addString("content", identity.id);
    hello.addInteger("contentVersion", identity.version);
    hello.addString("room", match.roomCode());
    hello.addString("name", "UI peer");
    hello.addString("token", "");

    std::function<void()> screenTick;
    const auto step = [&] {
        peer.poll();
        match.poll();
        if(screenTick) screenTick();
        else { ActionList ignored; match.takeEvents(ignored); }
        peer.poll();
        Tools::delay(5);
    };
    const auto until = [&](const std::function<bool()>& condition, int milliseconds = 5000) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
        do { step(); if(condition()) return true; } while(std::chrono::steady_clock::now() < deadline);
        return false;
    };
    const auto acknowledgePhase = [&] {
        const int previous = match.phase();
        const auto oldRevision = match.revision();
        match.ready();
        if(!until([&] { return peer.revision > oldRevision; })) return false;
        peer.request();
        return until([&] { return match.phase() != previous && peer.phase == match.phase(); });
    };
    check(until([&] { return peer.connection.connected(); }), "socket peer finishes nonblocking connection");
    peer.send(hello);
    check(until([&] { return match.canStart(); }), "socket peer completes lobby handshake");
    check(match.start(error), "start lobby match: " + error);
    check(acknowledgePhase() && match.phase() == Menu::MahjongPart, "both peers release roster barrier");
    if(failures) { match.leave(); GameTheme::clear(); Engine::quit(); return 1; }

    const Avatar host = match.localAvatar();
    LocalPlayer& local = *GameData::gamers.playerOfAvatar(host);
    LocalPlayer& remote = *GameData::gamers.playerOfWind(Wind::West);
    // Reproduce a screen opening after the phase wait consumed Begin/Data:
    // a draw already exists, and no replacement Turn event is forthcoming.
    local.newStone = GameStone(Stone::Skull5);
    {
        RuneScreen screen;
        screenTick = [&] { screen.tickEvent(SDL_GetTicks()); };
        const JsonObject initial = screen.toJsonObject();
        check(initial.getBoolean("playerReady") && !visible(initial, "buttonLocalReady") &&
              initial.getInteger("stoneSelected", -1) == static_cast<int>(local.stones.size()),
              "network screen reconstructs ready state and drawn-rune selection from snapshot");
        snapshot("network-duel-draw.png");
        screen.userEvent(Action::MahjongDropSelected, nullptr);
        const JsonObject droppedUi = screen.toJsonObject();
        check(!visible(droppedUi, "buttonPass") && !droppedUi.getObject("buttonPass")->getBoolean("pressed"),
              "host discard does not enqueue its opponent's Pass");
        check(until([&] { return GameData::dropStone.isValid() && peer.revision == match.revision(); }),
              "GUI discard reaches host authority and remote wire snapshot");
        for(int i = 0; i < 30; ++i) step();
        check(GameData::dropStone.isValid() && !visible(screen.toJsonObject(), "buttonPass"),
              "host waits for remote claim and never displays opponent's Pass");
        ClientButtonPass pass;
        peer.request(&pass);
        check(until([&] { return !GameData::dropStone.isValid() && GameData::currentWind == Wind(Wind::West); }),
              "remote Pass releases host discard");
        until([&] { return peer.revision == match.revision(); });
        // Deterministic claim layout inside the real network/UI flow.
        GameData::croupier.luckDraw.clear();
        remote.newStone = GameStone(Stone::Number3);
        local.stones.clear();
        for(const Stone::stone_t stone : {Stone::Number3, Stone::Number3, Stone::Skull1, Stone::Skull2,
            Stone::Skull4, Stone::Skull5, Stone::Skull7, Stone::Sword1, Stone::Sword2, Stone::Sword4,
            Stone::Sword5, Stone::Sword7, Stone::Sword9}) local.stones.add(GameStone(stone));
        ClientDropIndex remoteDrop(static_cast<int>(remote.stones.size()));
        peer.request(&remoteDrop);
        check(until([&] { return visible(screen.toJsonObject(), "buttonPung"); }),
              "remote discard enables host claim controls from refreshed snapshot");
        snapshot("network-duel-claim.png");
        screen.userEvent(Action::ButtonPung, nullptr);
        check(until([&] { return GameData::currentWind == local.wind && !GameData::dropStone.isValid() && !local.rules.empty(); }),
              "GUI Pung is committed through queued network command");
        check(until([&] { return screen.toJsonObject().getString("fastLogOwner") == local.wind.toString(); }),
              "committed Pung drives the guardian announcement handler");
        snapshot("network-duel-pung.png");
        // Retire the animation before testing the next screen and its snapshot.
        for(int i = 0; i < 100; ++i) step();
        screenTick = {};
    }
    {
        RuneScreen resumed;
        check(resumed.toJsonObject().getInteger("stoneSelected", -1) >= 0,
              "recreated screen restores required Pung discard without a drawn rune");
    }

    // Finish the Rune phase through its visible self-win control, then verify
    // that an already-consumed AdventureTurn cannot leave Done disabled.
    local.rules.clear();
    local.stones.clear();
    for(const Stone::stone_t stone : {Stone::Sword1, Stone::Sword2, Stone::Sword3, Stone::Sword4,
        Stone::Sword5, Stone::Sword6, Stone::Number4, Stone::Number5, Stone::Number6,
        Stone::Skull7, Stone::Skull8, Stone::Skull9, Stone::Dragon1}) local.stones.add(GameStone(stone));
    local.newStone = GameStone(Stone::Dragon1);
    {
        RuneScreen winner;
        screenTick = [&] { winner.tickEvent(SDL_GetTicks()); };
        check(visible(winner.toJsonObject(), "buttonLocalGame"), "recreated network view restores self-win button");
        winner.userEvent(Action::ButtonLocalGame, nullptr);
        check(until([&] { return match.phase() == Menu::MahjongSummaryPart; }), "GUI self-win reaches Rune summary");
        screenTick = {};
    }
    Land origin, destination;
    for(const Land land : local.lands())
    {
        for(const Land neighbor : GameData::landInfo(land).borders)
            if(!neighbor.isTowerWinds() && GameData::landInfo(neighbor).clan == local.clan)
            { origin = land; destination = neighbor; break; }
        if(origin.isValid()) break;
    }
    check(origin.isValid() && destination.isValid(), "find legal owned map movement fixture");
    local.army.join(Creature::SkeletonHorde, origin);
    const int unit = local.army.toBattleCreatures().front()->battleUnit();
    check(acknowledgePhase() && match.phase() == Menu::AdventurePart, "summary barrier starts island phase");
    {
        MapScreen map(host);
        screenTick = [&] { map.tickEvent(SDL_GetTicks()); };
        check(map.doneEnabled(), "island screen restores Done from snapshot after consumed AdventureTurn");
        const LandInfo& from = GameData::landInfo(origin);
        const LandInfo& to = GameData::landInfo(destination);
        map.userEvent(LandPolygonClickLeft, const_cast<LandInfo*>(&from));
        map.userEvent(Action::ButtonOrder, nullptr);
        map.userEvent(LandPolygonClickLeft, const_cast<LandInfo*>(&to));
        check(map.unitAt(unit, destination), "GUI stages a legal movement order");
        map.userEvent(Action::ButtonDone, nullptr);
        check(until([&] { return local.adventurePartDone(); }), "network FIFO commits movement before Done");
        check(until([&] { return map.unitAt(unit, destination) && !map.doneEnabled(); }),
              "own confirmed movement and Done refresh from network snapshots");
        snapshot("network-duel-island.png");
        screenTick = {};
    }
    match.leave();
    peer.connection.close();
    GameTheme::clear();
    Engine::quit();
    std::cout << "multiplayer UI: " << (failures ? "FAILED" : "ok") << '\n';
    return failures ? 1 : 0;
}
