#include "replay.h"

#include <algorithm>
#include <ctime>
#include <exception>

#include "aiprofile.h"
#include "contentpackage.h"
#include "gamedata.h"
#include "matchtopology.h"
#include "recovery.h"
#include "runegameruleset.h"

namespace GameData
{
extern bool skipRepeatSay;
}

namespace
{
constexpr std::size_t MaximumJournalSteps = 64;

JsonObject journalInitialState;
std::vector<Replay::Step> journalSteps;
std::string journalTailHash;
long long journalStartedAtEpoch = 0;
bool recordingEnabled = true;
std::size_t journalStepLimit = MaximumJournalSteps;

bool isAdventureAction(int type)
{
    return type == Action::ClientUnitMoved || type == Action::ClientLandClaim ||
           type == Action::ClientAdventureUndo || type == Action::ClientBattleReady ||
           type == Action::ClientBattleChoice;
}

long long parseEpoch(const std::string & value)
{
    if(value.empty()) return 0;
    try { return std::stoll(value); }
    catch(...) { return 0; }
}

void beginJournal(const JsonObject & state)
{
    journalInitialState = state;
    journalSteps.clear();
    journalStartedAtEpoch = static_cast<long long>(std::time(nullptr));
}

JsonObject stateWithoutArtifactIdentity(const JsonObject & state)
{
    JsonObject result;
    for(const std::string & key : state.keys())
    {
        if(key == RuneGameRulesetIdentityKey || key == MatchTopologyIdentityKey ||
           key == ContentPackageIdentityKey) continue;

        const JsonValue* value = state.getValue(key);
        if(!value) continue;
        switch(value->getType())
        {
            case JsonType::Null: result.addNull(key); break;
            case JsonType::Integer: result.addInteger(key, value->getInteger()); break;
            case JsonType::Double: result.addDouble(key, value->getDouble()); break;
            case JsonType::String: result.addString(key, value->getString()); break;
            case JsonType::Boolean: result.addBoolean(key, value->getBoolean()); break;
            case JsonType::Object:
                result.addObject(key, static_cast<const JsonObject &>(*value));
                break;
            case JsonType::Array:
                result.addArray(key, static_cast<const JsonArray &>(*value));
                break;
        }
    }
    return result;
}

std::string replayStateHash(const JsonObject & state)
{
    // Ruleset, match-topology and content-package identities are validated separately by
    // Replay::run. Keeping them outside the gameplay hash preserves existing
    // Classic replay hashes while rejecting incompatible artifacts up front.
    return Recovery::stateHash(stateWithoutArtifactIdentity(state));
}

struct RecordingPause
{
    const bool previous;

    RecordingPause() : previous(recordingEnabled) { recordingEnabled = false; }
    ~RecordingPause() { recordingEnabled = previous; }
};

struct BehaviorProfileReplayScope
{
    const bool previousEnabled;
    const AI::BehaviorProfile previousProfile;

    BehaviorProfileReplayScope(bool force, AI::BehaviorProfile profile) :
        previousEnabled(AI::behaviorProfileOverrideEnabled()),
        previousProfile(AI::behaviorProfileOverride())
    {
        if(force) AI::setBehaviorProfileOverride(profile);
        else AI::clearBehaviorProfileOverride();
    }

    ~BehaviorProfileReplayScope()
    {
        if(previousEnabled) AI::setBehaviorProfileOverride(previousProfile);
        else AI::clearBehaviorProfileOverride();
    }
};

bool decodeJournalSteps(const JsonObject & journal, std::vector<Replay::Step> & steps,
                        std::string & selectedProfile, std::string* error)
{
    const JsonArray* encodedSteps = journal.getArray("steps");
    if(!encodedSteps)
    {
        if(error) *error = "journal is missing steps";
        return false;
    }

    selectedProfile = journal.getString("aiBehaviorProfile", "native");
    AI::BehaviorProfile profile = AI::BehaviorProfile::Balanced;
    if(selectedProfile != "native" &&
       !AI::behaviorProfileFromString(selectedProfile, profile))
    {
        if(error) *error = "journal has an invalid AI behavior profile: " + selectedProfile;
        return false;
    }

    steps.clear();
    steps.reserve(encodedSteps->size());
    for(std::size_t index = 0; index < encodedSteps->size(); ++index)
    {
        const JsonObject* encoded = encodedSteps->getObject(index);
        const JsonObject* action = encoded ? encoded->getObject("action") : nullptr;
        const std::string operation = encoded ?
            encoded->getString("systemOperation") : std::string();
        const bool expectedAccepted = encoded ?
            encoded->getBoolean("expectedAccepted", true) : true;
        const std::string expectedException = encoded ?
            encoded->getString("expectedException") : std::string();
        const Avatar actor(encoded ? encoded->getString("avatar") : std::string());
        const std::string expected = encoded ?
            encoded->getString("expectedStateHash") : std::string();
        if(!encoded || expected.empty() ||
           (operation.empty() && (!action || !actor.isValid())) ||
           (!operation.empty() && action))
        {
            if(error) *error = "invalid journal step " + std::to_string(index);
            return false;
        }
        if(operation.empty()) steps.emplace_back(actor, *action, expected);
        else steps.emplace_back(operation, expected, expectedAccepted, expectedException);
    }

    return true;
}

BehaviorProfileReplayScope replayProfileScope(const std::string & selectedProfile)
{
    AI::BehaviorProfile profile = AI::BehaviorProfile::Balanced;
    const bool forceProfile = selectedProfile != "native";
    if(forceProfile) AI::behaviorProfileFromString(selectedProfile, profile);
    return BehaviorProfileReplayScope(forceProfile, profile);
}

bool applyReplayStep(const Replay::Step & step, std::size_t index, std::string* error,
                     Replay::Failure* failure = nullptr)
{
    auto fail = [&](Replay::FailureKind kind, const std::string & expected,
                    const std::string & actual, const std::string & detail,
                    const std::string & message)
    {
        if(failure)
        {
            failure->kind = kind;
            failure->step = index;
            failure->expected = expected;
            failure->actual = actual;
            failure->detail = detail;
        }
        if(error) *error = message;
        return false;
    };

    bool accepted = false;
    if(step.isSystem())
    {
        std::string actualException;
        try
        {
            if(step.systemOperation == "init_mahjong")
                accepted = GameData::initMahjong();
            else if(step.systemOperation == "init_adventure")
                accepted = GameData::initAdventure();
            else if(step.systemOperation == "game_summary")
            {
                GameData::setGamePart(Menu::GameSummaryPart);
                accepted = true;
            }
            else if(step.systemOperation == "mahjong_tick")
            {
                ActionList emitted;
                accepted = GameData::mahjong2Client(
                    GameData::currentPerson().avatar, emitted);
            }
            else if(step.systemOperation == "adventure_tick")
            {
                ActionList emitted;
                accepted = GameData::adventure2Client(
                    GameData::currentPerson().avatar, emitted);
            }
            else
            {
                return fail(Replay::FailureKind::UnknownSystemOperation,
                    "known system operation", step.systemOperation,
                    step.systemOperation, "unknown system operation at step " +
                    std::to_string(index));
            }
        }
        catch(const std::exception & failure)
        {
            actualException = failure.what();
        }
        catch(...)
        {
            actualException = "unknown exception";
        }

        if(actualException != step.expectedException)
        {
            return fail(Replay::FailureKind::SystemExceptionMismatch,
                step.expectedException.empty() ? "no exception" : step.expectedException,
                actualException.empty() ? "no exception" : actualException,
                step.systemOperation,
                "system operation exception mismatch at step " +
                std::to_string(index) + ": expected=" + step.expectedException +
                " actual=" + actualException);
        }
    }
    else
    {
        std::unique_ptr<ClientMessage> action = Replay::clientMessageFromJson(step.action);
        if(!action)
        {
            return fail(Replay::FailureKind::UnknownAction, "known action",
                step.action.toString(), std::string(),
                "unknown action at step " + std::to_string(index));
        }

        ActionList emitted;
        accepted = isAdventureAction(action->type()) ?
            GameData::client2Adventure(step.avatar, *action, emitted) :
            GameData::client2Mahjong(step.avatar, *action, emitted);
    }
    if(step.isSystem() && accepted != step.expectedAccepted)
    {
        return fail(Replay::FailureKind::SystemOutcomeMismatch,
            step.expectedAccepted ? "accepted" : "rejected",
            accepted ? "accepted" : "rejected", step.systemOperation,
            "system operation outcome mismatch at step " + std::to_string(index));
    }
    if(!step.isSystem() && !accepted)
    {
        return fail(Replay::FailureKind::ActionRejected, "accepted", "rejected",
            step.action.toString(), "action rejected at step " + std::to_string(index));
    }

    const std::string actualHash = Replay::authoritativeStateHash();
    if(actualHash != step.expectedStateHash)
    {
        return fail(Replay::FailureKind::StateHashMismatch,
            step.expectedStateHash, actualHash,
            step.isSystem() ? step.systemOperation : step.action.toString(),
            "state hash mismatch at step " + std::to_string(index) +
            ": expected=" + step.expectedStateHash + " actual=" + actualHash);
    }

    return true;
}
}

JsonObject Replay::Step::toJsonObject(void) const
{
    JsonObject result;
    if(isSystem())
    {
        result.addString("systemOperation", systemOperation);
        result.addBoolean("expectedAccepted", expectedAccepted);
        if(!expectedException.empty())
            result.addString("expectedException", expectedException);
    }
    else
    {
        result.addString("avatar", avatar.toString());
        result.addObject("action", action);
    }
    result.addString("expectedStateHash", expectedStateHash);
    return result;
}

std::unique_ptr<ClientMessage> Replay::clientMessageFromJson(const JsonObject & action)
{
    switch(action.getInteger("type"))
    {
        case Action::ClientReady: return std::make_unique<ClientReady>();
        case Action::ClientButtonGame: return std::make_unique<ClientButtonGame>();
        case Action::ClientButtonPass: return std::make_unique<ClientButtonPass>();
        case Action::ClientChaoVariant:
            return std::make_unique<ClientChaoVariant>(action.getInteger("variant"));
        case Action::ClientButtonPung: return std::make_unique<ClientButtonPung>();
        case Action::ClientButtonKong1: return std::make_unique<ClientButtonKong1>();
        case Action::ClientButtonKong2: return std::make_unique<ClientButtonKong2>();
        case Action::ClientDropIndex:
            return std::make_unique<ClientDropIndex>(action.getInteger("dropIndex"));
        case Action::ClientSayGame: return std::make_unique<ClientSayGame>();
        case Action::ClientSayChao: return std::make_unique<ClientSayChao>();
        case Action::ClientSayPung: return std::make_unique<ClientSayPung>();
        case Action::ClientSayKong:
            return std::make_unique<ClientSayKong>(action.getInteger("kongType"));
        case Action::ClientSummonCreature:
            return std::make_unique<ClientSummonCreature>(Creature(action.getString("creature")),
                Land(action.getString("land")), action.getBoolean("force"));
        case Action::ClientCastSpell:
        {
            const Spell spell(action.getString("spell"));
            if(action.hasKey("target"))
                return std::make_unique<ClientCastSpell>(spell, Avatar(action.getString("target")));
            if(action.hasKey("land") || action.hasKey("unit") || action.hasKey("force"))
                return std::make_unique<ClientCastSpell>(spell, Land(action.getString("land")),
                    action.getInteger("unit"), action.getBoolean("force"));
            return std::make_unique<ClientCastSpell>(spell);
        }
        case Action::ClientUnitMoved:
            return std::make_unique<ClientUnitMoved>(action.getInteger("unit"),
                Land(action.getString("land")));
        case Action::ClientLandClaim:
            return std::make_unique<ClientLandClaim>(Land(action.getString("land")));
        case Action::ClientAdventureUndo: return std::make_unique<ClientAdventureUndo>();
        case Action::ClientBattleReady: return std::make_unique<ClientBattleReady>();
        case Action::ClientBattleChoice:
            return std::make_unique<ClientBattleChoice>(action.getInteger("actor", -1),
                action.getInteger("target", -1), action.getBoolean("autoResolve"));
        case Action::ClientLuckChoice:
            return std::make_unique<ClientLuckChoice>(action.getInteger("index", -1));
        default: return nullptr;
    }
}

std::string Replay::authoritativeStateHash(void)
{
    const JsonObject state = GameData::authoritativeState();
    return replayStateHash(state);
}

bool Replay::actionRecordingEnabled(void)
{
    return recordingEnabled;
}

std::size_t Replay::actionJournalLimit(void)
{
    return journalStepLimit;
}

void Replay::setActionJournalLimit(std::size_t maximumSteps)
{
    journalStepLimit = maximumSteps;
}

void Replay::clearActionJournal(void)
{
    journalInitialState.clear();
    journalSteps.clear();
    journalTailHash.clear();
    journalStartedAtEpoch = 0;
}

std::size_t Replay::actionJournalSize(void)
{
    return journalSteps.size();
}

void Replay::recordAcceptedAction(const JsonObject & beforeState, const Avatar & actor,
                                  const ClientMessage & action, const JsonObject & afterState)
{
    if(!recordingEnabled) return;

    const std::string beforeHash = replayStateHash(beforeState);
    if(!journalInitialState.isValid() || journalTailHash != beforeHash ||
       (journalStepLimit && journalStepLimit <= journalSteps.size()))
    {
        beginJournal(beforeState);
    }

    journalTailHash = replayStateHash(afterState);
    journalSteps.emplace_back(actor, JsonObject(action), journalTailHash);
}

void Replay::recordSystemTransition(const JsonObject & beforeState,
                                    const std::string & operation,
                                    const JsonObject & afterState)
{
    if(!recordingEnabled || operation.empty()) return;

    const std::string beforeHash = replayStateHash(beforeState);
    if(!journalInitialState.isValid() || journalTailHash != beforeHash ||
       (journalStepLimit && journalStepLimit <= journalSteps.size()))
    {
        beginJournal(beforeState);
    }

    journalTailHash = replayStateHash(afterState);
    journalSteps.emplace_back(operation, journalTailHash);
}

bool Replay::recordSystemOperation(const std::string & operation,
                                   const std::function<bool(void)> & apply)
{
    if(!recordingEnabled || operation.empty()) return apply();

    const JsonObject beforeState = GameData::authoritativeState();
    bool accepted = false;
    std::string exceptionText;
    std::exception_ptr exception;
    try
    {
        RecordingPause pause;
        accepted = apply();
    }
    catch(const std::exception & failure)
    {
        exceptionText = failure.what();
        exception = std::current_exception();
    }
    catch(...)
    {
        exceptionText = "unknown exception";
        exception = std::current_exception();
    }
    const JsonObject afterState = GameData::authoritativeState();
    const std::string beforeHash = replayStateHash(beforeState);
    if(!journalInitialState.isValid() || journalTailHash != beforeHash ||
       (journalStepLimit && journalStepLimit <= journalSteps.size()))
    {
        beginJournal(beforeState);
    }
    journalTailHash = replayStateHash(afterState);
    journalSteps.emplace_back(operation, journalTailHash, accepted, exceptionText);
    if(exception) std::rethrow_exception(exception);
    return accepted;
}

JsonObject Replay::actionJournal(const JsonObject & checkpointState)
{
    JsonObject result;
    const bool hasSystemOperations = std::any_of(journalSteps.begin(), journalSteps.end(),
        [](const Step & step){ return step.isSystem(); });
    const bool forcedProfile = AI::behaviorProfileOverrideEnabled();
    result.addInteger("schema", forcedProfile ? 3 : (hasSystemOperations ? 2 : 1));
    result.addObject(RuneGameRulesetIdentityKey,
                     runeGameRulesetIdentityJson(activeRuneGameRuleset()));
    result.addObject(MatchTopologyIdentityKey,
                     matchTopologyIdentityJson(activeMatchTopology()));
    result.addObject(ContentPackageIdentityKey,
                     contentPackageIdentityJson(activeContentPackageManifest()));
    result.addString("aiBehaviorProfile", forcedProfile ?
        AI::behaviorProfileName(AI::behaviorProfileOverride()) : "native");
    result.addInteger("actionCount", static_cast<int>(journalSteps.size()));
    result.addBoolean("developerAssisted", GameData::developerAssisted());
    result.addString("startedAtEpoch", std::to_string(journalStartedAtEpoch));
    result.addString("savedAtEpoch", std::to_string(static_cast<long long>(std::time(nullptr))));
    result.addInteger("gamePart", checkpointState.getInteger("gamepart"));
    result.addString("difficulty", checkpointState.getString("ai:difficulty"));

    const std::string checkpointHash = replayStateHash(checkpointState);
    result.addString("checkpointStateHash", checkpointHash);
    result.addString("tailStateHash", journalTailHash);
    result.addBoolean("contiguousToCheckpoint",
                      !journalSteps.empty() && journalTailHash == checkpointHash);

    if(journalInitialState.isValid()) result.addObject("initialState", journalInitialState);
    JsonArray steps;
    for(const Step & step : journalSteps) steps.addObject(step.toJsonObject());
    result.addArray("steps", steps);
    return result;
}

bool Replay::inspectJournal(const JsonObject & journal, JournalInfo & info,
                            std::string* error)
{
    info = JournalInfo();
    const JsonObject* initialState = journal.getObject("initialState");
    const JsonArray* encodedSteps = journal.getArray("steps");
    if(!initialState || !encodedSteps)
    {
        if(error) *error = "journal is missing initialState or steps";
        return false;
    }

    const int schema = journal.getInteger("schema");
    if(schema < 1 || 3 < schema)
    {
        if(error) *error = "journal uses an unsupported schema";
        return false;
    }

    RuneGameRulesetIdentity journalRuleset;
    RuneGameRulesetIdentity stateRuleset;
    if(!resolveRuneGameRulesetIdentity(journal, journalRuleset, true, error) ||
       !resolveRuneGameRulesetIdentity(*initialState, stateRuleset, true, error))
        return false;
    if(!sameRuneGameRuleset(journalRuleset, stateRuleset))
    {
        if(error) *error = "journal and initial state use different Rune Game rulesets";
        return false;
    }

    MatchTopologyIdentity journalTopology;
    MatchTopologyIdentity stateTopology;
    if(!resolveMatchTopologyIdentity(journal, journalTopology, true, error) ||
       !resolveMatchTopologyIdentity(*initialState, stateTopology, true, error))
        return false;
    if(!sameMatchTopology(journalTopology, stateTopology))
    {
        if(error) *error = "journal and initial state use different match topologies";
        return false;
    }

    ContentPackageIdentity journalPackage;
    ContentPackageIdentity statePackage;
    if(!resolveContentPackageIdentity(journal, journalPackage, true, error) ||
       !resolveContentPackageIdentity(*initialState, statePackage, true, error))
        return false;
    if(!sameContentPackage(journalPackage, statePackage))
    {
        if(error) *error = "journal and initial state use different content packages";
        return false;
    }

    if(journal.getInteger("actionCount", -1) != static_cast<int>(encodedSteps->size()))
    {
        if(error) *error = "journal action count does not match its step list";
        return false;
    }

    for(std::size_t index = 0; index < encodedSteps->size(); ++index)
    {
        const JsonObject* encoded = encodedSteps->getObject(index);
        const JsonObject* action = encoded ? encoded->getObject("action") : nullptr;
        const std::string operation = encoded ?
            encoded->getString("systemOperation") : std::string();
        const Avatar actor(encoded ? encoded->getString("avatar") : std::string());
        const std::string expected = encoded ?
            encoded->getString("expectedStateHash") : std::string();
        if(!encoded || expected.empty() ||
           (operation.empty() && (!action || !actor.isValid())) ||
           (!operation.empty() && action))
        {
            if(error) *error = "invalid journal step " + std::to_string(index);
            return false;
        }
    }

    info.schema = schema;
    info.actionCount = static_cast<int>(encodedSteps->size());
    info.gamePart = journal.getInteger("gamePart", initialState->getInteger("gamepart"));
    info.startedAtEpoch = parseEpoch(journal.getString("startedAtEpoch"));
    info.savedAtEpoch = parseEpoch(journal.getString("savedAtEpoch"));
    info.difficulty = journal.getString("difficulty",
                                        initialState->getString("ai:difficulty"));
    info.rulesetId = journalRuleset.id;
    info.rulesetVersion = journalRuleset.version;
    info.topologyId = journalTopology.id;
    info.topologyVersion = journalTopology.version;
    info.contentPackageId = journalPackage.id;
    info.contentPackageVersion = journalPackage.version;
    info.contiguousToCheckpoint = journal.getBoolean("contiguousToCheckpoint");
    info.developerAssisted = journal.getBoolean("developerAssisted");
    if(error) error->clear();
    return true;
}

bool Replay::run(const JsonObject & initialState, const std::vector<Step> & steps,
                 std::string* error)
{
    RecordingPause pause;
    if(!GameData::restoreState(initialState))
    {
        if(error) *error = "initial state rejected";
        return false;
    }

    for(std::size_t index = 0; index < steps.size(); ++index)
        if(!applyReplayStep(steps[index], index, error)) return false;

    if(error) error->clear();
    return true;
}

bool Replay::run(const JsonObject & journal, std::string* error)
{
    JournalInfo info;
    if(!inspectJournal(journal, info, error)) return false;

    const JsonObject* initialState = journal.getObject("initialState");
    std::vector<Step> steps;
    std::string selectedProfile;
    if(!decodeJournalSteps(journal, steps, selectedProfile, error)) return false;
    auto profileScope = replayProfileScope(selectedProfile);

    return run(*initialState, steps, error);
}

Replay::Playback::Playback(const JsonObject & journal, std::string* error)
{
    open(journal, error);
}

Replay::Playback::~Playback()
{
    close();
}

bool Replay::Playback::open(const JsonObject & journal, std::string* error)
{
    close();
    playbackFailure = Failure();

    JournalInfo info;
    if(!inspectJournal(journal, info, error)) return false;

    const JsonObject* encodedInitial = journal.getObject("initialState");
    if(!encodedInitial)
    {
        if(error) *error = "journal is missing initialState";
        return false;
    }

    std::vector<Step> decodedSteps;
    std::string decodedProfile;
    if(!decodeJournalSteps(journal, decodedSteps, decodedProfile, error)) return false;

    previousState = GameData::authoritativeState();
    previousStateValid = Recovery::validateSaveState(previousState);
    previousSkipRepeatSay = GameData::skipRepeatSay;
    initialState = *encodedInitial;
    steps.swap(decodedSteps);
    behaviorProfile = decodedProfile;
    currentPosition = 0;
    phasePositions.clear();

    RecordingPause pause;
    auto profileScope = replayProfileScope(behaviorProfile);
    if(!GameData::restoreState(initialState))
    {
        restorePrevious();
        if(error) *error = "initial state rejected";
        return false;
    }

    phasePositions.push_back(0);
    int previousPart = GameData::loadedGamePart();
    for(std::size_t index = 0; index < steps.size(); ++index)
    {
        if(!applyReplayStep(steps[index], index, error, &playbackFailure))
        {
            restorePrevious();
            steps.clear();
            phasePositions.clear();
            return false;
        }

        const int currentPart = GameData::loadedGamePart();
        if(currentPart != previousPart)
        {
            phasePositions.push_back(index + 1);
            previousPart = currentPart;
        }
    }

    if(!GameData::restoreState(initialState))
    {
        restorePrevious();
        steps.clear();
        phasePositions.clear();
        if(error) *error = "initial state rejected after replay validation";
        return false;
    }

    opened = true;
    if(error) error->clear();
    return true;
}

void Replay::Playback::restorePrevious(void)
{
    if(previousStateValid)
    {
        GameData::restoreState(previousState);
        GameData::skipRepeatSay = previousSkipRepeatSay;
    }
    previousStateValid = false;
    previousSkipRepeatSay = false;
    previousState.clear();
}

void Replay::Playback::close(void)
{
    if(opened || previousStateValid) restorePrevious();
    opened = false;
    initialState.clear();
    steps.clear();
    phasePositions.clear();
    behaviorProfile.clear();
    currentPosition = 0;
    playbackFailure = Failure();
}

bool Replay::Playback::restoreInitial(std::string* error)
{
    RecordingPause pause;
    auto profileScope = replayProfileScope(behaviorProfile);
    if(!GameData::restoreState(initialState))
    {
        if(error) *error = "initial state rejected";
        return false;
    }
    currentPosition = 0;
    return true;
}

bool Replay::Playback::applyRange(std::size_t begin, std::size_t end,
                                  std::string* error, Failure* failure)
{
    RecordingPause pause;
    auto profileScope = replayProfileScope(behaviorProfile);
    for(std::size_t index = begin; index < end; ++index)
    {
        if(!applyReplayStep(steps[index], index, error, failure)) return false;
        currentPosition = index + 1;
    }
    if(error) error->clear();
    return true;
}

bool Replay::Playback::seek(std::size_t requestedPosition, std::string* error)
{
    playbackFailure = Failure();
    if(!opened)
    {
        if(error) *error = "replay playback is not open";
        return false;
    }
    if(steps.size() < requestedPosition)
    {
        if(error) *error = "replay position is out of range";
        return false;
    }
    if(requestedPosition == currentPosition)
    {
        if(error) error->clear();
        return true;
    }

    const std::size_t previousPosition = currentPosition;
    if(requestedPosition < currentPosition && !restoreInitial(error)) return false;
    if(applyRange(currentPosition, requestedPosition, error, &playbackFailure)) return true;

    // Keep a failed seek from leaving the viewer on an undocumented partial state.
    std::string ignored;
    Failure ignoredFailure;
    if(restoreInitial(&ignored))
        applyRange(0, previousPosition, &ignored, &ignoredFailure);
    return false;
}

bool Replay::Playback::stepForward(std::string* error)
{
    if(atEnd())
    {
        if(error) error->clear();
        return true;
    }
    return seek(currentPosition + 1, error);
}

bool Replay::Playback::stepBackward(std::string* error)
{
    if(atBeginning())
    {
        if(error) error->clear();
        return true;
    }
    return seek(currentPosition - 1, error);
}

std::size_t Replay::Playback::phaseIndex(void) const
{
    if(phasePositions.empty()) return 0;
    const auto next = std::upper_bound(phasePositions.begin(), phasePositions.end(),
                                       currentPosition);
    return next == phasePositions.begin() ? 0 :
        static_cast<std::size_t>(std::distance(phasePositions.begin(), next) - 1);
}

int Replay::Playback::gamePart(void) const
{
    return opened ? GameData::loadedGamePart() : 0;
}

bool Replay::Playback::nextPhase(std::string* error)
{
    if(!opened)
    {
        if(error) *error = "replay playback is not open";
        return false;
    }
    const auto next = std::upper_bound(phasePositions.begin(), phasePositions.end(),
                                       currentPosition);
    return next == phasePositions.end() ? seek(steps.size(), error) : seek(*next, error);
}

bool Replay::Playback::previousPhase(std::string* error)
{
    if(!opened)
    {
        if(error) *error = "replay playback is not open";
        return false;
    }
    auto previous = std::lower_bound(phasePositions.begin(), phasePositions.end(),
                                     currentPosition);
    if(previous == phasePositions.begin()) return seek(0, error);
    --previous;
    return seek(*previous, error);
}
