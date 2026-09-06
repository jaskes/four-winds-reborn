#include "gameplayrng.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <random>

namespace
{
constexpr std::uint64_t DefaultSeed = UINT64_C(0x4d595df4d0f33173);
constexpr std::uint64_t Multiplier = UINT64_C(2685821657736338717);

std::uint64_t generatorState = DefaultSeed;
std::uint64_t generatorSeed = DefaultSeed;
std::uint64_t generatorDraws = 0;

std::uint64_t parseUnsigned(const std::string & value, bool* valid)
{
    if(value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
    {
        if(valid) *valid = false;
        return 0;
    }
    try
    {
        std::size_t parsed = 0;
        const std::uint64_t result = std::stoull(value, &parsed, 10);
        if(valid) *valid = parsed == value.size();
        return result;
    }
    catch(...)
    {
        if(valid) *valid = false;
        return 0;
    }
}

std::uint64_t next(void)
{
    generatorState ^= generatorState >> 12;
    generatorState ^= generatorState << 25;
    generatorState ^= generatorState >> 27;
    ++generatorDraws;
    return generatorState * Multiplier;
}
}

void GameplayRng::seed(std::uint64_t value)
{
    generatorSeed = value ? value : DefaultSeed;
    generatorState = generatorSeed;
    generatorDraws = 0;
}

void GameplayRng::seedFromEntropy(void)
{
    if(const char* configured = std::getenv("FOUR_WINDS_RNG_SEED"))
    {
        bool valid = false;
        const std::uint64_t value = parseUnsigned(configured, &valid);
        if(valid)
        {
            seed(value);
            return;
        }
    }

    const std::uint64_t clock = static_cast<std::uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    std::uint64_t value = clock ^ DefaultSeed;
    try
    {
        std::random_device entropy;
        value ^= (static_cast<std::uint64_t>(entropy()) << 32) ^
            static_cast<std::uint64_t>(entropy());
    }
    catch(...)
    {
        // A high-resolution clock still provides a usable new-game seed on
        // platforms where std::random_device is unavailable.
    }
    seed(value);
}

std::uint64_t GameplayRng::state(void)
{
    return generatorState;
}

std::uint64_t GameplayRng::initialSeed(void)
{
    return generatorSeed;
}

std::uint64_t GameplayRng::draws(void)
{
    return generatorDraws;
}

int GameplayRng::uniform(int minimum, int maximum)
{
    if(maximum < minimum) std::swap(minimum, maximum);

    const std::uint64_t range = static_cast<std::uint64_t>(
        static_cast<std::int64_t>(maximum) - static_cast<std::int64_t>(minimum)) + 1;
    const std::uint64_t threshold = (UINT64_C(0) - range) % range;

    std::uint64_t value = 0;
    do value = next();
    while(value < threshold);

    return static_cast<int>(static_cast<std::int64_t>(minimum) +
        static_cast<std::int64_t>(value % range));
}

SWE::JsonObject GameplayRng::toJsonObject(void)
{
    SWE::JsonObject result;
    result.addString("algorithm", Algorithm);
    result.addString("seed", std::to_string(generatorSeed));
    result.addString("state", std::to_string(generatorState));
    result.addString("draws", std::to_string(generatorDraws));
    return result;
}

namespace
{
struct RngState
{
    std::uint64_t seed = 0;
    std::uint64_t state = 0;
    std::uint64_t draws = 0;
};

bool decodeState(const SWE::JsonObject & value, RngState & result)
{
    if(value.getString("algorithm") != GameplayRng::Algorithm) return false;

    bool stateValid = false;
    bool drawsValid = false;
    bool seedValid = false;
    const std::uint64_t restoredState = parseUnsigned(value.getString("state"), &stateValid);
    const std::uint64_t restoredDraws = parseUnsigned(value.getString("draws"), &drawsValid);
    const std::string encodedSeed = value.getString("seed");
    const std::uint64_t restoredSeed = encodedSeed.empty() ? restoredState :
        parseUnsigned(encodedSeed, &seedValid);
    if(encodedSeed.empty()) seedValid = stateValid;
    if(!stateValid || !drawsValid || !seedValid || !restoredState || !restoredSeed) return false;

    result = {restoredSeed, restoredState, restoredDraws};
    return true;
}
}

bool GameplayRng::isValidState(const SWE::JsonObject & value)
{
    RngState decoded;
    return decodeState(value, decoded);
}

bool GameplayRng::fromJsonObject(const SWE::JsonObject & value)
{
    RngState decoded;
    if(!decodeState(value, decoded)) return false;
    generatorSeed = decoded.seed;
    generatorState = decoded.state;
    generatorDraws = decoded.draws;
    return true;
}
