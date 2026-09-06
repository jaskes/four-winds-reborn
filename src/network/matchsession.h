#ifndef FOUR_WINDS_MATCH_SESSION_H
#define FOUR_WINDS_MATCH_SESSION_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "gamedata.h"

namespace Multiplayer
{
    struct HostOptions
    {
        std::string mode = "duel";
        std::string ruleset = "quick";
        std::string name = "Host";
        std::uint16_t port = 19782;
        int humanSeats = 2;
        // Zero uses entropy; fixed values support reproducible process tests.
        std::uint64_t seed = 0;
    };

    // One instance per application process; GameData stays on its UI thread.
    class MatchSession
    {
        struct Impl;
        std::unique_ptr<Impl> impl;
    public:
        MatchSession();
        ~MatchSession();
        MatchSession(const MatchSession&) = delete;
        MatchSession& operator=(const MatchSession&) = delete;

        bool host(const HostOptions&, std::string& error);
        bool join(const std::string& address, std::uint16_t port,
                  const std::string& roomCode, const std::string& name,
                  std::string& error);
        bool start(std::string& error);
        void poll();
        void leave();
        bool active() const;
        bool isHost() const;
        bool started() const;
        bool connected() const;
        bool paused() const;
        bool canStart() const;
        int occupiedSeats() const;
        int requiredSeats() const;
        int phase() const;
        std::uint64_t revision() const;
        std::uint16_t port() const;
        Avatar localAvatar() const;
        const std::string& roomCode() const;
        const std::string& mode() const;
        const std::string& ruleset() const;
        const std::string& status() const;
        std::vector<std::string> playerNames() const;

        bool submit(const ClientMessage&, ActionList&, ActionRejection* = nullptr);
        bool ready();
        void takeEvents(ActionList&);
    };

    MatchSession& session();
    // Offline paths keep their existing command/poll semantics.
    bool runeCommand(const Avatar&, const ClientMessage&, ActionList&, ActionRejection* = nullptr);
    bool adventureCommand(const Avatar&, const ClientMessage&, ActionList&, ActionRejection* = nullptr);
    void runeEvents(const Avatar&, ActionList&);
    void adventureEvents(const Avatar&, ActionList&);
}

#endif
