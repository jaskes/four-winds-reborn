#include "simulation.h"

#include <algorithm>
#include <exception>
#include <map>
#include <set>

#include "gameplayrng.h"
#include "recovery.h"
#include "replay.h"

namespace
{
class RecoverySuppression
{
    bool previous;

public:
    RecoverySuppression() : previous(Recovery::enabled())
    {
        Recovery::setEnabled(false);
    }

    ~RecoverySuppression()
    {
        Recovery::setEnabled(previous);
    }
};

class ReplayCaptureScope
{
    std::size_t previousLimit;

public:
    explicit ReplayCaptureScope(bool full) : previousLimit(Replay::actionJournalLimit())
    {
        if(full) Replay::setActionJournalLimit(0);
    }

    ~ReplayCaptureScope()
    {
        Replay::setActionJournalLimit(previousLimit);
    }
};

class BehaviorProfileScope
{
    bool previousEnabled;
    AI::BehaviorProfile previousProfile;

public:
    explicit BehaviorProfileScope(const Simulation::MatchConfig & config) :
        previousEnabled(AI::behaviorProfileOverrideEnabled()),
        previousProfile(AI::behaviorProfileOverride())
    {
        if(config.forceBehaviorProfile)
            AI::setBehaviorProfileOverride(config.behaviorProfile);
        else
            AI::clearBehaviorProfileOverride();
    }

    ~BehaviorProfileScope()
    {
        if(previousEnabled) AI::setBehaviorProfileOverride(previousProfile);
        else AI::clearBehaviorProfileOverride();
    }
};

class RuneGameRulesetScope
{
    RuneGameRulesetIdentity previous;

public:
    explicit RuneGameRulesetScope(const Simulation::MatchConfig & config) :
        previous(runeGameRulesetIdentity(activeRuneGameRuleset()))
    {
        selectActiveRuneGameRuleset(config.runeGameRulesetId,
                                    config.runeGameRulesetVersion);
    }

    ~RuneGameRulesetScope()
    {
        selectActiveRuneGameRuleset(previous.id, previous.version);
    }
};

class MatchTopologyScope
{
    MatchTopologyIdentity previous;

public:
    explicit MatchTopologyScope(const Simulation::MatchConfig & config) :
        previous(matchTopologyIdentity(activeMatchTopology()))
    {
        selectActiveMatchTopology(config.matchTopologyId,
                                  config.matchTopologyVersion);
    }

    ~MatchTopologyScope()
    {
        selectActiveMatchTopology(previous.id, previous.version);
    }
};

struct ObservedUnit
{
    Avatar avatar;
    Creature creature;
    Land land;
};

using ObservedUnits = std::map<int, ObservedUnit>;
using AvatarsByWind = std::map<int, Avatar>;

ObservedUnits observeUnits(void)
{
    ObservedUnits result;
    for(const LocalPlayer & player : GameData::players())
    {
        for(const BattleParty & party : player.army)
        {
            for(const BattleCreature* creature : party.toBattleCreatures())
            {
                if(creature)
                    result[creature->battleUnit()] =
                        { player.avatar, *creature, party.land() };
            }
        }
    }
    return result;
}

Simulation::MatchResult::PlayerTelemetry* playerTelemetry(
    Simulation::MatchResult & result, const Avatar & avatar)
{
    const auto found = std::find_if(result.players.begin(), result.players.end(),
        [&avatar](const Simulation::MatchResult::PlayerTelemetry & player)
        {
            return player.avatar == avatar;
        });
    return found == result.players.end() ? nullptr : &*found;
}

AvatarsByWind observeAvatarsByWind(void)
{
    AvatarsByWind result;
    for(const LocalPlayer & player : GameData::players())
        result[player.wind()] = player.avatar;
    return result;
}

Avatar avatarForWind(const AvatarsByWind & avatars, const Wind & wind)
{
    const auto found = avatars.find(wind());
    return found == avatars.end() ? Avatar() : found->second;
}

void captureActions(Simulation::MatchResult & result,
                    const AvatarsByWind & avatarsByWind,
                    const ActionList & actions)
{
    for(const ActionMessage & action : actions)
    {
        std::string type;
        std::string subject;
        Land land;
        if(action.type() == Action::MahjongSummon)
        {
            type = "summon";
            subject = action.getString("creature");
            land = Land(action.getString("land"));
        }
        else if(action.type() == Action::MahjongCast)
        {
            type = "spell";
            subject = action.getString("spell");
            land = Land(action.getString("land"));
        }
        else
            continue;

        const Avatar avatar = avatarForWind(avatarsByWind,
                                            Wind(action.getString("currentWind")));
        auto telemetry = playerTelemetry(result, avatar);
        if(!telemetry || subject.empty()) continue;

        if(type == "summon")
        {
            ++telemetry->summons;
            ++telemetry->summonedCreatures[subject];
        }
        else
        {
            ++telemetry->spellsCast;
            ++telemetry->castSpells[subject];
        }

        Simulation::MatchResult::Event event;
        event.type = type;
        event.tick = result.ticks;
        event.adventurePhase = result.adventurePhases;
        event.avatar = avatar;
        event.subject = subject;
        event.land = land;
        result.events.push_back(event);
    }
}

void captureUnitChanges(Simulation::MatchResult & result,
                        const ObservedUnits & before, const ObservedUnits & after,
                        const ActionList & actions)
{
    std::set<int> dismissedUnits;
    for(const ActionMessage & action : actions)
    {
        if(action.type() == Action::AdventureMoves &&
           !Land(action.getString("land")).isValid())
            dismissedUnits.insert(action.getInteger("unit"));
    }

    for(const auto & value : before)
    {
        if(after.find(value.first) != after.end()) continue;

        auto telemetry = playerTelemetry(result, value.second.avatar);
        const bool dismissed = dismissedUnits.find(value.first) != dismissedUnits.end();
        if(telemetry)
        {
            if(dismissed) ++telemetry->dismissals;
            else ++telemetry->casualties;
        }

        Simulation::MatchResult::Event event;
        event.type = dismissed ? "dismissal" : "casualty";
        event.tick = result.ticks;
        event.adventurePhase = result.adventurePhases;
        event.avatar = value.second.avatar;
        event.subject = value.second.creature.toString();
        event.land = value.second.land;
        event.unit = value.first;
        result.events.push_back(event);
    }

    std::map<int, std::size_t> counts;
    for(const auto & value : after) ++counts[value.second.avatar()];
    for(auto & player : result.players)
        player.peakUnits = std::max(player.peakUnits, counts[player.avatar()]);
}

void captureFinalArmy(Simulation::MatchResult & result)
{
    for(const LocalPlayer & source : GameData::players())
    {
        auto player = playerTelemetry(result, source.avatar);
        if(!player) continue;
        player->finalArmy.clear();
        player->finalUnits = 0;
        for(const BattleParty & sourceParty : source.army)
        {
            Simulation::MatchResult::PartyComposition party;
            party.land = sourceParty.land();
            for(const BattleCreature* creature : sourceParty.toBattleCreatures())
            {
                if(!creature) continue;
                party.creatures.push_back(creature->Creature::toString());
                ++player->finalUnits;
            }
            if(!party.creatures.empty()) player->finalArmy.push_back(party);
        }
    }
}

template <typename Operation>
bool recordedTransition(const std::string & operation, Operation apply)
{
    return Replay::recordSystemOperation(operation, apply);
}

bool validConfiguration(const Simulation::MatchConfig & config, std::string* error)
{
    if(!findRuneGameRuleset(config.runeGameRulesetId,
                            config.runeGameRulesetVersion))
    {
        if(error) *error = "Rune Game ruleset is unavailable or incompatible: " +
            config.runeGameRulesetId + "@" +
            std::to_string(config.runeGameRulesetVersion);
        return false;
    }
    if(!findMatchTopology(config.matchTopologyId,
                          config.matchTopologyVersion))
    {
        if(error) *error = "Match topology is unavailable or incompatible: " +
            config.matchTopologyId + "@" +
            std::to_string(config.matchTopologyVersion);
        return false;
    }
    if(config.persons.size() != winds_all.size())
    {
        if(error) *error = "a match requires exactly four players";
        return false;
    }
    if(config.maximumTicks == 0 || config.maximumUnchangedTicks == 0 ||
       config.stateHashInterval == 0)
    {
        if(error) *error = "watchdog limits must be positive";
        return false;
    }

    std::set<int> avatars;
    std::set<int> clans;
    std::set<int> winds;
    for(const Person & person : config.persons)
    {
        if(!person.isAI())
        {
            if(error) *error = "headless matches require every player to be AI";
            return false;
        }
        if(!person.avatar.isValid() || person.avatar == Avatar(Avatar::Random) ||
           !person.clan.isValid() || !person.wind.isValid())
        {
            if(error) *error = "every player needs a concrete avatar, clan and wind";
            return false;
        }
        if(!avatars.insert(person.avatar()).second || !clans.insert(person.clan()).second ||
           !winds.insert(person.wind()).second)
        {
            if(error) *error = "avatars, clans and winds must be unique";
            return false;
        }

        const AvatarInfo & avatar = GameData::avatarInfo(person.avatar);
        if(std::find(avatar.clans.begin(), avatar.clans.end(), person.clan) == avatar.clans.end())
        {
            if(error) *error = "an avatar was assigned to an unsupported clan";
            return false;
        }
    }
    return true;
}

void finishResult(Simulation::MatchResult & result)
{
    result.rngDraws = GameplayRng::draws();
    result.finalStateHash = Replay::authoritativeStateHash();
    result.actionReplay = Replay::actionJournal(GameData::authoritativeState());
    captureFinalArmy(result);
    result.score = MatchScore::current();
}

void captureStage(Simulation::MatchResult & result, Simulation::MatchStage stage)
{
    Simulation::MatchResult::StageSnapshot & snapshot =
        result.stages[Simulation::index(stage)];
    snapshot.adventurePhase = result.adventurePhases;
    snapshot.score = MatchScore::current();
}
}

const char* Simulation::stageName(MatchStage stage)
{
    switch(stage)
    {
        case MatchStage::Early: return "early";
        case MatchStage::Middle: return "middle";
        case MatchStage::Late: return "late";
        case MatchStage::Count: break;
    }
    return "unknown";
}

const char* Simulation::statusName(MatchStatus status)
{
    switch(status)
    {
        case MatchStatus::Complete: return "complete";
        case MatchStatus::InvalidConfiguration: return "invalid_configuration";
        case MatchStatus::InitializationFailed: return "initialization_failed";
        case MatchStatus::Stalled: return "stalled";
        case MatchStatus::TickLimit: return "tick_limit";
        case MatchStatus::Exception: return "exception";
    }
    return "unknown";
}

Simulation::MatchResult Simulation::runMatch(const MatchConfig & config)
{
    MatchResult result;
    result.seed = config.seed;
    result.runeGameRulesetId = config.runeGameRulesetId;
    result.runeGameRulesetVersion = config.runeGameRulesetVersion;
    result.matchTopologyId = config.matchTopologyId;
    result.matchTopologyVersion = config.matchTopologyVersion;

    if(!validConfiguration(config, &result.error)) return result;

    try
    {
        RecoverySuppression suppressRecovery;
        ReplayCaptureScope captureReplay(config.captureFullReplay);
        BehaviorProfileScope selectBehaviorProfile(config);
        RuneGameRulesetScope selectRuleset(config);
        MatchTopologyScope selectTopology(config);
        result.fullReplayCaptured = config.captureFullReplay;
        GameplayRng::seed(config.seed);
        GameData::setAIDifficulty(config.difficulty);
        if(!GameData::initPersons(config.persons))
        {
            result.status = MatchStatus::InitializationFailed;
            result.error = "GameData rejected the exact player configuration";
            return result;
        }
        for(const Person & person : config.persons)
        {
            MatchResult::PlayerTelemetry telemetry;
            telemetry.avatar = person.avatar;
            result.players.push_back(telemetry);
        }
        if(!recordedTransition("init_mahjong", [](){ return GameData::initMahjong(); }))
        {
            result.status = MatchStatus::InitializationFailed;
            result.error = "the first Mahjong phase did not initialize";
            finishResult(result);
            return result;
        }

        std::string checkpointHash = Replay::authoritativeStateHash();
        while(result.ticks < config.maximumTicks)
        {
            ++result.ticks;
            ActionList actions;
            bool advanced = false;
            const ObservedUnits unitsBefore = observeUnits();
            const AvatarsByWind avatarsByWind = observeAvatarsByWind();

            switch(GameData::loadedGamePart())
            {
                case Menu::MahjongPart:
                    if(config.captureFullReplay)
                        advanced = Replay::recordSystemOperation("mahjong_tick", [&actions]()
                        {
                            return GameData::mahjong2Client(
                                GameData::currentPerson().avatar, actions);
                        });
                    else
                        advanced = GameData::mahjong2Client(
                            GameData::currentPerson().avatar, actions);
                    break;

                case Menu::MahjongSummaryPart:
                    ++result.mahjongHands;
                    advanced = recordedTransition("init_adventure",
                        [](){ return GameData::initAdventure(); });
                    break;

                case Menu::AdventurePart:
                    if(config.captureFullReplay)
                        advanced = Replay::recordSystemOperation("adventure_tick", [&actions]()
                        {
                            return GameData::adventure2Client(
                                GameData::currentPerson().avatar, actions);
                        });
                    else
                        advanced = GameData::adventure2Client(
                            GameData::currentPerson().avatar, actions);
                    break;

                case Menu::BattleSummaryPart:
                    ++result.adventurePhases;
                    if(result.adventurePhases == 5)
                        captureStage(result, MatchStage::Early);
                    else if(result.adventurePhases == 10)
                        captureStage(result, MatchStage::Middle);

                    if(GameData::isGameOver())
                    {
                        captureStage(result, MatchStage::Late);
                        const JsonObject beforeSummary = GameData::authoritativeState();
                        GameData::setGamePart(Menu::GameSummaryPart);
                        Replay::recordSystemTransition(beforeSummary, "game_summary",
                                                      GameData::authoritativeState());
                        result.status = MatchStatus::Complete;
                        finishResult(result);
                        return result;
                    }
                    advanced = recordedTransition("init_mahjong",
                        [](){ return GameData::initMahjong(); });
                    break;

                default:
                    result.status = MatchStatus::Stalled;
                    result.error = "unexpected game phase " +
                        std::to_string(GameData::loadedGamePart());
                    finishResult(result);
                    return result;
            }

            result.emittedActions += actions.size();
            captureActions(result, avatarsByWind, actions);
            captureUnitChanges(result, unitsBefore, observeUnits(), actions);
            if(!advanced)
                ++result.unchangedTicks;
            else
                result.unchangedTicks = 0;

            if(config.maximumUnchangedTicks <= result.unchangedTicks)
            {
                result.status = MatchStatus::Stalled;
                result.error = !advanced ? "phase driver rejected progress" :
                    "authoritative state stopped changing";
                finishResult(result);
                return result;
            }

            if(result.ticks % config.stateHashInterval == 0)
            {
                const std::string currentHash = Replay::authoritativeStateHash();
                if(currentHash == checkpointHash)
                {
                    result.status = MatchStatus::Stalled;
                    result.error = "authoritative state did not change during a watchdog interval";
                    finishResult(result);
                    return result;
                }
                checkpointHash = currentHash;
            }
        }

        result.status = MatchStatus::TickLimit;
        result.error = "maximum match tick count reached";
        finishResult(result);
        return result;
    }
    catch(const std::exception & exception)
    {
        result.status = MatchStatus::Exception;
        result.error = exception.what();
    }
    catch(...)
    {
        result.status = MatchStatus::Exception;
        result.error = "unknown exception";
    }

    finishResult(result);
    return result;
}
