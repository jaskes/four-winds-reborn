#ifndef FOUR_WINDS_MATCH_SCORE_H
#define FOUR_WINDS_MATCH_SCORE_H

#include <array>
#include <cstddef>
#include <vector>

#include "gameobjects.h"

class MatchTopology;

namespace MatchScore
{
    enum class Category : std::size_t
    {
        Territory,
        SummonCircles,
        Units,
        SpellPoints,
        LandClaims,
        Count
    };

    constexpr std::size_t CategoryCount = static_cast<std::size_t>(Category::Count);

    struct PlayerInput
    {
        Person person;
        std::array<int, CategoryCount> scores{};
    };

    struct CategoryResult
    {
        int score = 0;
        int rank = 0;
        int standingPoints = 0;
    };

    struct PlayerResult
    {
        Person person;
        std::array<CategoryResult, CategoryCount> categories{};
        int totalScore = 0;
        int finalRank = 0;
        int teamId = -1;
        int teamScore = 0;
        int teamRank = 0;
    };

    using Results = std::vector<PlayerResult>;

    constexpr std::size_t index(Category category)
    {
        return static_cast<std::size_t>(category);
    }

    PlayerInput observe(const RemotePlayer &);
    Results calculate(const std::vector<PlayerInput> &);
    Results calculate(const std::vector<PlayerInput> &, const MatchTopology &);
    Results current(void);
    std::vector<std::size_t> winnerIndices(const Results &);
}

#endif
