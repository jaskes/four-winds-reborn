/***************************************************************************
 *   Copyright (C) 2026 by Four Winds Reborn contributors                 *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#include <algorithm>

#include "gameobjects.h"
#include "runegameruleset.h"

namespace
{
    class ClassicRuneGameRuleset : public RuneGameRuleset
    {
    public:
        const std::string & id(void) const override
        {
            static const std::string value(ClassicRuneGameRulesetId);
            return value;
        }

        int version(void) const override
        {
            return ClassicRuneGameRulesetVersion;
        }

        const std::vector<int> & wallStoneIds(void) const override
        {
            static const std::vector<int> stones = {
                Stone::Skull1, Stone::Skull2, Stone::Skull3,
                Stone::Skull4, Stone::Skull5, Stone::Skull6,
                Stone::Skull7, Stone::Skull8, Stone::Skull9,
                Stone::Sword1, Stone::Sword2, Stone::Sword3,
                Stone::Sword4, Stone::Sword5, Stone::Sword6,
                Stone::Sword7, Stone::Sword8, Stone::Sword9,
                Stone::Number1, Stone::Number2, Stone::Number3,
                Stone::Number4, Stone::Number5, Stone::Number6,
                Stone::Number7, Stone::Number8, Stone::Number9,
                Stone::Wind1, Stone::Wind2, Stone::Wind3, Stone::Wind4,
                Stone::Dragon1, Stone::Dragon2, Stone::Dragon3
            };
            return stones;
        }

        int wallCopies(void) const override
        {
            return 4;
        }

        int initialHandSize(void) const override
        {
            return 13;
        }

        bool allowsChao(bool nextPlayer, bool sequenceRune, int variantCount) const override
        {
            return nextPlayer && sequenceRune && 0 < variantCount;
        }

        bool allowsPung(bool otherPlayer, int matchingRuneCount) const override
        {
            return otherPlayer && 1 < matchingRuneCount;
        }

        bool allowsExposedKong(bool otherPlayer, int matchingRuneCount) const override
        {
            return otherPlayer && 2 < matchingRuneCount;
        }

        bool allowsSelfKong(bool currentPlayer, bool hasDrawnRune,
                            bool upgradesPung, int matchingRuneCount) const override
        {
            return currentPlayer && hasDrawnRune && (upgradesPung || 3 == matchingRuneCount);
        }

        bool allowsWinStone(bool hasDrawnRune, bool hasDiscard,
                            bool discardFromAnotherPlayer) const override
        {
            return hasDrawnRune || (hasDiscard && discardFromAnotherPlayer);
        }

        bool isWinningStructure(int groupCount, int pairCount) const override
        {
            return 3 < groupCount && 0 < pairCount;
        }

        int callChoicePriority(RuneGameCall call) const override
        {
            switch(call)
            {
                case RuneGameCall::Game: return 4;
                case RuneGameCall::Kong: return 3;
                case RuneGameCall::Pung: return 2;
                case RuneGameCall::Chao: return 1;
                case RuneGameCall::None: break;
            }
            return 0;
        }

        int discardClaimPriority(RuneGameCall call) const override
        {
            switch(call)
            {
                case RuneGameCall::Game: return 3;
                case RuneGameCall::Kong:
                case RuneGameCall::Pung: return 2;
                case RuneGameCall::Chao: return 1;
                case RuneGameCall::None: break;
            }
            return 0;
        }

        RuneGameRoundAdvance advanceRound(int roundWindId, int partWindId) const override
        {
            RuneGameRoundAdvance result;
            result.roundWindId = roundWindId;
            result.partWindId = partWindId;

            if(Wind::North == roundWindId && Wind::North == partWindId)
            {
                result.complete = true;
            }
            else if(Wind::None == roundWindId && Wind::None == partWindId)
            {
                result.roundWindId = Wind::East;
                result.partWindId = Wind::East;
            }
            else if(Wind::North == partWindId)
            {
                result.roundWindId = Wind(static_cast<Wind::wind_t>(roundWindId)).next()();
                result.partWindId = Wind::East;
            }
            else
            {
                result.partWindId = Wind(static_cast<Wind::wind_t>(partWindId)).next()();
                result.rotatePlayerWinds = true;
            }

            return result;
        }

        int firstTurnWindId(void) const override
        {
            return Wind::East;
        }

        int spellPointAward(RuneGameSpellPointEvent event) const override
        {
            switch(event)
            {
                case RuneGameSpellPointEvent::Discard: return 10;
                case RuneGameSpellPointEvent::Chao: return 20;
                case RuneGameSpellPointEvent::Pung: return 30;
                case RuneGameSpellPointEvent::Kong: return 40;
                case RuneGameSpellPointEvent::Win: return 50;
            }
            return 0;
        }

        int baseWinPoints(void) const override
        {
            return 20;
        }

        int scoreMultiplier(int doubles) const override
        {
            if(doubles <= 0) return 1;
            return 1 << std::min(doubles, 5);
        }

        int maximumWinScore(void) const override
        {
            return 500;
        }
    };

    class QuickRuneGameRuleset final : public ClassicRuneGameRuleset
    {
    public:
        const std::string & id(void) const override
        {
            static const std::string value(QuickRuneGameRulesetId);
            return value;
        }

        int version(void) const override
        {
            return QuickRuneGameRulesetVersion;
        }

        RuneGameRoundAdvance advanceRound(int roundWindId, int partWindId) const override
        {
            // Quick keeps every Classic rule and all four hands, but ends after
            // the East round. This makes it useful for short local matches and
            // as an end-to-end proof that the versioned ruleset seam is real.
            if(Wind::East == roundWindId && Wind::North == partWindId)
            {
                RuneGameRoundAdvance result;
                result.roundWindId = roundWindId;
                result.partWindId = partWindId;
                result.complete = true;
                return result;
            }
            return ClassicRuneGameRuleset::advanceRound(roundWindId, partWindId);
        }
    };
}

const RuneGameRuleset & classicRuneGameRuleset(void)
{
    static const ClassicRuneGameRuleset ruleset;
    return ruleset;
}

const RuneGameRuleset & quickRuneGameRuleset(void)
{
    static const QuickRuneGameRuleset ruleset;
    return ruleset;
}

namespace
{
    const RuneGameRuleset*& selectedRuneGameRuleset(void)
    {
        static const RuneGameRuleset* selected = &classicRuneGameRuleset();
        return selected;
    }

    std::string rulesetLabel(const std::string & id, int version)
    {
        return id + "@" + std::to_string(version);
    }
}

const RuneGameRuleset & activeRuneGameRuleset(void)
{
    return *selectedRuneGameRuleset();
}

const RuneGameRuleset* findRuneGameRuleset(const std::string & id, int version)
{
    const RuneGameRuleset & classic = classicRuneGameRuleset();
    if(id == classic.id() && version == classic.version()) return &classic;

    const RuneGameRuleset & quick = quickRuneGameRuleset();
    if(id == quick.id() && version == quick.version()) return &quick;

    return nullptr;
}

RuneGameRulesetIdentity runeGameRulesetIdentity(const RuneGameRuleset & ruleset)
{
    return { ruleset.id(), ruleset.version() };
}

SWE::JsonObject runeGameRulesetIdentityJson(const RuneGameRuleset & ruleset)
{
    SWE::JsonObject result;
    result.addString("id", ruleset.id());
    result.addInteger("version", ruleset.version());
    return result;
}

bool resolveRuneGameRulesetIdentity(const SWE::JsonObject & container,
                                    RuneGameRulesetIdentity & identity,
                                    bool allowLegacyClassic,
                                    std::string* error)
{
    identity = RuneGameRulesetIdentity();
    if(!container.hasKey(RuneGameRulesetIdentityKey))
    {
        if(!allowLegacyClassic)
        {
            if(error) *error = "Rune Game ruleset metadata is missing";
            return false;
        }
        identity = runeGameRulesetIdentity(classicRuneGameRuleset());
        if(error) error->clear();
        return true;
    }

    const SWE::JsonObject* encoded = container.getObject(RuneGameRulesetIdentityKey);
    if(!encoded || !encoded->isString("id") || !encoded->isInteger("version"))
    {
        if(error) *error = "Rune Game ruleset metadata is invalid";
        return false;
    }

    identity.id = encoded->getString("id");
    identity.version = encoded->getInteger("version");
    if(!identity.isValid())
    {
        if(error) *error = "Rune Game ruleset metadata is invalid";
        return false;
    }
    if(!findRuneGameRuleset(identity.id, identity.version))
    {
        if(error) *error = "Rune Game ruleset is unavailable or incompatible: " +
            rulesetLabel(identity.id, identity.version);
        return false;
    }

    if(error) error->clear();
    return true;
}

bool selectActiveRuneGameRuleset(const std::string & id, int version, std::string* error)
{
    const RuneGameRuleset* selected = findRuneGameRuleset(id, version);
    if(!selected)
    {
        if(error) *error = "Rune Game ruleset is unavailable or incompatible: " +
            rulesetLabel(id, version);
        return false;
    }
    selectedRuneGameRuleset() = selected;
    if(error) error->clear();
    return true;
}

bool sameRuneGameRuleset(const RuneGameRulesetIdentity & first,
                         const RuneGameRulesetIdentity & second)
{
    return first.id == second.id && first.version == second.version;
}
