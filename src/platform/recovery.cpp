#include "recovery.h"

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <iomanip>
#include <list>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "settings.h"
#include "battlesession.h"
#include "contentpackage.h"
#include "gameplayrng.h"
#include "matchtopology.h"
#include "runegameruleset.h"
#include "swe/swe_systems.h"

namespace
{
bool recoveryEnabled = true;

bool moveFile(const std::string & source, const std::string & destination)
{
    if(!SWE::Systems::isFile(source)) return true;
    if(SWE::Systems::isFile(destination)) std::remove(destination.c_str());
    return std::rename(source.c_str(), destination.c_str()) == 0;
}

std::string numberedPath(const std::string & directory, int slot, const char* extension)
{
    return SWE::Systems::concatePath(directory,
        std::string("recovery-") + std::to_string(slot) + extension);
}

std::string fnv1a64(const std::string & value)
{
    std::uint64_t hash = UINT64_C(14695981039346656037);
    for(unsigned char byte : value)
    {
        hash ^= byte;
        hash *= UINT64_C(1099511628211);
    }

    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(16) << hash;
    return stream.str();
}

std::string canonicalJson(const SWE::JsonValue & value)
{
    if(value.getType() == SWE::JsonType::Array)
    {
        const auto & array = static_cast<const SWE::JsonArray &>(value);
        std::string result = "[";
        for(std::size_t index = 0; index < array.size(); ++index)
        {
            if(index) result += ',';
            const SWE::JsonValue* child = array.getValue(index);
            result += child ? canonicalJson(*child) : "null";
        }
        return result + ']';
    }

    if(value.getType() == SWE::JsonType::Object)
    {
        const auto & object = static_cast<const SWE::JsonObject &>(value);
        const std::list<std::string> objectKeys = object.keys();
        std::vector<std::string> keys(objectKeys.begin(), objectKeys.end());
        std::sort(keys.begin(), keys.end());

        std::string result = "{";
        for(std::size_t index = 0; index < keys.size(); ++index)
        {
            if(index) result += ',';
            result += SWE::JsonString(keys[index]).toString();
            result += ':';
            const SWE::JsonValue* child = object.getValue(keys[index]);
            result += child ? canonicalJson(*child) : "null";
        }
        return result + '}';
    }

    return value.toString();
}
}

bool Recovery::enabled(void)
{
    return recoveryEnabled;
}

void Recovery::setEnabled(bool value)
{
    recoveryEnabled = value;
}

std::string Recovery::defaultDirectory(void)
{
    if(const char* overrideDirectory = SWE::Systems::environment("FOUR_WINDS_RECOVERY_DIR"))
        return overrideDirectory;

    return SWE::Systems::concatePath(Settings::shareDir(), "recovery");
}

std::string Recovery::stateHash(const std::string & saveData)
{
    return fnv1a64(saveData);
}

std::string Recovery::stateHash(const SWE::JsonValue & state)
{
    return fnv1a64(canonicalJson(state));
}

std::string Recovery::savePath(const std::string & directory, int slot)
{
    return numberedPath(directory, slot, ".sav");
}

std::string Recovery::metadataPath(const std::string & directory, int slot)
{
    return numberedPath(directory, slot, ".json");
}

bool Recovery::writeCheckpoint(const std::string & directory, const std::string & saveData,
                               const SWE::JsonObject & metadata)
{
    if(directory.empty() || saveData.empty() || !metadata.isValid()) return false;
    if(!SWE::Systems::isDirectory(directory) && !SWE::Systems::makeDirectory(directory))
        return false;

    const std::string saveTemporary = savePath(directory, 0) + ".tmp";
    const std::string metadataTemporary = metadataPath(directory, 0) + ".tmp";
    std::remove(saveTemporary.c_str());
    std::remove(metadataTemporary.c_str());

    if(!SWE::Systems::saveString2File(saveData, saveTemporary)) return false;
    if(!SWE::Systems::saveString2File(metadata.toString(), metadataTemporary))
    {
        std::remove(saveTemporary.c_str());
        return false;
    }

    for(int slot = SlotCount - 2; 0 <= slot; --slot)
    {
        if(!moveFile(savePath(directory, slot), savePath(directory, slot + 1)) ||
           !moveFile(metadataPath(directory, slot), metadataPath(directory, slot + 1)))
            return false;
    }

    if(!moveFile(saveTemporary, savePath(directory, 0))) return false;
    if(!moveFile(metadataTemporary, metadataPath(directory, 0))) return false;
    return true;
}

bool Recovery::validateSaveState(const SWE::JsonObject & state, std::string* error)
{
    if(!state.isValid())
    {
        if(error) *error = "save data is not valid JSON";
        return false;
    }

    if(state.getInteger("version") != FORMAT_VERSION_CURRENT)
    {
        if(error) *error = "save format is incompatible";
        return false;
    }

    RuneGameRulesetIdentity ruleset;
    if(!resolveRuneGameRulesetIdentity(state, ruleset, true, error)) return false;

    MatchTopologyIdentity topology;
    if(!resolveMatchTopologyIdentity(state, topology, true, error)) return false;

    ContentPackageIdentity package;
    if(!resolveContentPackageIdentity(state, package, true, error)) return false;

    // Share the loader's structural checks with save inspection and replacement.
    // No live state (including RNG or island ownership) is changed here.
    for(const char* key : {"croupier", "winresult"})
    {
        if(!state.isObject(key))
        {
            if(error) *error = std::string("save is missing object: ") + key;
            return false;
        }
    }
    const auto* history = state.getArray("history");
    if(!history)
    {
        if(error) *error = "save is missing battle history";
        return false;
    }
    for(std::size_t index = 0; index < history->size(); ++index)
    {
        if(!history->getObject(index))
        {
            if(error) *error = "save battle history entry is invalid";
            return false;
        }
    }
    for(const char* key : {"gameplayRng", "landOwners", "battleSession", "gui"})
    {
        if(state.hasKey(key) && !state.isObject(key))
        {
            if(error) *error = std::string("save object has an invalid type: ") + key;
            return false;
        }
    }
    if(const auto* rng = state.getObject("gameplayRng"))
    {
        if(!GameplayRng::isValidState(*rng))
        {
            if(error) *error = "unsupported or invalid gameplay RNG state";
            return false;
        }
    }
    if(const auto* battle = state.getObject("battleSession"))
    {
        const auto decoded = GameData::PendingBattle::fromJsonObject(*battle);
        if(!decoded.isValid() || decoded.session.phase() == Battle::Session::Phase::None)
        {
            if(error) *error = "invalid pending battle session";
            return false;
        }
    }

    const SWE::JsonObject* myPerson = state.getObject("myperson");
    const SWE::JsonArray* players = state.getArray("players");
    if(!myPerson)
    {
        if(error) *error = "save does not identify the local player";
        return false;
    }
    const MatchTopology & match = *findMatchTopology(topology.id, topology.version);
    if(!players || players->size() != match.seatCount())
    {
        if(error) *error = "save player count does not match its topology";
        return false;
    }

    const std::string myAvatar = myPerson->getString("avatar");
    const std::string myClan = myPerson->getString("clan");
    if(!Avatar(myAvatar).isValid() || myAvatar == "random" || !Clan(myClan).isValid())
    {
        if(error) *error = "local player identity is invalid";
        return false;
    }

    std::set<std::string> avatars;
    std::set<std::string> clans;
    std::set<std::string> winds;
    bool localPlayerFound = false;
    for(std::size_t index = 0; index < players->size(); ++index)
    {
        const SWE::JsonObject* player = players->getObject(index);
        if(!player)
        {
            if(error) *error = "save player entry is invalid";
            return false;
        }

        const std::string avatar = player->getString("avatar");
        const std::string clan = player->getString("clan");
        const std::string wind = player->getString("wind");
        if(!Avatar(avatar).isValid() || avatar == "random" ||
           !Clan(clan).isValid() || !match.hasWind(Wind(wind)()))
        {
            if(error) *error = "save player identity is invalid";
            return false;
        }
        if(!avatars.insert(avatar).second || !clans.insert(clan).second ||
           !winds.insert(wind).second)
        {
            if(error) *error = "save player identities are not unique";
            return false;
        }

        if(avatar == myAvatar && clan == myClan)
            localPlayerFound = true;
    }

    if(const auto* owners = state.getObject("landOwners"))
    {
        for(const auto landId : lands_all)
        {
            const Land land(landId);
            if(!land.isTowerWinds() && owners->hasKey(land.toString()) &&
               !clans.count(owners->getString(land.toString())))
            {
                if(error) *error = "territory has an invalid owner";
                return false;
            }
        }
    }

    if(match.seatCount() == 2)
    {
        const Clan first(players->getObject(0)->getString("clan"));
        const Clan second(players->getObject(1)->getString("clan"));
        if(!first.isValid() || !second.isValid() || match.alliedByClan(first(), second()))
        {
            if(error) *error = "Duel save must contain opposite island halves";
            return false;
        }
        const auto* owners = state.getObject("landOwners");
        if(!owners)
        {
            if(error) *error = "Duel save is missing its island ownership";
            return false;
        }
        for(const auto landId : lands_all)
        {
            const Land land(landId);
            if(!land.isTowerWinds())
            {
                const std::string owner = owners->getString(land.toString());
                if(!clans.count(owner))
                {
                    if(error) *error = "Duel territory has no participating owner";
                    return false;
                }
            }
        }
        for(const char* key : {"wind:current", "wind:part"})
        {
            const Wind wind(state.getString(key));
            if((state.getInteger("gamepart") != 0 || wind.isValid()) && !match.hasWind(wind()))
            {
                if(error) *error = "Duel save refers to an inactive seat";
                return false;
            }
        }
    }

    if(!localPlayerFound)
    {
        if(error) *error = "local player is missing from the save roster";
        return false;
    }

    if(error) error->clear();
    return true;
}

bool Recovery::validateSaveFile(const std::string & path, std::string* error,
                                std::string* saveData)
{
    std::string contents;
    if(!SWE::Systems::readFile2String(path, contents))
    {
        if(error) *error = "save file could not be read";
        return false;
    }

    const SWE::JsonObject state = SWE::JsonContentString(contents).toObject();
    if(contents.empty() || !state.isValid())
    {
        if(error) *error = "save data is not valid JSON";
        return false;
    }

    if(!validateSaveState(state, error)) return false;

    const int gamePart = state.getInteger("gamepart");
    if(gamePart < 3 || 8 < gamePart)
    {
        if(error) *error = "save game stage is invalid";
        return false;
    }

    if(saveData) *saveData = contents;
    if(error) error->clear();
    return true;
}

Recovery::CheckpointInfo Recovery::inspectCheckpoint(const std::string & directory, int slot)
{
    CheckpointInfo info;
    info.slot = slot;
    info.saveFile = savePath(directory, slot);
    info.metadataFile = metadataPath(directory, slot);

    const bool hasSave = SWE::Systems::isFile(info.saveFile);
    const bool hasMetadata = SWE::Systems::isFile(info.metadataFile);
    if(!hasSave && !hasMetadata) return info;
    if(!hasSave || !hasMetadata)
    {
        info.error = "save or metadata file is missing";
        return info;
    }

    std::string saveData;
    if(!validateSaveFile(info.saveFile, &info.error, &saveData)) return info;

    std::string metadataData;
    if(!SWE::Systems::readFile2String(info.metadataFile, metadataData))
    {
        info.error = "metadata file could not be read";
        return info;
    }

    const SWE::JsonObject metadata = SWE::JsonContentString(metadataData).toObject();
    if(!metadata.isValid())
    {
        info.error = "metadata is not valid JSON";
        return info;
    }

    if(metadata.getInteger("schema") != 1)
    {
        info.error = "unsupported metadata schema";
        return info;
    }

    if(metadata.getInteger("saveFormat") != FORMAT_VERSION_CURRENT)
    {
        info.error = "save format is incompatible";
        return info;
    }

    const SWE::JsonObject state = SWE::JsonContentString(saveData).toObject();
    RuneGameRulesetIdentity stateRuleset;
    RuneGameRulesetIdentity metadataRuleset;
    if(!resolveRuneGameRulesetIdentity(state, stateRuleset, true, &info.error) ||
       !resolveRuneGameRulesetIdentity(metadata, metadataRuleset, true, &info.error))
        return info;
    if(!sameRuneGameRuleset(stateRuleset, metadataRuleset))
    {
        info.error = "save and recovery metadata use different Rune Game rulesets";
        return info;
    }

    MatchTopologyIdentity stateTopology;
    MatchTopologyIdentity metadataTopology;
    if(!resolveMatchTopologyIdentity(state, stateTopology, true, &info.error) ||
       !resolveMatchTopologyIdentity(metadata, metadataTopology, true, &info.error))
        return info;
    if(!sameMatchTopology(stateTopology, metadataTopology))
    {
        info.error = "save and recovery metadata use different match topologies";
        return info;
    }

    ContentPackageIdentity statePackage;
    ContentPackageIdentity metadataPackage;
    if(!resolveContentPackageIdentity(state, statePackage, true, &info.error) ||
       !resolveContentPackageIdentity(metadata, metadataPackage, true, &info.error))
        return info;
    if(!sameContentPackage(statePackage, metadataPackage))
    {
        info.error = "save and recovery metadata use different content packages";
        return info;
    }

    info.stateBytes = metadata.getInteger("stateBytes");
    if(info.stateBytes != static_cast<int>(saveData.size()))
    {
        info.error = "save size does not match metadata";
        return info;
    }

    if(metadata.getString("fileHashFNV1a64") != stateHash(saveData))
    {
        info.error = "save hash does not match metadata";
        return info;
    }

    try
    {
        info.savedAtEpoch = std::stoll(metadata.getString("savedAtEpoch"));
    }
    catch(const std::exception &)
    {
        info.error = "checkpoint timestamp is invalid";
        return info;
    }

    info.reason = metadata.getString("reason");
    info.gameVersion = metadata.getString("gameVersion");
    info.gameRevision = metadata.getString("gameRevision");
    info.gamePart = metadata.getInteger("gamePart");
    info.roundWind = metadata.getString("roundWind");
    info.partWind = metadata.getString("partWind");
    info.currentWind = metadata.getString("currentWind");
    info.aiDifficulty = metadata.getString("aiDifficulty");
    info.runeGameRulesetId = stateRuleset.id;
    info.runeGameRulesetVersion = stateRuleset.version;
    info.contentPackageId = statePackage.id;
    info.contentPackageVersion = statePackage.version;
    info.valid = true;
    return info;
}

std::vector<Recovery::CheckpointInfo> Recovery::inspectCheckpoints(const std::string & directory)
{
    std::vector<CheckpointInfo> result;
    for(int slot = 0; slot < SlotCount; ++slot)
    {
        CheckpointInfo info = inspectCheckpoint(directory, slot);
        if(SWE::Systems::isFile(info.saveFile) || SWE::Systems::isFile(info.metadataFile))
            result.push_back(std::move(info));
    }
    return result;
}

bool Recovery::promoteSave(const std::string & source, const std::string & destination,
                           std::string* error)
{
    if(source == destination)
    {
        if(error) error->clear();
        return true;
    }

    std::string saveData;
    if(!validateSaveFile(source, error, &saveData)) return false;

    const std::string temporary = destination + ".recovery.tmp";
    const std::string backup = destination + ".before-recovery";
    std::remove(temporary.c_str());

    if(!SWE::Systems::saveString2File(saveData, temporary))
    {
        if(error) *error = "temporary primary save could not be written";
        return false;
    }

    const bool hadPrimary = SWE::Systems::isFile(destination);
    if(hadPrimary)
    {
        std::remove(backup.c_str());
        if(std::rename(destination.c_str(), backup.c_str()) != 0)
        {
            std::remove(temporary.c_str());
            if(error) *error = "current autosave could not be backed up";
            return false;
        }
    }

    if(std::rename(temporary.c_str(), destination.c_str()) != 0)
    {
        if(hadPrimary) std::rename(backup.c_str(), destination.c_str());
        std::remove(temporary.c_str());
        if(error) *error = "recovery save could not replace the current autosave";
        return false;
    }

    if(error) error->clear();
    return true;
}
