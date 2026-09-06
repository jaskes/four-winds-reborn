#include "matchauthority.h"

#include <algorithm>
#include <memory>

#include "aiprofile.h"
#include "aiturn.h"
#include "battlecommands.h"
#include "battlesession.h"
#include "clientview.h"
#include "gameplayrng.h"
#include "matchtopology.h"
#include "replay.h"
#include "runegameruleset.h"

namespace GameData
{
    extern CroupierSet croupier;
    extern Wind currentWind;
    extern Wind roundWind;
    extern Stone dropStone;
    extern bool skipNewTurn;
    extern bool skipRepeatSay;
}

namespace
{
    bool readyPhase(int phase)
    {
        return phase == Menu::ShowPlayers || phase == Menu::MahjongSummaryPart ||
               phase == Menu::BattleSummaryPart;
    }

    bool announcement(int type)
    {
        return type == Action::ClientSayGame || type == Action::ClientSayPung ||
               type == Action::ClientSayChao || type == Action::ClientSayKong;
    }

    RuneGameCall claimType(int type)
    {
        switch(type)
        {
            case Action::ClientSayGame:
            case Action::ClientButtonGame: return RuneGameCall::Game;
            case Action::ClientSayKong:
            case Action::ClientButtonKong1: return RuneGameCall::Kong;
            case Action::ClientSayPung:
            case Action::ClientButtonPung: return RuneGameCall::Pung;
            case Action::ClientSayChao:
            case Action::ClientChaoVariant: return RuneGameCall::Chao;
            default: return RuneGameCall::None;
        }
    }

    bool legalClaim(const LocalPlayer& player, const ClientMessage& command)
    {
        const RuneGameRuleset& rules = activeRuneGameRuleset();
        switch(claimType(command.type()))
        {
            case RuneGameCall::Game:
                return player.isWinMahjong(GameData::currentWind, GameData::roundWind,
                                           GameData::dropStone, nullptr, rules);
            case RuneGameCall::Kong:
                return (command.type() != Action::ClientSayKong ||
                        command.getInteger("kongType") == 1) &&
                    player.isMahjongKong1(GameData::currentWind, GameData::dropStone, rules);
            case RuneGameCall::Pung:
                return player.isMahjongPung(GameData::currentWind, GameData::dropStone, rules);
            case RuneGameCall::Chao:
            {
                if(!player.isMahjongChao(GameData::currentWind, GameData::dropStone, rules))
                    return false;
                if(command.type() == Action::ClientSayChao) return true;
                const int variant = command.getInteger("variant", -1);
                return variant >= 0 && static_cast<std::size_t>(variant) <
                    player.stones.findChaoVariants(GameData::dropStone).size();
            }
            default: return command.type() == Action::ClientButtonPass;
        }
    }

    ActionList committedPresentation(const ActionList& actions)
    {
        ActionList result;
        for(const ActionMessage& action : actions)
        {
            // Local play announces AI intentions while the human considers a
            // discard. Network claims remain pending until all humans reply;
            // announce only the selected claim, together with its resolution.
            if(action.getBoolean("sayOnly")) continue;
            const Wind wind(action.getString("currentWind"));
            switch(action.type())
            {
                case Action::MahjongGame: result.push_back(MahjongSayGame(wind)); break;
                case Action::MahjongKong1: result.push_back(MahjongSayKong(wind)); break;
                case Action::MahjongPung: result.push_back(MahjongSayPung(wind)); break;
                case Action::MahjongChao: result.push_back(MahjongSayChao(wind)); break;
                // Kong2 has its own voice and guardian animation handler.
                default: break;
            }
            result.push_back(action);
        }
        return result;
    }
}

namespace Multiplayer
{
    bool MatchAuthority::reject(ActionRejection* rejection, ActionRejectReason reason) const
    {
        if(rejection)
        {
            *rejection = ActionRejection();
            rejection->reason = reason;
        }
        return false;
    }

    const LocalPlayer* MatchAuthority::humanPlayer(const Avatar& avatar) const
    {
        const LocalPlayer* player = GameData::players().playerOfAvatar(avatar);
        return player && !player->isAI() ? player : nullptr;
    }

    int MatchAuthority::phase() const
    {
        return running ? GameData::loadedGamePart() : Menu::MainMenu;
    }

    bool MatchAuthority::start(const MatchConfig& config, std::string* error)
    {
        const auto fail = [&](const char* reason) {
            if(error) *error = reason;
            return false;
        };
        if(running) return fail("a multiplayer match is already active");
        const MatchTopology* topology = findMatchTopology(config.topologyId, config.topologyVersion);
        if(!topology || topology->controllerCount() != topology->seatCount())
            return fail("unsupported multiplayer topology");
        if(!findRuneGameRuleset(config.rulesetId, config.rulesetVersion))
            return fail("unsupported multiplayer ruleset");
        if(config.players.size() != static_cast<std::size_t>(topology->seatCount()))
            return fail("the lobby does not contain every player");

        const MatchTopologyIdentity previousTopology = matchTopologyIdentity(activeMatchTopology());
        const RuneGameRulesetIdentity previousRules = runeGameRulesetIdentity(activeRuneGameRuleset());
        selectActiveMatchTopology(config.topologyId, config.topologyVersion);
        selectActiveRuneGameRuleset(config.rulesetId, config.rulesetVersion);
        if(!GameData::initPersons(config.players))
        {
            selectActiveMatchTopology(previousTopology.id, previousTopology.version);
            selectActiveRuneGameRuleset(previousRules.id, previousRules.version);
            return fail("invalid player identities, clans or seats");
        }

        if(config.seed) GameplayRng::seed(config.seed);
        else GameplayRng::seedFromEntropy();
        roster = config.players;
        outgoing.clear();
        phaseReady.clear();
        claims.clear();
        claimResponders.clear();
        claimWindow = false;
        claimOpenedRevision = 0;
        running = true;
        stateRevision = 1;
        GameData::setGamePart(Menu::ShowPlayers);
        if(error) error->clear();
        return true;
    }

    void MatchAuthority::stop()
    {
        running = false;
        roster.clear();
        outgoing.clear();
        phaseReady.clear();
        claims.clear();
        claimResponders.clear();
        claimWindow = false;
        claimOpenedRevision = 0;
    }

    void MatchAuthority::broadcast(const ActionList& actions)
    {
        const ActionList presentation = committedPresentation(actions);
        for(const Person& person : roster)
        {
            if(person.isAI()) continue;
            ActionList visible = filterEvents(presentation, person.avatar);
            outgoing[person.avatar()].splice(outgoing[person.avatar()].end(), visible);
        }
    }

    void MatchAuthority::changed(const ActionList& actions)
    {
        ++stateRevision;
        if(readyPhase(phase())) phaseReady.clear();
        broadcast(actions);
    }

    bool MatchAuthority::hasReady(const Avatar& avatar) const
    {
        return phaseReady.count(avatar()) != 0;
    }

    bool MatchAuthority::allHumansReady() const
    {
        return std::all_of(roster.begin(), roster.end(), [&](const Person& player) {
            return player.isAI() || hasReady(player.avatar);
        });
    }

    bool MatchAuthority::advanceReadyPhase()
    {
        const int previous = phase();
        if(!readyPhase(previous) || !allHumansReady()) return false;
        ActionList actions;
        if(previous == Menu::MahjongSummaryPart)
        {
            Replay::recordSystemOperation("init_adventure", [] { return GameData::initAdventure(); });
            // Establish the undo snapshot before announcing that a human can
            // move. Otherwise a command arriving before the first timer tick
            // would become part of the supposedly pre-move undo snapshot.
            Replay::recordSystemOperation("adventure_tick", [&] {
                return GameData::adventure2Client(GameData::currentPerson().avatar, actions);
            });
        }
        else
        {
            const bool initialized = previous == Menu::ShowPlayers ? GameData::initMahjong() :
                Replay::recordSystemOperation("init_mahjong", [] { return GameData::initMahjong(); });
            if(initialized)
            {
                actions.push_back(MahjongBegin(GameData::currentWind, GameData::roundWind,
                                               GameData::toLocalData(roster.front().avatar).partWind == Wind(Wind::East)));
                actions.push_back(MahjongData(GameData::currentWind));
            }
            else
            {
                Replay::recordSystemOperation("game_summary", [] {
                    GameData::setGamePart(Menu::GameSummaryPart);
                    return true;
                });
            }
        }
        phaseReady.clear();
        claimWindow = false;
        claimOpenedRevision = 0;
        claims.clear();
        claimResponders.clear();
        changed(actions);
        return true;
    }

    bool MatchAuthority::ready(const Avatar& avatar, ActionRejection* rejection)
    {
        if(rejection) *rejection = ActionRejection();
        if(!running || !readyPhase(phase())) return reject(rejection, ActionRejectReason::WrongPhase);
        if(!humanPlayer(avatar)) return reject(rejection, ActionRejectReason::InvalidTarget);
        // Retrying the same phase-ready acknowledgement is harmless.
        if(hasReady(avatar)) return true;
        phaseReady.insert(avatar());
        if(!advanceReadyPhase()) ++stateRevision;
        return true;
    }

    void MatchAuthority::beginClaims(std::uint64_t openedRevision)
    {
        if(claimWindow || !GameData::dropStone.isValid()) return;
        claimWindow = true;
        claimOpenedRevision = openedRevision;
        claims.clear();
        claimResponders.clear();
        const RuneGameRuleset& rules = activeRuneGameRuleset();
        for(const LocalPlayer& player : GameData::players())
        {
            if(player.wind == GameData::currentWind) continue;
            claimResponders.insert(player.avatar());
            if(!GameData::usesAI(player)) continue;

            ClaimReply reply;
            reply.command = ClientButtonPass();
            if(player.isWinMahjong(GameData::currentWind, GameData::roundWind,
                                   GameData::dropStone, nullptr, rules))
            {
                reply.command = ClientButtonGame();
                reply.priority = rules.discardClaimPriority(RuneGameCall::Game);
            }
            else
            {
                const AI::StrategicIntent intent = AI::chooseStrategicIntent(
                    AI::observePlayer(player.avatar), AI::behaviorProfile(player), GameData::aiDifficulty());
                const AI::MahjongCallPlan plan = AI::chooseMahjongCall(
                    player, GameData::currentWind, GameData::dropStone, intent, rules);
                switch(plan.type)
                {
                    case AI::MahjongCallType::Kong: reply.command = ClientButtonKong1(); break;
                    case AI::MahjongCallType::Pung: reply.command = ClientButtonPung(); break;
                    case AI::MahjongCallType::Chao: reply.command = ClientChaoVariant(plan.variant); break;
                    default: break;
                }
                reply.priority = rules.discardClaimPriority(claimType(reply.command.getInteger("type")));
            }
            claims.emplace(player.avatar(), std::move(reply));
        }
    }

    bool MatchAuthority::awaitingClaim(const Avatar& avatar) const
    {
        return running && claimWindow && claimResponders.count(avatar()) && !claims.count(avatar());
    }

    bool MatchAuthority::acceptsClaimRevision(const Avatar& avatar, const ClientMessage& command,
                                             std::uint64_t expectedRevision) const
    {
        if(!running || phase() != Menu::MahjongPart || !GameData::dropStone.isValid() ||
           !awaitingClaim(avatar) || claimOpenedRevision == 0 ||
           expectedRevision < claimOpenedRevision || expectedRevision > stateRevision)
            return false;
        const LocalPlayer* player = humanPlayer(avatar);
        return player && (claimType(command.type()) != RuneGameCall::None ||
                          command.type() == Action::ClientButtonPass) && legalClaim(*player, command);
    }

    bool MatchAuthority::resolveClaims(ActionList& actions)
    {
        if(!claimWindow || claims.size() != claimResponders.size()) return false;
        const LocalPlayer* selected = nullptr;
        int priority = 0;
        // Stable wind ordering preserves the established tie break regardless
        // of packet arrival order or which player hosts the match.
        for(const Wind::wind_t wind : activeMatchTopology().winds())
        {
            const LocalPlayer* player = GameData::players().playerOfWind(wind);
            if(!player) continue;
            const auto found = claims.find(player->avatar());
            if(found != claims.end() && found->second.priority > priority)
            {
                selected = player;
                priority = found->second.priority;
            }
        }

        bool accepted = false;
        if(selected)
        {
            const std::unique_ptr<ClientMessage> command = Replay::clientMessageFromJson(
                claims.at(selected->avatar()).command);
            if(command) accepted = GameData::client2Mahjong(selected->avatar, *command, actions);
        }
        else
        {
            // All AI choices were evaluated with the same observer state and
            // deterministic strategy as the existing pass resolver. It cannot
            // introduce a new AI claim after every stored response is Pass.
            const auto human = std::find_if(GameData::players().begin(), GameData::players().end(),
                [](const LocalPlayer& player) { return !GameData::usesAI(player); });
            const Avatar actor = human == GameData::players().end() ?
                GameData::players().front().avatar : human->avatar;
            accepted = GameData::client2Mahjong(actor, ClientButtonPass(), actions);
        }
        if(accepted)
        {
            claims.clear();
            claimResponders.clear();
            claimWindow = false;
            claimOpenedRevision = 0;
        }
        return accepted;
    }

    bool MatchAuthority::submitClaim(const LocalPlayer& player, const ClientMessage& command,
                                     ActionRejection* rejection)
    {
        if(player.wind == GameData::currentWind)
            return reject(rejection, ActionRejectReason::WrongTurn);
        beginClaims(stateRevision);
        if(!awaitingClaim(player.avatar)) return reject(rejection, ActionRejectReason::StaleSelection);
        if(!legalClaim(player, command))
            return reject(rejection, player.isSilenced() ? ActionRejectReason::Silenced :
                          ActionRejectReason::IllegalMahjongCall);

        // Existing UI sends Say followed by the actual choice. A declaration
        // must not pre-empt another human's higher-priority final response.
        if(announcement(command.type())) return true;
        ClaimReply reply;
        reply.command = command;
        reply.priority = activeRuneGameRuleset().discardClaimPriority(claimType(command.type()));
        claims.emplace(player.avatar(), std::move(reply));
        ActionList actions;
        if(command.type() == Action::ClientButtonPass) actions.push_back(MahjongPass(player.wind));
        resolveClaims(actions);
        changed(actions);
        return true;
    }

    bool MatchAuthority::submit(const Avatar& avatar, const ClientMessage& command,
                                ActionRejection* rejection)
    {
        if(rejection) *rejection = ActionRejection();
        if(!running) return reject(rejection, ActionRejectReason::WrongPhase);
        const LocalPlayer* player = humanPlayer(avatar);
        if(!player) return reject(rejection, ActionRejectReason::InvalidTarget);
        if(command.getBoolean("force")) return reject(rejection, ActionRejectReason::InvalidTarget);
        if(command.type() == Action::ClientSummonCreature)
        {
            const Creature creature(command.getString("creature"));
            const Creatures& available = GameData::avatarInfo(player->avatar).creatures;
            if(!creature.isValid() || std::find(available.begin(), available.end(), creature) == available.end())
                return reject(rejection, ActionRejectReason::InvalidTarget);
        }
        if(command.type() == Action::ClientCastSpell && !Spell(command.getString("spell")).isValid())
            return reject(rejection, ActionRejectReason::InvalidTarget);

        ActionList actions;
        bool accepted = false;
        if(phase() == Menu::MahjongPart)
        {
            if(command.type() == Action::ClientReady)
            {
                ActionList resumed = resumeEvents(avatar);
                outgoing[avatar()].splice(outgoing[avatar()].end(), resumed);
                ++stateRevision;
                return true;
            }
            if(GameData::dropStone.isValid()) return submitClaim(*player, command, rejection);
            if(player->wind != GameData::currentWind)
                return reject(rejection, ActionRejectReason::WrongTurn);
            if(command.type() == Action::ClientButtonPass ||
               command.type() == Action::ClientSayChao || command.type() == Action::ClientChaoVariant ||
               command.type() == Action::ClientSayPung || command.type() == Action::ClientButtonPung ||
               command.type() == Action::ClientButtonKong1)
                return reject(rejection, ActionRejectReason::StaleSelection);
            if(GameData::croupier.hasLuckDraw() && command.type() != Action::ClientLuckChoice)
                return reject(rejection, ActionRejectReason::StaleSelection);
            if(!GameData::croupier.hasLuckDraw() && !player->newStone.isValid() && !GameData::skipNewTurn)
                return reject(rejection, ActionRejectReason::StaleSelection);
            accepted = GameData::client2Mahjong(avatar, command, actions, rejection);
            if(accepted && GameData::dropStone.isValid()) beginClaims(stateRevision + 1);
        }
        else if(phase() == Menu::AdventurePart)
        {
            accepted = GameData::client2Adventure(avatar, command, actions, rejection);
        }
        else return reject(rejection, ActionRejectReason::WrongPhase);

        if(accepted) changed(actions);
        return accepted;
    }

    bool MatchAuthority::tick()
    {
        if(!running) return false;
        if(readyPhase(phase())) return advanceReadyPhase();
        ActionList actions;
        if(phase() == Menu::MahjongPart)
        {
            if(GameData::dropStone.isValid())
            {
                beginClaims(stateRevision);
                if(!resolveClaims(actions)) return false;
            }
            else
            {
                const LocalPlayer& current = *GameData::players().playerOfAvatar(GameData::currentPerson().avatar);
                if(!GameData::usesAI(current) &&
                   (current.newStone.isValid() || GameData::skipNewTurn || GameData::croupier.hasLuckDraw()))
                    return false;
                Replay::recordSystemOperation("mahjong_tick", [&] {
                    return GameData::mahjong2Client(current.avatar, actions);
                });
                if(GameData::dropStone.isValid()) beginClaims(stateRevision + 1);
            }
        }
        else if(phase() == Menu::AdventurePart)
        {
            if(GameData::pendingBattle.isValid() && GameData::pendingBattle.session.awaitsChoice())
                return false;
            const Person& current = GameData::currentPerson();
            const LocalPlayer* currentPlayer = GameData::players().playerOfAvatar(current.avatar);
            if(!GameData::usesAI(current) && !currentPlayer->adventurePartDone() && GameData::skipRepeatSay)
                return false;
            const int previousPhase = phase();
            const Wind previousWind = GameData::currentWind;
            Replay::recordSystemOperation("adventure_tick", [&] {
                return GameData::adventure2Client(GameData::currentPerson().avatar, actions);
            });
            if(actions.empty() && phase() == Menu::AdventurePart && previousWind != GameData::currentWind)
                Replay::recordSystemOperation("adventure_tick", [&] {
                    return GameData::adventure2Client(GameData::currentPerson().avatar, actions);
                });
            if(previousPhase != phase() && phase() == Menu::BattleSummaryPart)
                actions.push_back(AdventureEnd(GameData::currentWind));
        }
        else return false;
        if(actions.empty()) return false;
        changed(actions);
        return true;
    }

    ActionList MatchAuthority::takeEvents(const Avatar& avatar)
    {
        ActionList result;
        const auto found = outgoing.find(avatar());
        if(found != outgoing.end()) result.splice(result.end(), found->second);
        return result;
    }

    ActionList MatchAuthority::resumeEvents(const Avatar& avatar) const
    {
        ActionList actions;
        if(!running || !humanPlayer(avatar)) return actions;
        if(phase() == Menu::MahjongPart)
        {
            const LocalPlayer& current = *GameData::players().playerOfAvatar(GameData::currentPerson().avatar);
            actions.push_back(MahjongBegin(GameData::currentWind, GameData::roundWind, false));
            actions.push_back(MahjongData(GameData::currentWind));
            if(GameData::dropStone.isValid())
                actions.push_back(MahjongDrop(GameData::currentWind, GameData::dropStone));
            else if(GameData::croupier.hasLuckDraw())
                actions.push_back(MahjongLuckChoice(GameData::currentWind, GameData::croupier.luckChoices()));
            else if(current.newStone.isValid() || GameData::skipNewTurn)
                actions.push_back(MahjongTurn(GameData::currentWind, current.newStone,
                    current.isMahjongKong2(GameData::currentWind, activeRuneGameRuleset()),
                    current.isWinMahjong(GameData::currentWind, GameData::roundWind,
                                         GameData::dropStone, nullptr, activeRuneGameRuleset())));
        }
        else if(phase() == Menu::AdventurePart)
        {
            actions.push_back(AdventureTurn(GameData::currentWind));
            GameData::emitPendingBattleChoice(actions);
        }
        return filterEvents(actions, avatar);
    }
}
