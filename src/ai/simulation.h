#ifndef FOUR_WINDS_SIMULATION_H
#define FOUR_WINDS_SIMULATION_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "aiprofile.h"
#include "gamedata.h"
#include "matchscore.h"
#include "matchtopology.h"
#include "runegameruleset.h"

namespace Simulation
{
    enum class MatchStage : std::size_t
    {
        Early,
        Middle,
        Late,
        Count
    };

    constexpr std::size_t MatchStageCount = static_cast<std::size_t>(MatchStage::Count);

    enum class MatchStatus
    {
        Complete,
        InvalidConfiguration,
        InitializationFailed,
        Stalled,
        TickLimit,
        Exception
    };

    struct MatchConfig
    {
        std::uint64_t seed = UINT64_C(1);
        AI::Difficulty difficulty = AI::Difficulty::Normal;
        bool forceBehaviorProfile = false;
        AI::BehaviorProfile behaviorProfile = AI::BehaviorProfile::Balanced;
        std::string runeGameRulesetId = ClassicRuneGameRulesetId;
        int runeGameRulesetVersion = ClassicRuneGameRulesetVersion;
        std::string matchTopologyId = ClassicFreeForAllTopologyId;
        int matchTopologyVersion = ClassicFreeForAllTopologyVersion;
        Persons persons;
        std::size_t maximumTicks = 100000;
        std::size_t maximumUnchangedTicks = 4;
        std::size_t stateHashInterval = 64;
        bool captureFullReplay = false;
    };

    struct MatchResult
    {
        struct Event
        {
            std::string type;
            std::size_t tick = 0;
            std::size_t adventurePhase = 0;
            Avatar avatar;
            std::string subject;
            Land land;
            int unit = 0;
        };

        struct PartyComposition
        {
            Land land;
            std::vector<std::string> creatures;
        };

        struct PlayerTelemetry
        {
            Avatar avatar;
            std::size_t summons = 0;
            std::size_t spellsCast = 0;
            std::size_t casualties = 0;
            std::size_t dismissals = 0;
            std::size_t peakUnits = 0;
            std::size_t finalUnits = 0;
            std::map<std::string, std::size_t> summonedCreatures;
            std::map<std::string, std::size_t> castSpells;
            std::vector<PartyComposition> finalArmy;
        };

        struct StageSnapshot
        {
            std::size_t adventurePhase = 0;
            MatchScore::Results score;

            bool captured(void) const { return !score.empty(); }
        };

        MatchStatus status = MatchStatus::InvalidConfiguration;
        std::uint64_t seed = 0;
        std::string runeGameRulesetId = ClassicRuneGameRulesetId;
        int runeGameRulesetVersion = ClassicRuneGameRulesetVersion;
        std::string matchTopologyId = ClassicFreeForAllTopologyId;
        int matchTopologyVersion = ClassicFreeForAllTopologyVersion;
        std::uint64_t rngDraws = 0;
        std::size_t ticks = 0;
        std::size_t unchangedTicks = 0;
        std::size_t mahjongHands = 0;
        std::size_t adventurePhases = 0;
        std::size_t emittedActions = 0;
        std::string finalStateHash;
        std::string error;
        bool fullReplayCaptured = false;
        JsonObject actionReplay;
        MatchScore::Results score;
        std::array<StageSnapshot, MatchStageCount> stages{};
        std::vector<PlayerTelemetry> players;
        std::vector<Event> events;

        bool completed(void) const { return status == MatchStatus::Complete; }
    };

    constexpr std::size_t index(MatchStage stage)
    {
        return static_cast<std::size_t>(stage);
    }

    const char* stageName(MatchStage);
    const char* statusName(MatchStatus);
    MatchResult runMatch(const MatchConfig &);
}

#endif
