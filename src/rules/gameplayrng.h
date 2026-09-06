#ifndef FOUR_WINDS_GAMEPLAY_RNG_H
#define FOUR_WINDS_GAMEPLAY_RNG_H

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <string>
#include <utility>

#include "swe/swe_json.h"

namespace GameplayRng
{
    constexpr const char* Algorithm = "xorshift64star-v1";

    void seed(std::uint64_t value);
    void seedFromEntropy(void);
    std::uint64_t initialSeed(void);
    std::uint64_t state(void);
    std::uint64_t draws(void);
    int uniform(int minimum, int maximum);

    SWE::JsonObject toJsonObject(void);
    bool isValidState(const SWE::JsonObject & value);
    bool fromJsonObject(const SWE::JsonObject & value);

    template<typename InputIterator>
    InputIterator choose(InputIterator first, InputIterator last)
    {
        const auto count = std::distance(first, last);
        if(count <= 0) return last;

        InputIterator result = first;
        if(1 < count) std::advance(result, uniform(0, static_cast<int>(count - 1)));
        return result;
    }

    template<typename RandomAccessIterator>
    void shuffle(RandomAccessIterator first, RandomAccessIterator last)
    {
        const auto count = std::distance(first, last);
        for(auto remaining = count; 1 < remaining; --remaining)
        {
            const auto selected = uniform(0, static_cast<int>(remaining - 1));
            std::iter_swap(first + (remaining - 1), first + selected);
        }
    }
}

#endif
