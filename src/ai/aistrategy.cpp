#include <algorithm>
#include <map>
#include <set>
#include <sstream>

#include "aistrategy.h"

namespace
{
    int visibleRuleStoneCount(const WinRule & rule, const Stone & stone)
    {
        if(rule.isHidden()) return 0;
        if(rule.isChao())
            return stone == rule.stone() || stone == rule.stone().next() ||
                   stone == rule.stone().next().next() ? 1 : 0;
        if(rule.isPung() || rule.isKong())
            return stone == rule.stone() ? rule.count() : 0;
        return 0;
    }

    int uncastedStoneCount(const GameStones & hand, const Stone & stone)
    {
        return static_cast<int>(std::count_if(hand.begin(), hand.end(), [&](const Stone & value)
        {
            return value == stone && !GameStone(value).isCasted();
        }));
    }

    int goalId(const AI::RuneGoal & goal)
    {
        return goal.type == AI::StrategicGoalType::Summon ? goal.creature() : goal.spell();
    }

    int adjacentVisibleThreat(const AI::AIObservation & observation, const Land & land)
    {
        int threat = 0;
        const std::vector<Land> & borders = GameData::landInfo(land).borders;
        for(const LocalPlayer & visible : observation.state().players)
        {
            if(GameData::allied(visible.clan, observation.player().clan)) continue;
            for(const Land & border : borders)
            {
                const BattleParty* party = visible.army.findPartyConst(border);
                if(!party) continue;
                const BaseStat stat = party->toBaseStatSummary();
                threat += stat.attack * 2 + stat.ranger * 2 + stat.defense + stat.loyalty;
            }
        }
        return threat;
    }

    int resultingPartySynergy(const BattleParty* party, const CreatureInfo & info)
    {
        if(!party) return 0;

        const BaseStat current = party->toBaseStatSummary();
        int synergy = party->count() * 30;
        if(0 < info.stat.ranger && current.ranger == 0) synergy += 70;
        if(info.specials.check(Speciality::SeeInvisible) &&
           party->toBattleCreatures(Specials() << Speciality::SeeInvisible, true).empty())
            synergy += 55;

        const int currentMove = party->movePoint();
        if(0 < currentMove)
        {
            if(info.stat.move < currentMove) synergy -= (currentMove - info.stat.move) * 45;
            else synergy += 20;
        }

        if(info.specials.check(Speciality::Merge))
        {
            const BattleCreatures members = party->toBattleCreatures();
            const int matching = static_cast<int>(std::count_if(
                members.begin(), members.end(), [&](const BattleCreature* creature)
                {
                    return creature && creature->id() == info.id.id() &&
                           creature->haveSpeciality(Speciality::Merge);
                }));
            synergy += matching * 220;
        }
        return synergy;
    }

    int combatStrength(const BaseStat & stat)
    {
        return stat.attack + stat.ranger * 2 + stat.defense + stat.loyalty * 2;
    }

    int visibleDefendingStrength(const AI::AIObservation & observation, const Land & land,
                                 bool* defended)
    {
        int strength = combatStrength(GameData::landInfo(land).stat);
        bool hasParty = false;
        const Clan owner = GameData::landInfo(land).clan;

        for(const LocalPlayer & visible : observation.state().players)
        {
            if(visible.clan != owner) continue;
            const BattleParty* party = visible.army.findPartyConst(land);
            if(!party) continue;
            strength += combatStrength(party->toBaseStatSummary());
            hasParty = true;
        }

        if(defended) *defended = hasParty;
        return strength;
    }

    int publicFriendlyBorders(const Land & land, const Clan & clan)
    {
        return static_cast<int>(std::count_if(
            GameData::landInfo(land).borders.begin(), GameData::landInfo(land).borders.end(),
            [&](const Land & border)
            {
                return !border.isTowerWinds() &&
                       GameData::allied(GameData::landInfo(border).clan, clan);
            }));
    }

    int publicFutureFrontiers(const Land & land, const Clan & clan)
    {
        return static_cast<int>(std::count_if(
            GameData::landInfo(land).borders.begin(), GameData::landInfo(land).borders.end(),
            [&](const Land & border)
            {
                if(border.isTowerWinds()) return false;
                const Clan owner = GameData::landInfo(border).clan;
                return owner.isValid() && !GameData::allied(owner, clan);
            }));
    }

    AI::AdventureFollowUp chooseVisibleAdventureFollowUp(
        const AI::AIObservation & observation, const Land & origin, int addedStrength,
        const AI::BehaviorRules & rules)
    {
        AI::AdventureFollowUp best;
        if(!origin.isValid()) return best;

        const LocalPlayer & player = observation.player();
        const BattleParty* party = player.army.findPartyConst(origin);
        const int attackStrength = (party ? combatStrength(party->toBaseStatSummary()) : 0) +
                                   addedStrength;
        if(attackStrength <= 0) return best;

        for(auto landId : lands_all)
        {
            const Land land(landId);
            if(land.isTowerWinds()) continue;

            const LandInfo & info = GameData::landInfo(land);
            if(!info.clan.isValid() || GameData::allied(info.clan, player.clan)) continue;

            const Lands path = Lands::pathfind(origin, land);
            if(path.empty()) continue;

            bool defended = false;
            const int defense = visibleDefendingStrength(observation, land, &defended);
            const int distance = static_cast<int>(path.size());
            const bool winnable = defense <= attackStrength;
            const int score = info.stat.point * rules.targetValueWeight / 100 -
                              distance * rules.targetDistancePenalty -
                              defense * rules.targetDefensePenalty +
                              (defended ? rules.targetDefendedBonus : rules.targetEmptyBonus) +
                              (winnable ? rules.targetWinnableBonus : 0) +
                              (info.stat.power ? rules.targetPowerBonus : 0) +
                              (0 < publicFriendlyBorders(land, player.clan) ?
                                  rules.targetThreatBonus : 0) +
                              publicFutureFrontiers(land, player.clan) *
                                  rules.targetFrontierWeight;

            AI::AdventureFollowUp candidate;
            candidate.origin = origin;
            candidate.target = land;
            candidate.distance = distance;
            candidate.visibleDefense = defense;
            candidate.attackStrength = attackStrength;
            candidate.score = score;

            if(!best.isValid() || candidate.score > best.score ||
               (candidate.score == best.score && candidate.target() < best.target()))
                best = candidate;
        }
        return best;
    }

    AI::AdventureFollowUp chooseVisibleAdventureFollowUp(
        const AI::AIObservation & observation, const AI::BehaviorRules & rules)
    {
        AI::AdventureFollowUp best;
        for(const BattleParty & party : observation.player().army)
        {
            const AI::AdventureFollowUp candidate = chooseVisibleAdventureFollowUp(
                observation, party.land(), 0, rules);
            if(!best.isValid() || (candidate.isValid() &&
               (candidate.score > best.score ||
                (candidate.score == best.score && candidate.origin() < best.origin()))))
                best = candidate;
        }
        return best;
    }

    int strategicGoalBonus(const AI::StrategicIntent & intent, AI::StrategicGoalType type,
                           int id)
    {
        for(std::size_t index = 0; index < intent.runeGoals.size(); ++index)
        {
            const AI::RuneGoal & goal = intent.runeGoals[index];
            if(goal.type != type || goalId(goal) != id) continue;
            const int rank = static_cast<int>(intent.runeGoals.size() - index);
            return std::clamp(std::max(0, goal.score) / 25 + rank, 0, 24);
        }
        return 0;
    }

    int boundedFollowUpScore(const AI::AdventureFollowUp & followUp)
    {
        return followUp.isValid() ? std::clamp(followUp.score / 25, -24, 24) : 0;
    }

    bool betterTurnBranch(const AI::TurnBranch & left, const AI::TurnBranch & right)
    {
        if(left.score != right.score) return left.score > right.score;
        if(left.action != right.action) return left.action == AI::StrategicAction::Spell;
        if(left.action == AI::StrategicAction::Spell)
        {
            if(left.spell.spell() != right.spell.spell())
                return left.spell.spell() < right.spell.spell();
            if(left.spell.unit != right.spell.unit) return left.spell.unit < right.spell.unit;
            if(left.spell.land() != right.spell.land()) return left.spell.land() < right.spell.land();
            return left.spell.target() < right.spell.target();
        }
        if(left.summon.score != right.summon.score)
            return left.summon.score > right.summon.score;
        if(left.summon.creature() != right.summon.creature())
            return left.summon.creature() < right.summon.creature();
        return left.summon.destination() < right.summon.destination();
    }

    AI::RuneGoal buildRuneGoal(const AI::AIObservation & observation,
                               AI::StrategicGoalType type, const Creature & creature,
                               const Spell & spell, const std::string & name,
                               const Stones & required, int manaRequired,
                               int goalValue, int drawHorizon)
    {
        AI::RuneGoal goal;
        goal.type = type;
        goal.creature = creature;
        goal.spell = spell;
        goal.name = name;
        goal.required = required;
        goal.manaRequired = manaRequired;
        goal.manaGap = std::max(0, manaRequired - observation.player().points);
        goal.goalValue = goalValue;

        std::map<int, int> used;
        for(const Stone & stone : required)
        {
            if(used[stone()] < uncastedStoneCount(observation.player().stones, stone))
                ++goal.matchedRunes;
            else
                goal.missing.push_back(stone);
            ++used[stone()];
        }

        std::map<int, int> missingCopies;
        for(const Stone & stone : goal.missing) ++missingCopies[stone()];

        int probability = goal.missing.empty() ? 100 : 100;
        const int unseenPool = std::max(1, observation.state().stoneLastCount);
        for(const auto & entry : missingCopies)
        {
            const Stone stone(static_cast<Stone::stone_t>(entry.first));
            const int unseenCopies = std::max(0, 4 - observation.visibleStoneCount(stone));
            goal.visibleRemaining += unseenCopies;

            for(int copy = 0; copy < entry.second; ++copy)
            {
                const int available = std::max(0, unseenCopies - copy);
                const int drawChance = std::clamp(available * drawHorizon * 100 / unseenPool, 0, 100);
                probability = probability * drawChance / 100;
            }
        }
        goal.completionProbability = goal.missing.empty() ? 100 : probability;

        goal.runeGain = goal.matchedRunes * 65 - static_cast<int>(goal.missing.size()) * 35;
        goal.availabilityValue = goal.completionProbability / 2;
        goal.score = goal.goalValue + goal.runeGain + goal.availabilityValue - goal.manaGap / 5;
        return goal;
    }
}

AI::AIObservation::AIObservation(const LocalData & value) : local(value)
{
}

int AI::AIObservation::visibleStoneCount(const Stone & stone) const
{
    int result = static_cast<int>(std::count(local.trashSet.begin(), local.trashSet.end(), stone));
    for(const LocalPlayer & visible : local.players)
    {
        result += static_cast<int>(std::count(visible.stones.begin(), visible.stones.end(), stone));
        if(visible.newStone == stone) ++result;
        for(const WinRule & rule : visible.rules)
            result += visibleRuleStoneCount(rule, stone);
    }
    return result;
}

bool AI::AIObservation::visibleCreature(const Creature & creature) const
{
    return std::any_of(local.players.begin(), local.players.end(), [&](const LocalPlayer & visible)
    {
        return visible.army.findCreature(creature);
    });
}

AI::RuneGoal::RuneGoal()
    : type(StrategicGoalType::Summon), matchedRunes(0), visibleRemaining(0),
      completionProbability(0), manaRequired(0), manaGap(0), goalValue(0),
      runeGain(0), availabilityValue(0), score(0)
{
}

bool AI::RuneGoal::requires(const Stone & stone) const
{
    return required.end() != std::find(required.begin(), required.end(), stone);
}

std::string AI::RuneGoal::trace(void) const
{
    std::ostringstream os;
    os << "goal=" << name << ", kind=" <<
        (type == StrategicGoalType::Summon ? "summon" : "spell") <<
        ", score=" << score << ", goal_value=" << goalValue <<
        ", rune_gain=" << runeGain << ", availability=" << availabilityValue <<
        ", progress=" << matchedRunes << "/" << required.size() <<
        ", completion=" << completionProbability << "%" <<
        ", visible_remaining=" << visibleRemaining <<
        ", mana_gap=" << manaGap;
    return os.str();
}

AI::StrategicIntent::StrategicIntent()
    : profile(BehaviorProfile::Balanced), difficulty(Difficulty::Normal)
{
}

AI::SummonCandidate::SummonCandidate()
    : staticValue(0), partySynergy(0), frontierValue(0), score(0)
{
}

AI::AdventureFollowUp::AdventureFollowUp()
    : distance(0), visibleDefense(0), attackStrength(0), score(0)
{
}

std::string AI::AdventureFollowUp::trace(void) const
{
    std::ostringstream os;
    os << "adventure=" << (target.isValid() ? target.toString() : "none")
       << ", origin=" << (origin.isValid() ? origin.toString() : "none")
       << ", distance=" << distance << ", visible_defense=" << visibleDefense
       << ", attack=" << attackStrength << ", score=" << score;
    return os.str();
}

AI::TurnBranch::TurnBranch()
    : action(StrategicAction::Summon), immediateScore(0), intentScore(0),
      followUpScore(0), score(0)
{
}

bool AI::TurnBranch::isValid(void) const
{
    return action == StrategicAction::Spell ? spell.isValid() : summon.isValid();
}

std::string AI::TurnBranch::trace(void) const
{
    std::ostringstream os;
    os << "action=" << (action == StrategicAction::Spell ? "spell" : "summon");
    if(action == StrategicAction::Spell)
    {
        os << ", spell=" << (spell.spell.isValid() ? GameData::spellInfo(spell.spell).name : "none")
           << ", target=" << spell.target() << ", land=" << spell.land.toString()
           << ", unit=" << spell.unit;
    }
    else
    {
        os << ", " << summon.trace();
    }
    os << ", total=" << score << ", immediate=" << immediateScore
       << ", intent=" << intentScore << ", follow_up=" << followUpScore
       << ", " << adventure.trace();
    return os.str();
}

const AI::TurnBranch* AI::TurnPlan::selected(void) const
{
    return branches.empty() ? nullptr : &branches.front();
}

std::string AI::TurnPlan::trace(void) const
{
    std::ostringstream os;
    os << intent.trace();
    for(std::size_t index = 0; index < branches.size(); ++index)
        os << "; branch[" << index << "] " << branches[index].trace();
    return os.str();
}

std::string AI::SummonCandidate::trace(void) const
{
    std::ostringstream os;
    os << "summon=" << (creature.isValid() ? GameData::creatureInfo(creature).name : "none")
       << ", destination=" << destination.toString() << ", score=" << score
       << ", static=" << staticValue << ", party_synergy=" << partySynergy
       << ", frontier=" << frontierValue;
    return os.str();
}

int AI::StrategicIntent::runeValue(const Stone & stone) const
{
    int value = 0;
    for(std::size_t index = 0; index < runeGoals.size(); ++index)
    {
        const RuneGoal & goal = runeGoals[index];
        if(!goal.requires(stone)) continue;

        const int rankWeight = static_cast<int>(runeGoals.size() - index);
        const int goalWeight = std::max(8, std::max(0, goal.score) /
                                           std::max(1, static_cast<int>(goal.required.size())));
        value += goalWeight * rankWeight / std::max(1, static_cast<int>(runeGoals.size()));
    }
    return value * difficultyRules(difficulty).runeValuePercent / 100;
}

std::string AI::StrategicIntent::trace(void) const
{
    std::ostringstream os;
    os << "profile=" << behaviorProfileName(profile)
       << ", difficulty=" << difficultyName(difficulty);
    for(const RuneGoal & goal : runeGoals) os << "; " << goal.trace();
    return os.str();
}

AI::AIObservation AI::observePlayer(const Avatar & avatar)
{
    return AIObservation(GameData::toLocalData(avatar));
}

AI::StrategicIntent AI::chooseStrategicIntent(const AIObservation & observation,
                                               BehaviorProfile profile, Difficulty difficulty)
{
    StrategicIntent intent;
    intent.profile = profile;
    intent.difficulty = difficulty;

    const LocalPlayer & player = observation.player();
    const AvatarInfo & avatar = GameData::avatarInfo(player.avatar);
    const DifficultyRules & budget = difficultyRules(difficulty);

    if(!player.army.isMaximumSummoning())
    {
        for(const Creature & creature : avatar.creatures)
        {
            const CreatureInfo & info = GameData::creatureInfo(creature);
            if(info.stones.empty() || (info.unique && observation.visibleCreature(creature))) continue;

            intent.runeGoals.push_back(buildRuneGoal(
                observation, StrategicGoalType::Summon, creature, Spell(), info.name,
                info.stones, info.cost, creatureSummonScore(creature, profile) / 8,
                budget.strategicDrawHorizon));
        }
    }

    std::set<int> knownSpells;
    auto addSpellGoal = [&](const Spell & spell)
    {
        if(!spell.isValid() || !knownSpells.insert(spell()).second) return;
        const SpellInfo & info = GameData::spellInfo(spell);
        if(info.stones.empty()) return;

        const int spellValue = 45 + info.cost / 2 +
                               static_cast<int>(info.stones.size()) * 12;
        intent.runeGoals.push_back(buildRuneGoal(
            observation, StrategicGoalType::Spell, Creature(), spell, info.name,
            info.stones, info.cost, spellValue, budget.strategicDrawHorizon));
    };
    for(const Spell & spell : avatar.spells) addSpellGoal(spell);
    for(const BattleCreature* creature : player.army.toBattleCreatures())
    {
        if(!creature) continue;
        for(const Speciality & speciality : GameData::creatureInfo(*creature).specials.toList())
            addSpellGoal(speciality.toSpell());
    }

    std::sort(intent.runeGoals.begin(), intent.runeGoals.end(), [](const RuneGoal & left,
                                                                  const RuneGoal & right)
    {
        if(left.score != right.score) return left.score > right.score;
        if(left.matchedRunes != right.matchedRunes) return left.matchedRunes > right.matchedRunes;
        if(left.type != right.type) return left.type == StrategicGoalType::Summon;
        return goalId(left) < goalId(right);
    });

    if(static_cast<int>(intent.runeGoals.size()) > budget.strategicGoalLimit)
        intent.runeGoals.resize(budget.strategicGoalLimit);
    return intent;
}

std::vector<AI::SummonCandidate> AI::rankSummonCandidates(const AIObservation & observation,
                                                          const Creatures & creatures,
                                                          BehaviorProfile profile)
{
    std::vector<SummonCandidate> result;
    const LocalPlayer & player = observation.player();
    Lands lands = Lands::thisClan(player.clan).powerOnly();

    for(const Creature & creature : creatures)
    {
        const CreatureInfo & info = GameData::creatureInfo(creature);
        if(info.unique && observation.visibleCreature(creature)) continue;

        for(const Land & land : lands)
        {
            const BattleParty* party = player.army.findPartyConst(land);
            if(party && !party->canJoin()) continue;

            SummonCandidate candidate;
            candidate.creature = creature;
            candidate.destination = land;
            candidate.staticValue = creatureSummonScore(creature, profile);
            candidate.partySynergy = resultingPartySynergy(party, info);

            const int threat = adjacentVisibleThreat(observation, land);
            candidate.frontierValue = GameData::landInfo(land).stat.point +
                std::min(180, threat * (info.stat.defense + info.stat.loyalty) / 20);
            candidate.score = candidate.staticValue + candidate.partySynergy +
                              candidate.frontierValue;
            result.push_back(candidate);
        }
    }

    std::sort(result.begin(), result.end(), [](const SummonCandidate & left,
                                               const SummonCandidate & right)
    {
        if(left.score != right.score) return left.score > right.score;
        if(left.creature() != right.creature()) return left.creature() < right.creature();
        return left.destination() < right.destination();
    });
    return result;
}

AI::TurnPlan AI::chooseStrategicTurnPlan(const AIObservation & observation,
                                         const Creatures & summons, const Spells & casts,
                                         BehaviorProfile profile, Difficulty difficulty)
{
    TurnPlan plan;
    plan.intent = chooseStrategicIntent(observation, profile, difficulty);
    const BehaviorRules & rules = behaviorRules(profile);

    std::set<int> plannedCreatures;
    const std::vector<SummonCandidate> summonCandidates =
        observation.player().army.isMaximumSummoning() ? std::vector<SummonCandidate>() :
        rankSummonCandidates(observation, summons, profile);
    for(const SummonCandidate & candidate : summonCandidates)
    {
        if(!plannedCreatures.insert(candidate.creature()).second) continue;

        TurnBranch branch;
        branch.action = StrategicAction::Summon;
        branch.summon = candidate;
        branch.adventure = chooseVisibleAdventureFollowUp(
            observation, candidate.destination,
            combatStrength(GameData::creatureInfo(candidate.creature).stat), rules);
        branch.immediateScore = rules.summonOverrideScore - 1;
        branch.intentScore = strategicGoalBonus(plan.intent, StrategicGoalType::Summon,
                                                candidate.creature());
        branch.followUpScore = boundedFollowUpScore(branch.adventure);
        branch.score = branch.immediateScore + branch.intentScore + branch.followUpScore;
        plan.branches.push_back(branch);
    }

    const Spells futureSpells = knownSpellCasts(observation.player());
    std::set<int> plannedSpells;
    for(const Spell & spell : casts)
    {
        if(!plannedSpells.insert(spell()).second) continue;

        Spells oneSpell;
        oneSpell.push_back(spell);
        const SpellCastPlan spellPlan = chooseSpellCast(
            observation.state(), oneSpell, profile, difficulty, futureSpells);
        if(!spellPlan.isValid()) continue;

        TurnBranch branch;
        branch.action = StrategicAction::Spell;
        branch.spell = spellPlan;
        branch.adventure = chooseVisibleAdventureFollowUp(observation, rules);
        branch.immediateScore = spellPlan.score;
        branch.intentScore = strategicGoalBonus(plan.intent, StrategicGoalType::Spell, spell());
        branch.followUpScore = boundedFollowUpScore(branch.adventure);
        if(branch.adventure.isValid() && spellPlan.land == branch.adventure.target)
            branch.followUpScore = std::min(24, branch.followUpScore + 8);
        branch.score = branch.immediateScore + branch.intentScore + branch.followUpScore;
        plan.branches.push_back(branch);
    }

    std::sort(plan.branches.begin(), plan.branches.end(), betterTurnBranch);
    const int branchLimit = difficultyRules(difficulty).strategicBranchLimit;
    if(static_cast<int>(plan.branches.size()) > branchLimit)
        plan.branches.resize(branchLimit);

    const LocalPlayer & player = observation.player();
    const int decisionKey = player.avatar() * 97 + player.points * 7 +
        static_cast<int>(player.stones.size()) * 13 +
        static_cast<int>(player.army.size()) * 17;
    if(1 < plan.branches.size() && preferNearBestChoice(
           difficulty, plan.branches[0].score, plan.branches[1].score, decisionKey))
        std::swap(plan.branches[0], plan.branches[1]);
    return plan;
}
