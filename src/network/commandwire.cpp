#include "commandwire.h"
#include "replay.h"
#include <set>

namespace Multiplayer
{
std::unique_ptr<ClientMessage> commandFromWire(const JsonObject& input, std::string& error)
{
    error = "Invalid multiplayer command";
    if(!input.isInteger("type")) return nullptr;
    std::set<std::string> fields{"type"};
    const auto integer = [&](const char* key, int low, int high) {
        fields.insert(key);
        return input.isInteger(key) && input.getInteger(key) >= low && input.getInteger(key) <= high;
    };
    const auto text = [&](const char* key) {
        fields.insert(key);
        return input.isString(key) && !input.getString(key).empty() && input.getString(key).size() <= 48;
    };
    const auto boolean = [&](const char* key) {
        fields.insert(key);
        return input.isBoolean(key);
    };
    bool valid = false;
    switch(input.getInteger("type"))
    {
        case Action::ClientReady: case Action::ClientButtonGame: case Action::ClientButtonPass:
        case Action::ClientButtonPung: case Action::ClientButtonKong1: case Action::ClientButtonKong2:
        case Action::ClientSayGame: case Action::ClientSayChao: case Action::ClientSayPung:
        case Action::ClientAdventureUndo: case Action::ClientBattleReady:
            valid = true; break;
        case Action::ClientDropIndex: valid = integer("dropIndex", 0, 32); break;
        case Action::ClientChaoVariant: valid = integer("variant", 0, 16); break;
        case Action::ClientLuckChoice: valid = integer("index", 0, 16); break;
        case Action::ClientSayKong:
            valid = integer("kongType", 1, 2); break;
        case Action::ClientSummonCreature:
            valid = text("creature") && Creature(input.getString("creature")).isValid()
                && text("land") && Land(input.getString("land")).isValid()
                && boolean("force") && !input.getBoolean("force");
            break;
        case Action::ClientCastSpell:
            valid = text("spell") && Spell(input.getString("spell")).isValid();
            if(input.hasKey("target"))
                valid = valid && text("target") && Avatar(input.getString("target")).isValid();
            else if(input.hasKey("land") || input.hasKey("unit") || input.hasKey("force"))
                valid = valid && text("land") && integer("unit", -1, 16777215)
                    && boolean("force") && !input.getBoolean("force");
            break;
        case Action::ClientUnitMoved:
            valid = integer("unit", 1, 1000000) && text("land"); break;
        case Action::ClientLandClaim:
            valid = text("land") && Land(input.getString("land")).isValid(); break;
        case Action::ClientBattleChoice:
            valid = integer("actor", -1, 16777215) && integer("target", -1, 16777215)
                && boolean("autoResolve"); break;
        default: break;
    }
    if(!valid || input.size() != fields.size()) return nullptr;
    for(const auto& key : input.keys()) if(fields.count(key) == 0) return nullptr;
    auto result = Replay::clientMessageFromJson(input);
    if(result) error.clear();
    return result;
}
}
