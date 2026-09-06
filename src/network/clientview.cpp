#include "clientview.h"

#include <algorithm>
#include <set>

#include "matchtopology.h"
#include "runegameruleset.h"

namespace
{
    bool fail(std::string* error, const char* message)
    {
        if(error) *error = message;
        return false;
    }

    bool sameJson(const JsonValue & first, const JsonValue & second)
    {
        if(first.getType() != second.getType()) return false;
        if(first.isObject())
        {
            const auto & a = static_cast<const JsonObject &>(first);
            const auto & b = static_cast<const JsonObject &>(second);
            if(a.size() != b.size()) return false;
            for(const auto & key : a.keys())
                if(!b.hasKey(key) || !sameJson(*a.getValue(key), *b.getValue(key))) return false;
            return true;
        }
        if(first.isArray())
        {
            const auto & a = static_cast<const JsonArray &>(first);
            const auto & b = static_cast<const JsonArray &>(second);
            if(a.size() != b.size()) return false;
            for(std::size_t i = 0; i < a.size(); ++i)
                if(!sameJson(*a.getValue(i), *b.getValue(i))) return false;
            return true;
        }
        return first.toString() == second.toString();
    }

    template<typename T> bool identifier(const std::string & value, bool none = true)
    {
        const T decoded(value);
        return decoded.toString() == value &&
            (decoded.isValid() || (none && value == T().toString()));
    }

    bool boundedTree(const JsonValue & value, unsigned depth = 0)
    {
        if(depth > 16) return false;
        if(value.isObject())
        {
            const auto & object = static_cast<const JsonObject &>(value);
            if(object.size() > 64) return false;
            for(const auto & key : object.keys())
            {
                if(key.size() > 64 || (!(key == "flags" && object.isInteger(key)) &&
                    !boundedTree(*object.getValue(key), depth + 1))) return false;
                if(object.isString(key))
                {
                    const auto text = object.getString(key);
                    if((key == "stone" || key == "dropStone" || key == "newStone" ||
                        key.compare(0, 6, "stone:") == 0) && !identifier<Stone>(text)) return false;
                    if((key == "wind" || key == "currentWind" || key == "roundWind" ||
                        key.compare(0, 5, "wind:") == 0) && !identifier<Wind>(text)) return false;
                    if((key == "avatar" || key == "recipient" || key == "attacker" ||
                        key == "defender" || key == "source") &&
                       (!identifier<Avatar>(text) || text == "random")) return false;
                    if((key == "clan" || key == "owner" || key == "previous" ||
                        key == "previousOwner") && !identifier<Clan>(text)) return false;
                    if((key == "land" || key == "position" || key == "territory") &&
                       !identifier<Land>(text)) return false;
                    if(key == "creature" && !identifier<Creature>(text)) return false;
                    if(key == "spell" && !identifier<Spell>(text)) return false;
                }
            }
            return true;
        }
        if(value.isArray())
        {
            const auto & array = static_cast<const JsonArray &>(value);
            if(array.size() > 4096) return false;
            for(std::size_t i = 0; i < array.size(); ++i)
                if(!boundedTree(*array.getValue(i), depth + 1)) return false;
            return true;
        }
        // Town targets encode their territory as landId << 16; these are
        // legitimate public battle identifiers larger than ordinary unit IDs.
        if(value.isInteger()) return -16777215 <= value.getInteger() && value.getInteger() <= 16777215;
        if(value.isString()) return value.getString().size() <= 4096;
        return value.isBoolean();
    }

    bool onlyKeys(const JsonObject & object, std::initializer_list<const char*> keys)
    {
        for(const auto & key : object.keys())
            if(std::none_of(keys.begin(), keys.end(), [&](const char* allowed) { return key == allowed; }))
                return false;
        return true;
    }

    bool summaryPhase(int phase)
    {
        return phase == Menu::MahjongSummaryPart || phase == Menu::AdventurePart ||
            phase == Menu::BattleSummaryPart || phase == Menu::GameSummaryPart;
    }

    bool validParty(const BattleParty & party, bool empty = false)
    {
        const JsonObject serialized = party.toJsonObject();
        if(serialized.getArray("creatures")->size() != 3 ||
           (!party.land().isValid() && !(empty && party.isEmpty()))) return false;
        for(const auto* creature : party.toBattleCreatures())
            if(creature && (creature->battleUnit() <= 0 || !creature->clan().isValid())) return false;
        return true;
    }

    bool validPlayer(const JsonObject & wire, LocalPlayer & player)
    {
        if(!wire.isArray("stones") || wire.getArray("stones")->size() > 18 ||
           !wire.isArray("rules") || wire.getArray("rules")->size() > 4 ||
           !wire.isArray("army") || wire.getArray("army")->size() > 45 ||
           !wire.isArray("affected") || wire.getArray("affected")->size() > 64 ||
           !wire.isObject("landClaims") || !wire.isObject("stone:new") ||
           !wire.isInteger("flags") || wire.getInteger("flags") < 0 || wire.getInteger("flags") > 7 ||
           !wire.isInteger("points") || wire.getInteger("points") < 0) return false;
        player = LocalPlayer::fromJsonObject(wire);
        if(!sameJson(wire, player.toJsonObject())) return false;
        for(const auto & stone : player.stones) if(!stone.isValid()) return false;
        for(const auto & rule : player.rules)
            if(!rule.stone().isValid() || rule.rule() < WinRule::Chao || rule.rule() > WinRule::Game) return false;
        std::set<int> units;
        for(const auto & party : player.army)
        {
            if(!validParty(party)) return false;
            for(const auto* creature : party.toBattleCreatures())
                if(creature && !units.insert(creature->battleUnit()).second) return false;
        }
        return true;
    }

    bool validLegend(const JsonObject & wire)
    {
        const auto legend = BattleLegend::fromJsonObject(wire);
        return legend.attacker.isValid() && legend.defender.isValid() &&
            legend.town.land().isValid() && validParty(legend.attackers) &&
            validParty(legend.defenders, true) && sameJson(wire, legend.toJsonObject());
    }

    bool integerArray(const JsonArray* array, std::size_t maximum = 256)
    {
        if(!array || array->size() > maximum) return false;
        for(std::size_t i = 0; i < array->size(); ++i)
            if(!array->getValue(i)->isInteger()) return false;
        return true;
    }

    bool validStrikes(const JsonArray* array)
    {
        if(!array || array->size() > 4096) return false;
        for(std::size_t i = 0; i < array->size(); ++i)
        {
            const auto* strike = array->getObject(i);
            if(!strike || !sameJson(*strike, BattleStrike::fromJsonObject(*strike).toJsonObject())) return false;
        }
        return true;
    }
}

JsonObject Multiplayer::buildClientView(const Avatar & recipient)
{
    if(!GameData::players().playerOfAvatar(recipient)) return JsonObject();
    LocalData local = GameData::toLocalData(recipient);
    const int phase = GameData::loadedGamePart();
    JsonObject result;
    result.addInteger("viewVersion", 1);
    result.addString("recipient", recipient.toString());
    result.addInteger("phase", phase);
    result.addObject(MatchTopologyIdentityKey, matchTopologyIdentityJson(activeMatchTopology()));
    result.addObject(RuneGameRulesetIdentityKey, runeGameRulesetIdentityJson(activeRuneGameRuleset()));
    result.addString("wind:round", local.roundWind.toString());
    result.addString("wind:part", local.partWind.toString());
    result.addString("wind:current", local.currentWind.toString());
    result.addString("stone:drop", local.dropStone.toString());
    result.addInteger("lastcount", local.stoneLastCount);
    result.addArray("trash", local.trashSet.toJsonArray());
    JsonArray roster;
    // Send canonical wind order, independent from the recipient's compass.
    for(const auto & actual : GameData::players())
    {
        auto found = std::find_if(local.players.begin(), local.players.end(), [&](const auto & player) {
            return player.avatar == actual.avatar;
        });
        if(found == local.players.end()) continue;
        LocalPlayer visible = *found;
        // At the completed match, unit categories and standings are public.
        if(phase == Menu::GameSummaryPart) visible.army = actual.army;
        roster.addObject(visible.toJsonObject());
    }
    result.addArray("players", roster);
    if(summaryPhase(phase)) result.addObject("winresult", local.winResult.toJsonObject());
    JsonObject owners;
    for(const auto id : lands_all)
    {
        const Land land(id);
        owners.addString(land.toString(), GameData::landInfo(land).clan.toString());
    }
    result.addObject("landOwners", owners);
    JsonArray history;
    for(const auto & player : GameData::players())
        for(const auto & legend : GameData::getBattleHistoryFor(player.avatar))
            history.addObject(legend.toJsonObject());
    result.addArray("history", history);
    return result;
}

bool Multiplayer::applyClientView(const JsonObject & wire, std::string* error)
{
    if(!boundedTree(wire) || !onlyKeys(wire, {"viewVersion", "recipient", "phase", MatchTopologyIdentityKey,
        RuneGameRulesetIdentityKey, "wind:round", "wind:part", "wind:current", "stone:drop", "lastcount",
        "trash", "players", "winresult", "landOwners", "history"}) ||
       !wire.isInteger("viewVersion") || wire.getInteger("viewVersion") != 1 || !wire.isInteger("phase"))
        return fail(error, "invalid client view envelope");
    const int phase = wire.getInteger("phase");
    if(phase != Menu::ShowPlayers && phase != Menu::MahjongPart && !summaryPhase(phase))
        return fail(error, "invalid client view phase");
    MatchTopologyIdentity topology;
    RuneGameRulesetIdentity rules;
    if(!resolveMatchTopologyIdentity(wire, topology, false, error) ||
       !resolveRuneGameRulesetIdentity(wire, rules, false, error)) return false;
    const MatchTopology & match = *findMatchTopology(topology.id, topology.version);
    const JsonArray* roster = wire.getArray("players");
    const JsonArray* trash = wire.getArray("trash");
    const JsonArray* history = wire.getArray("history");
    const JsonObject* owners = wire.getObject("landOwners");
    if(!roster || roster->size() != static_cast<std::size_t>(match.seatCount()) ||
       !trash || trash->size() > 136 || !history || !owners ||
       owners->size() != lands_all.size() || !wire.isInteger("lastcount") ||
       wire.getInteger("lastcount") < 0 || wire.getInteger("lastcount") > GAME_STONE_MAX ||
       !wire.isString("recipient") || !identifier<Avatar>(wire.getString("recipient"), false))
        return fail(error, "invalid client view roster or public counters");
    LocalData local{};
    LocalPlayers decoded;
    std::set<int> avatars, clans, winds;
    for(std::size_t i = 0; i < roster->size(); ++i)
    {
        LocalPlayer player;
        const auto* entry = roster->getObject(i);
        if(!entry || !validPlayer(*entry, player) || !player.avatar.isValid() ||
           !player.clan.isValid() || !match.hasWind(player.wind()) ||
           !avatars.insert(player.avatar()).second || !clans.insert(player.clan()).second ||
           !winds.insert(player.wind()).second)
            return fail(error, "invalid client view player");
        decoded.push_back(std::move(player));
    }
    const auto* mine = decoded.playerOfAvatar(Avatar(wire.getString("recipient")));
    if(!mine || mine->isAI()) return fail(error, "client view recipient is not a human participant");
    if(match.seatCount() == 2 && match.alliedByClan(decoded[0].clan(), decoded[1].clan()))
        return fail(error, "Duel players must own opposite island halves");
    for(const auto & player : decoded)
        if(player.avatar != mine->avatar && !player.isAffectedSpell(Spell::ScryRunes, mine->avatar) &&
           (!player.stones.empty() || player.newStone.isValid()))
            return fail(error, "unauthorized opponent hand in client view");
    local.compass = WindCompass(mine->wind);
    const Wind seats[] = { local.compass.left(), local.compass.right(), local.compass.top(), local.compass.bottom() };
    for(int i = 0; i < 4; ++i)
        if(const auto* player = decoded.playerOfWind(seats[i])) local.players[i] = *player;
    for(const char* key : {"wind:round", "wind:part", "wind:current", "stone:drop"})
        if(!wire.isString(key)) return fail(error, "missing client view turn data");
    local.roundWind = Wind(wire.getString("wind:round"));
    local.partWind = Wind(wire.getString("wind:part"));
    local.currentWind = Wind(wire.getString("wind:current"));
    local.dropStone = Stone(wire.getString("stone:drop"));
    if(phase != Menu::ShowPlayers && (!local.roundWind.isValid() || !match.hasWind(local.partWind()) ||
        !match.hasWind(local.currentWind()))) return fail(error, "invalid client view turn");
    local.stoneLastCount = wire.getInteger("lastcount");
    for(std::size_t i = 0; i < trash->size(); ++i)
    {
        if(!trash->getValue(i)->isString() || !identifier<Stone>(trash->getString(i), false))
            return fail(error, "invalid client view discard");
        local.trashSet.emplace_back(trash->getString(i));
    }
    if(summaryPhase(phase))
    {
        const auto* win = wire.getObject("winresult");
        if(!win) return fail(error, "missing client view summary");
        local.winResult = WinResults::fromJsonObject(*win);
        if(!sameJson(*win, local.winResult.toJsonObject()) || local.winResult.rules.size() > 5)
            return fail(error, "invalid client view summary");
    }
    else if(wire.hasKey("winresult")) return fail(error, "private hand result in active client view");
    std::vector<std::pair<Land, Clan>> decodedOwners;
    for(const auto id : lands_all)
    {
        const Land land(id);
        const auto name = land.toString();
        if(!owners->isString(name) || !identifier<Clan>(owners->getString(name)))
            return fail(error, "invalid client view territory owner");
        const Clan owner(owners->getString(name));
        if(!land.isTowerWinds() && !clans.count(owner()))
            return fail(error, "client view territory owner is absent");
        decodedOwners.emplace_back(land, owner);
    }
    std::list<BattleLegend> decodedHistory;
    for(std::size_t i = 0; i < history->size(); ++i)
    {
        const auto* legend = history->getObject(i);
        if(!legend || !validLegend(*legend)) return fail(error, "invalid client view battle history");
        decodedHistory.push_back(BattleLegend::fromJsonObject(*legend));
    }
    // The entire untrusted projection is checked before changing even the active
    // topology. Hydration never invokes save restore, RNG or turn advancement.
    selectActiveMatchTopology(topology.id, topology.version);
    selectActiveRuneGameRuleset(rules.id, rules.version);
    GameData::applyClientView(local, phase, decodedOwners, decodedHistory);
    if(error) error->clear();
    return true;
}

bool Multiplayer::eventFromWire(const JsonObject & wire, ActionMessage & output, std::string* error)
{
    if(!boundedTree(wire) || !wire.isInteger("type") || !wire.isString("currentWind") ||
       !identifier<Wind>(wire.getString("currentWind"), false))
        return fail(error, "invalid multiplayer event envelope");
    const int type = wire.getInteger("type");
    JsonObject expected;
    expected.addInteger("type", type);
    expected.addString("currentWind", wire.getString("currentWind"));
    bool valid = true;
    const auto string = [&](const char* key) {
        if(!wire.isString(key)) valid = false;
        else expected.addString(key, wire.getString(key));
    };
    const auto boolean = [&](const char* key) {
        if(!wire.isBoolean(key)) valid = false;
        else expected.addBoolean(key, wire.getBoolean(key));
    };
    const auto integer = [&](const char* key) {
        if(!wire.isInteger(key)) valid = false;
        else expected.addInteger(key, wire.getInteger(key));
    };
    const auto ints = [&](const char* key) {
        if(!integerArray(wire.getArray(key))) valid = false;
        else expected.addArray(key, *wire.getArray(key));
    };
    switch(type)
    {
        case Action::MahjongBegin: string("roundWind"); boolean("newRound"); break;
        case Action::MahjongTurn: string("newStone"); boolean("showKong"); boolean("showGame"); break;
        case Action::MahjongEnd: case Action::MahjongData: case Action::MahjongPass:
        case Action::MahjongKong2: case Action::AdventureBegin: case Action::AdventureTurn:
        case Action::AdventureEnd: break;
        case Action::MahjongDrop: string("dropStone"); break;
        case Action::MahjongInfo: string("info"); break;
        case Action::MahjongGame: case Action::MahjongChao:
        case Action::MahjongPung: case Action::MahjongKong1:
            boolean("sayOnly");
            if(wire.hasKey("dropStone")) string("dropStone");
            break;
        case Action::MahjongSummon: string("creature"); string("land"); break;
        case Action::MahjongCast:
            string("spell");
            if(wire.hasKey("target"))
            {
                string("target");
                valid = valid && identifier<Avatar>(wire.getString("target"), false);
            }
            if(wire.hasKey("land"))
            {
                string("land"); ints("targets"); ints("resists");
            }
            break;
        case Action::MahjongLuckChoice:
        {
            const auto* choices = wire.getArray("choices");
            if(!choices || choices->size() != 2) valid = false;
            else
            {
                for(std::size_t i = 0; i < choices->size(); ++i)
                    valid = valid && choices->getValue(i)->isString() && identifier<Stone>(choices->getString(i), false);
                expected.addArray("choices", *choices);
            }
            break;
        }
        case Action::AdventureMoves: integer("unit"); string("land"); break;
        case Action::AdventureClaim:
            string("land"); string("previousOwner"); string("owner"); integer("cost"); boolean("reverted"); break;
        case Action::AdventureCombat: case Action::AdventureBattleChoice:
        {
            const auto* legend = wire.getObject("legend");
            const auto* strikes = wire.getArray("strikes");
            if(!legend || !validLegend(*legend) || !validStrikes(strikes)) valid = false;
            else { expected.addObject("legend", *legend); expected.addArray("strikes", *strikes); }
            if(type == Action::AdventureBattleChoice)
            {
                string("phase"); ints("actors"); ints("targets");
                integer("recommendedActor"); integer("recommendedTarget");
                integer("choiceNumber"); integer("choiceCount");
            }
            break;
        }
        default: valid = false;
    }
    if(!valid || !sameJson(wire, expected)) return fail(error, "invalid multiplayer event fields");
    static_cast<JsonObject &>(output) = std::move(expected);
    if(error) error->clear();
    return true;
}

JsonObject Multiplayer::eventToWire(const ActionMessage & event, const Avatar & recipient)
{
    const auto* player = GameData::players().playerOfAvatar(recipient);
    if(!player) return JsonObject();
    ActionMessage decoded(Action::None);
    if(!eventFromWire(event, decoded)) return JsonObject();
    const bool actor = player->wind == Wind(event.getString("currentWind"));
    if(event.type() == Action::MahjongLuckChoice && !actor) return JsonObject();
    if(event.type() == Action::AdventureBattleChoice &&
       event.getObject("legend")->getString("attacker") != recipient.toString()) return JsonObject();
    if(event.type() == Action::AdventureMoves && !actor &&
       !GameData::toLocalData(recipient).findBattleUnitConst(event.getInteger("unit")))
        return JsonObject();
    if(event.type() == Action::MahjongTurn && !actor)
    {
        decoded.addString("newStone", Stone().toString());
        decoded.addBoolean("showKong", false);
        decoded.addBoolean("showGame", false);
    }
    return decoded;
}

ActionList Multiplayer::filterEvents(const ActionList & events, const Avatar & recipient)
{
    ActionList result;
    for(const auto & event : events)
    {
        const auto wire = eventToWire(event, recipient);
        ActionMessage decoded(Action::None);
        if(wire.isValid() && eventFromWire(wire, decoded)) result.push_back(std::move(decoded));
    }
    return result;
}
