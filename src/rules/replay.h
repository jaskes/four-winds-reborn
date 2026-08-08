#ifndef FOUR_WINDS_REPLAY_H
#define FOUR_WINDS_REPLAY_H

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "actions.h"

namespace Replay
{
    enum class FailureKind
    {
        None,
        UnknownSystemOperation,
        SystemExceptionMismatch,
        SystemOutcomeMismatch,
        UnknownAction,
        ActionRejected,
        StateHashMismatch
    };

    struct Failure
    {
        FailureKind kind = FailureKind::None;
        std::size_t step = 0;
        std::string expected;
        std::string actual;
        std::string detail;

        bool isValid(void) const { return kind != FailureKind::None; }
    };

    struct JournalInfo
    {
        int         schema = 0;
        int         actionCount = 0;
        int         gamePart = 0;
        long long   startedAtEpoch = 0;
        long long   savedAtEpoch = 0;
        std::string difficulty;
        std::string rulesetId;
        int         rulesetVersion = 0;
        std::string topologyId;
        int         topologyVersion = 0;
        std::string contentPackageId;
        int         contentPackageVersion = 0;
        bool        contiguousToCheckpoint = false;
        bool        developerAssisted = false;
    };

    struct Step
    {
        Avatar avatar;
        JsonObject action;
        std::string systemOperation;
        bool expectedAccepted = true;
        std::string expectedException;
        std::string expectedStateHash;

        Step(const Avatar & actor, const ClientMessage & message, const std::string & hash)
            : avatar(actor), action(message), expectedStateHash(hash) {}

        Step(const Avatar & actor, const JsonObject & message, const std::string & hash)
            : avatar(actor), action(message), expectedStateHash(hash) {}

        Step(const std::string & operation, const std::string & hash, bool accepted = true,
             const std::string & exception = std::string())
            : systemOperation(operation), expectedAccepted(accepted),
              expectedException(exception), expectedStateHash(hash) {}

        bool isSystem(void) const { return !systemOperation.empty(); }

        JsonObject toJsonObject(void) const;
    };

    class Playback
    {
        JsonObject              initialState;
        JsonObject              previousState;
        std::vector<Step>       steps;
        std::vector<std::size_t> phasePositions;
        std::string             behaviorProfile;
        std::size_t             currentPosition = 0;
        bool                    previousSkipRepeatSay = false;
        bool                    previousStateValid = false;
        bool                    opened = false;
        Failure                 playbackFailure;

        bool applyRange(std::size_t begin, std::size_t end, std::string* error,
                        Failure* failure);
        bool restoreInitial(std::string* error);
        void restorePrevious(void);

    public:
        Playback() = default;
        explicit Playback(const JsonObject &, std::string* error = nullptr);
        ~Playback();

        Playback(const Playback &) = delete;
        Playback & operator=(const Playback &) = delete;

        bool open(const JsonObject &, std::string* error = nullptr);
        void close(void);

        bool isOpen(void) const { return opened; }
        bool atBeginning(void) const { return currentPosition == 0; }
        bool atEnd(void) const { return currentPosition == steps.size(); }
        std::size_t position(void) const { return currentPosition; }
        std::size_t size(void) const { return steps.size(); }
        std::size_t phaseIndex(void) const;
        std::size_t phaseCount(void) const { return phasePositions.size(); }
        int gamePart(void) const;
        const Failure & failure(void) const { return playbackFailure; }

        bool seek(std::size_t, std::string* error = nullptr);
        bool stepForward(std::string* error = nullptr);
        bool stepBackward(std::string* error = nullptr);
        bool nextPhase(std::string* error = nullptr);
        bool previousPhase(std::string* error = nullptr);
    };

    std::unique_ptr<ClientMessage> clientMessageFromJson(const JsonObject & action);
    std::string authoritativeStateHash(void);
    bool actionRecordingEnabled(void);
    std::size_t actionJournalLimit(void);
    void setActionJournalLimit(std::size_t maximumSteps);
    void clearActionJournal(void);
    std::size_t actionJournalSize(void);
    void recordAcceptedAction(const JsonObject & beforeState, const Avatar & actor,
                              const ClientMessage & action, const JsonObject & afterState);
    void recordSystemTransition(const JsonObject & beforeState, const std::string & operation,
                                const JsonObject & afterState);
    bool recordSystemOperation(const std::string & operation,
                               const std::function<bool(void)> & apply);
    JsonObject actionJournal(const JsonObject & checkpointState);
    bool inspectJournal(const JsonObject & journal, JournalInfo & info,
                        std::string* error = nullptr);
    bool run(const JsonObject & initialState, const std::vector<Step> & steps,
             std::string* error = nullptr);
    bool run(const JsonObject & journal, std::string* error = nullptr);
}

#endif
