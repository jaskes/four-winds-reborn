#ifndef FOUR_WINDS_MATCH_AUTHORITY_H
#define FOUR_WINDS_MATCH_AUTHORITY_H

#include <cstdint>
#include <map>
#include <set>
#include <string>

#include "gamedata.h"

namespace Multiplayer
{
    struct MatchConfig
    {
        Persons players;
        std::string topologyId = "duel";
        int topologyVersion = 2;
        std::string rulesetId = "quick";
        int rulesetVersion = 1;
        // Zero requests an unpredictable new seed; explicit seeds support
        // repeatable process integration tests and server diagnostics.
        std::uint64_t seed = 0;
    };

    // One instance owns the process-wide GameData state. All methods run on
    // the host's game thread; socket callbacks only enqueue requests.
    class MatchAuthority
    {
        struct ClaimReply
        {
            JsonObject command;
            int priority = 0;
        };

        Persons roster;
        std::map<int, ActionList> outgoing;
        std::set<int> phaseReady;
        std::map<int, ClaimReply> claims;
        std::set<int> claimResponders;
        bool running = false;
        bool claimWindow = false;
        std::uint64_t stateRevision = 0;
        std::uint64_t claimOpenedRevision = 0;

        bool reject(ActionRejection*, ActionRejectReason) const;
        const LocalPlayer* humanPlayer(const Avatar&) const;
        void broadcast(const ActionList&);
        void changed(const ActionList&);
        void beginClaims(std::uint64_t openedRevision);
        bool resolveClaims(ActionList&);
        bool submitClaim(const LocalPlayer&, const ClientMessage&, ActionRejection*);
        bool advanceReadyPhase();
        bool allHumansReady() const;

    public:
        bool start(const MatchConfig&, std::string* error = nullptr);
        void stop();
        bool tick();
        bool submit(const Avatar& boundAvatar, const ClientMessage&,
                    ActionRejection* rejection = nullptr);
        bool ready(const Avatar& boundAvatar, ActionRejection* rejection = nullptr);

        bool active() const { return running; }
        int phase() const;
        std::uint64_t revision() const { return stateRevision; }
        const Persons& participants() const { return roster; }
        bool hasReady(const Avatar&) const;
        bool awaitingClaim(const Avatar&) const;
        // Concurrent human votes can share the discard's original revision.
        // This exception never spans a resolved discard or permits other actions.
        bool acceptsClaimRevision(const Avatar&, const ClientMessage&, std::uint64_t expectedRevision) const;
        ActionList takeEvents(const Avatar&);
        ActionList resumeEvents(const Avatar&) const;
    };
}

#endif
