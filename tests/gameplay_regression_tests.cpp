#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <initializer_list>
#include <map>
#include <sstream>
#include <vector>

#include "aiadventure.h"
#include "aibattle.h"
#include "aispell.h"
#include "aistrategy.h"
#include "aiturn.h"
#include "adventurehints.h"
#include "avatar_balance_tests.h"
#include "battle.h"
#include "contentcatalog.h"
#include "contentpackage.h"
#include "crashreport.h"
#ifdef BUILD_DEBUG
#include "developertools.h"
#endif
#include "gameplayrng.h"
#include "gametheme.h"
#include "gamesummarypart.h"
#include "intropart.h"
#include "matchtopology.h"
#include "recovery.h"
#include "replay.h"
#include "replayfiles.h"
#include "runegameruleset.h"
#include "savegames.h"
#include "settings.h"
#include "simulation.h"
#include "tournament.h"

#if defined(_WIN32)
#ifdef ERROR
#undef ERROR
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#ifdef ERROR
#undef ERROR
#endif
#ifdef DELETE
#undef DELETE
#endif
#endif

namespace GameData
{
bool loadIndexes(const JsonObject &);
extern std::vector<CreatureInfo> creaturesInfo;
extern std::vector<SpellInfo> spellsInfo;
extern std::vector<LandInfo> landsInfo;
extern std::vector<AvatarInfo> avatarsInfo;
extern std::vector<StoneInfo> stonesInfo;
extern std::vector<WindInfo> windsInfo;
extern std::vector<ClanInfo> clansInfo;
extern std::vector<AbilityInfo> abilitiesInfo;
extern std::vector<SpecialityInfo> specialsInfo;
extern std::vector<Clan> initialLandOwners;
extern int bonusStart;
extern int bonusGame;
extern int bonusKong;
extern int bonusPung;
extern int bonusChao;
extern int bonusPass;
extern LocalPlayers gamers;
extern Wind currentWind;
extern Wind roundWind;
extern Wind partWind;
extern CroupierSet croupier;
extern int stoneLastCount;
extern Stone dropStone;
extern WinResults winResult;
extern bool skipRepeatSay;
extern bool skipNewStone;
extern bool skipNewTurn;
JsonObject toJsonObject(const JsonObject &);
}

namespace
{
int failures = 0;

class AlternateRuneGameRuleset final : public RuneGameRuleset
{
public:
    const std::string & id(void) const override
    {
        static const std::string value("test-alternate-scoring");
        return value;
    }

    int version(void) const override { return 7; }
    const std::vector<int> & wallStoneIds(void) const override
    {
        static const std::vector<int> stones = { Stone::Skull1, Stone::Sword1 };
        return stones;
    }
    int wallCopies(void) const override { return 4; }
    int initialHandSize(void) const override { return 2; }
    bool allowsChao(bool nextPlayer, bool sequenceRune, int variantCount) const override
    {
        return nextPlayer && sequenceRune && 0 < variantCount;
    }
    bool allowsPung(bool otherPlayer, int matchingRuneCount) const override
    {
        return otherPlayer && 0 < matchingRuneCount;
    }
    bool allowsExposedKong(bool otherPlayer, int matchingRuneCount) const override
    {
        return otherPlayer && 1 < matchingRuneCount;
    }
    bool allowsSelfKong(bool currentPlayer, bool hasDrawnRune,
                        bool upgradesPung, int matchingRuneCount) const override
    {
        return currentPlayer && hasDrawnRune && (upgradesPung || 2 == matchingRuneCount);
    }
    bool allowsWinStone(bool hasDrawnRune, bool, bool) const override
    {
        return hasDrawnRune;
    }
    bool isWinningStructure(int groupCount, int pairCount) const override
    {
        return 0 < groupCount && 0 < pairCount;
    }
    int callChoicePriority(RuneGameCall call) const override
    {
        switch(call)
        {
            case RuneGameCall::Game: return 4;
            case RuneGameCall::Chao: return 3;
            case RuneGameCall::Pung: return 2;
            case RuneGameCall::Kong: return 1;
            case RuneGameCall::None: break;
        }
        return 0;
    }
    int discardClaimPriority(RuneGameCall call) const override
    {
        switch(call)
        {
            case RuneGameCall::Game: return 3;
            case RuneGameCall::Chao: return 2;
            case RuneGameCall::Pung:
            case RuneGameCall::Kong: return 1;
            case RuneGameCall::None: break;
        }
        return 0;
    }
    RuneGameRoundAdvance advanceRound(int roundWindId, int partWindId) const override
    {
        if(Wind::East == roundWindId && Wind::South == partWindId)
            return { roundWindId, partWindId, false, true };
        if(Wind::None == roundWindId && Wind::None == partWindId)
            return { Wind::East, Wind::East, false, false };
        return { Wind::East, Wind::South, false, false };
    }
    int firstTurnWindId(void) const override { return Wind::South; }
    int spellPointAward(RuneGameSpellPointEvent event) const override
    {
        switch(event)
        {
            case RuneGameSpellPointEvent::Discard: return 2;
            case RuneGameSpellPointEvent::Chao: return 4;
            case RuneGameSpellPointEvent::Pung: return 6;
            case RuneGameSpellPointEvent::Kong: return 8;
            case RuneGameSpellPointEvent::Win: return 10;
        }
        return 0;
    }
    int baseWinPoints(void) const override { return 30; }
    int scoreMultiplier(int doubles) const override { return 3 << std::max(0, doubles); }
    int maximumWinScore(void) const override { return 200; }
};

void expect(bool condition, const char* message)
{
    if(condition) return;

    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

JsonObject jsonObjectWithoutKey(const JsonObject & source, const std::string & omittedKey)
{
    JsonObject result;
    for(const std::string & key : source.keys())
    {
        if(key == omittedKey) continue;

        const JsonValue* value = source.getValue(key);
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

void testRuneGameRulesetIdentityContract()
{
    std::string rulesetError;
    expect(selectActiveRuneGameRuleset(ClassicRuneGameRulesetId,
                                       ClassicRuneGameRulesetVersion,
                                       &rulesetError) && rulesetError.empty(),
           "the registered Classic Rune Game ruleset must be selectable");

    const RuneGameRulesetIdentity classicIdentity =
        runeGameRulesetIdentity(activeRuneGameRuleset());
    const JsonObject encodedClassic =
        runeGameRulesetIdentityJson(activeRuneGameRuleset());
    expect(classicIdentity.id == ClassicRuneGameRulesetId &&
           classicIdentity.version == ClassicRuneGameRulesetVersion &&
           encodedClassic.getString("id") == ClassicRuneGameRulesetId &&
           encodedClassic.getInteger("version") == ClassicRuneGameRulesetVersion,
           "Classic ruleset identity must have one stable JSON representation");

    RuneGameRulesetIdentity resolved;
    const JsonObject legacyContainer;
    rulesetError.clear();
    expect(resolveRuneGameRulesetIdentity(legacyContainer, resolved, true, &rulesetError) &&
           sameRuneGameRuleset(resolved, classicIdentity) && rulesetError.empty(),
           "legacy artifacts without ruleset metadata must load as Classic");
    rulesetError.clear();
    expect(!resolveRuneGameRulesetIdentity(legacyContainer, resolved, false, &rulesetError) &&
           rulesetError == "Rune Game ruleset metadata is missing",
           "new artifacts must be able to require explicit ruleset metadata");

    JsonObject malformed;
    malformed.addString(RuneGameRulesetIdentityKey, "classic");
    rulesetError.clear();
    expect(!resolveRuneGameRulesetIdentity(malformed, resolved, true, &rulesetError) &&
           rulesetError == "Rune Game ruleset metadata is invalid",
           "malformed ruleset metadata must be rejected");

    JsonObject unavailableIdentity;
    unavailableIdentity.addString("id", "test-alternate-scoring");
    unavailableIdentity.addInteger("version", 7);
    JsonObject unavailable;
    unavailable.addObject(RuneGameRulesetIdentityKey, unavailableIdentity);
    rulesetError.clear();
    expect(!resolveRuneGameRulesetIdentity(unavailable, resolved, true, &rulesetError) &&
           rulesetError.find("test-alternate-scoring@7") != std::string::npos &&
           !selectActiveRuneGameRuleset("test-alternate-scoring", 7, nullptr) &&
           sameRuneGameRuleset(runeGameRulesetIdentity(activeRuneGameRuleset()),
                               classicIdentity),
           "unregistered rulesets must be rejected without changing the active ruleset");
}

void testMatchTopologyIdentityContract()
{
    std::string topologyError;
    expect(selectActiveMatchTopology(ClassicFreeForAllTopologyId,
                                     ClassicFreeForAllTopologyVersion,
                                     &topologyError) && topologyError.empty(),
           "the registered Classic free-for-all topology must be selectable");

    const MatchTopology & classic = activeMatchTopology();
    const MatchTopologyIdentity classicIdentity = matchTopologyIdentity(classic);
    const JsonObject encodedClassic = matchTopologyIdentityJson(classic);
    expect(classicIdentity.id == ClassicFreeForAllTopologyId &&
           classicIdentity.version == ClassicFreeForAllTopologyVersion &&
           encodedClassic.getString("id") == ClassicFreeForAllTopologyId &&
           encodedClassic.getInteger("version") == ClassicFreeForAllTopologyVersion &&
           classic.seatCount() == 4 && classic.controllerCount() == 4 &&
           classic.teamCount() == 4 &&
           classic.controllerForWind(Wind::East) == 0 &&
           classic.controllerForWind(Wind::North) == 3 &&
           classic.teamForWind(Wind::East) == 0 &&
           classic.teamForWind(Wind::North) == 3,
           "Classic topology identity and four independent seats must remain stable");
    expect(classic.sharesController(Wind::East, Wind::East) &&
           !classic.sharesController(Wind::East, Wind::South) &&
           classic.allied(Wind::West, Wind::West) &&
           !classic.allied(Wind::West, Wind::North) &&
           classic.controllerForWind(Wind::None) == -1 &&
           classic.teamForWind(99) == -1 &&
           !classic.allied(Wind::None, Wind::None),
           "Classic topology must not silently group distinct or invalid winds");

    MatchTopologyIdentity resolved;
    const JsonObject legacyContainer;
    topologyError.clear();
    expect(resolveMatchTopologyIdentity(legacyContainer, resolved, true, &topologyError) &&
           sameMatchTopology(resolved, classicIdentity) && topologyError.empty(),
           "legacy artifacts without topology metadata must load as Classic free-for-all");
    topologyError.clear();
    expect(!resolveMatchTopologyIdentity(legacyContainer, resolved, false, &topologyError) &&
           topologyError == "Match topology metadata is missing",
           "new artifacts must be able to require explicit topology metadata");

    JsonObject malformed;
    malformed.addString(MatchTopologyIdentityKey, "classic-ffa");
    topologyError.clear();
    expect(!resolveMatchTopologyIdentity(malformed, resolved, true, &topologyError) &&
           topologyError == "Match topology metadata is invalid",
           "malformed topology metadata must be rejected");

    JsonObject unavailableIdentity;
    unavailableIdentity.addString("id", "duel");
    unavailableIdentity.addInteger("version", 7);
    JsonObject unavailable;
    unavailable.addObject(MatchTopologyIdentityKey, unavailableIdentity);
    topologyError.clear();
    expect(!resolveMatchTopologyIdentity(unavailable, resolved, true, &topologyError) &&
           topologyError.find("duel@7") != std::string::npos &&
           !selectActiveMatchTopology("duel", 7, nullptr) &&
           sameMatchTopology(matchTopologyIdentity(activeMatchTopology()), classicIdentity),
           "unregistered topologies must be rejected without changing the active topology");
}

void testContentPackageIdentityContract()
{
    JsonObject classicManifest;
    classicManifest.addInteger("schema", ContentPackageManifestSchema);
    classicManifest.addString("id", ClassicContentPackageId);
    classicManifest.addInteger("version", ClassicContentPackageVersion);
    classicManifest.addString("engineContract", ClassicEngineContentContract);
    classicManifest.addString("gameData", "content.json");
    JsonArray classicVersions;
    classicVersions.addInteger(ClassicContentPackageVersion);
    classicManifest.addArray("compatibleVersions", classicVersions);
    JsonObject classicIndex;
    classicIndex.addObject(ContentPackageIdentityKey, classicManifest);

    std::string packageError;
    expect(activateContentPackage(classicIndex, &packageError) && packageError.empty(),
           "the Classic content package manifest must activate");
    const ContentPackageIdentity classicIdentity =
        contentPackageIdentity(activeContentPackageManifest());
    const JsonObject encodedClassic =
        contentPackageIdentityJson(activeContentPackageManifest());
    expect(classicIdentity.id == ClassicContentPackageId &&
           classicIdentity.version == ClassicContentPackageVersion &&
           encodedClassic.getString("id") == ClassicContentPackageId &&
           encodedClassic.getInteger("version") == ClassicContentPackageVersion,
           "Classic content package identity must have one stable JSON representation");

    ContentPackageIdentity resolved;
    const JsonObject legacyContainer;
    packageError.clear();
    expect(resolveContentPackageIdentity(legacyContainer, resolved, true, &packageError) &&
           sameContentPackage(resolved, classicIdentity) && packageError.empty(),
           "legacy artifacts without content package metadata must load as Classic");
    packageError.clear();
    expect(!resolveContentPackageIdentity(legacyContainer, resolved, false, &packageError) &&
           packageError == "Content package metadata is missing",
           "new artifacts must be able to require explicit content package metadata");

    JsonObject malformed;
    malformed.addString(ContentPackageIdentityKey, ClassicContentPackageId);
    packageError.clear();
    expect(!resolveContentPackageIdentity(malformed, resolved, true, &packageError) &&
           packageError == "Content package metadata is invalid",
           "malformed content package metadata must be rejected");

    JsonObject unavailableIdentity;
    unavailableIdentity.addString("id", "removed-package");
    unavailableIdentity.addInteger("version", 4);
    JsonObject unavailable;
    unavailable.addObject(ContentPackageIdentityKey, unavailableIdentity);
    packageError.clear();
    expect(!resolveContentPackageIdentity(unavailable, resolved, true, &packageError) &&
           packageError.find("removed-package@4") != std::string::npos,
           "artifacts from an unavailable content package must be rejected clearly");

    JsonObject compatibleManifest;
    compatibleManifest.addInteger("schema", ContentPackageManifestSchema);
    compatibleManifest.addString("id", ClassicContentPackageId);
    compatibleManifest.addInteger("version", 2);
    compatibleManifest.addString("engineContract", ClassicEngineContentContract);
    compatibleManifest.addString("gameData", "content-v2.json");
    JsonArray compatibleVersions;
    compatibleVersions.addInteger(1);
    compatibleVersions.addInteger(2);
    compatibleManifest.addArray("compatibleVersions", compatibleVersions);
    JsonObject compatibleIndex;
    compatibleIndex.addObject(ContentPackageIdentityKey, compatibleManifest);
    packageError.clear();
    expect(activateContentPackage(compatibleIndex, &packageError) &&
           activeContentPackageManifest().gameData == "content-v2.json" &&
           resolveContentPackageIdentity(legacyContainer, resolved, true, &packageError) &&
           resolved.version == ClassicContentPackageVersion,
           "a manifest must be able to declare compatibility with an older package version");

    JsonObject invalidManifest = compatibleManifest;
    invalidManifest.addInteger("schema", ContentPackageManifestSchema + 1);
    JsonObject invalidIndex;
    invalidIndex.addObject(ContentPackageIdentityKey, invalidManifest);
    packageError.clear();
    expect(!activateContentPackage(invalidIndex, &packageError) &&
           activeContentPackageManifest().identity.version == 2,
           "a rejected manifest must not replace the active content package");

    JsonObject unsupportedContractManifest = compatibleManifest;
    unsupportedContractManifest.addString("engineContract", "future-dynamic-v2");
    JsonObject unsupportedContractIndex;
    unsupportedContractIndex.addObject(ContentPackageIdentityKey,
                                       unsupportedContractManifest);
    packageError.clear();
    expect(!activateContentPackage(unsupportedContractIndex, &packageError) &&
           activeContentPackageManifest().identity.version == 2,
           "an unsupported engine content contract must fail transactionally");

    packageError.clear();
    expect(activateContentPackage(classicIndex, &packageError) && packageError.empty(),
           "content package tests must restore the Classic package");
}

void testInstalledContentCatalog()
{
    const std::string sourceRoot = FOUR_WINDS_SOURCE_DIR;
    const std::string sourceProgram =
        (std::filesystem::path(sourceRoot) / "four-winds-catalog-test.exe").string();
    const std::vector<InstalledContentPackage> installed =
        ContentCatalog::discover(sourceProgram, "four-winds-catalog-source-test");
    const auto original = std::find_if(installed.begin(), installed.end(),
        [](const InstalledContentPackage & package)
        {
            return package.theme == "classic";
        });
    std::string error;
    expect(original != installed.end() && original->compatible &&
           original->manifest.identity.id == ClassicContentPackageId &&
           original->manifest.identity.version == ClassicContentPackageVersion &&
           original->displayName() == "Classic" &&
           ContentCatalog::isAvailable(sourceProgram,
                                       "four-winds-catalog-source-test",
                                       "classic", &error) && error.empty(),
           "the installed Classic package must pass catalog validation");

    const auto reborn = std::find_if(installed.begin(), installed.end(),
        [](const InstalledContentPackage & package)
        {
            return package.theme == "reborn";
        });
    error.clear();
    expect(reborn != installed.end() && reborn->compatible &&
           reborn->manifest.identity.id == "reborn-community" &&
           reborn->manifest.identity.version == 1 &&
           reborn->displayName() == "Reborn Community Package" &&
           ContentCatalog::isAvailable(sourceProgram,
                                       "four-winds-catalog-source-test",
                                       "reborn", &error) && error.empty(),
           "the installed Reborn content package must pass catalog validation");

    const std::filesystem::path fixtureRoot =
        std::filesystem::temp_directory_path() / "four-winds-content-catalog-test";
    std::error_code filesystemError;
    std::filesystem::remove_all(fixtureRoot, filesystemError);
    std::filesystem::create_directories(fixtureRoot / "themes" / "broken",
                                        filesystemError);
    const std::string sourceIndex =
        (std::filesystem::path(sourceRoot) / "themes" / "classic" / "index.json").string();
    std::string indexText;
    const std::string brokenIndex =
        (fixtureRoot / "themes" / "broken" / "index.json").string();
    const bool fixtureWritten = Systems::readFile2String(sourceIndex, indexText) &&
        Systems::saveString2File(indexText, brokenIndex);
    const std::string fixtureProgram = (fixtureRoot / "game.exe").string();
    const std::vector<InstalledContentPackage> broken =
        ContentCatalog::discover(fixtureProgram, "four-winds-catalog-broken-test");
    const auto rejected = std::find_if(broken.begin(), broken.end(),
        [](const InstalledContentPackage & package)
        {
            return package.theme == "broken";
        });
    error.clear();
    expect(fixtureWritten && rejected != broken.end() && !rejected->compatible &&
           rejected->error.find("content.json") != std::string::npos &&
           !ContentCatalog::isAvailable(fixtureProgram,
                                        "four-winds-catalog-broken-test",
                                        "broken", &error) &&
           error.find("content.json") != std::string::npos,
           "incomplete packages must remain visible to diagnostics but unavailable");
    error.clear();
    expect(!ContentCatalog::isAvailable(sourceProgram,
                                        "four-winds-catalog-source-test",
                                        "missing", &error) &&
           error.find("not installed") != std::string::npos,
           "an absent stored package must produce a safe fallback diagnostic");
    std::filesystem::remove_all(fixtureRoot, filesystemError);
}

void testRuneGameRoundFlowRuleset()
{
    const RuneGameRuleset & classicRuleset = classicRuneGameRuleset();

    RuneGameRoundAdvance advance = classicRuleset.advanceRound(Wind::None, Wind::None);
    expect(advance.roundWindId == Wind::East && advance.partWindId == Wind::East &&
           !advance.rotatePlayerWinds && !advance.complete &&
           classicRuleset.firstTurnWindId() == Wind::East,
           "Classic ruleset must start the East round and East part with East taking the turn");

    advance = classicRuleset.advanceRound(Wind::East, Wind::East);
    expect(advance.roundWindId == Wind::East && advance.partWindId == Wind::South &&
           advance.rotatePlayerWinds && !advance.complete,
           "Classic ruleset must rotate seats when advancing within a round");

    advance = classicRuleset.advanceRound(Wind::East, Wind::North);
    expect(advance.roundWindId == Wind::South && advance.partWindId == Wind::East &&
           !advance.rotatePlayerWinds && !advance.complete,
           "Classic ruleset must begin the next round after the North part");

    advance = classicRuleset.advanceRound(Wind::North, Wind::North);
    expect(advance.complete,
           "Classic ruleset must finish after the North part of the North round");

    const AlternateRuneGameRuleset alternateRuleset;
    advance = alternateRuleset.advanceRound(Wind::None, Wind::None);
    expect(advance.roundWindId == Wind::East && advance.partWindId == Wind::East &&
           !advance.rotatePlayerWinds && !advance.complete &&
           alternateRuleset.firstTurnWindId() == Wind::South,
           "an injected ruleset must control its opening round and first turn");
    advance = alternateRuleset.advanceRound(Wind::East, Wind::East);
    expect(advance.roundWindId == Wind::East && advance.partWindId == Wind::South &&
           !advance.rotatePlayerWinds && !advance.complete,
           "an injected ruleset must control seat rotation independently from Classic");
    advance = alternateRuleset.advanceRound(Wind::East, Wind::South);
    expect(advance.complete,
           "an injected ruleset must control its own tournament completion point");

    const Wind savedRoundWind = GameData::roundWind;
    const Wind savedPartWind = GameData::partWind;
    GameData::roundWind = Wind(Wind::East);
    GameData::partWind = Wind(Wind::South);
    expect(!GameData::isGameOver(classicRuleset) && GameData::isGameOver(alternateRuleset),
           "GameData tournament completion must route through the injected ruleset");
    GameData::roundWind = savedRoundWind;
    GameData::partWind = savedPartWind;
}

void testRuneGameSpellPointRuleset()
{
    const RuneGameRuleset & classicRuleset = classicRuneGameRuleset();
    const AlternateRuneGameRuleset alternateRuleset;

    expect(classicRuleset.spellPointAward(RuneGameSpellPointEvent::Discard) == 10 &&
           classicRuleset.spellPointAward(RuneGameSpellPointEvent::Chao) == 20 &&
           classicRuleset.spellPointAward(RuneGameSpellPointEvent::Pung) == 30 &&
           classicRuleset.spellPointAward(RuneGameSpellPointEvent::Kong) == 40 &&
           classicRuleset.spellPointAward(RuneGameSpellPointEvent::Win) == 50,
           "Classic ruleset must preserve every established spell-point award");

    LocalPlayer classicDiscard;
    classicDiscard.points = 0;
    classicDiscard.stones.add(GameStone(Stone::Sword1));
    classicDiscard.setMahjongDrop(0);
    expect(classicDiscard.points == 10,
           "the compatibility discard path must keep the Classic spell-point award");

    LocalPlayer alternateDiscard;
    alternateDiscard.points = 0;
    alternateDiscard.stones.add(GameStone(Stone::Sword1));
    alternateDiscard.setMahjongDrop(0, alternateRuleset);
    expect(alternateDiscard.points == 2,
           "an injected ruleset must control the discard spell-point award");

    LocalPlayer alternateChao;
    alternateChao.points = 0;
    alternateChao.stones.add(GameStone(Stone::Sword1));
    alternateChao.stones.add(GameStone(Stone::Sword3));
    alternateChao.setMahjongChao(Stone(Stone::Sword2), 0, alternateRuleset);
    expect(alternateChao.points == 4 && alternateChao.rules.size() == 1 &&
           alternateChao.rules.front().isChao(),
           "an injected ruleset must control the Chao spell-point award");

    LocalPlayer alternatePung;
    alternatePung.points = 0;
    alternatePung.stones.add(GameStone(Stone::Dragon1));
    alternatePung.stones.add(GameStone(Stone::Dragon1));
    alternatePung.setMahjongPung(Stone(Stone::Dragon1), alternateRuleset);
    expect(alternatePung.points == 6 && alternatePung.rules.size() == 1 &&
           alternatePung.rules.front().isPung(),
           "an injected ruleset must control the Pung spell-point award");

    LocalPlayer exposedKong;
    exposedKong.points = 0;
    exposedKong.stones.add(GameStone(Stone::Sword5));
    exposedKong.stones.add(GameStone(Stone::Sword5));
    exposedKong.stones.add(GameStone(Stone::Sword5));
    exposedKong.setMahjongKong1(Stone(Stone::Sword5), alternateRuleset);
    expect(exposedKong.points == 8 && exposedKong.rules.size() == 1 &&
           exposedKong.rules.front().isKong(),
           "an injected ruleset must control the exposed Kong spell-point award");

    LocalPlayer concealedKong;
    concealedKong.points = 0;
    concealedKong.stones.add(GameStone(Stone::Number4));
    concealedKong.stones.add(GameStone(Stone::Number4));
    concealedKong.stones.add(GameStone(Stone::Number4));
    concealedKong.newStone = GameStone(Stone(Stone::Number4), false);
    concealedKong.setMahjongKong2(alternateRuleset);
    expect(concealedKong.points == 8 && concealedKong.rules.size() == 1 &&
           concealedKong.rules.front().isKong() && concealedKong.rules.front().isConcealed(),
           "an injected ruleset must control the concealed Kong spell-point award");

    LocalPlayer upgradedKong;
    upgradedKong.points = 0;
    upgradedKong.rules.push_back(WinRule(WinRule::Pung, Stone(Stone::Skull2), false));
    upgradedKong.newStone = GameStone(Stone(Stone::Skull2), false);
    upgradedKong.setMahjongKong2(alternateRuleset);
    expect(upgradedKong.points == 8 && upgradedKong.rules.front().isKong(),
           "an injected ruleset must control the upgraded Kong spell-point award");

    WinRules concealedRules;
    concealedRules << WinRule(WinRule::Chao, Stone(Stone::Sword1), false)
                   << WinRule(WinRule::Pung, Stone(Stone::Dragon1), false)
                   << WinRule(WinRule::Kong, Stone(Stone::Skull1), false);
    const WinResults alternateWin(
        Wind(Wind::East), Wind(Wind::South), Wind(Wind::West),
        WinRules(), concealedRules, Stone(Stone::Number2), Stone(Stone::Number2));
    LocalPlayer winner;
    winner.points = 0;
    winner.setMahjongGame(alternateWin, alternateRuleset);
    expect(winner.points == 28,
           "a winner must convert concealed sets and the win through one injected ruleset");
}

BattleCreature creature(int uid, int move, int loyalty = 3, int ranger = 1)
{
    CreatureStat stat;
    stat.attack = 2;
    stat.ranger = ranger;
    stat.defense = 1;
    stat.loyalty = loyalty;
    stat.move = move;
    return BattleCreature(Clan::Red, Creature::SkeletonHorde, CreatureSkill(stat), uid);
}

BattleCreature combatCreature(const Clan & clan, const Creature & id, int uid,
                              int attack, int ranger, int defense, int loyalty, int move = 1)
{
    CreatureStat stat;
    stat.attack = attack;
    stat.ranger = ranger;
    stat.defense = defense;
    stat.loyalty = loyalty;
    stat.move = move;
    return BattleCreature(clan, id, CreatureSkill(stat), uid);
}

template<class T>
std::vector<T> loadIndexed(const char* filename)
{
    JsonContentFile file(std::string(FOUR_WINDS_SOURCE_DIR) + "/themes/classic/json/gamedata/" + filename);
    expect(file.isValid(), "game data JSON must parse");

    const std::list<T> values = file.toArray().toStdList<T>();
    std::vector<T> result(values.size() + 1);
    for(const auto & value : values) result[value.id.index()] = value;
    return result;
}

const SpellInfo* findSpell(const std::list<SpellInfo> & spells, const Spell & spell)
{
    auto it = std::find_if(spells.begin(), spells.end(), [&](const SpellInfo & info)
    {
        return info.id == spell;
    });
    return it != spells.end() ? &*it : nullptr;
}

const AvatarInfo* findAvatar(const std::list<AvatarInfo> & avatars, const Avatar & avatar)
{
    auto it = std::find_if(avatars.begin(), avatars.end(), [&](const AvatarInfo & info)
    {
        return info.id == avatar;
    });
    return it != avatars.end() ? &*it : nullptr;
}

bool hasSpell(const AvatarInfo & avatar, const Spell & spell)
{
    return std::find(avatar.spells.begin(), avatar.spells.end(), spell) != avatar.spells.end();
}

AI::SpellCastPlan chooseOnly(const LocalPlayer & player, Spell::spell_t spell)
{
    Spells spells;
    spells.push_back(Spell(spell));
    return AI::chooseSpellCast(player, spells);
}

Spells spellSet(std::initializer_list<Spell::spell_t> values)
{
    Spells spells;
    for(Spell::spell_t spell : values) spells.push_back(Spell(spell));
    return spells;
}

void testDifficultyRules()
{
    AI::Difficulty parsedDifficulty = AI::Difficulty::Normal;
    AI::BehaviorProfile parsedProfile = AI::BehaviorProfile::Balanced;
    expect(AI::difficultyFromString("EASY") == AI::Difficulty::Easy,
           "AI difficulty parsing must be case insensitive");
    expect(AI::difficultyFromString("training") == AI::Difficulty::Training &&
           AI::difficultyFromString("UNFAIR") == AI::Difficulty::Unfair,
           "teaching and deliberately asymmetric difficulty names must round-trip");
    expect(AI::difficultyFromString("hard", parsedDifficulty) &&
           parsedDifficulty == AI::Difficulty::Hard &&
           !AI::difficultyFromString("impossible", parsedDifficulty),
           "strict AI difficulty parsing must reject unknown matrix axes");
    expect(AI::difficultyFromString("") == AI::Difficulty::Normal,
           "missing AI difficulty must preserve Normal compatibility for old saves");
    expect(AI::behaviorProfileFromString("CONTROL", parsedProfile) &&
           parsedProfile == AI::BehaviorProfile::Control &&
           !AI::behaviorProfileFromString("impossible", parsedProfile),
           "strict behavior-profile parsing must reject unknown matrix axes");
    AI::setBehaviorProfileOverride(AI::BehaviorProfile::Economic);
    expect(AI::behaviorProfileOverrideEnabled() &&
           AI::behaviorProfileOverride() == AI::BehaviorProfile::Economic,
           "balance laboratory must be able to force doctrine independently of avatar");
    AI::clearBehaviorProfileOverride();
    expect(!AI::behaviorProfileOverrideEnabled(),
           "behavior-profile override must return to native avatar doctrine");
    expect(AI::nextDifficulty(AI::Difficulty::Training) == AI::Difficulty::Easy &&
           AI::nextDifficulty(AI::Difficulty::Easy) == AI::Difficulty::Normal &&
           AI::nextDifficulty(AI::Difficulty::Normal) == AI::Difficulty::Hard &&
           AI::nextDifficulty(AI::Difficulty::Hard) == AI::Difficulty::Unfair &&
           AI::nextDifficulty(AI::Difficulty::Unfair) == AI::Difficulty::Training &&
           AI::previousDifficulty(AI::Difficulty::Training) == AI::Difficulty::Unfair &&
           AI::previousDifficulty(AI::Difficulty::Normal) == AI::Difficulty::Easy,
           "AI difficulty selector must cycle both ways through all five levels");

    expect(AI::difficultyRules(AI::Difficulty::Training).battleForecastSamples == 4 &&
           AI::difficultyRules(AI::Difficulty::Easy).battleForecastSamples == 8 &&
           AI::difficultyRules(AI::Difficulty::Normal).battleForecastSamples == 16 &&
           AI::difficultyRules(AI::Difficulty::Hard).battleForecastSamples == 48 &&
           AI::difficultyRules(AI::Difficulty::Unfair).battleForecastSamples == 128,
           "higher AI difficulty must spend more work on battle forecasts");
    expect(AI::difficultyRules(AI::Difficulty::Training).showPlayerBattleForecast &&
           AI::difficultyRules(AI::Difficulty::Easy).showPlayerBattleForecast &&
           AI::difficultyRules(AI::Difficulty::Normal).showPlayerBattleForecast &&
           !AI::difficultyRules(AI::Difficulty::Hard).showPlayerBattleForecast &&
           !AI::difficultyRules(AI::Difficulty::Unfair).showPlayerBattleForecast,
           "Hard and Unfair must hide map outcome percentages without reducing AI budgets");
    expect(AI::spellPlanningHorizon(AI::BehaviorProfile::Control, AI::Difficulty::Easy) == 1 &&
           AI::spellPlanningHorizon(AI::BehaviorProfile::Control, AI::Difficulty::Normal) == 3 &&
           AI::spellPlanningHorizon(AI::BehaviorProfile::Control, AI::Difficulty::Hard) == 4,
           "AI difficulty must control spell combination depth");
    expect(AI::maximumPartiesPerTarget(AI::BehaviorProfile::Aggressive,
                                       AI::Difficulty::Easy) == 1 &&
           AI::maximumPartiesPerTarget(AI::BehaviorProfile::Aggressive,
                                       AI::Difficulty::Hard) == 3,
           "hard AI must coordinate more attacking parties than easy AI");
    expect(AI::difficultyRules(AI::Difficulty::Easy).strategicGoalLimit == 2 &&
           AI::difficultyRules(AI::Difficulty::Normal).strategicGoalLimit == 4 &&
           AI::difficultyRules(AI::Difficulty::Hard).strategicGoalLimit == 6,
           "higher AI difficulty must retain a wider strategic goal beam");
    expect(AI::difficultyRules(AI::Difficulty::Easy).strategicBranchLimit == 3 &&
           AI::difficultyRules(AI::Difficulty::Normal).strategicBranchLimit == 6 &&
           AI::difficultyRules(AI::Difficulty::Hard).strategicBranchLimit == 10,
           "higher AI difficulty must retain a wider strategic action beam");
    expect(AI::difficultyRules(AI::Difficulty::Easy).offensiveSpellPenalty > 0 &&
           AI::difficultyRules(AI::Difficulty::Normal).offensiveSpellPenalty == 0 &&
           AI::difficultyRules(AI::Difficulty::Hard).offensiveSpellPenalty == 0,
           "Easy alone must explicitly restrain non-essential offensive spell pressure");
    expect(AI::difficultyRules(AI::Difficulty::Easy).runeValuePercent < 100 &&
           AI::difficultyRules(AI::Difficulty::Normal).runeValuePercent == 100 &&
           100 < AI::difficultyRules(AI::Difficulty::Hard).runeValuePercent,
           "difficulty must scale future-rune conservation around an unchanged Normal baseline");
    expect(AI::manaReserve(AI::BehaviorProfile::Balanced, AI::Difficulty::Easy) <
               AI::manaReserve(AI::BehaviorProfile::Balanced, AI::Difficulty::Normal) &&
           AI::manaReserve(AI::BehaviorProfile::Balanced, AI::Difficulty::Normal) <
               AI::manaReserve(AI::BehaviorProfile::Balanced, AI::Difficulty::Hard),
           "Hard must preserve more spell points than Normal and Easy on ordinary casts");
    expect(AI::minimumBattleWinChance(AI::BehaviorProfile::Balanced, AI::Difficulty::Easy) <
               AI::minimumBattleWinChance(AI::BehaviorProfile::Balanced, AI::Difficulty::Normal) &&
           AI::minimumBattleWinChance(AI::BehaviorProfile::Balanced, AI::Difficulty::Normal) <
               AI::minimumBattleWinChance(AI::BehaviorProfile::Balanced, AI::Difficulty::Hard),
           "Hard must demand safer public battle forecasts while Easy accepts more doubtful attacks");
    expect(AI::minimumThreatCaptureChance(AI::BehaviorProfile::Balanced, AI::Difficulty::Hard) <
               AI::minimumThreatCaptureChance(AI::BehaviorProfile::Balanced, AI::Difficulty::Normal) &&
           AI::minimumThreatCaptureChance(AI::BehaviorProfile::Balanced, AI::Difficulty::Normal) <
               AI::minimumThreatCaptureChance(AI::BehaviorProfile::Balanced, AI::Difficulty::Easy),
           "Hard must react to weaker visible threats while Easy waits for clearer danger");
    expect(AI::defensiveReservePercent(AI::BehaviorProfile::Balanced, AI::Difficulty::Easy) <
               AI::defensiveReservePercent(AI::BehaviorProfile::Balanced, AI::Difficulty::Normal) &&
           AI::defensiveReservePercent(AI::BehaviorProfile::Balanced, AI::Difficulty::Normal) <
               AI::defensiveReservePercent(AI::BehaviorProfile::Balanced, AI::Difficulty::Hard),
           "difficulty must make whole-turn defensive coordination deliberately distinct");
    expect(AI::preferNearBestChoice(AI::Difficulty::Easy, 100, 90, 6) &&
           !AI::preferNearBestChoice(AI::Difficulty::Easy, 100, 70, 6) &&
           !AI::preferNearBestChoice(AI::Difficulty::Easy, 100, 90, 7) &&
           !AI::preferNearBestChoice(AI::Difficulty::Normal, 100, 99, 6),
           "Easy mistakes must be deterministic, occasional and bounded to near-best choices");

    const AI::DifficultyRules & training = AI::difficultyRules(AI::Difficulty::Training);
    const AI::DifficultyRules & unfair = AI::difficultyRules(AI::Difficulty::Unfair);
    expect(training.supportSpellsOnly && !training.allowLandClaims &&
           !training.allowEnemyLandMoves && training.initialAiSpellPointBonus == 0,
           "Training must heal and defend without hostile spells, claims or invasions");
    expect(!unfair.supportSpellsOnly && unfair.allowLandClaims && unfair.allowEnemyLandMoves &&
           unfair.initialAiSpellPointBonus == 500 &&
           unfair.mahjongPartAiSpellPointBonus == 125 &&
           unfair.mahjongPartAiLandClaimBonus == 100,
           "Unfair must expose its explicit starting and recurring economy handicap");

    GameData::setAIDifficulty(AI::Difficulty::Unfair);
    expect(GameData::toJsonObject(JsonObject()).getString("ai:difficulty") == "unfair",
           "save data must persist the selected AI difficulty");
    GameData::setAIDifficulty(AI::Difficulty::Normal);
}

void testPolygonHitTesting()
{
    Points concavePoints;
    concavePoints << Point(0, 0) << Point(8, 0) << Point(8, 8) << Point(5, 8)
                  << Point(5, 3) << Point(3, 3) << Point(3, 8) << Point(0, 8);
    const SWE::Polygon concave(concavePoints);

    expect(concave & Point(1, 6),
           "polygon hit testing must accept the left side of a concave territory");
    expect(concave & Point(7, 6),
           "polygon hit testing must accept the right side of a concave territory");
    expect(!(concave & Point(4, 6)),
           "polygon hit testing must reject a concave cutout");
    expect(concave & Point(4, 0),
           "territory boundary pixels must remain clickable");
    expect(concave & Point(3, 5),
           "vertical territory edges must remain clickable");

    for(const Land::land_t id : lands_all)
    {
        const LandInfo & info = GameData::landInfo(Land(id));
        const SWE::Polygon territory(info.points);
        const std::string message =
            std::string("territory center must be clickable: ") + info.id.toString();
        expect(territory & info.center, message.c_str());
    }
}

void testDrawnMahjongResult()
{
    const WinResults drawn = WinResults::drawn(Wind::West, Wind::East);
    expect(drawn.isValid() && drawn.isDrawn(),
           "a wall draw must be a valid drawn Mahjong result");
    expect(drawn.dealWind == Wind(Wind::West) &&
           drawn.roundWind == Wind(Wind::East) &&
           !drawn.winWind.isValid(),
           "a wall draw must retain deal and round context without inventing a winner");

    const WinResults restored = WinResults::fromJsonObject(drawn.toJsonObject());
    expect(restored.isValid() && restored.isDrawn() &&
           restored.dealWind == drawn.dealWind &&
           restored.roundWind == drawn.roundWind &&
           !restored.winWind.isValid(),
           "draw context must survive save and recovery serialization");
}

void testStrategicRunePlanning()
{
    GameData::gamers.clear();

    const Avatar::avatar_t avatars[] = {
        Avatar::Orachi, Avatar::Lakkho, Avatar::Dayla, Avatar::Ziag
    };
    for(int index = 0; index < 4; ++index)
    {
        LocalPlayer player;
        player.avatar = avatars[index];
        player.clan = Clan(static_cast<Clan::clan_t>(Clan::Red + index));
        player.wind = Wind(static_cast<Wind::wind_t>(Wind::East + index));
        player.points = 500;
        GameData::gamers.push_back(player);
    }
    GameData::currentWind = Wind::East;

    LocalPlayer & orachi = GameData::gamers.front();
    orachi.stones.add(GameStone(Stone(Stone::Skull1), false));
    orachi.stones.add(GameStone(Stone(Stone::Number2), false));

    const AI::AIObservation baselineObservation = AI::observePlayer(orachi.avatar);
    const AI::StrategicIntent baseline = AI::chooseStrategicIntent(
        baselineObservation, AI::BehaviorProfile::Aggressive, AI::Difficulty::Normal);
    expect(!baseline.runeGoals.empty() &&
           baseline.runeValue(Stone(Stone::Skull1)) > baseline.runeValue(Stone(Stone::Number2)),
           "strategic Rune AI must value a held summon rune above an unrelated rune");

    AI::StrategicIntent easyConservation = baseline;
    easyConservation.difficulty = AI::Difficulty::Easy;
    AI::StrategicIntent hardConservation = baseline;
    hardConservation.difficulty = AI::Difficulty::Hard;
    expect(easyConservation.runeValue(Stone(Stone::Skull1)) <
               baseline.runeValue(Stone(Stone::Skull1)) &&
           baseline.runeValue(Stone(Stone::Skull1)) <
               hardConservation.runeValue(Stone(Stone::Skull1)),
           "Hard must conserve an observer-visible goal rune more strongly than Normal and Easy");

    const int drop = AI::mahjongSelect(orachi.stones, VecStones(), WinRules(), baseline);
    expect(0 <= drop && drop < static_cast<int>(orachi.stones.size()) &&
           orachi.stones[drop] == Stone(Stone::Number2),
           "strategic Rune AI must keep the rune needed by its ranked summon goal");
    expect(baseline.trace().find("goal=") != std::string::npos &&
           baseline.trace().find("rune_gain=") != std::string::npos &&
           baseline.trace().find("visible_remaining=") != std::string::npos,
           "strategic AI must expose deterministic score components in its trace");

    Creatures hiddenUniqueOptions;
    hiddenUniqueOptions.push_back(Creature(Creature::KilorCelsbane));
    hiddenUniqueOptions.push_back(Creature(Creature::SkeletonHorde));
    const std::vector<AI::SummonCandidate> baselineSummons = AI::rankSummonCandidates(
        baselineObservation, hiddenUniqueOptions, AI::BehaviorProfile::Aggressive);
    const Spells noStrategicCasts;
    const AI::SpellCastPlan baselineHiddenSpell = AI::chooseSpellCast(
        baselineObservation.state(), spellSet({ Spell::LightningBolt }),
        AI::BehaviorProfile::Aggressive, noStrategicCasts);
    const AI::TurnPlan baselineTurn = AI::chooseStrategicTurnPlan(
        baselineObservation, hiddenUniqueOptions, noStrategicCasts,
        AI::BehaviorProfile::Aggressive, AI::Difficulty::Normal);

    BattleParty hiddenParty(Clan::Yellow, Land::Zubrus);
    expect(hiddenParty.join(BattleCreature(Clan::Yellow, Creature::KilorCelsbane, 2450)),
           "strategic hidden-information fixture must join Kilor");
    GameData::gamers[1].army.push_back(hiddenParty);

    const AI::AIObservation hiddenObservation = AI::observePlayer(orachi.avatar);
    const AI::StrategicIntent withHiddenEnemy = AI::chooseStrategicIntent(
        hiddenObservation, AI::BehaviorProfile::Aggressive, AI::Difficulty::Normal);
    const std::vector<AI::SummonCandidate> hiddenSummons = AI::rankSummonCandidates(
        hiddenObservation, hiddenUniqueOptions, AI::BehaviorProfile::Aggressive);
    const AI::SpellCastPlan hiddenEnemySpell = AI::chooseSpellCast(
        hiddenObservation.state(), spellSet({ Spell::LightningBolt }),
        AI::BehaviorProfile::Aggressive, noStrategicCasts);
    const AI::TurnPlan hiddenTurn = AI::chooseStrategicTurnPlan(
        hiddenObservation, hiddenUniqueOptions, noStrategicCasts,
        AI::BehaviorProfile::Aggressive, AI::Difficulty::Normal);
    expect(!hiddenObservation.visibleCreature(Creature(Creature::KilorCelsbane)),
           "AIObservation must remove an undetected invisible third-party creature");
    expect(withHiddenEnemy.trace() == baseline.trace(),
           "an invisible third-party army must not affect the strategic AI plan");
    expect(hiddenSummons.size() == baselineSummons.size() &&
           std::equal(hiddenSummons.begin(), hiddenSummons.end(), baselineSummons.begin(),
                      [](const AI::SummonCandidate & left, const AI::SummonCandidate & right)
                      {
                          return left.trace() == right.trace();
                      }),
           "an invisible global unique creature must not alter observer-legal summon ranking");
    expect(!baselineHiddenSpell.isValid() && !hiddenEnemySpell.isValid(),
           "observer-safe spell planning must not target an undetected invisible creature");
    expect(hiddenTurn.trace() == baselineTurn.trace(),
           "an invisible third-party army must not alter the bounded strategic turn plan");
    expect(!baselineTurn.branches.empty() &&
           baselineTurn.trace().find("branch[0]") != std::string::npos &&
           baselineTurn.trace().find("adventure=") != std::string::npos &&
           baselineTurn.trace().find("visible_defense=") != std::string::npos,
           "strategic TurnPlan must expose deterministic action and Adventure scores");

    Creatures wideSummonOptions;
    for(const CreatureInfo & info : GameData::creaturesInfo)
    {
        if(info.id.isValid()) wideSummonOptions.push_back(info.id);
        if(12 <= wideSummonOptions.size()) break;
    }
    const AI::TurnPlan easyTurn = AI::chooseStrategicTurnPlan(
        hiddenObservation, wideSummonOptions, noStrategicCasts,
        AI::BehaviorProfile::Aggressive, AI::Difficulty::Easy);
    const AI::TurnPlan normalTurn = AI::chooseStrategicTurnPlan(
        hiddenObservation, wideSummonOptions, noStrategicCasts,
        AI::BehaviorProfile::Aggressive, AI::Difficulty::Normal);
    const AI::TurnPlan hardTurn = AI::chooseStrategicTurnPlan(
        hiddenObservation, wideSummonOptions, noStrategicCasts,
        AI::BehaviorProfile::Aggressive, AI::Difficulty::Hard);
    expect(easyTurn.branches.size() == 3 && normalTurn.branches.size() == 6 &&
           hardTurn.branches.size() == 10,
           "strategic TurnPlan must obey Easy, Normal and Hard branch budgets");

    LocalData fullArmyState = hiddenObservation.state();
    const Lands fullArmyLands = Lands::thisClan(fullArmyState.myPlayer().clan).powerOnly();
    expect(1 < fullArmyLands.size(), "full strategic army fixture needs two power lands");
    if(1 < fullArmyLands.size())
    {
        for(int partyIndex = 0; partyIndex < 2; ++partyIndex)
        {
            BattleParty party(fullArmyState.myPlayer().clan, fullArmyLands[partyIndex]);
            const int partySize = partyIndex == 0 ? 3 : 2;
            for(int member = 0; member < partySize; ++member)
                expect(party.join(BattleCreature(fullArmyState.myPlayer().clan,
                                                 Creature::SkeletonHorde,
                                                 2460 + partyIndex * 3 + member)),
                       "full strategic army fixture must join five creatures");
            fullArmyState.myPlayer().army.push_back(party);
        }
    }
    const AI::TurnPlan fullArmyTurn = AI::chooseStrategicTurnPlan(
        AI::AIObservation(fullArmyState), hiddenUniqueOptions,
        spellSet({ Spell::DrawNumber }), AI::BehaviorProfile::Aggressive,
        AI::Difficulty::Normal);
    expect(std::none_of(fullArmyTurn.branches.begin(), fullArmyTurn.branches.end(),
                        [](const AI::TurnBranch & branch)
                        {
                            return branch.action == AI::StrategicAction::Summon;
                        }),
           "a maximum-size army must not fill the bounded TurnPlan with rejected summons");

    GameData::gamers.clear();
    LocalPlayer javed;
    javed.avatar = Avatar::Javed;
    javed.clan = Clan::Red;
    javed.wind = Wind::East;
    javed.points = 1000;

    const Lands powerLands = Lands::thisClan(javed.clan).powerOnly();
    expect(!powerLands.empty(), "strategic summon fixture must have a power land");
    const auto tower = std::find(powerLands.begin(), powerLands.end(), Land(Land::TowerOf4Winds));
    const Land mergeLand = tower != powerLands.end() ? *tower :
                           (powerLands.empty() ? Land() : powerLands.front());
    BattleParty mergeParty(javed.clan, mergeLand);
    expect(mergeParty.join(BattleCreature(javed.clan, Creature::Griffon, 2451)),
           "strategic summon fixture must join its first Griffon");
    javed.army.push_back(mergeParty);
    GameData::gamers.push_back(javed);

    for(int index = 1; index < 4; ++index)
    {
        LocalPlayer opponent;
        opponent.avatar = avatars[index];
        opponent.clan = Clan(static_cast<Clan::clan_t>(Clan::Red + index));
        opponent.wind = Wind(static_cast<Wind::wind_t>(Wind::East + index));
        GameData::gamers.push_back(opponent);
    }

    Creatures summonOptions;
    summonOptions.push_back(Creature(Creature::AirElemental));
    summonOptions.push_back(Creature(Creature::Griffon));
    expect(AI::creatureSummonScore(Creature(Creature::AirElemental),
                                   AI::BehaviorProfile::Control) >
           AI::creatureSummonScore(Creature(Creature::Griffon),
                                   AI::BehaviorProfile::Control),
           "strategic summon fixture must start with the higher static Air Elemental score");

    const std::vector<AI::SummonCandidate> summonPlan = AI::rankSummonCandidates(
        AI::observePlayer(Avatar(Avatar::Javed)), summonOptions, AI::BehaviorProfile::Control);
    if(summonPlan.empty() || summonPlan.front().creature != Creature(Creature::Griffon) ||
       summonPlan.front().destination != mergeLand)
    {
        for(const AI::SummonCandidate & candidate : summonPlan)
            std::cerr << "  summon candidate: " << candidate.trace() << '\n';
    }
    expect(!summonPlan.empty() && summonPlan.front().creature == Creature(Creature::Griffon) &&
           summonPlan.front().destination == mergeLand && 0 < summonPlan.front().partySynergy,
           "Summon AI must choose the synergistic resulting Merge party over a higher static unit score");
    expect(!summonPlan.empty() && summonPlan.front().trace().find("party_synergy=") != std::string::npos,
           "Summon AI must explain resulting-party synergy in its trace");
}

void testSelectedPartyMovement()
{
    BattleArmy army;
    BattleParty source(Clan::Red, Land::Corzen);
    BattleCreature first = creature(250, 2);
    BattleCreature second = creature(251, 2);
    first.setSelected(true);
    second.setSelected(true);
    expect(source.join(first) && source.join(second), "selected movement source fixture must join");
    army.push_back(source);

    const std::vector<int> moved = army.moveSelectedCreatures(Land::Corzen, Land::Zubrus);
    const BattleParty* destination = army.findPartyConst(Land::Zubrus);
    expect(moved.size() == 2 && destination && destination->findBattleUnitConst(250) &&
           destination->findBattleUnitConst(251) && !army.findPartyConst(Land::Corzen),
           "selected movement must move the whole group to a legal destination");

    BattleArmy blocked;
    BattleParty blockedSource(Clan::Red, Land::Corzen);
    BattleCreature blockedFirst = creature(252, 2);
    BattleCreature blockedSecond = creature(253, 2);
    blockedFirst.setSelected(true);
    blockedSecond.setSelected(true);
    expect(blockedSource.join(blockedFirst) && blockedSource.join(blockedSecond),
           "blocked movement source fixture must join");
    blocked.push_back(blockedSource);

    BattleParty crowded(Clan::Red, Land::Zubrus);
    expect(crowded.join(creature(254, 2)) && crowded.join(creature(255, 2)),
           "blocked movement destination fixture must join");
    blocked.push_back(crowded);

    expect(blocked.moveSelectedCreatures(Land::Corzen, Land::Zubrus).empty() &&
           blocked.findPartyConst(Land::Corzen)->findBattleUnitConst(252) &&
           blocked.findPartyConst(Land::Corzen)->findBattleUnitConst(253) &&
           blocked.findPartyConst(Land::Zubrus)->count() == 2,
           "failed group movement must leave both parties unchanged");
    expect(!blocked.canMoveCreature(*blocked.findBattleUnitConst(252), Land::Corzen, Lands()),
           "empty movement paths must fail safely");
}

void testActionReplay()
{
    std::vector<std::unique_ptr<ClientMessage>> messages;
    messages.emplace_back(std::make_unique<ClientReady>());
    messages.emplace_back(std::make_unique<ClientButtonGame>());
    messages.emplace_back(std::make_unique<ClientButtonPass>());
    messages.emplace_back(std::make_unique<ClientChaoVariant>(2));
    messages.emplace_back(std::make_unique<ClientButtonPung>());
    messages.emplace_back(std::make_unique<ClientButtonKong1>());
    messages.emplace_back(std::make_unique<ClientButtonKong2>());
    messages.emplace_back(std::make_unique<ClientDropIndex>(4));
    messages.emplace_back(std::make_unique<ClientSayGame>());
    messages.emplace_back(std::make_unique<ClientSayChao>());
    messages.emplace_back(std::make_unique<ClientSayPung>());
    messages.emplace_back(std::make_unique<ClientSayKong>(1));
    messages.emplace_back(std::make_unique<ClientSummonCreature>(Creature::Durlock, Land::Corzen, true));
    messages.emplace_back(std::make_unique<ClientCastSpell>(Spell::Healing));
    messages.emplace_back(std::make_unique<ClientCastSpell>(Spell::ScryRunes, Avatar::Logun));
    messages.emplace_back(std::make_unique<ClientCastSpell>(Spell::Teleport, Land::Corzen, 601, true));
    messages.emplace_back(std::make_unique<ClientUnitMoved>(601, Land::None));
    messages.emplace_back(std::make_unique<ClientLandClaim>(Land::Corzen));
    messages.emplace_back(std::make_unique<ClientAdventureUndo>());
    messages.emplace_back(std::make_unique<ClientBattleReady>());
    messages.emplace_back(std::make_unique<ClientBattleChoice>(601, 701, false));
    messages.emplace_back(std::make_unique<ClientLuckChoice>(1));

    for(const auto & message : messages)
    {
        const std::unique_ptr<ClientMessage> rebuilt = Replay::clientMessageFromJson(*message);
        expect(rebuilt && rebuilt->toString() == message->toString(),
               "replay client-message factory must preserve every action payload");
    }

    JsonObject unknown;
    unknown.addInteger("type", Action::Last + 100);
    expect(!Replay::clientMessageFromJson(unknown),
           "replay client-message factory must reject unknown action types");

    GameData::initPersons(Person(Avatar::Nucrus, Clan::Red, Wind::East));
    LocalPlayer* player = GameData::gamers.playerOfWind(Wind::East);
    expect(player != nullptr, "replay fixture player must exist");
    if(!player) return;
    const Avatar actor = player->avatar;
    BattleParty party(Clan::Red, Land::Corzen);
    expect(party.join(combatCreature(Clan::Red, Creature::Durlock,
                                     601, 2, 0, 2, 3, 1)),
           "replay Adventure fixture must join");
    player->army.push_back(party);
    expect(GameData::initAdventure(), "replay Adventure fixture must initialize");

    const JsonObject initialState = GameData::authoritativeState();
    const ClientUnitMoved dismiss(601, Land::None);
    ActionList emitted;
    expect(GameData::client2Adventure(actor, dismiss, emitted),
           "replay reference action must be accepted");
    const std::string expectedHash = Replay::authoritativeStateHash();

    const std::vector<Replay::Step> replaySteps = {
        Replay::Step(actor, dismiss, expectedHash)
    };
    std::string replayError;
    expect(Replay::run(initialState, replaySteps, &replayError) &&
           GameData::findBattleCreature(601) == nullptr,
           "replay must restore, apply a validated Adventure action and match its state hash");

    const std::vector<Replay::Step> tamperedSteps = {
        Replay::Step(actor, dismiss, "0000000000000000")
    };
    replayError.clear();
    expect(!Replay::run(initialState, tamperedSteps, &replayError) &&
           replayError.find("state hash mismatch at step 0") != std::string::npos,
           "replay must stop at the first divergent authoritative state hash");

    GameplayRng::seed(UINT64_C(0x123456789abcdef));
    GameData::initPersons(Person(Avatar::Nucrus, Clan::Red, Wind::East));
    expect(GameData::initMahjong(), "RNG replay fixture must initialize Mahjong");
    LocalPlayer* randomPlayer = GameData::gamers.playerOfWind(Wind::East);
    expect(randomPlayer != nullptr, "RNG replay fixture East player must exist");
    if(!randomPlayer) return;

    randomPlayer->affected.insert(AffectedSpell(Spell(Spell::RandomDiscard), 1));
    const Avatar randomActor = randomPlayer->avatar;
    const JsonObject randomInitialState = GameData::authoritativeState();
    ActionList rejectedActions;
    ActionRejection unknownRejection;
    const ClientMessage unknownAction(Action::Last + 1);
    expect(!GameData::client2Mahjong(randomActor, unknownAction, rejectedActions,
                                     &unknownRejection) &&
           unknownRejection.reason == ActionRejectReason::UnknownAction &&
           std::string(GameData::actionRejectReasonName(unknownRejection.reason)) ==
               "unknown_action" &&
           !GameData::actionRejectionMessage(unknownRejection).empty() &&
           Replay::actionJournalSize() == 0,
           "unknown Mahjong actions must return a stable reason without entering the replay journal");
    const ActionRejection claimPointsRejection{
        ActionRejectReason::InsufficientClaimPoints, 1, 3
    };
    expect(std::string(GameData::actionRejectReasonName(claimPointsRejection.reason)) ==
               "insufficient_claim_points" &&
           GameData::actionRejectionMessage(claimPointsRejection).find("3") != std::string::npos &&
           GameData::actionRejectionMessage(claimPointsRejection).find("1") != std::string::npos,
           "claim-point rejections must remain distinct from spell-point failures");
    const ClientDropIndex randomDiscard(0);
    ActionList randomActions;
    expect(GameData::client2Mahjong(randomActor, randomDiscard, randomActions),
           "RNG replay reference Random Discard must be accepted");
    const JsonObject randomFinalState = GameData::authoritativeState();
    const std::string randomExpectedHash = Replay::authoritativeStateHash();
    AI::setBehaviorProfileOverride(AI::BehaviorProfile::Aggressive);
    const JsonObject journal = Replay::actionJournal(randomFinalState);
    AI::clearBehaviorProfileOverride();
    Replay::JournalInfo journalInfo;
    std::string journalInspectionError;
    const std::string stateBeforeJournalInspection =
        GameData::authoritativeState().toString();
    expect(Replay::inspectJournal(journal, journalInfo, &journalInspectionError) &&
           journalInspectionError.empty() && journalInfo.schema == 3 &&
           journalInfo.actionCount == 1 &&
           journalInfo.startedAtEpoch > 0 && journalInfo.savedAtEpoch > 0 &&
           journalInfo.gamePart == Menu::MahjongPart &&
           journalInfo.rulesetId == ClassicRuneGameRulesetId &&
           journalInfo.rulesetVersion == ClassicRuneGameRulesetVersion &&
           journalInfo.topologyId == ClassicFreeForAllTopologyId &&
           journalInfo.topologyVersion == ClassicFreeForAllTopologyVersion &&
           journalInfo.contentPackageId == ClassicContentPackageId &&
           journalInfo.contentPackageVersion == ClassicContentPackageVersion &&
           journalInfo.contiguousToCheckpoint &&
           GameData::authoritativeState().toString() == stateBeforeJournalInspection,
           "replay inspection must expose library metadata without mutating game state");
    const JsonObject* journalRuleset = journal.getObject(RuneGameRulesetIdentityKey);
    const JsonObject* journalTopology = journal.getObject(MatchTopologyIdentityKey);
    const JsonObject* journalPackage = journal.getObject(ContentPackageIdentityKey);
    expect(journal.getInteger("schema") == 3 &&
           journal.getString("aiBehaviorProfile") == "aggressive" &&
           journal.getInteger("actionCount") == 1 &&
           journal.getBoolean("contiguousToCheckpoint") &&
           journalRuleset && journalRuleset->getString("id") == ClassicRuneGameRulesetId &&
           journalRuleset->getInteger("version") == ClassicRuneGameRulesetVersion &&
           journalTopology && journalTopology->getString("id") == ClassicFreeForAllTopologyId &&
           journalTopology->getInteger("version") == ClassicFreeForAllTopologyVersion &&
           journalPackage && journalPackage->getString("id") == ClassicContentPackageId &&
           journalPackage->getInteger("version") == ClassicContentPackageVersion,
           "forced AI doctrine must enter a contiguous versioned replay journal");

    replayError.clear();
    expect(Replay::run(journal, &replayError) &&
           Replay::authoritativeStateHash() == randomExpectedHash &&
           !AI::behaviorProfileOverrideEnabled(),
           "replay journal must restore RNG state, AI doctrine and its caller scope");

    const std::string stateBeforePlayback = GameData::authoritativeState().toString();
    {
        Replay::Playback playback;
        replayError.clear();
        expect(playback.open(journal, &replayError) && replayError.empty() &&
               playback.position() == 0 && playback.size() == 1 &&
               playback.phaseCount() == 1 &&
               GameData::authoritativeState().toString() == randomInitialState.toString(),
               "replay playback must open at its validated initial state");
        expect(playback.stepForward(&replayError) && playback.atEnd() &&
               Replay::authoritativeStateHash() == randomExpectedHash,
               "replay playback must advance one deterministic action");
        expect(playback.stepBackward(&replayError) && playback.atBeginning() &&
               GameData::authoritativeState().toString() == randomInitialState.toString(),
               "replay playback must rebuild the previous deterministic position");
        expect(playback.seek(playback.size(), &replayError) &&
               playback.nextPhase(&replayError) && playback.atEnd() &&
               playback.previousPhase(&replayError) && playback.atBeginning(),
               "replay playback phase navigation must clamp to valid boundaries");
    }
    expect(GameData::authoritativeState().toString() == stateBeforePlayback &&
           !AI::behaviorProfileOverrideEnabled(),
           "closing replay playback must restore the caller's game and AI scope");

    JsonObject divergentJournal = journal;
    JsonArray divergentSteps;
    if(const JsonArray* recordedSteps = journal.getArray("steps"))
    {
        for(std::size_t index = 0; index < recordedSteps->size(); ++index)
        {
            JsonObject step = *recordedSteps->getObject(index);
            if(index == 0) step.addString("expectedStateHash", "0000000000000000");
            divergentSteps.addObject(step);
        }
    }
    divergentJournal.addArray("steps", divergentSteps);
    Replay::Playback divergentPlayback;
    replayError.clear();
    expect(!divergentPlayback.open(divergentJournal, &replayError) &&
           divergentPlayback.failure().isValid() &&
           divergentPlayback.failure().kind == Replay::FailureKind::StateHashMismatch &&
           divergentPlayback.failure().step == 0 &&
           divergentPlayback.failure().expected == "0000000000000000" &&
           !divergentPlayback.failure().actual.empty(),
           "replay playback must expose the first deterministic mismatch as structured data");

    JsonObject legacyJournal = jsonObjectWithoutKey(journal, RuneGameRulesetIdentityKey);
    legacyJournal = jsonObjectWithoutKey(legacyJournal, MatchTopologyIdentityKey);
    legacyJournal = jsonObjectWithoutKey(legacyJournal, ContentPackageIdentityKey);
    const JsonObject* journalInitial = journal.getObject("initialState");
    expect(journalInitial != nullptr,
           "versioned replay journal must retain its initial state");
    if(journalInitial)
    {
        JsonObject legacyInitial =
            jsonObjectWithoutKey(*journalInitial, RuneGameRulesetIdentityKey);
        legacyInitial = jsonObjectWithoutKey(legacyInitial, MatchTopologyIdentityKey);
        legacyInitial = jsonObjectWithoutKey(legacyInitial, ContentPackageIdentityKey);
        legacyJournal.addObject("initialState", legacyInitial);
        replayError.clear();
        expect(Replay::run(legacyJournal, &replayError) &&
               Replay::authoritativeStateHash() == randomExpectedHash,
               "legacy Classic replay without ruleset metadata must remain playable");
    }

    JsonObject invalidProfileJournal = journal;
    invalidProfileJournal.addString("aiBehaviorProfile", "impossible");
    replayError.clear();
    expect(!Replay::run(invalidProfileJournal, &replayError) &&
           replayError.find("invalid AI behavior profile") != std::string::npos,
           "replay must reject an unknown AI doctrine instead of silently using Balanced");

    JsonObject unavailableRulesetJournal = journal;
    JsonObject unavailableRuleset;
    unavailableRuleset.addString("id", "removed-variant");
    unavailableRuleset.addInteger("version", 3);
    unavailableRulesetJournal.addObject(RuneGameRulesetIdentityKey, unavailableRuleset);
    replayError.clear();
    expect(!Replay::run(unavailableRulesetJournal, &replayError) &&
           replayError.find("removed-variant@3") != std::string::npos,
           "replay must reject an unavailable ruleset before applying any action");

    JsonObject unavailableTopologyJournal = journal;
    JsonObject unavailableTopology;
    unavailableTopology.addString("id", "removed-duel");
    unavailableTopology.addInteger("version", 3);
    unavailableTopologyJournal.addObject(MatchTopologyIdentityKey, unavailableTopology);
    replayError.clear();
    expect(!Replay::run(unavailableTopologyJournal, &replayError) &&
           replayError.find("removed-duel@3") != std::string::npos,
           "replay must reject an unavailable topology before applying any action");

    JsonObject unavailablePackageJournal = journal;
    JsonObject unavailablePackage;
    unavailablePackage.addString("id", "removed-package");
    unavailablePackage.addInteger("version", 4);
    unavailablePackageJournal.addObject(ContentPackageIdentityKey, unavailablePackage);
    replayError.clear();
    expect(!Replay::run(unavailablePackageJournal, &replayError) &&
           replayError.find("removed-package@4") != std::string::npos,
           "replay must reject an unavailable content package before applying any action");

    JsonObject tamperedRandomState = randomInitialState;
    JsonObject tamperedRng = *randomInitialState.getObject("gameplayRng");
    tamperedRng.addString("state", "1234");
    tamperedRandomState.addObject("gameplayRng", tamperedRng);
    const std::vector<Replay::Step> randomSteps = {
        Replay::Step(randomActor, randomDiscard, randomExpectedHash)
    };
    replayError.clear();
    expect(!Replay::run(tamperedRandomState, randomSteps, &replayError) &&
           replayError.find("state hash mismatch at step 0") != std::string::npos,
           "replay must detect a divergent gameplay RNG state");
}

void testGameplayRngState()
{
    GameplayRng::seed(UINT64_C(0xfedcba987654321));
    const int first = GameplayRng::uniform(-10, 10);
    const JsonObject checkpoint = GameplayRng::toJsonObject();
    const std::vector<int> expected = {
        GameplayRng::uniform(0, 1000000),
        GameplayRng::uniform(0, 1000000),
        GameplayRng::uniform(0, 1000000)
    };

    expect(-10 <= first && first <= 10,
           "gameplay RNG uniform result must stay inside inclusive bounds");
    expect(GameplayRng::fromJsonObject(checkpoint),
           "gameplay RNG checkpoint must restore");
    const std::vector<int> actual = {
        GameplayRng::uniform(0, 1000000),
        GameplayRng::uniform(0, 1000000),
        GameplayRng::uniform(0, 1000000)
    };
    expect(actual == expected && GameplayRng::draws() == 4 &&
           GameplayRng::initialSeed() == UINT64_C(0xfedcba987654321),
           "restored gameplay RNG must reproduce values and draw count");

    std::vector<int> firstShuffle = { 1, 2, 3, 4, 5, 6, 7, 8 };
    std::vector<int> secondShuffle = firstShuffle;
    GameplayRng::seed(777);
    GameplayRng::shuffle(firstShuffle.begin(), firstShuffle.end());
    GameplayRng::seed(777);
    GameplayRng::shuffle(secondShuffle.begin(), secondShuffle.end());
    expect(firstShuffle == secondShuffle,
           "gameplay RNG shuffle must be deterministic for a fixed seed");
}

#ifdef BUILD_DEBUG
void testDeveloperFastForward()
{
    GameplayRng::seed(UINT64_C(0x15d15d));
    const Avatar human(Avatar::Nucrus);
    GameData::initPersons(Person(human, Clan::Red, Wind::East));
    expect(GameData::initMahjong(),
           "developer fast-forward fixture must initialize Mahjong");

    const DeveloperTools::FastForwardResult result =
        DeveloperTools::fastForward(DeveloperTools::Command::FinishPhase, human);
    expect(result.success && result.menu == Menu::MahjongSummaryPart && 0 < result.ticks,
           "developer fast-forward must legally reach the next Mahjong summary");
    expect(GameData::developerAssisted() && !GameData::developerAutoplay(human),
           "developer fast-forward must mark the session and return human control");

    const JsonObject assistedState = GameData::authoritativeState();
    expect(assistedState.getBoolean("developerAssisted") &&
           GameData::restoreState(assistedState) && GameData::developerAssisted(),
           "developer-assisted state must remain marked across save restoration");
    const JsonObject replay = Replay::actionJournal(GameData::authoritativeState());
    expect(replay.getBoolean("developerAssisted"),
           "developer-assisted replay metadata must be explicit");

    GameplayRng::seed(UINT64_C(0x15d15e));
    GameData::initPersons(Person(human, Clan::Red, Wind::East));
    expect(GameData::initMahjong(),
           "developer round fast-forward fixture must initialize Mahjong");
    const DeveloperTools::FastForwardResult roundResult =
        DeveloperTools::fastForward(DeveloperTools::Command::FinishRound, human);
    expect(roundResult.success && roundResult.menu == Menu::MahjongPart &&
           0 < roundResult.ticks,
           "developer fast-forward must recognize the next Rune Game as a completed round");

    GameplayRng::seed(UINT64_C(0x15d15f));
    GameData::initPersons(Person(human, Clan::Red, Wind::East));
    expect(GameData::initMahjong(),
           "developer Adventure fast-forward fixture must initialize Mahjong");
    const DeveloperTools::FastForwardResult adventureResult =
        DeveloperTools::fastForward(DeveloperTools::Command::NextAdventure, human);
    expect(adventureResult.success && adventureResult.menu == Menu::AdventurePart &&
           0 < adventureResult.ticks,
           "developer fast-forward must stop at the beginning of the next Adventure phase");

    const DeveloperTools::FastForwardResult battleResult =
        DeveloperTools::fastForward(DeveloperTools::Command::BattleFixture, human);
    expect(battleResult.success && battleResult.menu == Menu::AdventurePart &&
           GameData::authoritativeState().getObject("battleSession") != nullptr &&
           GameData::currentPerson().avatar == human &&
           !GameData::developerAutoplay(human),
           "developer battle fixture must stop on the human seat's normal battle choice");

    ActionList cleanup;
    expect(GameData::client2Adventure(human,
                                      ClientBattleChoice(-1, -1, true), cleanup),
           "developer battle fixture must remain resolvable by the normal battle action");

    GameplayRng::seed(UINT64_C(0x15d160));
    GameData::initPersons(Person(human, Clan::Red, Wind::East));
    expect(GameData::initMahjong(),
           "developer battle fixture from Mahjong must initialize the Rune Game");
    const DeveloperTools::FastForwardResult directBattleResult =
        DeveloperTools::fastForward(DeveloperTools::Command::BattleFixture, human);
    expect(directBattleResult.success && directBattleResult.menu == Menu::AdventurePart &&
           GameData::authoritativeState().getObject("battleSession") != nullptr &&
           GameData::currentPerson().avatar == human,
           "developer battle fixture must open manual combat directly from the Rune Game");

    GameplayRng::seed(UINT64_C(0x15d161));
    GameData::initPersons(Person(human, Clan::Red, Wind::East));
    expect(GameData::initMahjong(),
           "developer final Rune fixture must initialize a base game");
    const DeveloperTools::FastForwardResult finalRuneResult =
        DeveloperTools::fastForward(DeveloperTools::Command::FinalRuneFixture, human);
    const JsonObject finalRuneState = GameData::authoritativeState();
    std::string finalRuneValidationError;
    expect(finalRuneResult.success && finalRuneResult.menu == Menu::MahjongPart &&
           finalRuneState.getString("wind:round") == "north" &&
           finalRuneState.getString("wind:part") == "north" &&
           finalRuneState.getBoolean("developerAssisted") &&
           Recovery::validateSaveState(finalRuneState, &finalRuneValidationError),
           "developer final Rune fixture must deal a valid last hand");

    GameplayRng::seed(UINT64_C(0x15d162));
    GameData::initPersons(Person(human, Clan::Red, Wind::East));
    expect(GameData::initMahjong(),
           "developer final Adventure fixture must initialize a base game");
    const DeveloperTools::FastForwardResult finalAdventureResult =
        DeveloperTools::fastForward(DeveloperTools::Command::FinalAdventureFixture, human);
    const JsonObject finalAdventureState = GameData::authoritativeState();
    std::string finalAdventureValidationError;
    expect(finalAdventureResult.success && finalAdventureResult.menu == Menu::AdventurePart &&
           finalAdventureState.getString("wind:round") == "north" &&
           finalAdventureState.getString("wind:part") == "north" &&
           Recovery::validateSaveState(finalAdventureState, &finalAdventureValidationError),
           "developer final Adventure fixture must open a valid last map phase");

    const DeveloperTools::FastForwardResult finalScoreResult =
        DeveloperTools::fastForward(DeveloperTools::Command::FinishGame, human);
    expect(finalScoreResult.success && finalScoreResult.menu == Menu::GameSummaryPart &&
           0 < finalScoreResult.ticks,
           "developer final Adventure fixture must legally reach final scoring");
    const std::vector<ReplayFiles::Info> archivedReplays = ReplayFiles::inspect();
    expect(std::any_of(archivedReplays.begin(), archivedReplays.end(),
        [](const ReplayFiles::Info & info)
        {
            return info.valid && info.journal.developerAssisted &&
                   0 < info.journal.actionCount && info.journal.contiguousToCheckpoint;
        }), "developer Finish Game must archive a playable replay");
}
#endif

int runFixedSeedReplaySelfTest(const std::string & replayDirectory = std::string())
{
    constexpr uint64_t seed = UINT64_C(0x123456789abcdef);
    constexpr const char* expectedHash = "5f1012c76a1d1638";

    GameplayRng::seed(seed);
    GameData::initPersons(Person(Avatar::Nucrus, Clan::Red, Wind::East));
    if(!GameData::initMahjong())
    {
        std::cerr << "FAIL: fixed-seed replay could not initialize Mahjong\n";
        return 1;
    }

    LocalPlayer* player = GameData::gamers.playerOfWind(Wind::East);
    if(!player)
    {
        std::cerr << "FAIL: fixed-seed replay East player is missing\n";
        return 1;
    }

    player->affected.insert(AffectedSpell(Spell(Spell::RandomDiscard), 1));
    const Avatar actor = player->avatar;
    const ClientDropIndex randomDiscard(0);
    ActionList actions;
    if(!GameData::client2Mahjong(actor, randomDiscard, actions))
    {
        std::cerr << "FAIL: fixed-seed Random Discard action was rejected\n";
        return 1;
    }

    const JsonObject finalState = GameData::authoritativeState();
    const std::string actualHash = Replay::authoritativeStateHash();
    JsonObject journal = Replay::actionJournal(finalState);
    if(!replayDirectory.empty())
        journal.addBoolean("developerAssisted", true);
    std::string replayError;
    if(!Replay::run(journal, &replayError) || Replay::authoritativeStateHash() != actualHash)
    {
        std::cerr << "FAIL: fixed-seed journal replay diverged: " << replayError << '\n';
        return 1;
    }

    std::cout << "fixed-seed replay hash: " << actualHash << '\n';
    if(actualHash != expectedHash)
    {
        std::cerr << "FAIL: expected fixed-seed replay hash " << expectedHash
                  << ", got " << actualHash << '\n';
        return 1;
    }

    if(!replayDirectory.empty())
    {
        std::string replayFile;
        replayError.clear();
        if(!ReplayFiles::writeAutomatic(replayDirectory, journal, &replayFile, &replayError))
        {
            std::cerr << "FAIL: unable to write test replay: " << replayError << '\n';
            return 1;
        }
        std::cout << "test replay: " << replayFile << '\n';
    }

    return 0;
}

int runSettingsPersistenceSelfTest()
{
    const char* settingsFile = Systems::environment("FOUR_WINDS_SETTINGS_FILE");
    if(!settingsFile || !*settingsFile)
    {
        std::cerr << "FAIL: FOUR_WINDS_SETTINGS_FILE is required\n";
        return 1;
    }

    const std::string nativeSettingsFile =
        std::filesystem::path(settingsFile).make_preferred().string();
    Systems::setEnvironment("FOUR_WINDS_SETTINGS_FILE", nativeSettingsFile.c_str());
    settingsFile = Systems::environment("FOUR_WINDS_SETTINGS_FILE");

    std::error_code ec;
    std::filesystem::remove_all(std::filesystem::path(settingsFile).parent_path(), ec);

    Settings::setLanguage("ru_RU");
    Settings::setGameSpeed("fast");
    Settings::setContentTheme("alternate");
    Settings::setAIDifficulty(AI::Difficulty::Unfair);
    Settings::setMusicVolume(35);
    Settings::setEffectsVolume(60);
    Settings::setVoiceVolume(85);
    Settings::setSoundGuardianRules(true);
    Settings::setFullscreen(true);
    Settings::setWindowScale(150);
    std::string error;
    if(Settings::language() != "ru" || !Settings::write(&error))
    {
        std::cerr << "FAIL: settings write failed: " << error << '\n';
        return 1;
    }

    Settings::setLanguage("en");
    Settings::setGameSpeed("normal");
    Settings::setContentTheme("default");
    if(Settings::contentTheme() != "classic")
    {
        std::cerr << "FAIL: legacy default theme alias must migrate to classic\n";
        return 1;
    }
    Settings::setAIDifficulty(AI::Difficulty::Easy);
    Settings::setMusicVolume(100);
    Settings::setEffectsVolume(100);
    Settings::setVoiceVolume(100);
    Settings::setSoundGuardianRules(false);
    Settings::setFullscreen(false);
    Settings::setWindowScale(100);
    if(!Settings::read() || Settings::language() != "ru" || Settings::gameSpeed() != "fast" ||
       Settings::contentTheme() != "alternate" ||
       Settings::aiDifficulty() != AI::Difficulty::Unfair ||
       !Settings::music() || Settings::musicVolume() != 35 ||
       !Settings::sound() || Settings::effectsVolume() != 60 || Settings::voiceVolume() != 85 ||
       !Settings::soundGuardianRules() || !Settings::fullscreen() ||
       Settings::windowScale() != 150)
    {
        std::cerr << "FAIL: settings did not survive a read/write cycle\n";
        return 1;
    }

    const JsonObject saved = JsonContentFile(settingsFile).toObject();
    if(!saved.isValid() || saved.getString("language") != "ru" ||
       saved.getString("game:speed") != "fast" ||
       saved.getString("content:theme") != "alternate" ||
       saved.getString("ai:difficulty") != "unfair" ||
       !saved.getBoolean("music", false) || saved.getInteger("music:volume", -1) != 35 ||
       !saved.getBoolean("sound", false) || saved.getInteger("sound:volume", -1) != 60 ||
       saved.getInteger("voice:volume", -1) != 85 ||
       !saved.getBoolean("display:fullscreen", false) ||
       saved.getInteger("display:window_scale", -1) != 150 ||
       !saved.getBoolean("sound:guardianrules", false))
    {
        std::cerr << "FAIL: settings.json contract is invalid\n";
        return 1;
    }

    JsonObject legacy;
    legacy.addString("language", "en");
    legacy.addString("content:theme", "default");
    legacy.addBoolean("music", false);
    legacy.addBoolean("sound", false);
    if(!Systems::saveString2File(legacy.toString(), settingsFile) || !Settings::read() ||
       Settings::musicVolume() != 0 || Settings::effectsVolume() != 0 ||
       Settings::voiceVolume() != 0 || Settings::music() || Settings::sound() ||
       Settings::windowScale() != 100 ||
       Settings::contentTheme() != "classic" ||
       Settings::aiDifficulty() != AI::Difficulty::Normal)
    {
        std::cerr << "FAIL: legacy boolean audio settings are not load compatible\n";
        return 1;
    }

    Settings::setMusicVolume(-5);
    Settings::setEffectsVolume(125);
    Settings::setVoiceVolume(50);
    Settings::setWindowScale(189);
    if(Settings::musicVolume() != 0 || Settings::effectsVolume() != 100 ||
       Settings::voiceVolume() != 50 || Settings::mixerVolume(0) != 0 ||
       Settings::mixerVolume(50) != 64 || Settings::mixerVolume(100) != 128 ||
       Settings::windowScale() != 200)
    {
        std::cerr << "FAIL: audio volume normalization is invalid\n";
        return 1;
    }

    Settings::setLanguage("unsupported");
    if(Settings::language() != "en")
    {
        std::cerr << "FAIL: unsupported languages must fall back to English\n";
        return 1;
    }

    Settings::setGameSpeed("unsupported");
    if(Settings::gameSpeed() != "classic" || Settings::presentationDelay(100) != 175)
    {
        std::cerr << "FAIL: unsupported game speeds must fall back to Classic\n";
        return 1;
    }

    Settings::setGameSpeed("normal");
    if(Settings::presentationDelay(100) != 100)
    {
        std::cerr << "FAIL: Normal speed must preserve presentation timing\n";
        return 1;
    }

    Settings::setGameSpeed("fast");
    if(Settings::presentationDelay(100) != 55)
    {
        std::cerr << "FAIL: Fast speed must shorten presentation timing\n";
        return 1;
    }

    std::cout << "settings persistence: ok\n";
    return 0;
}

void testGateMovement()
{
    BattleArmy supportGate;
    BattleParty supportSource(Clan::Red, Land::Corzen);
    BattleCreature carol = combatCreature(Clan::Red, Creature::GreatCarol, 280, 3, 0, 4, 4, 1);
    BattleCreature ally = combatCreature(Clan::Red, Creature::Durlock, 281, 2, 0, 2, 3, 2);
    carol.setSelected(false);
    ally.setSelected(true);
    expect(supportSource.join(carol) && supportSource.join(ally), "Gate support fixture must join");
    supportGate.push_back(supportSource);

    expect(supportGate.canGateParty(Land::Corzen, Land::Maithaius),
           "Carol must open a Gate to a friendly summoning circle");
    const std::vector<int> supportMoved =
        supportGate.moveSelectedCreatures(Land::Corzen, Land::Maithaius);
    const BattleParty* supportOrigin = supportGate.findPartyConst(Land::Corzen);
    const BattleCreature* gatedAlly = supportGate.findBattleUnitConst(281);
    expect(supportMoved.size() == 1 && supportMoved.front() == 281 && supportOrigin &&
           supportOrigin->findBattleUnitConst(280) &&
           supportGate.findPartyConst(Land::Maithaius)->findBattleUnitConst(281),
           "Carol must be able to Gate selected allies while staying behind");
    expect(gatedAlly && gatedAlly->freeMovePoint() == 1,
           "Gate must consume exactly one movement point from each moved creature");

    BattleArmy groupGate;
    BattleParty groupSource(Clan::Red, Land::Corzen);
    BattleCreature groupCarol = combatCreature(Clan::Red, Creature::GreatCarol, 282, 3, 0, 4, 4, 1);
    BattleCreature groupAlly = combatCreature(Clan::Red, Creature::Durlock, 283, 2, 0, 2, 3, 1);
    groupCarol.setSelected(true);
    groupAlly.setSelected(true);
    expect(groupSource.join(groupCarol) && groupSource.join(groupAlly), "Gate group fixture must join");
    groupGate.push_back(groupSource);

    const std::vector<int> groupMoved =
        groupGate.moveSelectedCreatures(Land::Corzen, Land::PyramusReach);
    const BattleParty* groupDestination = groupGate.findPartyConst(Land::PyramusReach);
    expect(groupMoved.size() == 2 && groupMoved.front() == 283 && groupMoved.back() == 282 &&
           groupDestination && groupDestination->findBattleUnitConst(282) &&
           groupDestination->findBattleUnitConst(283) && !groupGate.findPartyConst(Land::Corzen),
           "Gate must move companions before Carol so the whole selected group arrives");
    expect(groupDestination && groupDestination->findBattleUnitConst(282)->freeMovePoint() == 0 &&
           groupDestination->findBattleUnitConst(283)->freeMovePoint() == 0,
           "Gate must charge one movement point to Carol and every companion");

    BattleArmy invalidGate;
    BattleParty invalidSource(Clan::Red, Land::Corzen);
    BattleCreature invalidCarol = combatCreature(Clan::Red, Creature::GreatCarol, 284, 3, 0, 4, 4, 1);
    invalidCarol.setSelected(true);
    expect(invalidSource.join(invalidCarol), "invalid Gate fixture must join");
    invalidGate.push_back(invalidSource);
    expect(!invalidGate.canGateParty(Land::Corzen, Land::Baliphon) &&
           !invalidGate.canGateParty(Land::Corzen, Land::Inkartha),
           "Gate must reject friendly non-power lands and enemy summoning circles");
    expect(invalidGate.moveSelectedCreatures(Land::Corzen, Land::Baliphon).empty(),
           "an invalid Gate destination must not bypass normal movement distance");

    expect(invalidGate.canGateParty(Land::Corzen, Land::TowerOf4Winds) &&
           invalidGate.moveSelectedCreatures(Land::Corzen, Land::TowerOf4Winds).size() == 1,
           "Gate must allow Carol to enter the Tower of Four Winds");

    LocalPlayer gateAI;
    gateAI.avatar = Avatar::Logun;
    gateAI.clan = Clan::Red;
    gateAI.wind = Wind::East;
    BattleParty aiSource(Clan::Red, Land::Corzen);
    expect(aiSource.join(combatCreature(Clan::Red, Creature::GreatCarol, 285, 20, 0, 10, 10, 1)) &&
           aiSource.join(combatCreature(Clan::Red, Creature::Durlock, 286, 20, 2, 10, 10, 1)),
           "Gate AI fixture must join");
    gateAI.army.push_back(aiSource);
    Lands gateTargets;
    gateTargets << Land::Sunspot;
    const AI::AdventureMovePlan gatePlan = AI::chooseAdventureMove(
        gateAI, *gateAI.army.findPartyConst(Land::Corzen), AI::BehaviorProfile::Balanced,
        gateTargets);
    expect(gatePlan.isValid() && gatePlan.target == Land(Land::Sunspot) &&
           gatePlan.destination == Land(Land::PyramusReach) && !gatePlan.engagesEnemy,
           "Adventure AI must use Gate when a friendly summoning circle shortens its route");
}

void testLuckAbility()
{
    CroupierSet wall;
    wall.bank.clear();
    wall.trash.clear();
    wall.luckDraw.clear();
    wall.bank.push_back(Stone(Stone::Number3));
    wall.bank.push_back(Stone(Stone::Skull1));
    wall.bank.push_back(Stone(Stone::Sword4));

    expect(wall.beginLuckDraw(), "Luck must reveal two runes when the wall has enough runes");
    expect(wall.luckChoices().size() == 2 &&
           wall.luckChoices()[0] == Stone(Stone::Sword4) &&
           wall.luckChoices()[1] == Stone(Stone::Skull1),
           "Luck must reveal runes in their original draw order");
    expect(!wall.resolveLuckDraw(-1).isValid() && wall.hasLuckDraw(),
           "an invalid Luck choice must not consume either rune");

    const Stone chosen = wall.resolveLuckDraw(1);
    expect(chosen == Stone(Stone::Skull1) && !wall.hasLuckDraw() && wall.bank.size() == 2,
           "Luck must keep exactly the selected rune");
    expect(wall.bank.front() == Stone(Stone::Sword4) &&
           wall.bank.back() == Stone(Stone::Number3),
           "Luck must return the rejected rune to the bottom of the wall");
    const VecStones resolvedBank = wall.bank;
    expect(!wall.resolveLuckDraw(0).isValid() && wall.bank == resolvedBank,
           "a repeated Luck response must not consume another rune");

    expect(wall.beginLuckDraw(), "Luck reset fixture must start a pending choice");
    wall.reset();
    expect(!wall.hasLuckDraw() && wall.bank.size() == 136 && wall.trash.empty(),
           "a new round must clear pending Luck state and rebuild the wall");

    LocalData roundStart;
    roundStart.partWind = Wind(Wind::East);
    roundStart.stoneLastCount = GAME_STONE_MAX;
    expect(roundStart.newRound(),
           "the client must recognize the server rune count as a new round");

    CroupierSet saved;
    saved.bank.clear();
    saved.trash.clear();
    saved.luckDraw.clear();
    saved.bank.push_back(Stone(Stone::Dragon1));
    saved.bank.push_back(Stone(Stone::Number7));
    saved.bank.push_back(Stone(Stone::Sword8));
    expect(saved.beginLuckDraw(), "Luck save fixture must start a pending choice");
    const CroupierSet restored = CroupierSet::fromJsonObject(saved.toJsonObject());
    expect(restored.bank == saved.bank && restored.luckChoices() == saved.luckChoices(),
           "a pending Luck choice and wall order must survive save/load");

    CroupierSet malformed;
    malformed.bank.clear();
    malformed.trash.clear();
    malformed.luckDraw.clear();
    malformed.bank.push_back(Stone(Stone::Wind1));
    malformed.luckDraw.push_back(Stone(Stone::Dragon2));
    const CroupierSet repaired = CroupierSet::fromJsonObject(malformed.toJsonObject());
    expect(!repaired.hasLuckDraw() && repaired.bank.size() == 2 &&
           repaired.bank.front() == Stone(Stone::Dragon2),
           "a malformed pending Luck choice must restore its rune to the wall");

    LocalPlayer nucrus;
    nucrus.avatar = Avatar::Nucrus;
    nucrus.clan = Clan::Red;
    nucrus.wind = Wind::East;
    CroupierSet nucrusWall;
    nucrusWall.bank.clear();
    nucrusWall.trash.clear();
    nucrusWall.luckDraw.clear();
    nucrusWall.bank.push_back(Stone(Stone::Wind2));
    nucrusWall.bank.push_back(Stone(Stone::Number5));
    nucrusWall.bank.push_back(Stone(Stone::Sword6));
    expect(nucrus.newTurnEvent(nucrusWall, false) && nucrusWall.hasLuckDraw() &&
           !nucrus.newStone.isValid(),
           "Nucrus must choose between two runes on an unforced turn draw");

    LocalPlayer directedNucrus;
    directedNucrus.avatar = Avatar::Nucrus;
    directedNucrus.clan = Clan::Red;
    directedNucrus.wind = Wind::East;
    expect(directedNucrus.mahjongApplySpell(Spell(Spell::DrawNumber)),
           "directed draw fixture must apply Summon Number Rune");
    CroupierSet directedWall;
    directedWall.bank.clear();
    directedWall.trash.clear();
    directedWall.luckDraw.clear();
    directedWall.bank.push_back(Stone(Stone::Number8));
    directedWall.bank.push_back(Stone(Stone::Skull4));
    expect(!directedNucrus.newTurnEvent(directedWall, false) &&
           directedNucrus.newStone.isNumber() && !directedWall.hasLuckDraw(),
           "directed draw spells must take precedence over Luck");

    LocalPlayers openingDeal;
    const Avatar::avatar_t avatars[] = {
        Avatar::Nucrus, Avatar::Orachi, Avatar::Lakkho, Avatar::Dayla
    };
    for(int index = 0; index < 4; ++index)
    {
	LocalPlayer player;
	player.avatar = avatars[index];
	player.clan = Clan(static_cast<Clan::clan_t>(Clan::Red + index));
	player.wind = Wind(static_cast<Wind::wind_t>(Wind::East + index));
	openingDeal.push_back(player);
    }
    CroupierSet openingWall;
    openingDeal.distributeStones(openingWall);
    expect(openingDeal.front().stones.size() == GAME_SET_COUNT && !openingWall.hasLuckDraw(),
           "Luck must not trigger during the initial thirteen-rune deal");

    const RuneGameRuleset & classicRuleset = classicRuneGameRuleset();
    expect(classicRuleset.wallStoneIds().size() == 34 &&
           classicRuleset.wallCopies() == 4 &&
           classicRuleset.initialHandSize() == GAME_SET_COUNT,
           "Classic ruleset must own the established 136-rune wall and thirteen-rune hand");

    const AlternateRuneGameRuleset alternateRuleset;
    CroupierSet alternateWall;
    alternateWall.reset(alternateRuleset);
    expect(alternateWall.bank.size() == 8 &&
           std::count(alternateWall.bank.begin(), alternateWall.bank.end(), Stone(Stone::Skull1)) == 4 &&
           std::count(alternateWall.bank.begin(), alternateWall.bank.end(), Stone(Stone::Sword1)) == 4,
           "an injected ruleset must control wall composition and copy count");

    LocalPlayers alternateDeal = openingDeal;
    for(auto & player : alternateDeal) player.stones.clear();
    alternateDeal.distributeStones(alternateWall, alternateRuleset);
    expect(std::all_of(alternateDeal.begin(), alternateDeal.end(), [](const LocalPlayer & player)
           {
               return player.stones.size() == 2;
           }) && alternateWall.bank.empty(),
           "an injected ruleset must control the initial hand size without changing Classic");

    GameStones hand;
    hand.add(GameStone(Stone(Stone::Sword2), false));
    hand.add(GameStone(Stone(Stone::Sword3), false));
    VecStones choices;
    choices.push_back(Stone(Stone::Sword4));
    choices.push_back(Stone(Stone::Dragon1));
    expect(AI::mahjongLuckChoice(hand, choices, VecStones(), WinRules()) == 0,
           "Luck AI must prefer a rune that completes a sequence");

    const MahjongLuckChoice serverChoice(Wind(Wind::East), choices);
    const ClientLuckChoice clientChoice(1);
    expect(serverChoice.type() == Action::MahjongLuckChoice &&
           serverChoice.currentWind() == Wind(Wind::East) &&
           serverChoice.choices() == choices &&
           clientChoice.type() == Action::ClientLuckChoice && clientChoice.index() == 1,
           "Luck client/server messages must preserve their choice payloads");
}

void testAvatarPassives()
{
    LocalPlayer logun;
    logun.avatar = Avatar::Logun;
    logun.clan = Clan::Red;
    logun.wind = Wind::East;

    BattleParty band(Clan::Red, Land::Corzen);
    BattleCreature badlyWounded(Clan::Red, Creature::Durlock, 380);
    BattleCreature lightlyWounded(Clan::Red, Creature::Durlock, 381);
    badlyWounded.applyDamage(3);
    lightlyWounded.applyDamage(1);
    expect(band.join(badlyWounded) && band.join(lightlyWounded),
           "Bard healing fixture must join");
    logun.army.push_back(band);
    logun.initAdventurePart();

    const BattleCreature* healedThree = logun.army.findBattleUnitConst(380);
    const BattleCreature* healedToFull = logun.army.findBattleUnitConst(381);
    expect(healedThree && healedThree->loyalty() == 3,
           "Bard must restore exactly two loyalty at map phase start");
    expect(healedToFull && healedToFull->loyalty() == healedToFull->baseLoyalty(),
           "Bard healing must not exceed base loyalty");

    BattleCreature ordinary(Clan::Red, Creature::Durlock, 382);
    ordinary.applyDamage(3);
    ordinary.initAdventurePart(Ability::None);
    expect(ordinary.loyalty() == 1,
           "creatures without Bard or a healing speciality must not heal automatically");

    const LocalPlayers savedPlayers = GameData::gamers;
    GameData::gamers.clear();

    LocalPlayer orachi;
    orachi.avatar = Avatar::Orachi;
    orachi.clan = Clan::Red;
    orachi.wind = Wind::East;
    BattleParty ownHiddenParty(Clan::Red, Land::Maithaius);
    expect(ownHiddenParty.join(BattleCreature(Clan::Red, Creature::Shadow, 389)),
           "own invisibility fixture must join");
    orachi.army.push_back(ownHiddenParty);
    GameData::gamers.push_back(orachi);

    LocalPlayer ziag;
    ziag.avatar = Avatar::Ziag;
    ziag.clan = Clan::Aqua;
    ziag.wind = Wind::South;
    GameData::gamers.push_back(ziag);

    LocalPlayer lakkho;
    lakkho.avatar = Avatar::Lakkho;
    lakkho.clan = Clan::Yellow;
    lakkho.wind = Wind::West;
    BattleParty hiddenParty(Clan::Yellow, Land::Zubrus);
    expect(hiddenParty.join(BattleCreature(Clan::Yellow, Creature::Shadow, 390)),
           "Monocle invisibility fixture must join");
    lakkho.army.push_back(hiddenParty);
    BattleParty towerHiddenParty(Clan::Yellow, Land::TowerOf4Winds);
    expect(towerHiddenParty.join(BattleCreature(Clan::Yellow, Creature::Shadow, 391)),
           "Tower invisibility fixture must join");
    lakkho.army.push_back(towerHiddenParty);
    GameData::gamers.push_back(lakkho);

    LocalPlayer dayla;
    dayla.avatar = Avatar::Dayla;
    dayla.clan = Clan::Purple;
    dayla.wind = Wind::North;
    BattleParty thirdPartyDetector(Clan::Purple, Land::Corzen);
    expect(thirdPartyDetector.join(BattleCreature(Clan::Purple, Creature::AdventureParty, 392)),
           "third-party See Invisible fixture must join");
    dayla.army.push_back(thirdPartyDetector);
    GameData::gamers.push_back(dayla);

    const LocalData ordinaryView = GameData::toLocalData(Avatar(Avatar::Orachi));
    const LocalData monocleView = GameData::toLocalData(Avatar(Avatar::Ziag));
    const LocalData detectorOwnerView = GameData::toLocalData(Avatar(Avatar::Dayla));
    expect(!ordinaryView.playerOfAvatar(Avatar(Avatar::Lakkho)).army.findBattleUnitConst(390),
           "another player's adjacent detector must not reveal hidden creatures to the observer");
    expect(monocleView.playerOfAvatar(Avatar(Avatar::Lakkho)).army.findBattleUnitConst(390),
           "Monocle must reveal invisible creatures anywhere on the map");
    expect(detectorOwnerView.playerOfAvatar(Avatar(Avatar::Lakkho)).army.findBattleUnitConst(390),
           "an observer's detector must work even while occupying another clan's territory");
    expect(ordinaryView.playerOfAvatar(Avatar(Avatar::Lakkho)).army.findBattleUnitConst(391),
           "invisible creatures at the Tower of Four Winds must remain public");
    expect(ordinaryView.playerOfAvatar(Avatar(Avatar::Orachi)).army.findBattleUnitConst(389),
           "an observer must always receive their own invisible creatures");
    expect(GameData::gamers.playerOfAvatar(Avatar(Avatar::Lakkho))->army.findBattleUnitConst(390),
           "visibility filtering must not mutate the authoritative army");

    LocalPlayer* ordinaryPlayer = GameData::gamers.playerOfAvatar(Avatar(Avatar::Orachi));
    LocalPlayer* detectorOwner = GameData::gamers.playerOfAvatar(Avatar(Avatar::Dayla));
    expect(ordinaryPlayer != nullptr, "ordinary visibility observer must exist");
    expect(detectorOwner != nullptr, "third-party visibility observer must exist");
    if(ordinaryPlayer && detectorOwner)
    {
        detectorOwner->army.clear();
        ordinaryPlayer->army.clear();
        BattleParty unrelatedDetector(Clan::Red, Land::Corzen);
        expect(unrelatedDetector.join(BattleCreature(Clan::Red, Creature::AdventureParty, 395)),
               "unrelated See Invisible fixture must join");
        ordinaryPlayer->army.push_back(unrelatedDetector);
        const LocalData unrelatedView = GameData::toLocalData(Avatar(Avatar::Dayla));
        expect(!unrelatedView.playerOfAvatar(Avatar(Avatar::Lakkho)).army.findBattleUnitConst(390),
               "a detector owned by another player must not leak visibility to the observer");

        ordinaryPlayer->army.clear();
        BattleParty farDetector(Clan::Red, Land::Maithaius);
        expect(farDetector.join(BattleCreature(Clan::Red, Creature::AdventureParty, 393)),
               "non-adjacent See Invisible fixture must join");
        ordinaryPlayer->army.push_back(farDetector);
        const LocalData farView = GameData::toLocalData(Avatar(Avatar::Orachi));
        expect(!farView.playerOfAvatar(Avatar(Avatar::Lakkho)).army.findBattleUnitConst(390),
               "See Invisible must not reveal creatures outside adjacent territories");

        ordinaryPlayer->army.clear();
        BattleParty invadingDetector(Clan::Red, Land::Inkartha);
        expect(invadingDetector.join(BattleCreature(Clan::Red, Creature::AdventureParty, 394)),
               "observer-specific See Invisible fixture must join");
        ordinaryPlayer->army.push_back(invadingDetector);
        const LocalData adjacentView = GameData::toLocalData(Avatar(Avatar::Orachi));
        expect(adjacentView.playerOfAvatar(Avatar(Avatar::Lakkho)).army.findBattleUnitConst(390),
               "the observer's adjacent detector must reveal invisibility even from invaded land");
    }
    GameData::gamers = savedPlayers;

    LocalPlayer javed;
    javed.avatar = Avatar::Javed;
    javed.clan = Clan::Red;
    javed.wind = Wind::South;
    javed.points = 500;
    expect(!javed.mahjongApplySpell(Spell(Spell::Silence), Avatar(Avatar::Orachi)) &&
           !javed.isAffectedSpell(Spell::Silence),
           "Telepath must reject newly cast Silence");

    javed.affected.insert(AffectedSpell(Spell(Spell::Silence), 3));
    expect(!javed.isSilenced(),
           "Telepath must ignore Silence restored from an older save");
    expect(javed.allowCastSpell(Spell(Spell::Healing)),
           "Telepath must retain spell casting while Silence is present");

    LocalPlayer chao = javed;
    chao.stones.add(GameStone(Stone::Sword1));
    chao.stones.add(GameStone(Stone::Sword3));
    expect(chao.isMahjongChao(Wind(Wind::East), Stone(Stone::Sword2)),
           "Telepath must retain Chao while Silence is present");

    LocalPlayer pung = javed;
    pung.stones.add(GameStone(Stone::Sword4));
    pung.stones.add(GameStone(Stone::Sword4));
    expect(pung.isMahjongPung(Wind(Wind::East), Stone(Stone::Sword4)),
           "Telepath must retain Pung while Silence is present");

    LocalPlayer kong = javed;
    kong.stones.add(GameStone(Stone::Sword5));
    kong.stones.add(GameStone(Stone::Sword5));
    kong.stones.add(GameStone(Stone::Sword5));
    expect(kong.isMahjongKong1(Wind(Wind::East), Stone(Stone::Sword5)),
           "Telepath must retain Kong while Silence is present");

    LocalPlayer game = javed;
    const Stone::stone_t winningStones[] = {
        Stone::Skull1, Stone::Skull2, Stone::Skull3,
        Stone::Sword1, Stone::Sword2, Stone::Sword3,
        Stone::Number1, Stone::Number2, Stone::Number3,
        Stone::Dragon1, Stone::Dragon1, Stone::Dragon1,
        Stone::WindEast
    };
    for(const Stone::stone_t stone : winningStones) game.stones.add(GameStone(stone));
    expect(game.isWinMahjong(Wind(Wind::East), Wind(Wind::East), Stone(Stone::WindEast)),
           "Telepath must retain Game while Silence is present");
    expect(!game.isWinMahjong(Wind(Wind::South), Wind(Wind::East), Stone(Stone::WindEast)),
           "a player must not claim Game on their own discard");

    game.newStone = GameStone(Stone(Stone::WindEast), false);
    expect(game.isWinMahjong(Wind(Wind::South), Wind(Wind::East), Stone()),
           "a valid self-drawn Game must remain available");

    LocalPlayer ordinaryWizard;
    ordinaryWizard.avatar = Avatar::Lakkho;
    ordinaryWizard.points = 500;
    expect(ordinaryWizard.mahjongApplySpell(Spell(Spell::Silence), Avatar(Avatar::Orachi)) &&
           ordinaryWizard.isSilenced() &&
           !ordinaryWizard.allowCastSpell(Spell(Spell::Healing)),
           "Silence must still block a non-Telepath wizard");
}

void testMahjongCallPlanning()
{
    const RuneGameRuleset & classicRuleset = classicRuneGameRuleset();
    const AlternateRuneGameRuleset alternateRuleset;
    expect(classicRuleset.callChoicePriority(RuneGameCall::Kong) >
               classicRuleset.callChoicePriority(RuneGameCall::Pung) &&
           classicRuleset.callChoicePriority(RuneGameCall::Pung) >
               classicRuleset.callChoicePriority(RuneGameCall::Chao),
           "Classic call choice priority must remain Kong, Pung, Chao");
    expect(classicRuleset.discardClaimPriority(RuneGameCall::Game) >
               classicRuleset.discardClaimPriority(RuneGameCall::Pung) &&
           classicRuleset.discardClaimPriority(RuneGameCall::Pung) ==
               classicRuleset.discardClaimPriority(RuneGameCall::Kong) &&
           classicRuleset.discardClaimPriority(RuneGameCall::Pung) >
               classicRuleset.discardClaimPriority(RuneGameCall::Chao),
           "Classic discard claims must keep Game first and Pung/Kong above Chao");

    LocalPlayer pung;
    pung.avatar = Avatar::Orachi;
    pung.clan = Clan::Red;
    pung.wind = Wind::South;
    pung.stones.add(GameStone(Stone::Dragon1));
    pung.stones.add(GameStone(Stone::Dragon1));

    const AI::MahjongCallPlan ordinaryPung = AI::chooseMahjongCall(
        pung, Wind(Wind::East), Stone(Stone::Dragon1), AI::StrategicIntent());
    expect(ordinaryPung.type == AI::MahjongCallType::Pung && 0 < ordinaryPung.score,
           "Mahjong AI must accept a useful Pung when it does not sacrifice a strategic rune goal");

    LocalPlayer singleMatch = pung;
    singleMatch.stones.pop_back();
    expect(!singleMatch.isMahjongPung(Wind(Wind::East), Stone(Stone::Dragon1),
                                      classicRuleset) &&
           singleMatch.isMahjongPung(Wind(Wind::East), Stone(Stone::Dragon1),
                                     alternateRuleset),
           "an injected ruleset must control Pung legality without changing Classic");

    AI::StrategicIntent protectedIntent;
    AI::RuneGoal protectedDragon;
    protectedDragon.required << Stone(Stone::Dragon1);
    protectedDragon.score = 300;
    protectedIntent.runeGoals.push_back(protectedDragon);
    const AI::MahjongCallPlan protectedPung = AI::chooseMahjongCall(
        pung, Wind(Wind::East), Stone(Stone::Dragon1), protectedIntent);
    expect(protectedPung.type == AI::MahjongCallType::Pass,
           "Mahjong AI must pass when exposing a Pung would destroy a much stronger rune goal");

    LocalPlayer kong = pung;
    kong.stones.add(GameStone(Stone::Dragon1));
    const AI::MahjongCallPlan ordinaryKong = AI::chooseMahjongCall(
        kong, Wind(Wind::East), Stone(Stone::Dragon1), AI::StrategicIntent());
    expect(ordinaryKong.type == AI::MahjongCallType::Kong,
           "Mahjong AI must retain Kong as the strongest ordinary matching-rune call");

    LocalPlayer chao;
    chao.avatar = Avatar::Orachi;
    chao.clan = Clan::Red;
    chao.wind = Wind::South;
    chao.stones.add(GameStone(Stone::Sword1));
    chao.stones.add(GameStone(Stone::Sword2));
    chao.stones.add(GameStone(Stone::Sword4));
    chao.stones.add(GameStone(Stone::Sword5));

    AI::StrategicIntent chaoIntent;
    AI::RuneGoal protectedSequence;
    protectedSequence.required << Stone(Stone::Sword1) << Stone(Stone::Sword2);
    protectedSequence.score = 400;
    chaoIntent.runeGoals.push_back(protectedSequence);
    const AI::MahjongCallPlan selectedChao = AI::chooseMahjongCall(
        chao, Wind(Wind::East), Stone(Stone::Sword3), chaoIntent);
    expect(selectedChao.type == AI::MahjongCallType::Chao && selectedChao.variant == 2,
           "Mahjong AI must choose the Chao variant that preserves its strategic runes");
    const AI::MahjongCallPlan alternateChao = AI::chooseMahjongCall(
        chao, Wind(Wind::East), Stone(Stone::Sword3), chaoIntent, alternateRuleset);
    expect(alternateChao.type == AI::MahjongCallType::Chao &&
           alternateChao.variant == selectedChao.variant &&
           alternateChao.score == selectedChao.score - 8,
           "Mahjong AI call scoring must consume the injected spell-point economy");

    LocalPlayer compactWin;
    compactWin.avatar = Avatar::Orachi;
    compactWin.clan = Clan::Red;
    compactWin.wind = Wind::South;
    compactWin.stones.add(GameStone(Stone(Stone::Sword1)));
    compactWin.stones.add(GameStone(Stone(Stone::Sword2)));
    compactWin.stones.add(GameStone(Stone(Stone::Sword3)));
    compactWin.stones.add(GameStone(Stone(Stone::Dragon1)));
    compactWin.newStone = GameStone(Stone(Stone::Dragon1), false);
    expect(!compactWin.isWinMahjong(Wind(Wind::South), Wind(Wind::East), Stone(),
                                    nullptr, classicRuleset) &&
           compactWin.isWinMahjong(Wind(Wind::South), Wind(Wind::East), Stone(),
                                   nullptr, alternateRuleset),
           "an injected ruleset must control winning structure validation");

    compactWin.newStone.reset();
    expect(!compactWin.isWinMahjong(Wind(Wind::East), Wind(Wind::East),
                                    Stone(Stone::Dragon1), nullptr, alternateRuleset),
           "an injected ruleset must control whether a discard may complete a win");

    LocalPlayer duplicateChao;
    duplicateChao.stones.add(GameStone(Stone::Sword2));
    duplicateChao.stones.add(GameStone(Stone::Sword2));
    duplicateChao.stones.add(GameStone(Stone::Sword3));
    duplicateChao.stones.add(GameStone(Stone::Sword3));
    duplicateChao.stones.add(GameStone(Stone::Sword9));
    duplicateChao.setMahjongChao(Stone(Stone::Sword1), 0);
    expect(duplicateChao.rules.size() == 1 && duplicateChao.rules.front().isChao() &&
           duplicateChao.stones.size() == 3 &&
           duplicateChao.stones.countStone(Stone(Stone::Sword1)) == 0 &&
           duplicateChao.stones.countStone(Stone(Stone::Sword2)) == 1 &&
           duplicateChao.stones.countStone(Stone(Stone::Sword3)) == 1,
           "Chao must move exactly one copy of all three sequence runes out of the concealed hand");
}

void testExposedKongDispatchAndReplacementDraw()
{
    const LocalPlayers savedPlayers = GameData::gamers;
    const Wind savedCurrentWind = GameData::currentWind;
    const Wind savedRoundWind = GameData::roundWind;
    const CroupierSet savedCroupier = GameData::croupier;
    const int savedStoneLastCount = GameData::stoneLastCount;
    const Stone savedDropStone = GameData::dropStone;
    const WinResults savedWinResult = GameData::winResult;
    const bool savedSkipRepeatSay = GameData::skipRepeatSay;
    const bool savedSkipNewStone = GameData::skipNewStone;
    const bool savedSkipNewTurn = GameData::skipNewTurn;

    auto player = [](Avatar::avatar_t avatar, Clan::clan_t clan, Wind::wind_t wind)
    {
        LocalPlayer result;
        result.avatar = avatar;
        result.clan = clan;
        result.wind = wind;
        result.points = 0;
        result.setAI(true);
        return result;
    };

    GameData::gamers.clear();
    GameData::gamers.push_back(player(Avatar::Javed, Clan::Red, Wind::East));
    GameData::gamers.push_back(player(Avatar::Orachi, Clan::Yellow, Wind::South));
    GameData::gamers.push_back(player(Avatar::Niana, Clan::Aqua, Wind::West));
    GameData::gamers.push_back(player(Avatar::Dayla, Clan::Purple, Wind::North));

    LocalPlayer & caller = GameData::gamers[1];
    caller.stones.add(GameStone(Stone::Dragon1));
    caller.stones.add(GameStone(Stone::Dragon1));
    caller.stones.add(GameStone(Stone::Dragon1));
    caller.stones.add(GameStone(Stone::Sword1));

    GameData::currentWind = Wind(Wind::East);
    GameData::roundWind = Wind(Wind::East);
    GameData::croupier = CroupierSet();
    GameData::stoneLastCount = 100;
    GameData::dropStone = Stone(Stone::Dragon1);
    GameData::winResult = WinResults();
    GameData::skipRepeatSay = false;
    GameData::skipNewStone = false;
    GameData::skipNewTurn = false;

    ActionList announced;
    const bool announcedKong = AI::mahjongGameKongPungChao(
        GameData::currentWind, GameData::roundWind, GameData::dropStone,
        GameData::winResult, announced, true);
    const auto kongAnnouncement = std::find_if(announced.begin(), announced.end(),
        [](const ActionMessage & action)
        {
            return action.type() == Action::MahjongKong1 &&
                action.getBoolean("sayOnly") &&
                action.getString("currentWind") == "south";
        });
    expect(announcedKong && kongAnnouncement != announced.end(),
           "Mahjong AI must announce an exposed Kong as Kong type 1");

    ActionList committed;
    expect(GameData::client2Mahjong(caller.avatar, ClientButtonKong1(), committed),
           "an announced exposed Kong must be accepted");
    expect(caller.rules.size() == 1 && caller.rules.front().isKong() &&
           caller.stones.size() == 1 && caller.stones.front() == Stone(Stone::Sword1),
           "an exposed Kong must consume exactly the three matching concealed runes");
    expect(!GameData::skipNewStone,
           "an exposed Kong must draw a replacement rune instead of skipping the draw");

    ActionList replacementTurn;
    expect(GameData::mahjong2Client(caller.avatar, replacementTurn),
           "an exposed Kong caller must receive a replacement turn");
    expect(GameData::stoneLastCount == 99 && caller.stones.size() == 1,
           "the replacement draw must consume one wall rune without emptying the caller's hand");

    GameData::gamers = savedPlayers;
    GameData::currentWind = savedCurrentWind;
    GameData::roundWind = savedRoundWind;
    GameData::croupier = savedCroupier;
    GameData::stoneLastCount = savedStoneLastCount;
    GameData::dropStone = savedDropStone;
    GameData::winResult = savedWinResult;
    GameData::skipRepeatSay = savedSkipRepeatSay;
    GameData::skipNewStone = savedSkipNewStone;
    GameData::skipNewTurn = savedSkipNewTurn;
}

void testSpellAndSpecialityContracts()
{
    struct PersistentEffectCase
    {
        Spell::spell_t spell;
        int attack;
        int ranger;
        int defense;
        int loyalty;
        const char* name;
    };

    const PersistentEffectCase effects[] = {
        { Spell::Smoke, 0, -1, 0, 0, "Smoke" },
        { Spell::DemonicCompulsion, 0, 0, 0, 1, "Demonic Compulsion" },
        { Spell::MassPanic, 0, 0, 0, -1, "Mass Panic" },
        { Spell::Reduction, -1, 0, -1, 0, "Reduction" },
        { Spell::BattleFury, 1, 0, 0, 0, "Battle Fury" },
        { Spell::DustCloud, -1, 0, 0, 0, "Dust Cloud" },
        { Spell::Heroism, 1, 0, 0, 1, "Heroism" },
        { Spell::BrilliantLights, 0, 0, -1, 0, "Brilliant Lights" },
        { Spell::MagicalAura, 0, 0, 1, 0, "Magical Aura" }
    };

    int unit = 500;
    for(const PersistentEffectCase & effect : effects)
    {
        BattleCreature target = combatCreature(Clan::Red, Creature::Durlock,
                                                unit++, 5, 4, 3, 6);
        const Spell spell(effect.spell);
        expect(target.applySpell(spell),
               (std::string(effect.name) + " must apply to a legal creature").c_str());
        expect(target.isAffectedSpell(spell) &&
               target.attack() == 5 + effect.attack &&
               target.ranger() == 4 + effect.ranger &&
               target.defense() == 3 + effect.defense &&
               target.loyalty() == 6 + effect.loyalty,
               (std::string(effect.name) + " must expose its exact persistent stat effect").c_str());
        expect(target.applySpell(Spell(Spell::DispelMagic)) &&
               target.attack() == 5 && target.ranger() == 4 &&
               target.defense() == 3 && target.loyalty() == 6,
               (std::string("Dispel Magic must remove ") + effect.name).c_str());
    }

    auto directedDraw = [](Spell::spell_t spellId, Stone::stone_t expectedRune,
                           Stone::stone_t ordinaryTop, const char* name)
    {
        RemotePlayer player;
        player.avatar = Avatar::Orachi;
        expect(player.mahjongApplySpell(Spell(spellId)),
               (std::string(name) + " must apply to its owning player").c_str());

        CroupierSet wall;
        wall.bank.clear();
        wall.trash.clear();
        wall.luckDraw.clear();
        wall.bank.push_back(Stone(expectedRune));
        wall.bank.push_back(Stone(ordinaryTop));
        const Stone drawn = wall.get(player);
        expect(drawn == Stone(expectedRune) && !player.isAffectedSpell(Spell(spellId)),
               (std::string(name) +
                " must find the requested rune family and expire after one draw").c_str());
    };
    directedDraw(Spell::DrawSkull, Stone::Skull4, Stone::Number9, "Summon Skull Rune");
    directedDraw(Spell::DrawSword, Stone::Sword6, Stone::Skull9, "Summon Sword Rune");
    directedDraw(Spell::DrawNumber, Stone::Number7, Stone::Sword9, "Summon Number Rune");

    const JsonObject savedRng = GameplayRng::toJsonObject();
    auto seedForRoll = [](int expected)
    {
        for(std::uint64_t seed = 1; seed < 100000; ++seed)
        {
            GameplayRng::seed(seed);
            if(GameplayRng::uniform(1, 100) == expected) return seed;
        }
        return UINT64_C(0);
    };
    const std::uint64_t resistanceSeed = seedForRoll(90);
    expect(resistanceSeed != 0, "Magic Resistance boundary fixture must find a deterministic 90 roll");
    if(resistanceSeed)
    {
        GameplayRng::seed(resistanceSeed);
        BattleCreature resistant = combatCreature(Clan::Yellow, Creature::KingDrago,
                                                   unit++, 5, 1, 6, 6);
        expect(!resistant.applySpell(Spell(Spell::BrilliantLights)) && resistant.defense() == 6,
               "90% Magic Resistance must reject a spell on the inclusive 90 boundary");
    }
    expect(SpecialityMagicResistence().chance(Creature::MazRa) == 25 &&
           SpecialityMagicResistence().chance(Creature::KingDrago) == 90,
           "minor and major Magic Resistance chances must remain 25% and 90%");
    expect(GameplayRng::fromJsonObject(savedRng),
           "Magic Resistance tests must restore the gameplay RNG state");

    BattleCreature devoted = combatCreature(Clan::Purple, Creature::Shanahan,
                                             unit++, 5, 2, 4, 8);
    devoted.applyDamage(3);
    devoted.initAdventurePart(Ability::None);
    expect(devoted.loyalty() == 7,
           "Devotion must restore exactly two loyalty before movement without exceeding base loyalty");

    struct CreatureCastCase
    {
        Creature::creature_t creature;
        Speciality::speciality_t speciality;
        Spell::spell_t spell;
    };
    const CreatureCastCase creatureCasts[] = {
        { Creature::MazRa, Speciality::CastHellblast, Spell::HellBlast },
        { Creature::WhiteDragon, Speciality::CastDrawNumber, Spell::DrawNumber },
        { Creature::GreenDragon, Speciality::CastDrawSword, Spell::DrawSword },
        { Creature::RedDragon, Speciality::CastDrawSkull, Spell::DrawSkull },
        { Creature::FireElemental, Speciality::CastRandomDiscard, Spell::RandomDiscard },
        { Creature::AirElemental, Speciality::CastSilence, Spell::Silence },
        { Creature::EarthElemental, Speciality::CastScryRunes, Spell::ScryRunes },
        { Creature::WaterElemental, Speciality::CastManaFog, Spell::ManaFog }
    };

    BattleArmy casterArmy;
    for(const CreatureCastCase & cast : creatureCasts)
    {
        expect(Speciality(cast.speciality).toSpell() == Spell(cast.spell) &&
               GameData::creatureInfo(cast.creature).specials.check(Speciality(cast.speciality)),
               "creature-cast speciality must map its owning creature to the normal spell contract");
        BattleParty party(Clan::Red, Land::Corzen);
        expect(party.join(combatCreature(Clan::Red, Creature(cast.creature),
                                         unit++, 3, 1, 3, 5)),
               "creature-cast speciality fixture must join its caster");
        casterArmy.push_back(party);
    }

    const Spells granted = casterArmy.allCastSpells();
    for(const CreatureCastCase & cast : creatureCasts)
        expect(std::find(granted.begin(), granted.end(), Spell(cast.spell)) != granted.end(),
               "an in-play creature must grant its mapped spell during the Mahjong phase");

    LocalPlayer dragonWizard;
    dragonWizard.avatar = Avatar::Orachi;
    dragonWizard.clan = Clan::Red;
    dragonWizard.wind = Wind::East;
    dragonWizard.army = casterArmy;
    expect(dragonWizard.allowCastSpell(Spell(Spell::DrawSkull)) &&
           dragonWizard.allowCastSpell(Spell(Spell::DrawSword)) &&
           dragonWizard.allowCastSpell(Spell(Spell::DrawNumber)),
           "in-play dragons must authorize all three rune-draw spells in their owning phase");
}

void testSpellCastingAI()
{
    GameData::gamers.clear();

    LocalPlayer red;
    red.avatar = Avatar::Orachi;
    red.clan = Clan::Red;
    red.wind = Wind::East;
    red.points = 1000;

    BattleParty allies(Clan::Red, Land::Corzen);
    BattleCreature wounded = combatCreature(Clan::Red, Creature::Durlock, 270, 3, 2, 2, 4);
    wounded.applyDamage(2);
    expect(allies.join(wounded), "spell AI wounded ally fixture must join");
    expect(allies.join(combatCreature(Clan::Red, Creature::SkeletonHorde, 271, 3, 0, 1, 3)),
           "spell AI non-ranged ally fixture must join");
    expect(allies.join(combatCreature(Clan::Red, Creature::EarthElemental, 272, 1, 0, 1, 3)),
           "spell AI creature-caster fixture must join");
    red.army.push_back(allies);

    LocalPlayer yellow;
    yellow.avatar = Avatar::Lakkho;
    yellow.clan = Clan::Yellow;
    yellow.wind = Wind::South;
    yellow.points = 900;

    BattleParty enemies(Clan::Yellow, Land::Zubrus);
    expect(enemies.join(combatCreature(Clan::Yellow, Creature::Durlock, 280, 5, 1, 2, 1)),
           "spell AI lethal enemy fixture must join");
    BattleCreature buffedEnemy = combatCreature(Clan::Yellow, Creature::SkeletonHorde, 281, 2, 1, 2, 4);
    expect(buffedEnemy.applySpell(Spell(Spell::BattleFury)),
           "spell AI dispel fixture must accept a beneficial enchantment");
    expect(enemies.join(buffedEnemy), "spell AI enchanted enemy fixture must join");
    yellow.army.push_back(enemies);

    GameData::gamers.push_back(red);
    GameData::gamers.push_back(yellow);
    LocalPlayer & caster = GameData::gamers.front();
    caster.stones.add(GameStone(Stone("wind1"), false));

    const Spells available = AI::availableSpellCasts(caster);
    expect(std::find(available.begin(), available.end(), Spell(Spell::ScryRunes)) != available.end(),
           "spell AI must include spells granted by creatures in the army");
    expect(AI::behaviorProfile(caster) == AI::BehaviorProfile::Aggressive,
           "spell AI must load Orachi's aggressive profile from the theme");
    expect(AI::behaviorProfile(GameData::gamers.back()) == AI::BehaviorProfile::Economic,
           "spell AI must load Lakkho's economic profile from the theme");
    expect(AI::behaviorProfileFromString("unknown") == AI::BehaviorProfile::Balanced,
           "an unknown spell AI profile must safely fall back to balanced");
    expect(AI::creatureSummonScore(Creature(Creature::MazRa), AI::BehaviorProfile::Aggressive) >
           AI::creatureSummonScore(Creature(Creature::MazRa), AI::BehaviorProfile::Economic),
           "aggressive AI must value an expensive damage summon more than economic AI");
    expect(AI::creatureSummonScore(Creature(Creature::EarthElemental), AI::BehaviorProfile::Control) >
           AI::creatureSummonScore(Creature(Creature::EarthElemental), AI::BehaviorProfile::Balanced),
           "control AI must value creature spell abilities when ordering summons");

    const AI::SpellCastPlan healing = chooseOnly(caster, Spell::Healing);
    expect(healing.spell == Spell(Spell::Healing) && healing.unit == 270 && healing.land == Land(Land::Corzen),
           "spell AI must heal a wounded friendly creature");

    const Spells noFuture;
    const AI::SpellCastPlan trainingSupport = AI::chooseSpellCast(
        caster, spellSet({ Spell::Healing, Spell::LightningBolt }),
        AI::BehaviorProfile::Aggressive, AI::Difficulty::Training, noFuture);
    expect(trainingSupport.spell == Spell(Spell::Healing) && trainingSupport.unit == 270,
           "Training AI must choose healing even when a lethal hostile spell is available");
    expect(!AI::chooseSpellCast(caster, spellSet({ Spell::LightningBolt }),
                                AI::BehaviorProfile::Aggressive,
                                AI::Difficulty::Training, noFuture).isValid(),
           "Training AI must reject every offensive-only spell plan");

    const AI::SpellCastPlan lightning = chooseOnly(caster, Spell::LightningBolt);
    expect(lightning.spell == Spell(Spell::LightningBolt) && lightning.unit == 280 &&
           lightning.land == Land(Land::Zubrus),
           "spell AI must prioritize a lethal Lightning Bolt target");
    const ClientCastSpell lightningCommand = AI::spellCastCommand(lightning);
    expect(lightningCommand.spell() == Spell(Spell::LightningBolt) &&
           lightningCommand.land() == Land(Land::Zubrus) && lightningCommand.unit() == 280,
           "spell AI plan must convert to the correct creature-target command");

    const AI::SpellCastPlan guidance = chooseOnly(caster, Spell::Guidance);
    expect(guidance.spell == Spell(Spell::Guidance) && guidance.unit == 270,
           "spell AI must cast Guidance only on a creature with ranged attack");

    const AI::SpellCastPlan forceShield = chooseOnly(caster, Spell::ForceShield);
    expect(forceShield.spell == Spell(Spell::ForceShield) && forceShield.unit == 270,
           "spell AI must find a useful Force Shield target");

    const AI::SpellCastPlan dispel = chooseOnly(caster, Spell::DispelMagic);
    expect(dispel.spell == Spell(Spell::DispelMagic) && dispel.unit == 281,
           "spell AI must remove a beneficial enchantment from an enemy");

    const AI::SpellCastPlan massDispel = chooseOnly(caster, Spell::MassDispel);
    expect(massDispel.spell == Spell(Spell::MassDispel) && massDispel.land == Land(Land::Zubrus),
           "spell AI must choose the land with a profitable Mass Dispel");

    const AI::SpellCastPlan hellBlast = chooseOnly(caster, Spell::HellBlast);
    expect(hellBlast.spell == Spell(Spell::HellBlast) && hellBlast.land == Land(Land::Zubrus) &&
           (hellBlast.unit == 280 || hellBlast.unit == 281),
           "spell AI must encode an enemy party through one of its battle units");

    const AI::SpellCastPlan teleport = chooseOnly(caster, Spell::Teleport);
    expect(teleport.spell == Spell(Spell::Teleport) && teleport.unit == 270 &&
           teleport.land != Land(Land::Corzen) &&
           (teleport.land.isTowerWinds() || GameData::landInfo(teleport.land).clan == Clan(Clan::Red)),
           "spell AI must select a legal friendly Teleport destination");

    const AI::SpellCastPlan silence = chooseOnly(caster, Spell::Silence);
    expect(silence.spell == Spell(Spell::Silence) && silence.target == Avatar(Avatar::Lakkho),
           "spell AI must select another player for Silence");
    const ClientCastSpell silenceCommand = AI::spellCastCommand(silence);
    expect(silenceCommand.target() == Avatar(Avatar::Lakkho),
           "spell AI plan must convert to the correct player-target command");

    const Spells damageOrControl = spellSet({ Spell::LightningBolt, Spell::Silence });
    const AI::SpellCastPlan aggressiveChoice = AI::chooseSpellCast(
        caster, damageOrControl, AI::BehaviorProfile::Aggressive, noFuture);
    const AI::SpellCastPlan controlChoice = AI::chooseSpellCast(
        caster, damageOrControl, AI::BehaviorProfile::Control, noFuture);
    const AI::SpellCastPlan easyLethalChoice = AI::chooseSpellCast(
        caster, damageOrControl, AI::BehaviorProfile::Aggressive,
        AI::Difficulty::Easy, noFuture);
    expect(aggressiveChoice.spell == Spell(Spell::LightningBolt),
           "aggressive spell AI must prefer lethal damage over player control");
    expect(easyLethalChoice.spell == Spell(Spell::LightningBolt),
           "Easy must still take an available lethal spell instead of suppressing all aggression");
    expect(controlChoice.spell == Spell(Spell::Silence),
           "control spell AI must prefer Silence over the same damage option");
    expect(AI::shouldCastBeforeSummon(aggressiveChoice) && AI::shouldCastBeforeSummon(controlChoice),
           "high-value aggressive and control plans must be allowed to override summoning");

    Creatures strategicSummons;
    strategicSummons.push_back(Creature(Creature::SkeletonHorde));
    const AI::TurnPlan aggressiveTurn = AI::chooseStrategicTurnPlan(
        AI::observePlayer(caster.avatar), strategicSummons, damageOrControl,
        AI::BehaviorProfile::Aggressive, AI::Difficulty::Normal);
    if(!aggressiveTurn.selected() ||
       aggressiveTurn.selected()->action != AI::StrategicAction::Spell)
        std::cerr << "  aggressive turn plan: " << aggressiveTurn.trace() << '\n';
    expect(aggressiveTurn.selected() &&
           aggressiveTurn.selected()->action == AI::StrategicAction::Spell &&
           aggressiveTurn.selected()->spell.spell == Spell(Spell::LightningBolt),
           "bounded TurnPlan must cast a high-value tactical spell before summoning");

    const AI::SpellCastPlan controlCombo = AI::chooseSpellCast(
        caster, spellSet({ Spell::DispelMagic }), AI::BehaviorProfile::Control,
        spellSet({ Spell::Paralyze, Spell::LightningBolt }));
    expect(controlCombo.spell == Spell(Spell::DispelMagic) && controlCombo.unit == 281,
           "control spell AI combo must start by dispelling the enchanted target");
    expect(controlCombo.followUps.size() == 2 &&
           controlCombo.followUps[0].spell == Spell(Spell::Paralyze) &&
           controlCombo.followUps[0].unit == 281 &&
           controlCombo.followUps[1].spell == Spell(Spell::LightningBolt) &&
           controlCombo.followUps[1].unit == 281,
           "control spell AI must project Dispel, Paralyze and Lightning on one target");
    expect(controlCombo.futureScore > 0 &&
           controlCombo.score == controlCombo.immediateScore + controlCombo.futureScore,
           "spell AI combo score must separate immediate and discounted future value");
    const BattleCreature* projectedTarget = GameData::getBattleCreature(281);
    expect(projectedTarget && projectedTarget->isAffectedSpell(Spell::BattleFury) &&
           !projectedTarget->isAffectedSpell(Spell::Paralyze),
           "spell AI projection must not mutate the current board state");

    GameData::gamers.back().avatar = Avatar::Javed;
    expect(!chooseOnly(caster, Spell::Silence).isValid(),
           "spell AI must respect Javed's Telepath immunity to Silence");
    expect(chooseOnly(caster, Spell::ScryRunes).target == Avatar(Avatar::Javed),
           "spell AI must consider Scry Runes useful even against an empty hand");
    expect(chooseOnly(caster, Spell::RandomDiscard).target == Avatar(Avatar::Javed),
           "spell AI must select another player for Random Discard");
    expect(chooseOnly(caster, Spell::ManaFog).spell == Spell(Spell::ManaFog),
           "spell AI must evaluate all-player control spells");
    expect(chooseOnly(caster, Spell::DrawNumber).spell == Spell(Spell::DrawNumber),
           "spell AI must evaluate self-target rune spells");

    for(BattleCreature* creature : caster.army.toBattleCreatures())
        expect(creature->applySpell(Spell(Spell::MysticalFountain)),
               "spell AI Mystical Fountain fixture must accept the enchantment");
    expect(!chooseOnly(caster, Spell::MysticalFountain).isValid(),
           "spell AI must recognize every stored Mystical Fountain variant");

    GameData::gamers.back().army.clear();
    BattleParty durableEnemies(Clan::Yellow, Land::Zubrus);
    expect(durableEnemies.join(combatCreature(Clan::Yellow, Creature::Durlock, 290, 1, 0, 1, 5)),
           "spell AI economic profile fixture must join");
    GameData::gamers.back().army.push_back(durableEnemies);
    const Spells damageOrRune = spellSet({ Spell::LightningBolt, Spell::DrawNumber });
    const AI::SpellCastPlan aggressiveEconomyChoice = AI::chooseSpellCast(
        caster, damageOrRune, AI::BehaviorProfile::Aggressive, noFuture);
    const AI::SpellCastPlan economicChoice = AI::chooseSpellCast(
        caster, damageOrRune, AI::BehaviorProfile::Economic, noFuture);
    expect(aggressiveEconomyChoice.spell == Spell(Spell::LightningBolt),
           "aggressive spell AI must prefer pressure over rune economy");
    expect(economicChoice.spell == Spell(Spell::DrawNumber),
           "economic spell AI must prefer rune economy over non-lethal damage");
    const AI::SpellCastPlan easyPressureChoice = AI::chooseSpellCast(
        caster, damageOrRune, AI::BehaviorProfile::Aggressive,
        AI::Difficulty::Easy, noFuture);
    const AI::SpellCastPlan normalPressureChoice = AI::chooseSpellCast(
        caster, damageOrRune, AI::BehaviorProfile::Aggressive,
        AI::Difficulty::Normal, noFuture);
    expect(easyPressureChoice.spell == Spell(Spell::DrawNumber) &&
           normalPressureChoice.spell == Spell(Spell::LightningBolt),
           "Easy must conserve a routine non-lethal Lightning Bolt while Normal keeps reference pressure");
    expect(!AI::shouldCastBeforeSummon(economicChoice),
           "an ordinary economic spell must not delay a legal summon");
    LocalData ordinaryEconomicState = GameData::toLocalData(caster.avatar);
    ordinaryEconomicState.myPlayer().points = 220;
    const AI::AIObservation ordinaryEconomicObservation(ordinaryEconomicState);
    const AI::SpellCastPlan ordinaryEconomicSpell = AI::chooseSpellCast(
        ordinaryEconomicState, spellSet({ Spell::DrawNumber }), AI::BehaviorProfile::Economic);
    expect(ordinaryEconomicSpell.isValid() && !AI::shouldCastBeforeSummon(ordinaryEconomicSpell),
           "ordinary economic TurnPlan fixture must stay below the summon override threshold");
    const AI::TurnPlan economicTurn = AI::chooseStrategicTurnPlan(
        ordinaryEconomicObservation, strategicSummons, spellSet({ Spell::DrawNumber }),
        AI::BehaviorProfile::Economic, AI::Difficulty::Normal);
    if(!economicTurn.selected() ||
       economicTurn.selected()->action != AI::StrategicAction::Summon)
        std::cerr << "  economic turn plan: " << economicTurn.trace() << '\n';
    expect(economicTurn.selected() &&
           economicTurn.selected()->action == AI::StrategicAction::Summon,
           "bounded TurnPlan must keep summoning ahead of an ordinary economic spell");

    const int savedPoints = caster.points;
    caster.points = 100;
    expect(!AI::chooseSpellCast(caster, spellSet({ Spell::DrawNumber }),
                                AI::BehaviorProfile::Economic, noFuture).isValid(),
           "economic spell AI must preserve its configured mana reserve");
    caster.points = savedPoints;

    caster.affected.insert(AffectedSpell(Spell(Spell::Silence), 3));
    expect(!chooseOnly(caster, Spell::LightningBolt).isValid(),
           "Silence must block spell AI planning");
    caster.affected.clear();
    caster.affected.insert(AffectedSpell(Spell(Spell::ManaFog), 3));
    expect(!chooseOnly(caster, Spell::LightningBolt).isValid(),
           "Mana Fog must block spell AI planning");

    expect(!AI::spellCastCommand(AI::SpellCastPlan()).spell().isValid(),
           "an empty spell plan must convert safely without indexing spell data");
}

void testAdventureProfiles()
{
    GameData::gamers.clear();

    LocalPlayer red;
    red.avatar = Avatar::Orachi;
    red.clan = Clan::Red;
    red.wind = Wind::East;
    red.addLandClaimPoints(Clan::Yellow, 3000);
    red.addLandClaimPoints(Clan::Purple, 3000);
    red.addLandClaimPoints(Clan::Aqua, 3000);
    BattleParty attackers(Clan::Red, Land::Greenbaw);
    expect(attackers.join(combatCreature(Clan::Red, Creature::Durlock, 300, 12, 2, 4, 8, 3)),
           "Adventure AI attacker fixture must join");
    red.army.push_back(attackers);

    LocalPlayer yellow;
    yellow.avatar = Avatar::Lakkho;
    yellow.clan = Clan::Yellow;
    yellow.wind = Wind::South;
    BattleParty kernDefenders(Clan::Yellow, Land::Kern);
    expect(kernDefenders.join(combatCreature(Clan::Yellow, Creature::KnightTemplar,
                                               301, 5, 0, 3, 5)),
           "Adventure AI defended target fixture must join");
    yellow.army.push_back(kernDefenders);

    LocalPlayer aqua;
    aqua.avatar = Avatar::Ziag;
    aqua.clan = Clan::Aqua;
    aqua.wind = Wind::West;

    LocalPlayer purple;
    purple.avatar = Avatar::Dayla;
    purple.clan = Clan::Purple;
    purple.wind = Wind::North;

    GameData::gamers.push_back(red);
    GameData::gamers.push_back(yellow);
    GameData::gamers.push_back(aqua);
    GameData::gamers.push_back(purple);

    const LocalPlayer & player = GameData::gamers.front();
    const BattleParty & party = player.army.front();
    const AI::AdventureClaimPlan aggressiveClaim =
        AI::chooseAdventureClaim(player, AI::BehaviorProfile::Aggressive);
    const AI::AdventureClaimPlan economicClaim =
        AI::chooseAdventureClaim(player, AI::BehaviorProfile::Economic);
    const AI::AdventureClaimPlan balancedClaim =
        AI::chooseAdventureClaim(player, AI::BehaviorProfile::Balanced);
    expect(!AI::chooseAdventureClaim(player, AI::BehaviorProfile::Aggressive,
                                     AI::Difficulty::Training).isValid(),
           "Training AI must never claim enemy territory");
    expect(aggressiveClaim.land == Land(Land::Corimar),
           "aggressive Adventure AI must claim the highest-pressure frontier");
    expect(economicClaim.land == Land(Land::Sunspot),
           "economic Adventure AI must claim the cheapest safe expansion");
    expect(balancedClaim.land == Land(Land::Sunspot),
           "balanced Adventure AI must preserve the cheapest-claim baseline");
    expect(GameData::landInfo(Land::Corimar).clan == Clan(Clan::Yellow) &&
           GameData::landInfo(Land::Sunspot).clan == Clan(Clan::Purple),
           "Adventure AI claim planning must not mutate territory ownership");

    LocalPlayer lowBudget = player;
    lowBudget.landClaims = LandClaims();
    lowBudget.addLandClaimPoints(Clan::Purple, 300);
    expect(AI::chooseAdventureClaim(lowBudget, AI::BehaviorProfile::Aggressive).isValid(),
           "aggressive Adventure AI must spend its final claim points");
    expect(!AI::chooseAdventureClaim(lowBudget, AI::BehaviorProfile::Economic).isValid(),
           "economic Adventure AI must preserve its Land Claim reserve");

    Lands nearbyTargets;
    nearbyTargets << Land::Kern << Land::Corimar;
    const AI::AdventureMovePlan aggressiveMove =
        AI::chooseAdventureMove(player, party, AI::BehaviorProfile::Aggressive, nearbyTargets);
    const AI::AdventureMovePlan economicMove =
        AI::chooseAdventureMove(player, party, AI::BehaviorProfile::Economic, nearbyTargets);
    expect(!AI::chooseAdventureMove(player, party, AI::BehaviorProfile::Aggressive,
                                    AI::Difficulty::Training, nearbyTargets).isValid(),
           "Training AI must never plan a move into enemy territory");
    expect(aggressiveMove.target == Land(Land::Kern) &&
           aggressiveMove.destination == Land(Land::Kern) && aggressiveMove.engagesEnemy,
           "aggressive Adventure AI must accept a calculated attack on a defended land");
    expect(economicMove.target == Land(Land::Corimar) &&
           economicMove.destination == Land(Land::Corimar) && economicMove.engagesEnemy,
           "economic Adventure AI must prefer a winnable empty territory");

    Lands defendedTarget;
    defendedTarget << Land::Kern;
    expect(AI::chooseAdventureMove(player, party, AI::BehaviorProfile::Aggressive,
                                   defendedTarget).isValid(),
           "aggressive Adventure AI must tolerate the configured battle risk");
    const AI::AdventureMovePlan economicDefendedMove =
        AI::chooseAdventureMove(player, party, AI::BehaviorProfile::Economic, defendedTarget);
    expect(economicDefendedMove.isValid() &&
           economicDefendedMove.captureChance >=
               AI::behaviorRules(AI::BehaviorProfile::Economic).minimumBattleWinChance,
           "Battle forecast must allow an economic attack that the old strength ratio rejected");

    BattleParty unsafeRaid(Clan::Red, Land::Greenbaw);
    expect(unsafeRaid.join(combatCreature(Clan::Red, Creature::Durlock, 302, 1, 0, 0, 1)),
           "Adventure AI unsafe forecast fixture must join");
    expect(!AI::chooseAdventureMove(player, unsafeRaid, AI::BehaviorProfile::Aggressive,
                                    defendedTarget).isValid(),
           "Adventure AI must reject a genuinely unsafe simulated battle");

    Lands strategicTargets;
    strategicTargets << Land::Corimar << Land::Inkartha;
    const AI::AdventureMovePlan controlMove =
        AI::chooseAdventureMove(player, party, AI::BehaviorProfile::Control, strategicTargets);
    expect(controlMove.target == Land(Land::Inkartha),
           "control Adventure AI must prioritize a strategic power territory");
    expect(player.army.front().land() == Land(Land::Greenbaw),
           "Adventure AI movement planning must not mutate the army");
}

void testAdventureCoordination()
{
    GameData::gamers.clear();

    LocalPlayer red;
    red.avatar = Avatar::Orachi;
    red.clan = Clan::Red;
    red.wind = Wind::East;

    BattleParty borderGuard(Clan::Red, Land::Corzen);
    expect(borderGuard.join(combatCreature(Clan::Red, Creature::Durlock, 310, 1, 0, 1, 1)),
           "Adventure coordinator border guard fixture must join");
    red.army.push_back(borderGuard);

    BattleParty mainForce(Clan::Red, Land::Greenbaw);
    expect(mainForce.join(combatCreature(Clan::Red, Creature::Durlock, 311, 8, 1, 3, 5, 2)),
           "Adventure coordinator main force fixture must join");
    expect(mainForce.join(combatCreature(Clan::Red, Creature::SkeletonHorde, 312, 5, 1, 2, 4, 2)),
           "Adventure coordinator main force second unit fixture must join");
    expect(mainForce.join(combatCreature(Clan::Red, Creature::KnightTemplar, 313, 4, 0, 3, 5, 2)),
           "Adventure coordinator main force third unit fixture must join");
    red.army.push_back(mainForce);

    BattleParty flank(Clan::Red, Land::Talon);
    expect(flank.join(combatCreature(Clan::Red, Creature::Durlock, 314, 6, 1, 2, 4, 2)),
           "Adventure coordinator flank fixture must join");
    red.army.push_back(flank);

    LocalPlayer yellow;
    yellow.avatar = Avatar::Lakkho;
    yellow.clan = Clan::Yellow;
    yellow.wind = Wind::South;
    BattleParty counterAttack(Clan::Yellow, Land::Zubrus);
    expect(counterAttack.join(combatCreature(Clan::Yellow, Creature::KnightTemplar,
                                              320, 4, 1, 3, 5, 1)),
           "Adventure coordinator counter-attack fixture must join");
    yellow.army.push_back(counterAttack);

    GameData::gamers.push_back(red);
    GameData::gamers.push_back(yellow);

    const LocalPlayer & player = GameData::gamers.front();
    const std::vector<AI::AdventureThreat> economicThreats =
        AI::predictAdventureThreats(player, AI::BehaviorProfile::Economic);
    const auto corzenThreat = std::find_if(economicThreats.begin(), economicThreats.end(),
                                           [](const AI::AdventureThreat & threat)
    {
        return threat.land == Land(Land::Corzen);
    });
    expect(corzenThreat != economicThreats.end() &&
           corzenThreat->enemyOrigin == Land(Land::Zubrus) &&
           corzenThreat->captureChance >=
               AI::behaviorRules(AI::BehaviorProfile::Economic).minimumThreatCaptureChance &&
           corzenThreat->enemyStrength < corzenThreat->defenseStrength,
           "economic Adventure AI must predict the next-turn attack from Zubrus to Corzen");

    const std::vector<AI::AdventureThreat> aggressiveThreats =
        AI::predictAdventureThreats(player, AI::BehaviorProfile::Aggressive);
    expect(std::none_of(aggressiveThreats.begin(), aggressiveThreats.end(),
                        [](const AI::AdventureThreat & threat)
    {
        return threat.land == Land(Land::Corzen);
    }), "aggressive Adventure AI must ignore the same moderate border threat");

    const AI::AdventureTurnPlan economicPlan =
        AI::chooseAdventureTurn(player, AI::BehaviorProfile::Economic);
    const auto guardOrder = std::find_if(economicPlan.orders.begin(), economicPlan.orders.end(),
                                         [](const AI::AdventurePartyOrder & order)
    {
        return order.origin == Land(Land::Corzen);
    });
    expect(economicPlan.reservedParties == 1 && guardOrder != economicPlan.orders.end() &&
           guardOrder->hold && guardOrder->defend == Land(Land::Corzen),
           "economic Adventure AI must hold the threatened border party in reserve");

    const AI::AdventureTurnPlan aggressivePlan =
        AI::chooseAdventureTurn(player, AI::BehaviorProfile::Aggressive);
    expect(aggressivePlan.reservedParties == 0,
           "aggressive Adventure AI must keep all parties available when no serious threat exists");

    const AI::AdventureTurnPlan trainingPlan = AI::chooseAdventureTurn(
        player, AI::BehaviorProfile::Aggressive, AI::Difficulty::Training);
    expect(std::all_of(trainingPlan.orders.begin(), trainingPlan.orders.end(),
                       [&](const AI::AdventurePartyOrder & order)
    {
        return !order.isMove() ||
               GameData::landInfo(order.move.destination).clan == player.clan;
    }), "Training whole-turn planning must contain only holds and friendly reinforcements");

    std::map<int, int> arrivals;
    std::map<int, int> targetAssignments;
    for(const AI::AdventurePartyOrder & order : aggressivePlan.orders)
    {
        if(!order.isMove()) continue;
        arrivals[order.move.destination()] += static_cast<int>(order.units.size());
        targetAssignments[order.move.target()]++;
    }
    for(const auto & arrival : arrivals)
    {
        const BattleParty* occupants = player.army.findPartyConst(
            Land(static_cast<Land::land_t>(arrival.first)));
        expect((occupants ? occupants->count() : 0) + arrival.second <= 3,
               "Adventure coordinator must not overbook a destination party");
    }
    for(const auto & assignment : targetAssignments)
        expect(assignment.second <= AI::behaviorRules(AI::BehaviorProfile::Aggressive).maxPartiesPerTarget,
               "Adventure coordinator must respect profile target concentration");

    expect(player.army.findPartyConst(Land(Land::Corzen))->findBattleUnitConst(310) &&
           player.army.findPartyConst(Land(Land::Greenbaw))->findBattleUnitConst(311) &&
           player.army.findPartyConst(Land(Land::Talon))->findBattleUnitConst(314),
           "Adventure whole-turn planning must not mutate unit positions");

    LocalPlayer & playerWithoutGuard = GameData::gamers.front();
    const BattleCreature removedGuard =
        *playerWithoutGuard.army.findPartyConst(Land(Land::Corzen))->findBattleUnitConst(310);
    playerWithoutGuard.army.remove(removedGuard);
    const AI::AdventureTurnPlan reinforcementPlan =
        AI::chooseAdventureTurn(playerWithoutGuard, AI::BehaviorProfile::Economic);
    const auto reinforcement = std::find_if(reinforcementPlan.orders.begin(),
                                             reinforcementPlan.orders.end(),
                                             [](const AI::AdventurePartyOrder & order)
    {
        return order.origin == Land(Land::Greenbaw) &&
               order.defend == Land(Land::Corzen);
    });
    expect(reinforcement != reinforcementPlan.orders.end() && reinforcement->isMove() &&
           reinforcement->move.destination == Land(Land::Corzen),
           "economic Adventure AI must reinforce an unguarded threatened border");
}

void testBattleAI()
{
    BattleParty forecastAttackers(Clan::Red, Land::Baliphon);
    expect(forecastAttackers.join(combatCreature(Clan::Red, Creature::Durlock,
                                                  325, 12, 0, 3, 10)),
           "Battle forecast overwhelming attacker fixture must join");
    BattleTown forecastTown(BattleUnit(BaseStat(0, 0, 0, 2)), Clan::Yellow, Land::Baliphon);
    const AI::BattleForecast winningForecast = AI::forecastBattle(
        forecastAttackers, forecastTown, nullptr, AI::BehaviorProfile::Aggressive,
        AI::BehaviorProfile::Balanced, 32);
    const AI::BattleForecast repeatedForecast = AI::forecastBattle(
        forecastAttackers, forecastTown, nullptr, AI::BehaviorProfile::Aggressive,
        AI::BehaviorProfile::Balanced, 32);
    expect(winningForecast.captureChance == 100 &&
           winningForecast.captureChance == repeatedForecast.captureChance &&
           winningForecast.attackerSurvival == repeatedForecast.attackerSurvival,
           "Battle forecast must be deterministic and recognize an overwhelming victory");
    expect(forecastAttackers.findBattleUnitConst(325)->loyalty() == 10 &&
           forecastTown.loyalty() == 2,
           "Battle forecast must run only on copied combat state");

    BattleParty doomedAttackers(Clan::Red, Land::Baliphon);
    expect(doomedAttackers.join(combatCreature(Clan::Red, Creature::Durlock,
                                                326, 1, 0, 0, 1)),
           "Battle forecast doomed attacker fixture must join");
    BattleTown fortress(BattleUnit(BaseStat(10, 0, 10, 10)), Clan::Yellow, Land::Baliphon);
    expect(AI::forecastBattle(doomedAttackers, fortress, nullptr,
                              AI::BehaviorProfile::Aggressive,
                              AI::BehaviorProfile::Balanced, 32).captureChance == 0,
           "Battle forecast must recognize a hopeless attack");

    std::srand(4242);
    const int expectedFirstRandom = std::rand();
    const int expectedSecondRandom = std::rand();
    std::srand(4242);
    const int actualFirstRandom = std::rand();
    AI::forecastBattle(forecastAttackers, forecastTown, nullptr,
                       AI::BehaviorProfile::Balanced, AI::BehaviorProfile::Balanced, 8);
    const int actualSecondRandom = std::rand();
    expect(actualFirstRandom == expectedFirstRandom && actualSecondRandom == expectedSecondRandom,
           "Battle forecast must not consume the real game's global random stream");

    BattleParty opening(Clan::Red, Land::Baliphon);
    expect(opening.join(combatCreature(Clan::Red, Creature::Durlock, 330, 12, 0, 2, 4)),
           "Battle AI opening damage fixture must join");
    expect(opening.join(combatCreature(Clan::Red, Creature::GreatCarol, 331, 1, 0, 1, 2)),
           "Battle AI opening First Strike fixture must join");
    const std::vector<int> openingOrder = AI::chooseBattleInitiative(
        opening, AI::BattleAttackMode::Melee, AI::BehaviorProfile::Balanced, true);
    const std::vector<int> normalOrder = AI::chooseBattleInitiative(
        opening, AI::BattleAttackMode::Melee, AI::BehaviorProfile::Balanced, false);
    expect(!openingOrder.empty() && openingOrder.front() == 331,
           "Battle AI must move a First Strike creature to opening initiative");
    expect(!normalOrder.empty() && normalOrder.front() == 330,
           "First Strike must not distort initiative after the opening");

    BattleParty doctrine(Clan::Red, Land::Baliphon);
    expect(doctrine.join(combatCreature(Clan::Red, Creature::Durlock, 340, 10, 0, 0, 2)),
           "Battle AI aggressive doctrine fixture must join");
    expect(doctrine.join(combatCreature(Clan::Red, Creature::AdventureParty, 341, 3, 0, 8, 10)),
           "Battle AI economic doctrine fixture must join");
    const std::vector<int> aggressiveOrder = AI::chooseBattleInitiative(
        doctrine, AI::BattleAttackMode::Melee, AI::BehaviorProfile::Aggressive);
    const std::vector<int> economicOrder = AI::chooseBattleInitiative(
        doctrine, AI::BattleAttackMode::Melee, AI::BehaviorProfile::Economic);
    expect(!aggressiveOrder.empty() && aggressiveOrder.front() == 340,
           "aggressive Battle AI must lead with the highest damage creature");
    expect(!economicOrder.empty() && economicOrder.front() == 341,
           "economic Battle AI must lead with the durable creature");

    BattleParty controlDoctrine(Clan::Red, Land::Baliphon);
    expect(controlDoctrine.join(combatCreature(Clan::Red, Creature::Durlock, 342, 7, 0, 3, 5)),
           "Battle AI control baseline fixture must join");
    expect(controlDoctrine.join(combatCreature(Clan::Red, Creature::SkeletonHorde,
                                                343, 4, 0, 1, 3)),
           "Battle AI control speciality fixture must join");
    const std::vector<int> controlOrder = AI::chooseBattleInitiative(
        controlDoctrine, AI::BattleAttackMode::Melee, AI::BehaviorProfile::Control);
    expect(!controlOrder.empty() && controlOrder.front() == 343,
           "control Battle AI must prioritize a tactical Swarm attacker");

    BattleParty shooters(Clan::Red, Land::Baliphon);
    expect(shooters.join(combatCreature(Clan::Red, Creature::Durlock, 350, 1, 2, 1, 3)),
           "Battle AI first ranged fixture must join");
    expect(shooters.join(combatCreature(Clan::Red, Creature::Durlock, 351, 1, 2, 1, 3)),
           "Battle AI second ranged fixture must join");
    BattleParty rangedTargets(Clan::Yellow, Land::Baliphon);
    expect(rangedTargets.join(combatCreature(Clan::Yellow, Creature::Durlock, 360, 1, 0, 0, 2)),
           "Battle AI lethal ranged target fixture must join");
    expect(rangedTargets.join(combatCreature(Clan::Yellow, Creature::Durlock, 361, 1, 0, 0, 6)),
           "Battle AI durable ranged target fixture must join");
    const AI::BattleRoundPlan rangedPlan = AI::chooseRangedBattlePlan(
        shooters, rangedTargets, AI::BehaviorProfile::Balanced);
    expect(rangedPlan.attacks.size() == 2 && rangedPlan.attacks[0].targets.front() == 360 &&
           rangedPlan.attacks[1].targets.front() == 361,
           "Battle AI must redirect later missiles after projecting a lethal shot");
    expect(rangedTargets.findBattleUnitConst(360)->loyalty() == 2 &&
           rangedTargets.findBattleUnitConst(361)->loyalty() == 6,
           "Battle AI ranged projection must not apply its predicted damage");

    BattleParty missileTargets(Clan::Yellow, Land::Baliphon);
    expect(missileTargets.join(combatCreature(Clan::Yellow, Creature::KiLin, 362, 1, 0, 0, 1)),
           "Battle AI Ignore Missiles fixture must join");
    expect(missileTargets.join(combatCreature(Clan::Yellow, Creature::Durlock, 363, 1, 0, 0, 3)),
           "Battle AI legal missile fixture must join");
    const AI::BattleRoundPlan missilePlan = AI::chooseRangedBattlePlan(
        shooters, missileTargets, AI::BehaviorProfile::Balanced);
    expect(std::all_of(missilePlan.attacks.begin(), missilePlan.attacks.end(),
                       [](const AI::BattleAttackPlan & attack)
    {
        return !attack.targets.empty() && attack.targets.front() != 362;
    }), "Battle AI must never assign missiles to an immune creature");

    BattleParty meleeActor(Clan::Red, Land::Baliphon);
    expect(meleeActor.join(combatCreature(Clan::Red, Creature::Durlock, 370, 5, 0, 1, 5)),
           "Battle AI melee attacker fixture must join");
    BattleParty meleeTargets(Clan::Yellow, Land::Baliphon);
    expect(meleeTargets.join(combatCreature(Clan::Yellow, Creature::Durlock, 371, 0, 0, 0, 1)),
           "Battle AI easy melee target fixture must join");
    expect(meleeTargets.join(combatCreature(Clan::Yellow, Creature::GreatCarol, 372, 8, 3, 1, 8)),
           "Battle AI dangerous melee target fixture must join");
    const AI::BattleAttackPlan aggressiveAttack = AI::chooseMeleeBattlePlan(
        meleeActor, meleeTargets, AI::BehaviorProfile::Aggressive);
    const AI::BattleAttackPlan controlAttack = AI::chooseMeleeBattlePlan(
        meleeActor, meleeTargets, AI::BehaviorProfile::Control);
    expect(aggressiveAttack.isValid() && aggressiveAttack.targets.front() == 371,
           "aggressive Battle AI must take the immediate kill");
    expect(controlAttack.isValid() && controlAttack.targets.front() == 372,
           "control Battle AI must prioritize the more dangerous tactical target");
    expect(meleeTargets.findBattleUnitConst(371)->loyalty() == 1 &&
           meleeTargets.findBattleUnitConst(372)->loyalty() == 8,
           "Battle AI melee planning must not mutate its targets");

    BattleParty overkillTarget(Clan::Yellow, Land::Baliphon);
    expect(overkillTarget.join(combatCreature(Clan::Yellow, Creature::Durlock,
                                               373, 0, 0, 0, 1)),
           "difficulty overkill fixture must join");
    GameData::setAIDifficulty(AI::Difficulty::Easy);
    const AI::BattleAttackPlan easyOverkill = AI::chooseMeleeBattlePlan(
        meleeActor, overkillTarget, AI::BehaviorProfile::Balanced);
    GameData::setAIDifficulty(AI::Difficulty::Normal);
    const AI::BattleAttackPlan normalOverkill = AI::chooseMeleeBattlePlan(
        meleeActor, overkillTarget, AI::BehaviorProfile::Balanced);
    GameData::setAIDifficulty(AI::Difficulty::Hard);
    const AI::BattleAttackPlan hardOverkill = AI::chooseMeleeBattlePlan(
        meleeActor, overkillTarget, AI::BehaviorProfile::Balanced);
    GameData::setAIDifficulty(AI::Difficulty::Unfair);
    const AI::BattleAttackPlan unfairOverkill = AI::chooseMeleeBattlePlan(
        meleeActor, overkillTarget, AI::BehaviorProfile::Balanced);
    GameData::setAIDifficulty(AI::Difficulty::Normal);
    expect(easyOverkill.isValid() && normalOverkill.isValid() && hardOverkill.isValid() &&
           unfairOverkill.isValid() &&
           unfairOverkill.score < hardOverkill.score &&
           hardOverkill.score < normalOverkill.score &&
           normalOverkill.score < easyOverkill.score,
           "Unfair must penalize wasted battle damage more strongly than Hard, Normal and Easy");
}

void testMahjongClaimPriorityRuleset()
{
    const LocalPlayers savedPlayers = GameData::gamers;
    const Wind savedCurrentWind = GameData::currentWind;
    const Wind savedRoundWind = GameData::roundWind;
    const CroupierSet savedCroupier = GameData::croupier;
    const int savedStoneLastCount = GameData::stoneLastCount;
    const Stone savedDropStone = GameData::dropStone;
    const WinResults savedWinResult = GameData::winResult;
    const bool savedSkipRepeatSay = GameData::skipRepeatSay;
    const bool savedSkipNewStone = GameData::skipNewStone;
    const bool savedSkipNewTurn = GameData::skipNewTurn;

    auto player = [](Avatar::avatar_t avatar, Clan::clan_t clan, Wind::wind_t wind)
    {
        LocalPlayer result;
        result.avatar = avatar;
        result.clan = clan;
        result.wind = wind;
        result.setAI(true);
        return result;
    };

    GameData::gamers.clear();
    GameData::gamers.push_back(player(Avatar::Javed, Clan::Red, Wind::East));
    GameData::gamers.push_back(player(Avatar::Orachi, Clan::Yellow, Wind::South));
    GameData::gamers.push_back(player(Avatar::Niana, Clan::Aqua, Wind::West));
    GameData::gamers.push_back(player(Avatar::Dayla, Clan::Purple, Wind::North));

    LocalPlayer & chaoCaller = GameData::gamers[1];
    chaoCaller.stones.add(GameStone(Stone(Stone::Sword1)));
    chaoCaller.stones.add(GameStone(Stone(Stone::Sword2)));
    chaoCaller.stones.add(GameStone(Stone(Stone::Dragon2)));

    LocalPlayer & pungCaller = GameData::gamers[2];
    pungCaller.stones.add(GameStone(Stone(Stone::Sword3)));
    pungCaller.stones.add(GameStone(Stone(Stone::Sword3)));
    pungCaller.stones.add(GameStone(Stone(Stone::Dragon2)));

    GameData::currentWind = Wind(Wind::East);
    GameData::roundWind = Wind(Wind::East);
    GameData::croupier = CroupierSet();
    GameData::stoneLastCount = 100;
    GameData::dropStone = Stone(Stone::Sword3);
    GameData::winResult = WinResults();
    GameData::skipRepeatSay = false;
    GameData::skipNewStone = false;
    GameData::skipNewTurn = false;

    ActionList classicActions;
    expect(AI::mahjongGameKongPungChao(
               GameData::currentWind, GameData::roundWind, GameData::dropStone,
               GameData::winResult, classicActions, true, classicRuneGameRuleset()),
           "Classic ruleset must resolve a competing discard claim");
    const auto classicPung = std::find_if(classicActions.begin(), classicActions.end(),
        [](const ActionMessage & action)
        {
            return action.type() == Action::MahjongPung && action.getBoolean("sayOnly") &&
                action.getString("currentWind") == "west";
        });
    expect(classicPung != classicActions.end(),
           "Classic claim priority must choose Pung over Chao");

    ActionList alternateActions;
    const AlternateRuneGameRuleset alternateRuleset;
    expect(AI::mahjongGameKongPungChao(
               GameData::currentWind, GameData::roundWind, GameData::dropStone,
               GameData::winResult, alternateActions, true, alternateRuleset),
           "an alternate ruleset must resolve the same competing discard claim");
    const auto alternateChao = std::find_if(alternateActions.begin(), alternateActions.end(),
        [](const ActionMessage & action)
        {
            return action.type() == Action::MahjongChao && action.getBoolean("sayOnly") &&
                action.getString("currentWind") == "south";
        });
    expect(alternateChao != alternateActions.end(),
           "an injected ruleset must be able to prefer Chao over Pung");

    GameData::gamers = savedPlayers;
    GameData::currentWind = savedCurrentWind;
    GameData::roundWind = savedRoundWind;
    GameData::croupier = savedCroupier;
    GameData::stoneLastCount = savedStoneLastCount;
    GameData::dropStone = savedDropStone;
    GameData::winResult = savedWinResult;
    GameData::skipRepeatSay = savedSkipRepeatSay;
    GameData::skipNewStone = savedSkipNewStone;
    GameData::skipNewTurn = savedSkipNewTurn;
}

void testAdventureHints()
{
    const LocalPlayers savedPlayers = GameData::gamers;
    const Clan savedCorzenOwner = GameData::landsInfo[Land::Corzen].clan;
    const Clan savedZubrusOwner = GameData::landsInfo[Land::Zubrus].clan;

    GameData::landsInfo[Land::Corzen].clan = Clan::Red;
    GameData::landsInfo[Land::Zubrus].clan = Clan::Yellow;
    GameData::gamers.clear();

    LocalPlayer red;
    red.avatar = Avatar::Orachi;
    red.clan = Clan::Red;
    red.wind = Wind::East;
    red.addLandClaimPoints(Clan::Yellow, 500);
    BattleParty attackers(Clan::Red, Land::Corzen);
    BattleCreature selected(Clan::Red, Creature::SkeletonHorde, 3600);
    selected.setSelected(true);
    expect(attackers.join(selected), "Adventure hint attacker fixture must join");
    red.army.push_back(attackers);

    LocalPlayer yellow;
    yellow.avatar = Avatar::Lakkho;
    yellow.clan = Clan::Yellow;
    yellow.wind = Wind::South;
    LocalPlayer aqua;
    aqua.avatar = Avatar::Ziag;
    aqua.clan = Clan::Aqua;
    aqua.wind = Wind::West;
    LocalPlayer purple;
    purple.avatar = Avatar::Dayla;
    purple.clan = Clan::Purple;
    purple.wind = Wind::North;
    GameData::gamers.push_back(red);
    GameData::gamers.push_back(yellow);
    GameData::gamers.push_back(aqua);
    GameData::gamers.push_back(purple);

    const LocalData baselineView = GameData::toLocalData(Avatar(Avatar::Orachi));
    const AdventureHints::BattlePreview baseline = AdventureHints::battlePreview(
        baselineView, Land::Corzen, Land::Zubrus, AI::Difficulty::Normal);
    expect(baseline.available && baseline.showPercentages && 0 < baseline.samples &&
           baseline.attackerCount == 1 && baseline.visibleDefenderCount == 0,
           "Normal Adventure preview must explain its observer-visible battle basis");
    expect(AdventureHints::destinationCue(baselineView, Land::Corzen, Land::Zubrus) ==
               AdventureHints::DestinationCue::Attack,
           "an observer-legal enemy destination must receive an attack cue");

    GameData::landsInfo[Land::Zubrus].clan = Clan::Red;
    expect(AdventureHints::destinationCue(baselineView, Land::Corzen, Land::Zubrus) ==
               AdventureHints::DestinationCue::Move,
           "an observer-legal friendly destination must receive a move cue");
    GameData::landsInfo[Land::Zubrus].clan = Clan::Yellow;

    BattleParty hiddenGuard(Clan::Yellow, Land::Zubrus);
    expect(hiddenGuard.join(BattleCreature(Clan::Yellow, Creature::Shadow, 3601)),
           "Adventure hint invisible guard fixture must join");
    GameData::gamers[1].army.push_back(hiddenGuard);

    const LocalData hiddenView = GameData::toLocalData(Avatar(Avatar::Orachi));
    expect(!hiddenView.playerOfClan(Clan(Clan::Yellow)).army.findBattleUnitConst(3601),
           "the Adventure hint observer view must not contain an invisible guard");
    const AdventureHints::BattlePreview hidden = AdventureHints::battlePreview(
        hiddenView, Land::Corzen, Land::Zubrus, AI::Difficulty::Normal);
    expect(hidden.captureChance == baseline.captureChance &&
           hidden.attackerSurvival == baseline.attackerSurvival &&
           hidden.visibleDefenderCount == baseline.visibleDefenderCount,
           "an invisible guard must not influence a passive battle preview");
    expect(AdventureHints::canClaimObserved(hiddenView, Land::Zubrus) &&
           !GameData::canClaimLand(GameData::gamers[0], Land::Zubrus),
           "a passive claim cue must not reveal a hidden guard before an explicit attempt");

    const AdventureHints::BattlePreview hard = AdventureHints::battlePreview(
        hiddenView, Land::Corzen, Land::Zubrus, AI::Difficulty::Hard);
    expect(hard.available && !hard.showPercentages && hard.samples == 0 &&
           hard.attackerCount == 1 && hard.visibleDefenderCount == 0,
           "Hard must expose known forces without leaking outcome percentages");
    const AdventureHints::BattlePreview unfair = AdventureHints::battlePreview(
        hiddenView, Land::Corzen, Land::Zubrus, AI::Difficulty::Unfair);
    expect(unfair.available && !unfair.showPercentages && unfair.samples == 0,
           "Unfair must hide outcome percentages under the same observer-safe contract as Hard");

    GameplayRng::seed(0x156EULL);
    const std::uint64_t stateBefore = GameplayRng::state();
    const std::uint64_t drawsBefore = GameplayRng::draws();
    AdventureHints::battlePreview(hiddenView, Land::Corzen, Land::Zubrus,
                                  AI::Difficulty::Normal);
    expect(GameplayRng::state() == stateBefore && GameplayRng::draws() == drawsBefore,
           "opening a battle preview must not consume gameplay RNG");

    GameData::gamers = savedPlayers;
    GameData::landsInfo[Land::Corzen].clan = savedCorzenOwner;
    GameData::landsInfo[Land::Zubrus].clan = savedZubrusOwner;
}

void testInteractiveBattleSession()
{
    const Battle::RandomRoll highRoll = [](int, int maximum) { return maximum; };

    BattleParty attackers(Clan::Red, Land::Baliphon);
    expect(attackers.join(combatCreature(Clan::Red, Creature::Durlock,
                                         380, 9, 0, 1, 8)),
           "interactive battle ordinary attacker fixture must join");
    expect(attackers.join(combatCreature(Clan::Red, Creature::GreatCarol,
                                         381, 4, 0, 1, 8)),
           "interactive battle First Strike fixture must join");
    BattleParty defenders(Clan::Yellow, Land::Baliphon);
    expect(defenders.join(combatCreature(Clan::Yellow, Creature::Durlock,
                                         390, 3, 0, 1, 6)),
           "interactive battle first defender fixture must join");
    expect(defenders.join(combatCreature(Clan::Yellow, Creature::AdventureParty,
                                         391, 3, 0, 1, 6)),
           "interactive battle second defender fixture must join");
    BattleTown town(BattleUnit(BaseStat(1, 0, 1, 8)), Clan::Yellow, Land::Baliphon);

    Battle::Session session(attackers, town, &defenders,
                            AI::BehaviorProfile::Balanced,
                            AI::BehaviorProfile::Balanced);
    expect(session.prepare(highRoll, false) && session.awaitsChoice() &&
           session.phase() == Battle::Session::Phase::OpeningLeader,
           "human BattleSession must pause for the opening leader");
    const std::vector<int> legalActors = session.legalActors();
    expect(legalActors.size() == 2 &&
           std::find(legalActors.begin(), legalActors.end(), 380) != legalActors.end(),
           "opening leader choice must expose every living attacker as a stable unit id");
    expect(session.legalTargets(380).empty(),
           "opening leader choice must not expose a combat target");

    const std::string awaitingState = session.toJsonObject().toString();
    expect(!session.choose(9999, -1, highRoll, false) &&
           session.toJsonObject().toString() == awaitingState,
           "BattleSession must reject an unknown actor without mutating combat");

    Battle::Session restored = Battle::Session::fromJsonObject(session.toJsonObject());
    expect(restored.phase() == Battle::Session::Phase::OpeningLeader &&
           restored.legalActors() == session.legalActors(),
           "pending opening leader choice must survive a save/load round trip");
    expect(restored.choose(381, -1, highRoll, false) && restored.awaitsChoice() &&
           restored.phase() == Battle::Session::Phase::AttackerChoice,
           "a First Strike opening leader must advance directly to manual melee");

    const std::vector<int> legalTargets = restored.legalTargets(380);
    expect(legalTargets.size() == 2 &&
           std::find(legalTargets.begin(), legalTargets.end(), 391) != legalTargets.end(),
           "manual melee must expose every living defender as a legal target");
    const std::string meleeState = restored.toJsonObject().toString();
    expect(!restored.choose(380, 9999, highRoll, false) &&
           restored.toJsonObject().toString() == meleeState,
           "BattleSession must reject an unknown melee target without mutating combat");

    restored = Battle::Session::fromJsonObject(restored.toJsonObject());
    expect(restored.choose(380, 391, highRoll, false) && restored.awaitsChoice(),
           "a legal melee choice must pause again when the battle continues");
    expect(!restored.strikes().empty() && restored.strikes().front().unit1 == 380 &&
           restored.strikes().front().unit2 == 391,
           "the first manual melee strike must use the selected actor and target");
    expect(restored.choose(380, 390, highRoll, false) && restored.awaitsChoice(),
           "manual melee must allow a second round without hidden Auto Resolve");
    expect(restored.choose(380, town.battleUnit(), highRoll, false) && restored.isComplete(),
           "manual melee must complete after the selected attacker captures the town");

    BattleParty automaticAttackers = attackers;
    BattleParty automaticDefenders = defenders;
    BattleTown automaticTown = town;
    const BattleStrikes adapterStrikes = Battle::doAttackParty(
        automaticAttackers, automaticTown, &automaticDefenders,
        AI::BehaviorProfile::Balanced, AI::BehaviorProfile::Balanced, highRoll, false);
    Battle::Session automaticSession(attackers, town, &defenders,
                                     AI::BehaviorProfile::Balanced,
                                     AI::BehaviorProfile::Balanced);
    expect(automaticSession.autoResolve(highRoll, false) && automaticSession.isComplete(),
           "BattleSession Auto Resolve must complete without a UI choice");
    expect(automaticSession.attackers().toString() == automaticAttackers.toString() &&
           automaticSession.defenders().toString() == automaticDefenders.toString() &&
           automaticSession.town().toString() == automaticTown.toString() &&
           automaticSession.strikes().toString() == adapterStrikes.toString(),
           "legacy automatic battle entry point must use the same BattleSession primitives");

    BattleParty townAttackers(Clan::Red, Land::Baliphon);
    expect(townAttackers.join(combatCreature(Clan::Red, Creature::Durlock,
                                             395, 9, 0, 1, 8)),
           "interactive town attacker fixture must join");
    BattleTown targetTown(BattleUnit(BaseStat(0, 0, 0, 4)), Clan::Yellow, Land::Baliphon);
    Battle::Session townSession(townAttackers, targetTown, nullptr,
                                AI::BehaviorProfile::Balanced,
                                AI::BehaviorProfile::Balanced);
    expect(townSession.prepare(highRoll, false) &&
           townSession.phase() == Battle::Session::Phase::OpeningLeader &&
           townSession.choose(395, -1, highRoll, false) && townSession.awaitsChoice() &&
           townSession.legalTargets(395).size() == 1 &&
           townSession.legalTargets(395).front() == targetTown.battleUnit(),
           "an undefended territory must expose its stable town id as the manual target");

    BattleParty rangedAttackers(Clan::Red, Land::Baliphon);
    expect(rangedAttackers.join(combatCreature(Clan::Red, Creature::Durlock,
                                                430, 1, 2, 0, 5)) &&
           rangedAttackers.join(combatCreature(Clan::Red, Creature::Durlock,
                                                431, 1, 2, 0, 5)),
           "interactive ranged attacker fixtures must join");
    BattleParty rangedDefenders(Clan::Yellow, Land::Baliphon);
    expect(rangedDefenders.join(combatCreature(Clan::Yellow, Creature::Durlock,
                                                440, 0, 0, 0, 1)) &&
           rangedDefenders.join(combatCreature(Clan::Yellow, Creature::KiLin,
                                                441, 0, 0, 0, 8)),
           "interactive ranged defender fixtures must join");
    BattleTown rangedTargetTown(BattleUnit(BaseStat(0, 0, 0, 3)),
                                 Clan::Yellow, Land::Baliphon);
    Battle::Session rangedSession(rangedAttackers, rangedTargetTown, &rangedDefenders,
                                  AI::BehaviorProfile::Balanced,
                                  AI::BehaviorProfile::Balanced);
    expect(rangedSession.prepare(highRoll, false) &&
           rangedSession.choose(430, -1, highRoll, false) &&
           rangedSession.phase() == Battle::Session::Phase::AttackerRangedChoice,
           "ordinary opening leader must advance to manual missile assignment");
    const int firstShooter = rangedSession.legalActors().front();
    const std::vector<int> missileTargets = rangedSession.legalTargets(firstShooter);
    expect(std::find(missileTargets.begin(), missileTargets.end(), 440) != missileTargets.end() &&
           std::find(missileTargets.begin(), missileTargets.end(), 441) == missileTargets.end(),
           "manual missile assignment must exclude Ignore Missiles targets");
    expect(rangedSession.choose(firstShooter, 440, highRoll, false) &&
           rangedSession.phase() == Battle::Session::Phase::AttackerRangedChoice &&
           rangedSession.strikes().empty(),
           "the first missile assignment must not apply a partial volley");

    rangedSession = Battle::Session::fromJsonObject(rangedSession.toJsonObject());
    expect(rangedSession.phase() == Battle::Session::Phase::AttackerRangedChoice &&
           rangedSession.legalActors().size() == 1,
           "a partially assigned missile volley must survive save/load");
    const int secondShooter = rangedSession.legalActors().front();
    expect(secondShooter != firstShooter &&
           rangedSession.choose(secondShooter, 440, highRoll, false),
           "the restored volley must accept the remaining shooter assignment");
    expect(std::count_if(rangedSession.strikes().begin(), rangedSession.strikes().end(),
                         [](const BattleStrike & strike)
                         { return strike.type == BattleStrike::Ranger; }) == 2,
           "all manually assigned shooters must fire from the untouched pre-volley state");
    expect(rangedSession.autoResolve(highRoll, false) && rangedSession.isComplete(),
           "Auto Resolve must finish from a partially manual BattleSession phase");
}

void testBattleSessionSpecialities()
{
    const Battle::RandomRoll highRoll = [](int, int maximum) { return maximum; };
    const Battle::RandomRoll lowRoll = [](int minimum, int) { return minimum; };

    // Creature-cast Hellblast resolves simultaneously at session entry.
    BattleParty hellAttackers(Clan::Red, Land::Baliphon);
    BattleParty hellDefenders(Clan::Yellow, Land::Baliphon);
    expect(hellAttackers.join(combatCreature(Clan::Red, Creature::MazRa,
                                              500, 0, 0, 0, 10)) &&
           hellAttackers.join(combatCreature(Clan::Red, Creature::Durlock,
                                              501, 0, 0, 0, 10)) &&
           hellDefenders.join(combatCreature(Clan::Yellow, Creature::MazRa,
                                              510, 0, 0, 0, 10)) &&
           hellDefenders.join(combatCreature(Clan::Yellow, Creature::Durlock,
                                              511, 0, 0, 0, 10)),
           "BattleSession Hellblast fixtures must join");
    BattleTown hellTown(BattleUnit(BaseStat(0, 0, 20, 20)),
                        Clan::Yellow, Land::Baliphon);
    Battle::Session hellSession(hellAttackers, hellTown, &hellDefenders,
                                AI::BehaviorProfile::Balanced,
                                AI::BehaviorProfile::Balanced);
    expect(hellSession.prepare(highRoll, false) && hellSession.strikes().size() == 4,
           "opposing BattleSession Hellblast casters must hit every pre-entry creature");
    expect(std::count_if(hellSession.strikes().begin(), hellSession.strikes().end(),
                         [](const BattleStrike & strike)
                         { return strike.unit1 == 500 || strike.unit1 == 510; }) == 4,
           "BattleSession Hellblast strikes must retain their stable caster ids");

    // First Strike is a leader decision and suppresses both creature volleys.
    BattleParty firstAttackers(Clan::Red, Land::Baliphon);
    BattleParty firstDefenders(Clan::Yellow, Land::Baliphon);
    expect(firstAttackers.join(combatCreature(Clan::Red, Creature::GreatCarol,
                                               520, 5, 0, 5, 10)) &&
           firstAttackers.join(combatCreature(Clan::Red, Creature::Durlock,
                                               521, 1, 8, 5, 10)) &&
           firstDefenders.join(combatCreature(Clan::Yellow, Creature::Durlock,
                                               522, 1, 8, 0, 10)),
           "BattleSession First Strike fixtures must join");
    Battle::Session firstSession(firstAttackers, hellTown, &firstDefenders,
                                 AI::BehaviorProfile::Balanced,
                                 AI::BehaviorProfile::Balanced);
    expect(firstSession.prepare(highRoll, false) &&
           firstSession.choose(520, -1, highRoll, false) &&
           firstSession.phase() == Battle::Session::Phase::AttackerChoice &&
           std::none_of(firstSession.strikes().begin(), firstSession.strikes().end(),
                        [](const BattleStrike & strike)
                        { return strike.type == BattleStrike::Ranger; }),
           "selected First Strike leader must skip the simultaneous creature volley");

    // Matching Merge defenders alter the real manual hit calculation.
    BattleParty mergeAttackers(Clan::Red, Land::Baliphon);
    BattleParty mergeDefenders(Clan::Yellow, Land::Baliphon);
    expect(mergeAttackers.join(combatCreature(Clan::Red, Creature::Durlock,
                                               530, 2, 0, 20, 10)) &&
           mergeDefenders.join(combatCreature(Clan::Yellow, Creature::Griffon,
                                               531, 0, 0, 1, 5)) &&
           mergeDefenders.join(combatCreature(Clan::Yellow, Creature::Griffon,
                                               532, 0, 0, 1, 5)),
           "BattleSession Merge fixtures must join");
    Battle::Session mergeSession(mergeAttackers, hellTown, &mergeDefenders,
                                 AI::BehaviorProfile::Balanced,
                                 AI::BehaviorProfile::Balanced);
    expect(mergeSession.prepare(highRoll, false) &&
           mergeSession.choose(530, -1, highRoll, false) &&
           mergeSession.choose(530, 531, highRoll, false),
           "BattleSession Merge fixture must reach and accept manual melee");
    expect(std::any_of(mergeSession.strikes().begin(), mergeSession.strikes().end(),
                       [](const BattleStrike & strike)
                       { return strike.unit1 == 530 && strike.unit2 == 531 &&
                                strike.damage == 0; }),
           "Merge defense must turn the deterministic manual melee hit into a miss");
    const Battle::AttackPreview mergePreview = Battle::previewAttack(
        mergeSession.attackers(), mergeSession.town(), &mergeSession.defenders(),
        530, 531, false);
    expect(mergePreview.valid && mergePreview.damage == 1 && mergePreview.hitChance == 50,
           "damage preview must use the same Merge modifier as BattleSession");

    // Mighty Blow uses an injected deterministic roll and guarantees one damage.
    BattleParty mightyAttackers(Clan::Red, Land::Baliphon);
    BattleParty mightyDefenders(Clan::Yellow, Land::Baliphon);
    expect(mightyAttackers.join(combatCreature(Clan::Red, Creature::KnightTemplar,
                                                540, 0, 0, 20, 10)) &&
           mightyDefenders.join(combatCreature(Clan::Yellow, Creature::Durlock,
                                                541, 0, 0, 10, 5)),
           "BattleSession Mighty Blow fixtures must join");
    Battle::Session mightySession(mightyAttackers, hellTown, &mightyDefenders,
                                  AI::BehaviorProfile::Balanced,
                                  AI::BehaviorProfile::Balanced);
    expect(mightySession.prepare(highRoll, false) &&
           mightySession.choose(540, -1, highRoll, false) &&
           mightySession.choose(540, 541, lowRoll, false),
           "BattleSession Mighty Blow fixture must accept its forced proc");
    expect(std::any_of(mightySession.strikes().begin(), mightySession.strikes().end(),
                       [](const BattleStrike & strike)
                       { return strike.unit1 == 540 && strike.unit2 == 541 &&
                                strike.damage == 1; }),
           "a triggered Mighty Blow must deal at least one BattleSession damage");
    const Battle::AttackPreview mightyPreview = Battle::previewAttack(
        mightySession.attackers(), mightySession.town(), &mightySession.defenders(),
        540, 541, false);
    expect(mightyPreview.valid && mightyPreview.mightyChance == 25 &&
           mightyPreview.mightyDamage == 1,
           "damage preview must expose the Mighty Blow branch without rolling it");

    // Fire Shield retaliates even when the selected melee strike is lethal.
    BattleParty fireAttackers(Clan::Red, Land::Baliphon);
    BattleParty fireDefenders(Clan::Yellow, Land::Baliphon);
    expect(fireAttackers.join(combatCreature(Clan::Red, Creature::Durlock,
                                              550, 5, 0, 20, 10)) &&
           fireDefenders.join(combatCreature(Clan::Yellow, Creature::FireElemental,
                                              551, 0, 0, 0, 5)),
           "BattleSession Fire Shield fixtures must join");
    Battle::Session fireSession(fireAttackers, hellTown, &fireDefenders,
                                AI::BehaviorProfile::Balanced,
                                AI::BehaviorProfile::Balanced);
    expect(fireSession.prepare(highRoll, false) &&
           fireSession.choose(550, -1, highRoll, false) &&
           fireSession.choose(550, 551, highRoll, false),
           "BattleSession Fire Shield fixture must accept manual melee");
    expect(std::any_of(fireSession.strikes().begin(), fireSession.strikes().end(),
                       [](const BattleStrike & strike)
                       { return strike.type == BattleStrike::FireShield &&
                                strike.unit1 == 551 && strike.unit2 == 550 &&
                                strike.damage == 1; }) &&
           fireSession.attackers().findBattleUnitConst(550)->loyalty() == 9,
           "Fire Shield must retaliate through the selected BattleSession action");

    // Swarm retains the selected first target and strikes the entire party.
    BattleParty swarmAttackers(Clan::Red, Land::Baliphon);
    BattleParty swarmDefenders(Clan::Yellow, Land::Baliphon);
    expect(swarmAttackers.join(combatCreature(Clan::Red, Creature::SkeletonHorde,
                                               560, 10, 0, 20, 10)) &&
           swarmDefenders.join(combatCreature(Clan::Yellow, Creature::Durlock,
                                               561, 0, 0, 0, 1)) &&
           swarmDefenders.join(combatCreature(Clan::Yellow, Creature::Durlock,
                                               562, 0, 0, 0, 1)) &&
           swarmDefenders.join(combatCreature(Clan::Yellow, Creature::Durlock,
                                               563, 0, 0, 0, 1)),
           "BattleSession Swarm fixtures must join");
    Battle::Session swarmSession(swarmAttackers, hellTown, &swarmDefenders,
                                 AI::BehaviorProfile::Balanced,
                                 AI::BehaviorProfile::Balanced);
    expect(swarmSession.prepare(highRoll, false) &&
           swarmSession.choose(560, -1, highRoll, false) &&
           swarmSession.choose(560, 562, highRoll, false),
           "BattleSession Swarm fixture must accept a selected first target");
    expect(std::count_if(swarmSession.strikes().begin(), swarmSession.strikes().end(),
                         [](const BattleStrike & strike)
                         { return strike.type == BattleStrike::Melee &&
                                  strike.unit1 == 560 && 561 <= strike.unit2 &&
                                  strike.unit2 <= 563; }) == 3 &&
           swarmSession.defenders().isEmpty(),
           "Swarm must strike every defender through one manual BattleSession choice");
}

void testAdventureBattleSessionFlow()
{
    GameData::initPersons(Person(Avatar::Lakkho, Clan::Yellow, Wind::East));
    GameData::gamers.clear();

    LocalPlayer attacker;
    attacker.avatar = Avatar::Lakkho;
    attacker.clan = Clan::Yellow;
    attacker.wind = Wind::East;
    BattleParty invadingParty(Clan::Yellow, Land::Baliphon);
    expect(invadingParty.join(combatCreature(Clan::Yellow, Creature::GreatCarol,
                                              410, 8, 0, 2, 20)),
           "Adventure interactive attacker fixture must join");
    attacker.army.push_back(invadingParty);

    LocalPlayer defender;
    defender.avatar = Avatar::Nucrus;
    defender.clan = Clan::Red;
    defender.wind = Wind::South;
    defender.setAI(true);
    BattleParty defendingParty(Clan::Red, Land::Baliphon);
    expect(defendingParty.join(combatCreature(Clan::Red, Creature::Durlock,
                                               420, 2, 0, 2, 12)),
           "Adventure interactive defender fixture must join");
    defender.army.push_back(defendingParty);

    LocalPlayer observerWest;
    observerWest.avatar = Avatar::Dayla;
    observerWest.clan = Clan::Aqua;
    observerWest.wind = Wind::West;
    observerWest.setAI(true);

    LocalPlayer observerNorth;
    observerNorth.avatar = Avatar::Ziag;
    observerNorth.clan = Clan::Purple;
    observerNorth.wind = Wind::North;
    observerNorth.setAI(true);

    GameData::gamers.push_back(attacker);
    GameData::gamers.push_back(defender);
    GameData::gamers.push_back(observerWest);
    GameData::gamers.push_back(observerNorth);
    GameData::landsInfo[Land::Baliphon].clan = Clan::Red;
    expect(GameData::initAdventure(),
           "Adventure interactive battle fixture must initialize");

    ActionList actions;
    expect(GameData::client2Adventure(attacker.avatar, ClientBattleReady(), actions),
           "human attacker must be allowed to finish movement before battle choice");
    expect(GameData::adventure2Client(attacker.avatar, actions) && !actions.empty() &&
           actions.back().type() == Action::AdventureBattleChoice &&
           GameData::currentWind == Wind(Wind::East),
           "Adventure must pause on a battle choice without shifting the current wind");

    const AdventureBattleChoice choice =
        static_cast<const AdventureBattleChoice &>(actions.back());
    expect(choice.phase() == "opening_leader" && choice.choiceNumber() == 1 &&
           choice.choiceCount() == 0 && choice.recommendedActor() == 410 &&
           choice.recommendedTarget() == -1 && choice.targets().empty(),
           "Adventure battle choice must expose a target-free opening leader phase");
    const JsonObject pendingState = GameData::authoritativeState();
    expect(pendingState.getObject("battleSession") != nullptr &&
           GameData::restoreState(pendingState) &&
           GameData::authoritativeState().getObject("battleSession") != nullptr,
           "authoritative save/load must preserve a pending Adventure battle");

    const std::string beforeInvalidChoice = GameData::authoritativeState().toString();
    ActionList rejected;
    ActionRejection battleRejection;
    expect(!GameData::client2Adventure(attacker.avatar,
                                       ClientBattleChoice(9999, -1),
                                       rejected, &battleRejection) && rejected.empty() &&
           battleRejection.reason == ActionRejectReason::InvalidBattleChoice &&
           GameData::authoritativeState().toString() == beforeInvalidChoice,
           "Adventure must explain a forged battle actor without mutating state");

    ActionList meleeActions;
    expect(GameData::client2Adventure(attacker.avatar,
                                      ClientBattleChoice(choice.recommendedActor(), -1),
                                      meleeActions) && !meleeActions.empty() &&
           meleeActions.back().type() == Action::AdventureBattleChoice,
           "a legal opening leader must emit the next Adventure battle phase");
    const AdventureBattleChoice meleeChoice =
        static_cast<const AdventureBattleChoice &>(meleeActions.back());
    expect(meleeChoice.phase() == "attacker_melee" && meleeChoice.choiceNumber() == 1 &&
           meleeChoice.choiceCount() == 0 &&
           meleeChoice.recommendedTarget() == 420,
           "Adventure must expose the stable recommended target for manual melee");

    ActionList continued;
    expect(GameData::client2Adventure(attacker.avatar,
                                      ClientBattleChoice(meleeChoice.recommendedActor(),
                                                         meleeChoice.recommendedTarget()),
                                      continued) && !continued.empty() &&
           continued.back().type() == Action::AdventureBattleChoice,
           "a nonlethal manual attack must emit the next battle decision");
    const AdventureBattleChoice continuedChoice =
        static_cast<const AdventureBattleChoice &>(continued.back());
    expect(continuedChoice.choiceNumber() == 2 && !continuedChoice.strikes().empty(),
           "each later battle decision must carry its authoritative event timeline");

    ActionList resolved;
    expect(GameData::client2Adventure(attacker.avatar,
                                      ClientBattleChoice(-1, -1, true), resolved) &&
           !resolved.empty() && resolved.back().type() == Action::AdventureCombat,
           "Auto Resolve must finish an Adventure battle from a later manual phase");
    expect(GameData::authoritativeState().getObject("battleSession") == nullptr &&
           GameData::currentWind == Wind(Wind::East),
           "resolved combat must clear the pending session and leave wind advancement to the loop");
}

int runRecoverySelfTest()
{
    const char* directoryValue = std::getenv("FOUR_WINDS_RECOVERY_DIR");
    if(!directoryValue || !*directoryValue)
    {
        std::cerr << "FAIL: recovery test directory is missing\n";
        return 1;
    }

    const std::filesystem::path directory(directoryValue);
    std::error_code error;
    std::filesystem::remove_all(directory, error);

    for(int sequence = 1; sequence <= 4; ++sequence)
    {
        JsonObject metadata;
        metadata.addInteger("sequence", sequence);
        if(!Recovery::writeCheckpoint(directory.string(),
                                      std::string("state-") + std::to_string(sequence), metadata))
        {
            std::cerr << "FAIL: recovery checkpoint write failed at sequence " << sequence << '\n';
            return 1;
        }
    }

    bool valid = true;
    for(int slot = 0; slot < Recovery::SlotCount; ++slot)
    {
        std::string save;
        std::string metadataText;
        const int expected = 4 - slot;
        valid = valid && Systems::readFile2String(Recovery::savePath(directory.string(), slot), save) &&
            save == std::string("state-") + std::to_string(expected);
        valid = valid && Systems::readFile2String(Recovery::metadataPath(directory.string(), slot), metadataText) &&
            JsonContentString(metadataText).toObject().getInteger("sequence") == expected;
    }

    valid = valid && !Systems::isFile(Recovery::savePath(directory.string(), Recovery::SlotCount)) &&
        !Systems::isFile(Recovery::metadataPath(directory.string(), Recovery::SlotCount)) &&
        !Systems::isFile(Recovery::savePath(directory.string(), 0) + ".tmp") &&
        !Systems::isFile(Recovery::metadataPath(directory.string(), 0) + ".tmp");

    if(!valid)
    {
        std::cerr << "FAIL: recovery rotation or metadata pairing is invalid\n";
        return 1;
    }

    JsonObject orderedState;
    orderedState.addInteger("alpha", 1);
    orderedState.addInteger("omega", 2);
    JsonObject reversedState;
    reversedState.addInteger("omega", 2);
    reversedState.addInteger("alpha", 1);
    if(Recovery::stateHash(orderedState) != Recovery::stateHash(reversedState))
    {
        std::cerr << "FAIL: canonical state hash depends on object insertion order\n";
        return 1;
    }

    GameData::initPersons(Person(Avatar::Nucrus, Clan::Red, Wind::East));
    LocalPlayer* recoveryPlayer = GameData::gamers.playerOfWind(Wind::East);
    if(!recoveryPlayer)
    {
        std::cerr << "FAIL: production recovery player is missing\n";
        return 1;
    }
    BattleParty recoveryParty(recoveryPlayer->clan, Land::Corzen);
    if(!recoveryParty.join(combatCreature(recoveryPlayer->clan, Creature::Durlock,
                                          701, 2, 0, 2, 3, 1)))
    {
        std::cerr << "FAIL: production recovery replay party could not be created\n";
        return 1;
    }
    recoveryPlayer->army.push_back(recoveryParty);
    GameData::setAIDifficulty(AI::Difficulty::Hard);
    GameData::initAdventure();
    ActionList recoveryActions;
    if(!GameData::client2Adventure(recoveryPlayer->avatar,
                                   ClientUnitMoved(701, Land::None), recoveryActions))
    {
        std::cerr << "FAIL: production recovery replay action was rejected\n";
        return 1;
    }
    JsonObject gui;
    gui.addString("type", "RecoverySelfTest");
    if(!GameData::saveRecovery(gui, "production-self-test"))
    {
        std::cerr << "FAIL: production recovery checkpoint failed\n";
        return 1;
    }

    std::string productionSave;
    std::string productionMetadataText;
    valid = Systems::readFile2String(Recovery::savePath(directory.string(), 0), productionSave) &&
        Systems::readFile2String(Recovery::metadataPath(directory.string(), 0), productionMetadataText);
    const JsonObject productionState = JsonContentString(productionSave).toObject();
    const JsonObject productionMetadata = JsonContentString(productionMetadataText).toObject();
    const JsonArray* breadcrumbs = productionMetadata.getArray("recentBreadcrumbs");
    const JsonObject* metadataRng = productionMetadata.getObject("gameplayRng");
    const JsonObject* savedRng = productionState.getObject("gameplayRng");
    const JsonObject* replay = productionMetadata.getObject("replay");
    const JsonObject* savedRuleset = productionState.getObject(RuneGameRulesetIdentityKey);
    const JsonObject* metadataRuleset =
        productionMetadata.getObject(RuneGameRulesetIdentityKey);
    const JsonObject* replayRuleset = replay ?
        replay->getObject(RuneGameRulesetIdentityKey) : nullptr;
    const JsonObject* savedTopology = productionState.getObject(MatchTopologyIdentityKey);
    const JsonObject* metadataTopology =
        productionMetadata.getObject(MatchTopologyIdentityKey);
    const JsonObject* replayTopology = replay ?
        replay->getObject(MatchTopologyIdentityKey) : nullptr;
    const JsonObject* savedPackage = productionState.getObject(ContentPackageIdentityKey);
    const JsonObject* metadataPackage =
        productionMetadata.getObject(ContentPackageIdentityKey);
    const JsonObject* replayPackage = replay ?
        replay->getObject(ContentPackageIdentityKey) : nullptr;
    std::string replayError;
    valid = valid && productionState.isValid() &&
        productionState.getInteger("version") == FORMAT_VERSION_CURRENT &&
        productionMetadata.getInteger("schema") == 1 &&
        productionMetadata.getString("reason") == "production-self-test" &&
        productionMetadata.getString("gameRevision").size() > 6 &&
        productionMetadata.getString("engineRevision").size() > 6 &&
        productionMetadata.getString("fileHashFNV1a64") == Recovery::stateHash(productionSave) &&
        productionMetadata.getString("stateHashFNV1a64") == Recovery::stateHash(productionState) &&
        productionMetadata.getInteger("gamePart") == Menu::AdventurePart &&
        productionMetadata.getString("aiDifficulty") == "hard" &&
        metadataRng && savedRng && metadataRng->getString("algorithm") == GameplayRng::Algorithm &&
        metadataRng->getString("state") == savedRng->getString("state") &&
        savedRuleset && metadataRuleset && replayRuleset &&
        savedRuleset->getString("id") == ClassicRuneGameRulesetId &&
        savedRuleset->getInteger("version") == ClassicRuneGameRulesetVersion &&
        metadataRuleset->getString("id") == ClassicRuneGameRulesetId &&
        metadataRuleset->getInteger("version") == ClassicRuneGameRulesetVersion &&
        replayRuleset->getString("id") == ClassicRuneGameRulesetId &&
        replayRuleset->getInteger("version") == ClassicRuneGameRulesetVersion &&
        savedTopology && metadataTopology && replayTopology &&
        savedTopology->getString("id") == ClassicFreeForAllTopologyId &&
        savedTopology->getInteger("version") == ClassicFreeForAllTopologyVersion &&
        metadataTopology->getString("id") == ClassicFreeForAllTopologyId &&
        metadataTopology->getInteger("version") == ClassicFreeForAllTopologyVersion &&
        replayTopology->getString("id") == ClassicFreeForAllTopologyId &&
        replayTopology->getInteger("version") == ClassicFreeForAllTopologyVersion &&
        savedPackage && metadataPackage && replayPackage &&
        savedPackage->getString("id") == ClassicContentPackageId &&
        savedPackage->getInteger("version") == ClassicContentPackageVersion &&
        metadataPackage->getString("id") == ClassicContentPackageId &&
        metadataPackage->getInteger("version") == ClassicContentPackageVersion &&
        replayPackage->getString("id") == ClassicContentPackageId &&
        replayPackage->getInteger("version") == ClassicContentPackageVersion &&
        replay && replay->getInteger("actionCount") == 1 &&
        replay->getBoolean("contiguousToCheckpoint") && Replay::run(*replay, &replayError) &&
        breadcrumbs && 0 < breadcrumbs->size();

    const std::filesystem::path replayDirectory = directory / "replays";
    std::string replayFile;
    std::string replayStorageError;
    const bool replayWritten = replay && ReplayFiles::writeAutomatic(
        replayDirectory.string(), *replay, &replayFile, &replayStorageError);
    const std::vector<ReplayFiles::Info> storedReplays =
        ReplayFiles::inspect(replayDirectory.string());
    valid = valid && replayWritten && replayStorageError.empty() &&
        Systems::isFile(replayFile) && storedReplays.size() == 1 &&
        storedReplays[0].valid && storedReplays[0].path == replayFile &&
        storedReplays[0].journal.actionCount == 1 &&
        storedReplays[0].journal.difficulty == "hard" &&
        storedReplays[0].journal.rulesetId == ClassicRuneGameRulesetId &&
        storedReplays[0].journal.topologyId == ClassicFreeForAllTopologyId &&
        storedReplays[0].journal.contentPackageId == ClassicContentPackageId;

    const std::filesystem::path brokenReplay = replayDirectory / "broken.fwr";
    valid = valid && Systems::saveString2File("not-json", brokenReplay.string());
    const std::vector<ReplayFiles::Info> inspectedReplays =
        ReplayFiles::inspect(replayDirectory.string());
    valid = valid && inspectedReplays.size() == 2 && inspectedReplays[0].valid &&
        !inspectedReplays[1].valid && !inspectedReplays[1].error.empty();

    const std::filesystem::path outsideReplay = directory / "outside.fwr";
    valid = valid && Systems::saveString2File(replay ? replay->toString() : "{}",
                                               outsideReplay.string());

    const std::filesystem::path transferLibrary = directory / "transfer-library";
    const std::filesystem::path transferOutput = directory / "transfer-output";
    std::string importedReplay;
    replayStorageError.clear();
    valid = valid && ReplayFiles::importReplay(transferLibrary.string(),
                                               outsideReplay.string(),
                                               &importedReplay,
                                               &replayStorageError) &&
        replayStorageError.empty() && Systems::isFile(importedReplay) &&
        ReplayFiles::inspect(transferLibrary.string()).size() == 1;

    std::string duplicateImport;
    replayStorageError.clear();
    valid = valid && ReplayFiles::importReplay(transferLibrary.string(),
                                               outsideReplay.string(),
                                               &duplicateImport,
                                               &replayStorageError) &&
        replayStorageError.empty() && duplicateImport != importedReplay &&
        Systems::isFile(duplicateImport) &&
        ReplayFiles::inspect(transferLibrary.string()).size() == 2;

    std::string exportedReplay;
    replayStorageError.clear();
    const std::filesystem::path exportWithoutExtension = transferOutput / "shared-session";
    valid = valid && ReplayFiles::exportReplay(transferLibrary.string(), importedReplay,
                                               exportWithoutExtension.string(), false,
                                               &exportedReplay, &replayStorageError) &&
        replayStorageError.empty() &&
        std::filesystem::path(exportedReplay).extension() == ".fwr" &&
        Systems::isFile(exportedReplay);
    std::string importedContents;
    std::string exportedContents;
    valid = valid && Systems::readFile2String(importedReplay, importedContents) &&
        Systems::readFile2String(exportedReplay, exportedContents) &&
        importedContents == exportedContents;

    replayStorageError.clear();
    valid = valid && !ReplayFiles::exportReplay(transferLibrary.string(), importedReplay,
                                                exportedReplay, false, nullptr,
                                                &replayStorageError) &&
        !replayStorageError.empty();
    replayStorageError.clear();
    valid = valid && ReplayFiles::exportReplay(transferLibrary.string(), importedReplay,
                                               exportedReplay, true, nullptr,
                                               &replayStorageError) &&
        replayStorageError.empty();

    const std::filesystem::path invalidImport = directory / "invalid-import.fwr";
    valid = valid && Systems::saveString2File("not-json", invalidImport.string());
    replayStorageError.clear();
    valid = valid && !ReplayFiles::importReplay(transferLibrary.string(),
                                                invalidImport.string(), nullptr,
                                                &replayStorageError) &&
        !replayStorageError.empty() &&
        ReplayFiles::inspect(transferLibrary.string()).size() == 2;
    replayStorageError.clear();
    valid = valid && !ReplayFiles::exportReplay(transferLibrary.string(),
                                                outsideReplay.string(),
                                                (transferOutput / "outside-copy.fwr").string(),
                                                false, nullptr, &replayStorageError) &&
        !replayStorageError.empty();

    replayStorageError.clear();
    valid = valid && !ReplayFiles::deleteReplay(replayDirectory.string(),
                                                outsideReplay.string(),
                                                &replayStorageError) &&
        !replayStorageError.empty() && Systems::isFile(outsideReplay.string());
    replayStorageError.clear();
    valid = valid && ReplayFiles::deleteReplay(replayDirectory.string(), replayFile,
                                                &replayStorageError) &&
        replayStorageError.empty() && !Systems::isFile(replayFile);

    JsonObject legacyState = jsonObjectWithoutKey(productionState,
                                                   RuneGameRulesetIdentityKey);
    legacyState = jsonObjectWithoutKey(legacyState, MatchTopologyIdentityKey);
    legacyState = jsonObjectWithoutKey(legacyState, ContentPackageIdentityKey);
    std::string legacyError;
    valid = valid && Recovery::validateSaveState(legacyState, &legacyError) &&
        legacyError.empty() && GameData::restoreState(legacyState) &&
        activeRuneGameRuleset().id() == ClassicRuneGameRulesetId &&
        activeRuneGameRuleset().version() == ClassicRuneGameRulesetVersion &&
        activeMatchTopology().id() == ClassicFreeForAllTopologyId &&
        activeMatchTopology().version() == ClassicFreeForAllTopologyVersion;

    JsonObject incompatibleState = productionState;
    JsonObject incompatibleRuleset;
    incompatibleRuleset.addString("id", "removed-variant");
    incompatibleRuleset.addInteger("version", 3);
    incompatibleState.addObject(RuneGameRulesetIdentityKey, incompatibleRuleset);
    std::string incompatibleError;
    const std::string stateBeforeIncompatibleRestore =
        GameData::authoritativeState().toString();
    valid = valid && !Recovery::validateSaveState(incompatibleState, &incompatibleError) &&
        incompatibleError.find("removed-variant@3") != std::string::npos &&
        !GameData::restoreState(incompatibleState) &&
        GameData::authoritativeState().toString() == stateBeforeIncompatibleRestore;

    JsonObject incompatibleTopologyState = productionState;
    JsonObject incompatibleTopology;
    incompatibleTopology.addString("id", "removed-duel");
    incompatibleTopology.addInteger("version", 3);
    incompatibleTopologyState.addObject(MatchTopologyIdentityKey, incompatibleTopology);
    std::string incompatibleTopologyError;
    const std::string stateBeforeIncompatibleTopologyRestore =
        GameData::authoritativeState().toString();
    valid = valid &&
        !Recovery::validateSaveState(incompatibleTopologyState, &incompatibleTopologyError) &&
        incompatibleTopologyError.find("removed-duel@3") != std::string::npos &&
        !GameData::restoreState(incompatibleTopologyState) &&
        GameData::authoritativeState().toString() == stateBeforeIncompatibleTopologyRestore;

    JsonObject incompatiblePackageState = productionState;
    JsonObject incompatiblePackage;
    incompatiblePackage.addString("id", "removed-package");
    incompatiblePackage.addInteger("version", 4);
    incompatiblePackageState.addObject(ContentPackageIdentityKey, incompatiblePackage);
    std::string incompatiblePackageError;
    const std::string stateBeforeIncompatiblePackageRestore =
        GameData::authoritativeState().toString();
    valid = valid &&
        !Recovery::validateSaveState(incompatiblePackageState, &incompatiblePackageError) &&
        incompatiblePackageError.find("removed-package@4") != std::string::npos &&
        !GameData::restoreState(incompatiblePackageState) &&
        GameData::authoritativeState().toString() == stateBeforeIncompatiblePackageRestore;

    const std::filesystem::path manualDirectory = directory / "manual-saves";
    std::string manualFile;
    std::string saveError;
    valid = valid && SaveGames::writeManual(manualDirectory.string(), productionState,
                                            "  Round One  ", false,
                                            &manualFile, &saveError) &&
        saveError.empty() && Systems::isFile(manualFile);

    const std::vector<SaveGames::Info> manualSaves =
        SaveGames::inspect(manualDirectory.string());
    valid = valid && manualSaves.size() == 1 && manualSaves[0].valid &&
        manualSaves[0].name == "Round One" && manualSaves[0].path == manualFile &&
        manualSaves[0].savedAtEpoch > 0 &&
        manualSaves[0].gamePart == Menu::AdventurePart &&
        SaveGames::existingPath(manualDirectory.string(), "round one") == manualFile;

    saveError.clear();
    valid = valid && !SaveGames::writeManual(manualDirectory.string(), productionState,
                                             "Round One", false, nullptr, &saveError) &&
        !saveError.empty();

    saveError.clear();
    std::string overwrittenFile;
    valid = valid && SaveGames::writeManual(manualDirectory.string(), productionState,
                                            "Round One", true,
                                            &overwrittenFile, &saveError) &&
        saveError.empty() && overwrittenFile == manualFile &&
        Systems::isFile(manualFile + ".before-overwrite");

    std::filesystem::path screenshot = std::filesystem::path(manualFile);
    screenshot.replace_extension(".png");
    valid = valid && Systems::saveString2File("screenshot", screenshot.string());

    const std::filesystem::path outsideSave = directory / "outside.sav";
    valid = valid && Systems::saveString2File(productionSave, outsideSave.string());
    saveError.clear();
    valid = valid && !SaveGames::deleteManual(manualDirectory.string(),
                                              outsideSave.string(), &saveError) &&
        !saveError.empty() && Systems::isFile(outsideSave.string());

    saveError.clear();
    valid = valid && SaveGames::deleteManual(manualDirectory.string(), manualFile,
                                             &saveError) &&
        saveError.empty() && !Systems::isFile(manualFile) &&
        !Systems::isFile(screenshot.string()) &&
        !Systems::isFile(manualFile + ".before-overwrite") &&
        SaveGames::inspect(manualDirectory.string()).empty();

    const std::string autosave = (directory / "autosave.sav").string();
    saveError.clear();
    valid = valid && SaveGames::writeAutosave(autosave, productionState, &saveError) &&
        saveError.empty() && Recovery::validateSaveFile(autosave);
    std::string autosaveText;
    valid = valid && Systems::readFile2String(autosave, autosaveText);
    const JsonObject autosaveState = JsonContentString(autosaveText).toObject();
    valid = valid && autosaveState.getString("save:kind") == "autosave" &&
        autosaveState.getString("save:name") == "Autosave" &&
        !autosaveState.getString("save:savedAtEpoch").empty();
    saveError.clear();
    valid = valid && SaveGames::writeAutosave(autosave, productionState, &saveError) &&
        saveError.empty() && Systems::isFile(autosave + ".previous");

    const JsonArray* productionPlayers = productionState.getArray("players");
    if(!productionPlayers || productionPlayers->size() < 2)
    {
        valid = false;
    }
    else
    {
        JsonArray incompletePlayers;
        incompletePlayers.addObject(*productionPlayers->getObject(0));
        incompletePlayers.addObject(*productionPlayers->getObject(1));
        JsonObject incompleteState = productionState;
        incompleteState.addArray("players", incompletePlayers);

        std::string incompleteError;
        const std::string stateBeforeRejectedRestore = GameData::authoritativeState().toString();
        valid = valid && !Recovery::validateSaveState(incompleteState, &incompleteError) &&
            incompleteError == "save does not contain four players" &&
            !GameData::restoreState(incompleteState) &&
            GameData::authoritativeState().toString() == stateBeforeRejectedRestore;
    }

    const std::vector<Recovery::CheckpointInfo> inspected =
        Recovery::inspectCheckpoints(directory.string());
    valid = valid && inspected.size() == Recovery::SlotCount &&
        inspected[0].slot == 0 && inspected[0].valid &&
        inspected[0].savedAtEpoch > 0 &&
        inspected[0].gamePart == Menu::AdventurePart &&
        inspected[0].aiDifficulty == "hard" &&
        inspected[0].runeGameRulesetId == ClassicRuneGameRulesetId &&
        inspected[0].runeGameRulesetVersion == ClassicRuneGameRulesetVersion &&
        inspected[0].contentPackageId == ClassicContentPackageId &&
        inspected[0].contentPackageVersion == ClassicContentPackageVersion &&
        !inspected[1].valid && !inspected[2].valid;

    const std::string promoted = (directory / "game.sav").string();
    const std::string promotedBackup = promoted + ".before-recovery";
    std::string promotionError;
    std::string promotedData;
    std::string promotedBackupData;
    valid = valid && Systems::saveString2File("old-primary", promoted) &&
        Recovery::promoteSave(Recovery::savePath(directory.string(), 0), promoted,
                              &promotionError) && promotionError.empty() &&
        Systems::readFile2String(promoted, promotedData) && promotedData == productionSave &&
        Systems::readFile2String(promotedBackup, promotedBackupData) &&
        promotedBackupData == "old-primary" &&
        !Systems::isFile(promoted + ".recovery.tmp");

    valid = valid && Systems::saveString2File(productionSave + " ",
                                               Recovery::savePath(directory.string(), 0)) &&
        !Recovery::inspectCheckpoint(directory.string(), 0).valid;

    if(!valid)
    {
        std::cerr << "FAIL: production recovery metadata or state is invalid\n";
        return 1;
    }

    std::cout << "recovery checkpointing: ok\n";
    return 0;
}

#if defined(_WIN32)
int runWindowsCrashReportSelfTest(const char* executable)
{
    const char* directoryValue = std::getenv("FOUR_WINDS_DIAGNOSTICS_DIR");
    if(!directoryValue || !*directoryValue)
    {
        std::cerr << "FAIL: Windows crash test diagnostics directory is missing\n";
        return 1;
    }

    const std::filesystem::path directory(directoryValue);
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    for(const auto & entry : std::filesystem::directory_iterator(directory, error))
    {
        if(entry.path().extension() == ".dmp")
            std::filesystem::remove(entry.path(), error);
    }
    std::filesystem::remove(directory / "crash-report.log", error);

    const std::string childExecutable = std::filesystem::absolute(executable).string();
    std::string command = "\"" + childExecutable + "\" --windows-crash-report-child";
    std::vector<char> commandLine(command.begin(), command.end());
    commandLine.push_back('\0');

    STARTUPINFOA startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process = {};
    if(!CreateProcessA(nullptr, commandLine.data(), nullptr, nullptr, FALSE, 0,
                       nullptr, nullptr, &startup, &process))
    {
        std::cerr << "FAIL: Windows crash test child could not start\n";
        return 1;
    }

    const DWORD waitResult = WaitForSingleObject(process.hProcess, 30000);
    DWORD exitCode = STILL_ACTIVE;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

    constexpr int captureAttemptsMaximum = 40;
    constexpr DWORD captureRetryDelayMs = 50;
    bool dumpFound = false;
    bool reportValid = false;
    int captureAttempts = 0;
    std::string report;
    for(; captureAttempts < captureAttemptsMaximum; ++captureAttempts)
    {
        dumpFound = false;
        error.clear();
        for(const auto & entry : std::filesystem::directory_iterator(directory, error))
        {
            if(entry.path().extension() == ".dmp" && entry.file_size(error) > 0)
                dumpFound = true;
        }

        report.clear();
        reportValid = Systems::readFile2String(
            (directory / "crash-report.log").string(), report) &&
            report.find("[FATAL WINDOWS EXCEPTION]") != std::string::npos &&
            report.find("code=0xC0000005") != std::string::npos &&
            report.find("status=written") != std::string::npos;

        if(dumpFound && reportValid) break;
        if(captureAttempts + 1 < captureAttemptsMaximum) Sleep(captureRetryDelayMs);
    }

    if(waitResult != WAIT_OBJECT_0 || exitCode == 0 || !dumpFound || !reportValid)
    {
        std::cerr << "FAIL: Windows native crash capture is incomplete"
                  << ", wait=" << waitResult << ", exit=" << exitCode
                  << ", dump=" << dumpFound << ", report=" << reportValid
                  << ", attempts=" << std::min(captureAttempts + 1, captureAttemptsMaximum)
                  << ", report_bytes=" << report.size() << '\n';
        return 1;
    }

    std::cout << "windows native crash reporting: ok\n";
    return 0;
}
#endif

void testMahjongCastDeadTargetMessage()
{
    const LocalPlayers savedPlayers = GameData::gamers;
    const Wind savedCurrentWind = GameData::currentWind;
    GameData::gamers.clear();

    LocalPlayer targetOwner;
    targetOwner.avatar = Avatar::Lakkho;
    targetOwner.clan = Clan::Yellow;
    targetOwner.wind = Wind::South;
    BattleParty targetParty(Clan::Yellow, Land::Zubrus);
    expect(targetParty.join(combatCreature(Clan::Yellow, Creature::Durlock,
                                           490, 1, 0, 1, 1)),
           "lethal spell message fixture must join");
    targetOwner.army.push_back(targetParty);
    GameData::gamers.push_back(targetOwner);

    LocalData observerSnapshot;
    observerSnapshot.players[0] = targetOwner;

    BattleTargets targets;
    targets << GameData::findBattleCreature(490);
    const MahjongCast message(Wind::East, Spell(Spell::LightningBolt),
                              Land::Zubrus, targets, std::vector<int>());
    expect(message.targetUnits().size() == 1 && message.targetUnits().front() == 490,
           "Mahjong cast messages must preserve target ids independently of live state");

    BattleCreature* liveTarget = GameData::findBattleCreature(490);
    expect(liveTarget != nullptr, "lethal spell message target must begin in live state");
    if(liveTarget) liveTarget->applyDamage(1);
    GameData::gamers.front().army.removeUnloyalty();

    expect(GameData::findBattleCreature(490) == nullptr,
           "lethal spell fixture must remove the target before the client reads its message");
    expect(message.targets().empty(),
           "decoding a stale spell target must not throw or resolve a removed live unit");
    expect(observerSnapshot.findBattleUnitConst(490) != nullptr,
           "the observer snapshot must retain target details for spell presentation");

    GameData::gamers.clear();
    LocalPlayer caster;
    caster.avatar = Avatar::Dayla;
    caster.clan = Clan::Purple;
    caster.wind = Wind::East;
    caster.points = 1000;
    GameData::gamers.push_back(caster);

    targetOwner.avatar = Avatar::Logun;
    targetOwner.clan = Clan::Yellow;
    targetOwner.wind = Wind::South;
    targetOwner.army.clear();
    BattleParty lethalParty(Clan::Yellow, Land::TowerOf4Winds);
    expect(lethalParty.join(combatCreature(Clan::Yellow, Creature::Durlock,
                                           491, 1, 0, 1, 1)),
           "lethal spell dispatch fixture must join");
    targetOwner.army.push_back(lethalParty);
    GameData::gamers.push_back(targetOwner);
    GameData::currentWind = Wind(Wind::East);

    ActionList emitted;
    const ClientCastSpell lethalCast(Spell(Spell::LightningBolt),
                                     Land(Land::TowerOf4Winds), 491, true);
    expect(GameData::client2Mahjong(Avatar(Avatar::Dayla), lethalCast, emitted),
           "lethal Lightning Bolt dispatch must complete without allocation failure");
    auto info = std::find_if(emitted.begin(), emitted.end(), [](const ActionMessage & action)
    {
        return action.type() == Action::MahjongInfo;
    });
    const std::string vanquishedInfo = info == emitted.end() ? std::string() :
        info->getString("info");
    expect(vanquishedInfo.find("vanquished") != std::string::npos,
           "lethal spell dispatch must own a valid vanquished-unit message");
    expect(vanquishedInfo.find("Durlock") != std::string::npos &&
           vanquishedInfo.find("battleUnit") == std::string::npos &&
           vanquishedInfo.find("creature(") == std::string::npos,
           "vanquished-unit UI messages must use the display name without diagnostic ids");
    expect(GameData::findBattleCreature(491) == nullptr,
           "lethal Lightning Bolt dispatch must remove its defeated target");

    GameData::gamers = savedPlayers;
    GameData::currentWind = savedCurrentWind;
}

int runCapturedLightningRepro(const char* savePath)
{
    if(!savePath || !GameData::loadGame(savePath))
    {
        std::cerr << "FAIL: captured recovery save could not be loaded\n";
        return 1;
    }

    ActionList actions;
    if(!GameData::mahjong2Client(Avatar(Avatar::Logun), actions))
    {
        std::cerr << "FAIL: captured pre-crash Mahjong prompt was not restored\n";
        return 1;
    }

    actions.clear();
    if(!GameData::client2Mahjong(Avatar(Avatar::Logun), ClientButtonPass(), actions))
    {
        std::cerr << "FAIL: captured pre-crash pass action was rejected\n";
        return 1;
    }

    actions.clear();
    if(!GameData::mahjong2Client(Avatar(Avatar::Logun), actions))
    {
        std::cerr << "FAIL: captured post-Pung AI turn did not advance\n";
        return 1;
    }

    const bool vanquishedMessage = std::any_of(actions.begin(), actions.end(),
        [](const ActionMessage & action)
        {
            return action.type() == Action::MahjongInfo &&
                action.getString("info").find("vanquished") != std::string::npos;
        });
    if(!vanquishedMessage || GameData::findBattleCreature(1))
    {
        std::cerr << "FAIL: captured continuation did not execute lethal Lightning Bolt\n";
        for(const ActionMessage & action : actions)
            std::cerr << "  action=" << action.toString() << '\n';
        return 1;
    }

    std::cout << "captured Lightning Bolt replay: ok\n";
    return 0;
}

void testGameSummaryInputGuard()
{
    using namespace GameSummaryInput;

    expect(!acceptsVictoryPageClick(false, 0, 0),
           "victory page must reject a release whose press was not observed");
    expect(!acceptsVictoryPageClick(true, 0, 1),
           "victory page must reject a click pressed on the previous winner");
    expect(acceptsVictoryPageClick(true, 1, 1),
           "victory page must accept a fresh press and release on the same winner");

    expect(!blocksScorePageDone(0, 1000),
           "score-page input guard must be inactive until the summary is opened");
    expect(blocksScorePageDone(1000, 1000),
           "score-page input guard must block the transition event itself");
    expect(blocksScorePageDone(1000, 1000 + ScorePageGuardMilliseconds - 1),
           "score-page input guard must remain active through its final millisecond");
    expect(!blocksScorePageDone(1000, 1000 + ScorePageGuardMilliseconds),
           "score-page input guard must release normal input at the boundary");
    expect(!blocksScorePageDone(1000, 999),
           "score-page input guard must tolerate a non-monotonic test timestamp");

    expect(doneDisposition(true, 0, 1000) == DoneDisposition::AdvanceVictory,
           "the visible Done button must advance while a winner portrait is shown");
    expect(doneDisposition(false, 1000, 1100) == DoneDisposition::Ignore,
           "the Done button must ignore the event that just opened the score page");
    expect(doneDisposition(false, 1000, 1000 + ScorePageGuardMilliseconds) ==
               DoneDisposition::Close,
           "the Done button must close the settled score page");
}

void testMatchScoreContract()
{
    std::vector<MatchScore::PlayerInput> inputs(4);
    inputs[0].person = Person(Avatar::Nucrus, Clan::Red, Wind::East);
    inputs[1].person = Person(Avatar::Lakkho, Clan::Yellow, Wind::South);
    inputs[2].person = Person(Avatar::Ziag, Clan::Aqua, Wind::West);
    inputs[3].person = Person(Avatar::Dayla, Clan::Purple, Wind::North);

    inputs[0].scores = {{ 10, 3, 300, 100, 50 }};
    inputs[1].scores = {{ 10, 2, 250, 200, 40 }};
    inputs[2].scores = {{ 5, 2, 300, 50, 50 }};
    inputs[3].scores = {{ 0, 1, 100, -10, 10 }};

    const MatchScore::Results scores = MatchScore::calculate(inputs);
    expect(scores.size() == 4, "match score must retain all players");
    if(scores.size() != 4) return;

    expect(scores[0].categories[MatchScore::index(MatchScore::Category::Territory)].rank == 1 &&
           scores[1].categories[MatchScore::index(MatchScore::Category::Territory)].rank == 1 &&
           scores[2].categories[MatchScore::index(MatchScore::Category::Territory)].rank == 3,
           "category ties must use competition ranks");
    expect(scores[0].totalScore == 19 && scores[1].totalScore == 15 &&
           scores[2].totalScore == 15 && scores[3].totalScore == 5,
           "total score must sum category standing points");
    expect(scores[0].finalRank == 1 && scores[1].finalRank == 2 &&
           scores[2].finalRank == 2 && scores[3].finalRank == 4,
           "final ties must retain shared competition rank");
    expect(MatchScore::winnerIndices(scores) == std::vector<std::size_t>({ 0 }),
           "winner selection must return the sole first-place player");
    expect(scores[3].categories[MatchScore::index(MatchScore::Category::SpellPoints)].score == 0,
           "negative resource values must not reduce canonical score");

    inputs[1].scores = inputs[0].scores;
    const MatchScore::Results tiedScores = MatchScore::calculate(inputs);
    expect(MatchScore::winnerIndices(tiedScores) == std::vector<std::size_t>({ 0, 1 }),
           "winner selection must preserve every tied first-place player");
}

void configureSimulationBonuses()
{
    GameData::bonusStart = 250;
    GameData::bonusGame = 50;
    GameData::bonusKong = 40;
    GameData::bonusPung = 30;
    GameData::bonusChao = 20;
    GameData::bonusPass = 10;
}

bool applyBalanceRoster(Tournament::Config &, const char*);

void testTournamentContract()
{
    Tournament::Config config;
    config.seeds = { UINT64_C(0x15a50001), UINT64_C(0x15a50002) };
    config.avatars = Tournament::baselineAvatars();
    config.maximumTicks = 20000;

    const Tournament::Schedule schedule = Tournament::buildSchedule(config);
    expect(schedule.valid(), "baseline tournament schedule must be valid");
    expect(schedule.legalClanAssignments == 1,
           "fixed-clan baseline must have exactly one legal clan assignment");
    expect(schedule.matches.size() == 8,
           "two baseline seeds must produce four seat rotations each");

    Tournament::Config forced = config;
    forced.seeds.resize(1);
    forced.difficulty = AI::Difficulty::Hard;
    forced.forceBehaviorProfile = true;
    forced.behaviorProfile = AI::BehaviorProfile::Economic;
    const Tournament::Schedule forcedSchedule = Tournament::buildSchedule(forced);
    expect(forcedSchedule.valid() &&
           std::all_of(forcedSchedule.matches.begin(), forcedSchedule.matches.end(),
               [](const Tournament::PlannedMatch & match)
               {
                   return match.match.difficulty == AI::Difficulty::Hard &&
                          match.match.forceBehaviorProfile &&
                          match.match.behaviorProfile == AI::BehaviorProfile::Economic &&
                          match.match.runeGameRulesetId == ClassicRuneGameRulesetId &&
                          match.match.runeGameRulesetVersion ==
                              ClassicRuneGameRulesetVersion;
               }),
           "every matrix cell must propagate difficulty, doctrine and ruleset to every match");

    for(std::size_t seedIndex = 0; seedIndex < config.seeds.size(); ++seedIndex)
    {
        std::map<int, int> windMasks;
        for(const Avatar & avatar : config.avatars) windMasks[avatar()] = 0;

        for(const Tournament::PlannedMatch & match : schedule.matches)
        {
            if(match.seedIndex != seedIndex) continue;
            expect(match.match.persons.size() == 4,
                   "every planned match must retain four seats");
            for(std::size_t seat = 0; seat < match.match.persons.size(); ++seat)
            {
                const Person & person = match.match.persons[seat];
                expect(person.wind() == static_cast<int>(seat + 1),
                       "planned persons must be stored in canonical wind/seat order");
                windMasks[person.avatar()] |= person.wind.mask();
            }
        }

        const int allWindMask = Wind(Wind::East).mask() | Wind(Wind::South).mask() |
            Wind(Wind::West).mask() | Wind(Wind::North).mask();
        for(const auto & item : windMasks)
            expect(item.second == allWindMask,
                   "every avatar must occupy all four winds for every seed");
    }

    Tournament::Config flexible = config;
    flexible.seeds.resize(1);
    flexible.avatars = Avatars(std::vector<Avatar>{
        Avatar(Avatar::Orachi), Avatar(Avatar::Niana),
        Avatar(Avatar::Kierac), Avatar(Avatar::Logun)
    });
    const Tournament::Schedule flexibleSchedule = Tournament::buildSchedule(flexible);
    expect(flexibleSchedule.legalClanAssignments == 4 &&
           flexibleSchedule.matches.size() == 16,
           "clan rotation must enumerate only the four legal bijections");

    Tournament::Config parsedRoster = config;
    expect(applyBalanceRoster(parsedRoster, "orachi,lakkho,ziag,dayla") &&
           parsedRoster.avatars.size() == 4 &&
           parsedRoster.avatars.front() == Avatar(Avatar::Orachi) &&
           Tournament::buildSchedule(parsedRoster).legalClanAssignments == 1,
           "CLI roster parsing must retain four unique legal avatar ids");

    Tournament::Config duplicate = config;
    duplicate.avatars[1] = duplicate.avatars[0];
    expect(!Tournament::buildSchedule(duplicate).valid(),
           "duplicate tournament avatars must be rejected");

    Tournament::Config unavailableRuleset = config;
    unavailableRuleset.runeGameRulesetId = "removed-variant";
    unavailableRuleset.runeGameRulesetVersion = 3;
    expect(!Tournament::buildSchedule(unavailableRuleset).valid() &&
           Tournament::buildSchedule(unavailableRuleset).error.find(
               "removed-variant@3") != std::string::npos,
           "tournament schedule must reject an unavailable ruleset");

    Tournament::Result synthetic;
    synthetic.config = config;
    synthetic.config.seeds.resize(1);
    synthetic.config.forceBehaviorProfile = true;
    synthetic.config.behaviorProfile = AI::BehaviorProfile::Control;
    synthetic.schedule = Tournament::buildSchedule(synthetic.config);
    Tournament::MatchRecord record;
    record.plan = synthetic.schedule.matches.front();
    record.result.status = Simulation::MatchStatus::Complete;
    record.result.seed = record.plan.match.seed;
    record.result.ticks = 123;
    record.result.mahjongHands = 16;
    record.result.adventurePhases = 16;
    record.result.emittedActions = 456;
    record.result.rngDraws = 789;
    record.result.finalStateHash = "synthetic";

    std::vector<MatchScore::PlayerInput> inputs;
    for(std::size_t player = 0; player < record.plan.match.persons.size(); ++player)
    {
        MatchScore::PlayerInput input;
        input.person = record.plan.match.persons[player];
        input.scores = {{ static_cast<int>(40 - player * 5),
                          static_cast<int>(4 - player),
                          static_cast<int>(400 - player * 50),
                          static_cast<int>(100 - player * 10),
                          static_cast<int>(80 - player * 10) }};
        inputs.push_back(input);
    }
    record.result.score = MatchScore::calculate(inputs);
    for(const Person & person : record.plan.match.persons)
    {
        Simulation::MatchResult::PlayerTelemetry telemetry;
        telemetry.avatar = person.avatar;
        telemetry.summons = 2;
        telemetry.spellsCast = 1;
        telemetry.casualties = 1;
        telemetry.dismissals = 1;
        telemetry.peakUnits = 3;
        telemetry.finalUnits = 2;
        record.result.players.push_back(telemetry);
    }
    for(std::size_t stage = 0; stage < Simulation::MatchStageCount; ++stage)
    {
        record.result.stages[stage].adventurePhase = stage == 0 ? 5 : (stage == 1 ? 10 : 16);
        record.result.stages[stage].score = record.result.score;
    }
    synthetic.matches.push_back(record);
    Tournament::summarize(synthetic);

    const std::string json = Tournament::toJsonObject(synthetic).toString();
    const std::string csv = Tournament::toCsv(synthetic);
    const std::string text = Tournament::toText(synthetic);
    const JsonContentString parsed(json);
    const JsonObject parsedTournament = parsed.toObject();
    const JsonObject* exportedRuleset =
        parsedTournament.getObject(RuneGameRulesetIdentityKey);
    expect(parsed.isValid() && parsedTournament.getInteger("schema_version") == 2 &&
           parsedTournament.getString("behavior_profile") == "control" &&
           exportedRuleset && exportedRuleset->getString("id") ==
               ClassicRuneGameRulesetId &&
           exportedRuleset->getInteger("version") == ClassicRuneGameRulesetVersion,
           "balance JSON export must be valid and versioned");
    expect(csv.find("early_territory") != std::string::npos &&
           csv.find("behavior_profile") != std::string::npos &&
           csv.find("ruleset_id,ruleset_version") != std::string::npos &&
           csv.find("spells_cast") != std::string::npos &&
           csv.find("avatar,clan,clan_id,wind") != std::string::npos &&
           csv.find(",Red,red,") != std::string::npos &&
           csv.find(",classic,1,") != std::string::npos &&
           csv.find("synthetic") != std::string::npos,
           "balance CSV must contain canonical clan names, ids, telemetry and hashes");
    expect(json.find("\"clan\": \"red\"") != std::string::npos &&
           json.find("\"clan_name\": \"Red\"") != std::string::npos,
           "balance JSON must expose canonical clan ids and display names");
    expect(text.find("95% Wilson") != std::string::npos &&
           text.find("Rune Game ruleset: classic@1") != std::string::npos,
           "human balance report must label its ruleset and uncertainty interval");
    expect(synthetic.summaries.size() == 4 && synthetic.summaries[0].completed == 1,
           "balance aggregation must retain every completed avatar result");

    Tournament::Result outliers;
    outliers.config = config;
    outliers.schedule = schedule;
    for(std::size_t index = 0; index < schedule.matches.size(); ++index)
    {
        Tournament::MatchRecord sample = record;
        sample.plan = schedule.matches[index];
        sample.result.seed = sample.plan.match.seed;
        sample.result.ticks = index + 1 == schedule.matches.size() ? 1000 : 100;
        outliers.matches.push_back(sample);
    }
    Tournament::summarize(outliers);
    expect(outliers.matches.back().replayRetentionReasons.size() == 1 &&
           outliers.matches.back().replayRetentionReasons.front() ==
               "outlier:match_ticks_iqr",
           "IQR watchdog outliers must be marked for full replay retention");
}

int runHeadlessMatchSelfTest()
{
    configureSimulationBonuses();

    Persons players;
    players.push_back(Person(Avatar::Nucrus, Clan::Red, Wind::East));
    players.push_back(Person(Avatar::Lakkho, Clan::Yellow, Wind::South));
    players.push_back(Person(Avatar::Ziag, Clan::Aqua, Wind::West));
    players.push_back(Person(Avatar::Dayla, Clan::Purple, Wind::North));
    for(Person & player : players) player.setAI(true);

    Simulation::MatchConfig config;
    config.seed = UINT64_C(0x15a50001);
    config.difficulty = AI::Difficulty::Normal;
    config.forceBehaviorProfile = true;
    config.behaviorProfile = AI::BehaviorProfile::Aggressive;
    config.persons = players;
    config.maximumTicks = 20000;
    config.captureFullReplay = true;

    const Simulation::MatchResult first = Simulation::runMatch(config);
    if(!first.completed())
    {
        std::cerr << "FAIL: first headless match status=" << Simulation::statusName(first.status)
                  << ", ticks=" << first.ticks << ", hands=" << first.mahjongHands
                  << ", error=" << first.error << '\n';
        return 1;
    }

    std::string fullReplayError;
    const JsonObject* fullReplayRuleset =
        first.actionReplay.getObject(RuneGameRulesetIdentityKey);
    if(!first.fullReplayCaptured ||
       first.runeGameRulesetId != ClassicRuneGameRulesetId ||
       first.runeGameRulesetVersion != ClassicRuneGameRulesetVersion ||
       !fullReplayRuleset ||
       fullReplayRuleset->getString("id") != ClassicRuneGameRulesetId ||
       fullReplayRuleset->getInteger("version") != ClassicRuneGameRulesetVersion ||
       first.actionReplay.getString("aiBehaviorProfile") != "aggressive" ||
       first.actionReplay.getInteger("actionCount") <= 64 ||
       !first.actionReplay.getBoolean("contiguousToCheckpoint") ||
       !Replay::run(first.actionReplay, &fullReplayError) ||
       Replay::authoritativeStateHash() != first.finalStateHash)
    {
        std::cerr << "FAIL: full-match action replay did not reproduce the final state: "
                  << fullReplayError
                  << ", captured=" << first.fullReplayCaptured
                  << ", profile=" << first.actionReplay.getString("aiBehaviorProfile")
                  << ", steps=" << first.actionReplay.getInteger("actionCount")
                  << ", contiguous="
                  << first.actionReplay.getBoolean("contiguousToCheckpoint") << '\n';
        return 1;
    }

    const Simulation::MatchResult second = Simulation::runMatch(config);
    if(!second.completed())
    {
        std::cerr << "FAIL: repeated headless match status=" << Simulation::statusName(second.status)
                  << ", ticks=" << second.ticks << ", hands=" << second.mahjongHands
                  << ", error=" << second.error << '\n';
        return 1;
    }
    if(!Recovery::enabled() || AI::behaviorProfileOverrideEnabled())
    {
        std::cerr << "FAIL: headless match did not restore recovery/profile scope\n";
        return 1;
    }

    if(first.mahjongHands != 16 || first.adventurePhases != 16 ||
       first.finalStateHash != second.finalStateHash || first.ticks != second.ticks ||
       first.emittedActions != second.emittedActions || first.rngDraws != second.rngDraws ||
       first.score.size() != second.score.size() ||
       first.players.size() != second.players.size() ||
       first.events.size() != second.events.size())
    {
        std::cerr << "FAIL: repeated headless match was not deterministic"
                  << ", first=" << first.finalStateHash << ", second=" << second.finalStateHash
                  << ", ticks=" << first.ticks << '/' << second.ticks
                  << ", hands=" << first.mahjongHands << '/' << second.mahjongHands
                  << ", adventure=" << first.adventurePhases << '/' << second.adventurePhases
                  << ", actions=" << first.emittedActions << '/' << second.emittedActions
                  << ", rng=" << first.rngDraws << '/' << second.rngDraws << '\n';
        return 1;
    }

    std::size_t summons = 0;
    for(std::size_t player = 0; player < first.players.size(); ++player)
    {
        const auto & lhs = first.players[player];
        const auto & rhs = second.players[player];
        summons += lhs.summons;
        if(lhs.avatar != rhs.avatar || lhs.summons != rhs.summons ||
           lhs.spellsCast != rhs.spellsCast || lhs.casualties != rhs.casualties ||
           lhs.dismissals != rhs.dismissals ||
           lhs.peakUnits != rhs.peakUnits || lhs.finalUnits != rhs.finalUnits ||
           lhs.summonedCreatures != rhs.summonedCreatures ||
           lhs.castSpells != rhs.castSpells)
        {
            std::cerr << "FAIL: repeated match produced different event telemetry\n";
            return 1;
        }
    }
    if(summons == 0 || first.events.empty())
    {
        std::cerr << "FAIL: headless match did not capture authoritative match events\n";
        return 1;
    }

    for(const Simulation::MatchResult::Event & event : first.events)
    {
        if(event.type != "summon" && event.type != "spell")
            continue;

        const AvatarInfo & avatar = GameData::avatarInfo(event.avatar);
        bool legal = false;
        if(event.type == "summon")
        {
            const Creature creature(event.subject);
            legal = std::find(avatar.creatures.begin(), avatar.creatures.end(), creature) !=
                    avatar.creatures.end();
        }
        else if(event.type == "spell")
        {
            const Spell spell(event.subject);
            legal = std::find(avatar.spells.begin(), avatar.spells.end(), spell) !=
                    avatar.spells.end();
            for(const Creature & creature : avatar.creatures)
            {
                if(legal) break;
                for(const Speciality & speciality :
                    GameData::creatureInfo(creature).specials.toList())
                {
                    if(speciality.toSpell() == spell)
                    {
                        legal = true;
                        break;
                    }
                }
            }
        }

        if(!legal)
        {
            std::cerr << "FAIL: event telemetry attributed an illegal " << event.type
                      << " to " << event.avatar.toString() << ": " << event.subject << '\n';
            return 1;
        }
    }

    for(std::size_t player = 0; player < first.score.size(); ++player)
    {
        if(first.score[player].person.avatar != second.score[player].person.avatar ||
           first.score[player].totalScore != second.score[player].totalScore ||
           first.score[player].finalRank != second.score[player].finalRank)
        {
            std::cerr << "FAIL: repeated headless match produced a different final score\n";
            return 1;
        }
        for(std::size_t category = 0; category < MatchScore::CategoryCount; ++category)
        {
            const MatchScore::CategoryResult & lhs = first.score[player].categories[category];
            const MatchScore::CategoryResult & rhs = second.score[player].categories[category];
            if(lhs.score != rhs.score || lhs.rank != rhs.rank ||
               lhs.standingPoints != rhs.standingPoints)
            {
                std::cerr << "FAIL: repeated headless match produced different category scores\n";
                return 1;
            }
        }
    }

    const std::array<std::size_t, Simulation::MatchStageCount> expectedPhases = {{ 5, 10, 16 }};
    for(std::size_t stage = 0; stage < Simulation::MatchStageCount; ++stage)
    {
        if(!first.stages[stage].captured() ||
           first.stages[stage].adventurePhase != expectedPhases[stage] ||
           first.stages[stage].score.size() != second.stages[stage].score.size())
        {
            std::cerr << "FAIL: headless match did not capture deterministic stage telemetry\n";
            return 1;
        }
        for(std::size_t player = 0; player < first.stages[stage].score.size(); ++player)
        {
            for(std::size_t category = 0; category < MatchScore::CategoryCount; ++category)
            {
                if(first.stages[stage].score[player].categories[category].score !=
                   second.stages[stage].score[player].categories[category].score)
                {
                    std::cerr << "FAIL: repeated match produced different stage telemetry\n";
                    return 1;
                }
            }
        }
    }

    Simulation::MatchConfig invalid = config;
    invalid.persons[1].wind = Wind(Wind::East);
    const Simulation::MatchResult rejected = Simulation::runMatch(invalid);
    if(rejected.status != Simulation::MatchStatus::InvalidConfiguration)
    {
        std::cerr << "FAIL: duplicate wind configuration was not rejected\n";
        return 1;
    }

    Settings::setContentTheme("../unsafe");
    if(Settings::contentTheme() != "classic")
    {
        std::cerr << "FAIL: unsafe content theme names must fall back to classic\n";
        return 1;
    }

    Simulation::MatchConfig unavailableRuleset = config;
    unavailableRuleset.runeGameRulesetId = "removed-variant";
    unavailableRuleset.runeGameRulesetVersion = 3;
    const Simulation::MatchResult unavailable = Simulation::runMatch(unavailableRuleset);
    if(unavailable.status != Simulation::MatchStatus::InvalidConfiguration ||
       unavailable.error.find("removed-variant@3") == std::string::npos)
    {
        std::cerr << "FAIL: unavailable Rune Game ruleset was not rejected\n";
        return 1;
    }

    std::cout << "headless match simulation: ok"
              << ", seed=" << first.seed
              << ", hash=" << first.finalStateHash
              << ", ticks=" << first.ticks
              << ", actions=" << first.emittedActions
              << ", rng_draws=" << first.rngDraws;
    if(!first.score.empty())
    {
        const auto leader = std::min_element(first.score.begin(), first.score.end(),
            [](const MatchScore::PlayerResult & lhs, const MatchScore::PlayerResult & rhs)
            {
                return lhs.finalRank < rhs.finalRank;
            });
        std::cout << ", leader=" << leader->person.name()
                  << ", total=" << leader->totalScore;
    }
    std::cout << '\n';
    return 0;
}

Tournament::Config baselineTournamentConfig(std::size_t seedCount)
{
    Tournament::Config config;
    config.seeds.assign(Tournament::publishedSeeds().begin(),
                        Tournament::publishedSeeds().begin() + seedCount);
    config.avatars = Tournament::baselineAvatars();
    config.maximumTicks = 20000;
    return config;
}

bool applyBalanceScenario(Tournament::Config & config, const char* difficulty,
                          const char* behaviorProfile)
{
    if(difficulty && !AI::difficultyFromString(difficulty, config.difficulty))
    {
        std::cerr << "difficulty must be training, easy, normal, hard or unfair\n";
        return false;
    }

    const std::string profile = behaviorProfile ? behaviorProfile : "native";
    config.forceBehaviorProfile = profile != "native";
    if(config.forceBehaviorProfile &&
       !AI::behaviorProfileFromString(profile, config.behaviorProfile))
    {
        std::cerr << "behavior profile must be native, balanced, aggressive, economic or control\n";
        return false;
    }
    return true;
}

bool applyBalanceRoster(Tournament::Config & config, const char* roster)
{
    const std::string selected = roster ? roster : "baseline";
    if(selected.empty() || selected == "baseline")
    {
        config.avatars = Tournament::baselineAvatars();
        return true;
    }

    std::vector<Avatar> avatars;
    std::stringstream stream(selected);
    std::string token;
    while(std::getline(stream, token, ','))
    {
        const std::size_t first = token.find_first_not_of(" \t\r\n");
        const std::size_t last = token.find_last_not_of(" \t\r\n");
        if(first == std::string::npos)
        {
            std::cerr << "roster contains an empty avatar id\n";
            return false;
        }
        token = token.substr(first, last - first + 1);
        const Avatar avatar(token);
        if(!avatar.isValid() || avatar == Avatar(Avatar::Random) ||
           std::find(avatars.begin(), avatars.end(), avatar) != avatars.end())
        {
            std::cerr << "roster avatars must be concrete and unique: " << token << '\n';
            return false;
        }
        avatars.push_back(avatar);
    }

    if(avatars.size() != 4)
    {
        std::cerr << "roster must contain exactly four comma-separated avatars\n";
        return false;
    }
    config.avatars = Avatars(avatars);
    return true;
}

std::string isolatedRecordName(std::size_t index)
{
    std::ostringstream name;
    name << "match-" << std::setfill('0') << std::setw(6) << index << ".json";
    return name.str();
}

bool validSeedCount(std::size_t seedCount)
{
    return 0 < seedCount && seedCount <= Tournament::publishedSeeds().size();
}

int runBalanceMatch(int argc, char** argv)
{
    if(argc < 5)
    {
        std::cerr << "usage: --balance-match <record.json> <seed-count> <schedule-index> "
                     "[easy|normal|hard] [native|balanced|aggressive|economic|control] "
                     "[avatar,avatar,avatar,avatar]\n";
        return 2;
    }
    const std::size_t seedCount =
        static_cast<std::size_t>(std::strtoul(argv[3], nullptr, 10));
    const std::size_t scheduleIndex =
        static_cast<std::size_t>(std::strtoul(argv[4], nullptr, 10));
    if(!validSeedCount(seedCount)) return 2;

    configureSimulationBonuses();
    Tournament::Config config = baselineTournamentConfig(seedCount);
    if(!applyBalanceScenario(config, 5 < argc ? argv[5] : nullptr,
                             6 < argc ? argv[6] : nullptr) ||
       !applyBalanceRoster(config, 7 < argc ? argv[7] : nullptr)) return 2;
    const Tournament::Schedule schedule = Tournament::buildSchedule(config);
    if(!schedule.valid() || schedule.matches.size() <= scheduleIndex)
    {
        std::cerr << (!schedule.valid() ? schedule.error :
            "isolated schedule index is out of range") << '\n';
        return 2;
    }

    Tournament::MatchRecord record;
    record.plan = schedule.matches[scheduleIndex];
    record.plan.match.captureFullReplay = true;
    record.result = Simulation::runMatch(record.plan.match);

    std::string saveError;
    if(!Tournament::saveMatchRecord(record, scheduleIndex, argv[2], &saveError))
    {
        std::cerr << "FAIL: " << saveError << '\n';
        return 1;
    }
    std::cout << "isolated balance match " << scheduleIndex + 1 << '/'
              << schedule.matches.size() << ": seed=" << record.result.seed
              << ", rotation=" << record.plan.seatRotation
              << ", status=" << Simulation::statusName(record.result.status)
              << ", hash=" << record.result.finalStateHash
              << ", replay_steps=" << record.result.actionReplay.getInteger("actionCount")
              << ", events=" << record.result.events.size() << '\n';
    // A simulated failure is a valid record. The merge process decides batch success
    // and retains its replay; a non-zero child exit is reserved for process/I/O failure.
    return 0;
}

int runBalanceMerge(int argc, char** argv)
{
    if(argc < 5)
    {
        std::cerr << "usage: --balance-merge <output-dir> <records-dir> <seed-count> "
                     "[easy|normal|hard] [native|balanced|aggressive|economic|control] "
                     "[avatar,avatar,avatar,avatar]\n";
        return 2;
    }
    const std::size_t seedCount =
        static_cast<std::size_t>(std::strtoul(argv[4], nullptr, 10));
    if(!validSeedCount(seedCount)) return 2;

    configureSimulationBonuses();
    Tournament::Config config = baselineTournamentConfig(seedCount);
    if(!applyBalanceScenario(config, 5 < argc ? argv[5] : nullptr,
                             6 < argc ? argv[6] : nullptr) ||
       !applyBalanceRoster(config, 7 < argc ? argv[7] : nullptr)) return 2;
    const Tournament::Schedule schedule = Tournament::buildSchedule(config);
    if(!schedule.valid())
    {
        std::cerr << schedule.error << '\n';
        return 2;
    }
    std::vector<Tournament::MatchRecord> records;
    records.reserve(schedule.matches.size());
    for(std::size_t index = 0; index < schedule.matches.size(); ++index)
    {
        Tournament::MatchRecord record;
        const std::string path = Systems::concatePath(argv[3], isolatedRecordName(index));
        std::string loadError;
        if(!Tournament::loadMatchRecord(path, schedule.matches[index], index,
                                        record, &loadError))
        {
            std::cerr << "FAIL: " << loadError << '\n';
            return 1;
        }
        records.push_back(record);
    }

    const Tournament::Result result = Tournament::assemble(config, records);
    std::string saveError;
    if(!result.error.empty() ||
       !Tournament::saveReports(result, argv[2], &saveError))
    {
        std::cerr << "FAIL: " << (!result.error.empty() ? result.error : saveError) << '\n';
        return 1;
    }

    const std::size_t retained = static_cast<std::size_t>(std::count_if(
        result.matches.begin(), result.matches.end(), [](const auto & record)
        {
            return !record.replayRetentionReasons.empty();
        }));
    std::cout << Tournament::toText(result)
              << "Retained failure/outlier replays: " << retained << '\n'
              << "Reports: " << argv[2] << '\n';
    return result.completed() ? 0 : 1;
}

int runBalanceScheduleSize(int argc, char** argv)
{
    if(argc < 3)
    {
        std::cerr << "usage: --balance-schedule-size <seed-count> [easy|normal|hard] "
                     "[native|balanced|aggressive|economic|control] "
                     "[avatar,avatar,avatar,avatar]\n";
        return 2;
    }
    const std::size_t seedCount =
        static_cast<std::size_t>(std::strtoul(argv[2], nullptr, 10));
    if(!validSeedCount(seedCount)) return 2;

    configureSimulationBonuses();
    Tournament::Config config = baselineTournamentConfig(seedCount);
    if(!applyBalanceScenario(config, 3 < argc ? argv[3] : nullptr,
                             4 < argc ? argv[4] : nullptr) ||
       !applyBalanceRoster(config, 5 < argc ? argv[5] : nullptr)) return 2;
    const Tournament::Schedule schedule = Tournament::buildSchedule(config);
    if(!schedule.valid())
    {
        std::cerr << schedule.error << '\n';
        return 2;
    }
    std::cout << schedule.matches.size() << '\n';
    return 0;
}

int runBalanceLab(int argc, char** argv)
{
    const std::string outputDirectory = 2 < argc ? argv[2] : "balance-lab-output";
    const std::size_t seedCount = 3 < argc ?
        static_cast<std::size_t>(std::strtoul(argv[3], nullptr, 10)) : 1;
    if(!validSeedCount(seedCount))
    {
        std::cerr << "seed count must be between 1 and "
                  << Tournament::publishedSeeds().size() << '\n';
        return 2;
    }

    configureSimulationBonuses();
    Tournament::Config config = baselineTournamentConfig(seedCount);
    if(!applyBalanceScenario(config, 4 < argc ? argv[4] : nullptr,
                             5 < argc ? argv[5] : nullptr) ||
       !applyBalanceRoster(config, 6 < argc ? argv[6] : nullptr)) return 2;

    const Tournament::Result result = Tournament::run(config,
        [](std::size_t complete, std::size_t total, const Tournament::MatchRecord & record)
        {
            std::cout << "balance match " << complete << '/' << total
                      << ": seed=" << record.result.seed
                      << ", rotation=" << record.plan.seatRotation
                      << ", status=" << Simulation::statusName(record.result.status)
                      << ", hash=" << record.result.finalStateHash << '\n';
        });

    std::string saveError;
    if(!Tournament::saveReports(result, outputDirectory, &saveError))
    {
        std::cerr << "FAIL: " << saveError << '\n';
        return 1;
    }

    std::cout << Tournament::toText(result)
              << "Reports: " << outputDirectory << '\n';
    return result.completed() ? 0 : 1;
}

int runBalanceReplayVerification(int argc, char** argv)
{
    if(argc != 3)
    {
        std::cerr << "usage: --verify-balance-replay <retained-match.json>\n";
        return 2;
    }

    // The standalone test harness does not load the UI configuration that owns
    // these gameplay awards. Match generation applies the same explicit values.
    configureSimulationBonuses();

    const JsonContentFile replayFile(argv[2]);
    if(!replayFile.isValid())
    {
        std::cerr << "FAIL: retained balance replay is not valid JSON: " << argv[2] << '\n';
        return 1;
    }

    const JsonObject root = replayFile.toObject();
    const JsonObject* match = root.getObject("match");
    const JsonObject* replay = match ? match->getObject("action_replay") : nullptr;
    const std::string matchProfile = match ?
        match->getString("behavior_profile") : std::string();
    const std::string replayProfile = replay ?
        replay->getString("aiBehaviorProfile", "native") : std::string();
    const int replaySchema = replay ? replay->getInteger("schema") : 0;
    if(root.getInteger("schema_version") != 2 || !match || !replay ||
       (replaySchema != 2 && replaySchema != 3) ||
       matchProfile != replayProfile ||
       (replaySchema == 3) != (replayProfile != "native") ||
       !replay->getBoolean("contiguousToCheckpoint"))
    {
        std::cerr << "FAIL: retained balance replay has an invalid record contract: "
                  << argv[2] << '\n';
        return 1;
    }

    const std::string expectedHash = match->getString("final_state_hash");
    std::string replayError;
    if(expectedHash.empty() || !Replay::run(*replay, &replayError))
    {
        std::cerr << "FAIL: retained balance replay diverged: " << replayError << '\n';
        return 1;
    }

    const std::string actualHash = Replay::authoritativeStateHash();
    if(actualHash != expectedHash)
    {
        std::cerr << "FAIL: retained balance replay final hash mismatch: expected="
                  << expectedHash << " actual=" << actualHash << '\n';
        return 1;
    }

    std::cout << "balance replay verified: schedule_index="
              << root.getInteger("schedule_index")
              << ", seed=" << match->getString("seed")
              << ", difficulty=" << match->getString("difficulty")
              << ", profile=" << match->getString("behavior_profile")
              << ", steps=" << replay->getInteger("actionCount")
              << ", hash=" << actualHash << '\n';
    return 0;
}
}

int main(int argc, char** argv)
{
#if defined(_WIN32)
    if(1 < argc && std::string(argv[1]) == "--windows-crash-report-child")
    {
        CrashReport::install("four-winds-reborn-native-test");
        RaiseException(EXCEPTION_ACCESS_VIOLATION, EXCEPTION_NONCONTINUABLE, 0, nullptr);
        return 2;
    }

    if(1 < argc && std::string(argv[1]) == "--windows-crash-report-self-test")
        return runWindowsCrashReportSelfTest(argv[0]);
#endif

    if(1 < argc && std::string(argv[1]) == "--crash-report-self-test")
    {
        CrashReport::install("four-winds-reborn-test");
        CrashReport::breadcrumb("crash reporter self-test action");
        CrashReport::breadcrumb("crash reporter self-test phase transition");
        CrashReport::reportException("crash reporter self-test exception");
        const std::string report = CrashReport::filePath();
        CrashReport::shutdown();

        std::string content;
        bool valid = Systems::readFile2String(report, content) &&
            content.find("[ACTION] seq=1 crash reporter self-test action") != std::string::npos &&
            content.find("[ACTION] seq=2 crash reporter self-test phase transition") != std::string::npos &&
            content.find("[FATAL EXCEPTION] crash reporter self-test exception") != std::string::npos &&
            content.find("[SESSION END] failure") != std::string::npos &&
            CrashReport::breadcrumbSequence() == 2 &&
            CrashReport::recentBreadcrumbs().size() == 2;
#if defined(__APPLE__) || defined(__linux__)
        valid = valid && content.find("Stack trace:") != std::string::npos;
#endif
        if(!valid)
        {
            std::cerr << "FAIL: crash report is incomplete: " << report << '\n';
            return 1;
        }

        std::cout << "crash reporting: ok\n";
        return 0;
    }

    // No manually launched test may write checkpoints to a player's real
    // application data. CTest gives every mode its own isolated directory.
    if(!std::getenv("FOUR_WINDS_RECOVERY_DIR")) Recovery::setEnabled(false);

    const Engine::exception diagnosticException("mahjongTick", "invalid state");
    expect(diagnosticException.function() == "mahjongTick" &&
           diagnosticException.message() == "invalid state",
           "engine exceptions must retain diagnostic context");

#if defined(_WIN32)
    const std::string windowsHome = Systems::homeDirectory("four-winds-reborn");
    expect(Systems::basename(windowsHome) == "four-winds-reborn",
           "Windows application data path must retain its application subdirectory");
#endif

    const std::string unicodeSample = "Four Winds: Привет, мир! 🌬️";
    expect(UnicodeString(unicodeSample).toString() == unicodeSample,
           "engine UTF-8/UTF-16 conversion must preserve Cyrillic and supplementary characters");

    JsonContentFile indexFile(std::string(FOUR_WINDS_SOURCE_DIR) + "/themes/classic/index.json");
    expect(indexFile.isValid(), "theme index JSON must parse");
    std::string bootstrapPackageError;
    expect(activateContentPackage(indexFile.toObject(), &bootstrapPackageError) &&
           activeContentPackageManifest().engineContract == ClassicEngineContentContract &&
           activeContentPackageManifest().gameData == "content.json",
           "theme content package manifest must activate");
    JsonContentFile contentFile(std::string(FOUR_WINDS_SOURCE_DIR) +
        "/themes/classic/content.json");
    expect(contentFile.isValid(), "authoritative theme content JSON must parse");
    expect(GameData::loadIndexes(contentFile.toObject()), "theme content indexes must load");

    JsonContentFile soundsFile(std::string(FOUR_WINDS_SOURCE_DIR) +
        "/themes/classic/json/gamedata/sounds.json");
    expect(soundsFile.isValid() && soundsFile.isArray(), "theme sound JSON must parse");
    const std::list<FileInfo> soundRecords = soundsFile.toArray().toStdList<FileInfo>();
    const auto localizedSound = std::find_if(soundRecords.begin(), soundRecords.end(),
        [](const FileInfo & info) { return info.id == "orachi_game"; });
    expect(localizedSound != soundRecords.end(), "localized sound fixture must exist");
    if(localizedSound != soundRecords.end())
    {
        expect(GameTheme::localizedSoundFile(*localizedSound, "en") == "1053.ogg" &&
               GameTheme::localizedSoundFile(*localizedSound, "ru_RU") == "voice_ru_1053.ogg",
               "sound resources must follow the selected English/Russian language");
    }

    JsonContentFile rebornSoundsFile(std::string(FOUR_WINDS_SOURCE_DIR) +
        "/themes/reborn/json/gamedata/sounds.json");
    expect(rebornSoundsFile.isValid() && rebornSoundsFile.isArray(),
           "Reborn theme sound JSON must parse");
    const std::list<FileInfo> rebornSoundRecords =
        rebornSoundsFile.toArray().toStdList<FileInfo>();
    const auto localizedRebornAnnouncer =
        std::find_if(rebornSoundRecords.begin(), rebornSoundRecords.end(),
            [](const FileInfo & info) { return info.id == "begin"; });
    const auto localizedRebornCreature =
        std::find_if(rebornSoundRecords.begin(), rebornSoundRecords.end(),
            [](const FileInfo & info) { return info.id == "creature01"; });
    expect(localizedRebornAnnouncer != rebornSoundRecords.end() &&
           localizedRebornCreature != rebornSoundRecords.end(),
           "Reborn localized voice fixtures must exist");
    if(localizedRebornAnnouncer != rebornSoundRecords.end() &&
       localizedRebornCreature != rebornSoundRecords.end())
    {
        expect(GameTheme::localizedSoundFile(*localizedRebornAnnouncer, "en_US") ==
                   "announcer_en_01.ogg" &&
               GameTheme::localizedSoundFile(*localizedRebornAnnouncer, "ru_RU") ==
                   "announcer_ru_01.ogg" &&
               GameTheme::localizedSoundFile(*localizedRebornCreature, "en") ==
                   "creature_name_en_01.ogg" &&
               GameTheme::localizedSoundFile(*localizedRebornCreature, "ru") ==
                   "creature_name_ru_01.ogg",
               "Reborn announcer and creature voices must follow the selected language");
    }
    expect(GameTheme::isVoiceSound("orachi_chao") &&
           GameTheme::isVoiceSound("creature01") &&
           GameTheme::isVoiceSound("intro_ru_01") &&
           GameTheme::isVoiceSound("round_east") &&
           GameTheme::isVoiceSound("anim_game") &&
           !GameTheme::isVoiceSound("button") &&
           !GameTheme::isVoiceSound("spell1030") &&
           !GameTheme::isVoiceSound("stone2"),
           "voice and effect sound IDs must use separate volume groups");

    JsonContentFile imagesFile(std::string(FOUR_WINDS_SOURCE_DIR) +
        "/themes/classic/json/gamedata/images.json");
    expect(imagesFile.isValid() && imagesFile.isArray(), "theme image JSON must parse");
    const std::list<ImageInfo> imageRecords = imagesFile.toArray().toStdList<ImageInfo>();
    const auto localizedPassButton = std::find_if(imageRecords.begin(), imageRecords.end(),
        [](const ImageInfo & info) { return info.id == "but_pass1"; });
    const auto localizedOrderButton = std::find_if(imageRecords.begin(), imageRecords.end(),
        [](const ImageInfo & info) { return info.id == "but_order1"; });
    expect(localizedPassButton != imageRecords.end() &&
           localizedOrderButton != imageRecords.end(),
           "localized button fixtures must exist");
    if(localizedPassButton != imageRecords.end() && localizedOrderButton != imageRecords.end())
    {
        expect(GameTheme::localizedSoundFile(*localizedPassButton, "en") == "buttons.png" &&
               GameTheme::localizedSoundFile(*localizedPassButton, "ru_RU") == "buttons_ru.png" &&
               GameTheme::localizedSoundFile(*localizedOrderButton, "en") == "order_buttons.png" &&
               GameTheme::localizedSoundFile(*localizedOrderButton, "ru_RU") == "order_buttons_ru.png",
               "button sprites must follow the selected English/Russian language");
    }
    expect(IntroScreen::supportsLanguage("en_US") && IntroScreen::supportsLanguage("ru_RU") &&
           !IntroScreen::supportsLanguage("unsupported"),
           "the original intro must run only when an authentic narration track exists");

    GameData::creaturesInfo = loadIndexed<CreatureInfo>("creatures.json");
    GameData::spellsInfo = loadIndexed<SpellInfo>("spells.json");
    GameData::landsInfo = loadIndexed<LandInfo>("lands.json");
    GameData::avatarsInfo = loadIndexed<AvatarInfo>("avatars.json");
    GameData::stonesInfo = loadIndexed<StoneInfo>("stones.json");
    GameData::windsInfo = loadIndexed<WindInfo>("winds.json");
    GameData::clansInfo = loadIndexed<ClanInfo>("clans.json");
    GameData::abilitiesInfo = loadIndexed<AbilityInfo>("abilities.json");
    GameData::specialsInfo = loadIndexed<SpecialityInfo>("specials.json");
    GameData::initialLandOwners.resize(GameData::landsInfo.size());
    for(std::size_t index = 0; index < GameData::landsInfo.size(); ++index)
        GameData::initialLandOwners[index] = GameData::landsInfo[index].clan;

    std::vector<Clan> localizationOwnerSnapshot;
    localizationOwnerSnapshot.reserve(GameData::landsInfo.size());
    for(const LandInfo & info : GameData::landsInfo)
        localizationOwnerSnapshot.push_back(info.clan);

    const std::string russianCatalog = std::string(FOUR_WINDS_SOURCE_DIR) +
        "/themes/classic/lang/ru.mo";
    Translation::reset();
    Translation::setStripContext('|');
    expect(Translation::bindDomain("four-winds-localization-test", russianCatalog),
           "compiled Russian catalog must load");
    expect(Translation::setDomain("four-winds-localization-test"),
           "Russian test domain must become active");
    Translation::setLanguage("ru");
    GameData::retranslateThemeData();
    expect(GameData::clanInfo(Clan::Red).name == "Красный" &&
           GameData::avatarInfo(Avatar::Nucrus).name == "Нукрус" &&
           GameData::creatureInfo(Creature::SkeletonHorde).name == "Орда Скелетов",
           "runtime Russian switch must retranslate cached theme data");
    for(std::size_t index = 0; index < GameData::landsInfo.size(); ++index)
        expect(GameData::landsInfo[index].clan == localizationOwnerSnapshot[index],
               "runtime translation must not mutate authoritative land ownership");

    Translation::reset();
    Translation::setLanguage("en");
    GameData::retranslateThemeData();
    expect(GameData::clanInfo(Clan::Red).name == "Red" &&
           GameData::avatarInfo(Avatar::Nucrus).name == "Nucrus" &&
           GameData::creatureInfo(Creature::SkeletonHorde).name == "Skeleton Horde",
           "switching back to English must restore source theme strings");

    expect(Clan("Red") == Clan(Clan::Red) &&
           Clan("Yellow") == Clan(Clan::Yellow) &&
           Clan("Aqua") == Clan(Clan::Aqua) &&
           Clan("Purple") == Clan(Clan::Purple),
           "canonical color clan names must parse to their canonical ids");
    expect(Clan("maitha") == Clan(Clan::Red) &&
           Clan("kartha") == Clan(Clan::Yellow) &&
           Clan("iz") == Clan(Clan::Aqua) &&
           Clan("marz") == Clan(Clan::Purple),
           "legacy named clan ids must remain accepted as load-only aliases");
    expect(Clan(Clan::Red).canonicalName() == "Red" &&
           Clan(Clan::Yellow).canonicalName() == "Yellow" &&
           Clan(Clan::Aqua).canonicalName() == "Aqua" &&
           Clan(Clan::Purple).canonicalName() == "Purple" &&
           Clan(Clan::Red).toString() == "red" &&
           Clan(Clan::Yellow).toString() == "yellow" &&
           Clan(Clan::Aqua).toString() == "aqua" &&
           Clan(Clan::Purple).toString() == "purple",
           "canonical clan ids must be emitted by serialization");
    expect(GameData::clanInfo(Clan::Red).name == "Red" &&
           GameData::clanInfo(Clan::Yellow).name == "Yellow" &&
           GameData::clanInfo(Clan::Aqua).name == "Aqua" &&
           GameData::clanInfo(Clan::Purple).name == "Purple",
           "Classic theme clan names must match the canonical identity contract");

    expect(GameData::creatureInfo(Creature::Tornado).cost == 240,
           "Tornado must use the canonical 240-point price");
    expect(GameData::creatureInfo(Creature::Griffon).cost == 170,
           "Griffon must use the canonical 170-point price");
    expect(GameData::creatureInfo(Creature::GreatCarol).cost == 180,
           "Carol must retain the documented Reborn 180-point price");
    expect(GameData::spellInfo(Spell::DemonicCompulsion).cost == 30,
           "Demonic Compulsion must retain the documented Reborn 30-point price");
    expect(GameData::spellInfo(Spell::DispelMagic).cost == 120,
           "Dispel Magic must retain the documented Reborn 120-point price");
    expect(GameData::spellInfo(Spell::Heroism).cost == 30,
           "Heroism must retain the documented Reborn 30-point price");

    if(2 < argc && std::string(argv[1]) == "--captured-lightning-repro")
        return runCapturedLightningRepro(argv[2]);

    if(1 < argc && std::string(argv[1]) == "--recovery-self-test")
        return runRecoverySelfTest();

    if(1 < argc && std::string(argv[1]) == "--settings-self-test")
        return runSettingsPersistenceSelfTest();

    if(2 < argc && std::string(argv[1]) == "--write-test-replay")
        return runFixedSeedReplaySelfTest(argv[2]);

    if(1 < argc && std::string(argv[1]) == "--fixed-seed-replay")
        return runFixedSeedReplaySelfTest();

    if(1 < argc && std::string(argv[1]) == "--headless-match-self-test")
        return runHeadlessMatchSelfTest();

    if(1 < argc && std::string(argv[1]) == "--balance-match")
        return runBalanceMatch(argc, argv);

    if(1 < argc && std::string(argv[1]) == "--balance-merge")
        return runBalanceMerge(argc, argv);

    if(1 < argc && std::string(argv[1]) == "--balance-schedule-size")
        return runBalanceScheduleSize(argc, argv);

    if(1 < argc && std::string(argv[1]) == "--balance-lab")
        return runBalanceLab(argc, argv);

    if(1 < argc && std::string(argv[1]) == "--verify-balance-replay")
        return runBalanceReplayVerification(argc, argv);

    if(1 < argc && std::string(argv[1]) == "--balance-only")
    {
        const int balanceFailures = runAvatarBalanceTests();
        if(balanceFailures) return 1;
        std::cout << "avatar balance metrics: ok\n";
        return 0;
    }

    testDifficultyRules();
    testRuneGameRulesetIdentityContract();
    testMatchTopologyIdentityContract();
    testContentPackageIdentityContract();
    testInstalledContentCatalog();
    testRuneGameRoundFlowRuleset();
    testRuneGameSpellPointRuleset();
    testPolygonHitTesting();
    testDrawnMahjongResult();
    testStrategicRunePlanning();
    testSelectedPartyMovement();
    testGateMovement();
    testLuckAbility();
    testAvatarPassives();
    testMahjongCallPlanning();
    testMahjongClaimPriorityRuleset();
    testExposedKongDispatchAndReplacementDraw();
    testSpellAndSpecialityContracts();
    testSpellCastingAI();
    testMahjongCastDeadTargetMessage();
    testAdventureProfiles();
    testAdventureCoordination();
    testBattleAI();
    testAdventureHints();
    testInteractiveBattleSession();
    testBattleSessionSpecialities();
    testAdventureBattleSessionFlow();
    testGameplayRngState();
#ifdef BUILD_DEBUG
    testDeveloperFastForward();
#endif
    testActionReplay();
    testGameSummaryInputGuard();
    testMatchScoreContract();
    testTournamentContract();

    expect(Speciality(Speciality::CastSilence).toSpell()() == Spell::Silence,
           "CastSilence must grant Silence");
    expect(Speciality(Speciality::CastScryRunes).toSpell()() == Spell::ScryRunes,
           "CastScryRunes must grant ScryRunes");

    AffectedSpells fog;
    fog.insert(AffectedSpell(Spell(Spell::ManaFog), 3));
    expect(fog.isAffected(Spell::ManaFog), "Mana Fog must start active");
    fog.spellAffected(Spell::ManaFog);
    expect(fog.isAffected(Spell::ManaFog), "Mana Fog must survive the first turn");
    fog.spellAffected(Spell::ManaFog);
    expect(fog.isAffected(Spell::ManaFog), "Mana Fog must survive the second turn");
    fog.spellAffected(Spell::ManaFog);
    expect(!fog.isAffected(Spell::ManaFog), "Mana Fog must expire after the third turn");

    AffectedSpells scry;
    const Avatar scryCaster(Avatar::Niana);
    scry.insert(AffectedSpell(Spell(Spell::ScryRunes), 5, scryCaster));
    expect(scry.isAffected(Spell::ScryRunes, scryCaster),
           "Scry Runes must reveal the hand to its caster");
    expect(!scry.isAffected(Spell::ScryRunes, Avatar(Avatar::Javed)),
           "Scry Runes must not reveal the hand to another player");

    AffectedSpells restoredScry = AffectedSpells::fromJsonArray(scry.toJsonArray());
    expect(restoredScry.isAffected(Spell::ScryRunes, scryCaster),
           "Scry Runes caster must survive save/load");

    AffectedSpells enchantments;
    enchantments.insert(AffectedSpell(Spell(Spell::ForceShield), AffectedSpell::Permanent));
    enchantments.insert(AffectedSpell(Spell(Spell::ForceShield), AffectedSpell::Permanent));
    expect(enchantments.size() == 1, "the same persistent enchantment must not stack");
    enchantments.advanceAdventureRound();
    enchantments.advanceAdventureRound();
    expect(enchantments.isAffected(Spell::ForceShield),
           "persistent enchantments must survive Adventure rounds");

    AffectedSpells restoredEnchantments = AffectedSpells::fromJsonArray(enchantments.toJsonArray());
    expect(restoredEnchantments.isAffected(Spell::ForceShield),
           "persistent enchantments must survive save/load");

    AffectedSpells timedEnchantments;
    timedEnchantments.insert(AffectedSpell(Spell(Spell::Paralyze), 1));
    timedEnchantments.advanceAdventureRound();
    expect(!timedEnchantments.isAffected(Spell::Paralyze),
           "one-round enchantments must expire after one Adventure phase");

    AffectedSpells expiredEnchantments;
    expiredEnchantments.insert(AffectedSpell(Spell(Spell::Paralyze), 0));
    expect(expiredEnchantments.empty(), "expired enchantments must not enter active state");

    expect(Battle::rangedDamage(3, 1) == 2,
           "Force Shield must reduce ranged damage by one");
    expect(Battle::rangedDamage(0, 1) == 0,
           "Force Shield must not turn ranged damage negative");
    expect(Battle::meleeHitChance(5, 4) == 100, "melee above defense must always hit");
    expect(Battle::meleeHitChance(4, 4) == 50, "equal melee and defense must have 50% hit chance");
    expect(Battle::meleeHitChance(3, 4) == 25, "one defense advantage must have 25% hit chance");
    expect(Battle::meleeHitChance(2, 4) == 12, "two defense advantage must have 12% hit chance");
    expect(Battle::meleeHitChance(1, 4) == 6, "three defense advantage must have 6% hit chance");

    LandClaims claims;
    claims.add(Clan::Yellow, 500);
    expect(claims.points(Clan::Yellow) == 500, "land claim points must be credited per opponent clan");
    expect(!claims.spend(Clan::Yellow, 501), "land claim points must reject overdraft");
    expect(claims.spend(Clan::Yellow, 200), "land claim points must pay a valid deed cost");
    expect(claims.points(Clan::Yellow) == 300, "land claim payment must deduct its cost");
    const LandClaims restoredClaims = LandClaims::fromJsonObject(claims.toJsonObject());
    expect(restoredClaims.points(Clan::Yellow) == 300, "land claim points must survive save/load");
    JsonObject legacyClaims;
    legacyClaims.addInteger("kartha", 275);
    const LandClaims restoredLegacyClaims = LandClaims::fromJsonObject(legacyClaims);
    expect(restoredLegacyClaims.points(Clan::Yellow) == 275,
           "legacy named-clan land claims must remain loadable");

    JsonContentFile spellFile(std::string(FOUR_WINDS_SOURCE_DIR) +
                              "/themes/classic/json/gamedata/spells.json");
    expect(spellFile.isValid(), "spell theme JSON must parse");
    const std::list<SpellInfo> spellInfos = spellFile.toArray().toStdList<SpellInfo>();

    const SpellInfo* blindAmbition = findSpell(spellInfos, Spell(Spell::BlindAmbition));
    expect(blindAmbition != nullptr, "Blind Ambition must exist in the theme");
    if(blindAmbition)
    {
        expect(blindAmbition->effect.attack == 1 && blindAmbition->effect.ranger == 1 &&
               blindAmbition->effect.defense == 0 && blindAmbition->effect.loyalty == -2,
               "Blind Ambition must grant melee and ranged attack at the cost of loyalty");
    }

    const SpellInfo* hellBlast = findSpell(spellInfos, Spell(Spell::HellBlast));
    expect(hellBlast != nullptr, "Hell Blast must exist in the theme");
    if(hellBlast)
    {
        expect(hellBlast->target() == (SpellTarget::Enemy | SpellTarget::Party),
               "Hell Blast must target one enemy party");
        expect(hellBlast->effect.loyalty == -2,
               "Hell Blast must remove two loyalty");
    }

    const SpellInfo* forceShield = findSpell(spellInfos, Spell(Spell::ForceShield));
    expect(forceShield != nullptr, "Force Shield must exist in the theme");
    if(forceShield)
        expect(forceShield->value == 1, "Force Shield must absorb one ranged damage");

    const SpellInfo* silence = findSpell(spellInfos, Spell(Spell::Silence));
    expect(silence && silence->duration() == 3,
           "Silence must last three turns");

    const SpellInfo* scryRunes = findSpell(spellInfos, Spell(Spell::ScryRunes));
    expect(scryRunes && scryRunes->duration() == 5,
           "Scry Runes must last five turns");

    const SpellInfo* randomDiscard = findSpell(spellInfos, Spell(Spell::RandomDiscard));
    expect(randomDiscard && randomDiscard->duration() == 1,
           "Random Discard must remain active until the next discard");
    if(randomDiscard)
    {
        AffectedSpells discardEffects;
        discardEffects.insert(AffectedSpell(randomDiscard->id, randomDiscard->duration()));
        expect(discardEffects.isAffected(Spell::RandomDiscard),
               "Random Discard must enter active state");
        discardEffects.spellAffected(Spell::RandomDiscard);
        expect(!discardEffects.isAffected(Spell::RandomDiscard),
               "Random Discard must expire after one discard");
    }

    const SpellInfo* massDispel = findSpell(spellInfos, Spell(Spell::MassDispel));
    expect(massDispel && massDispel->target() == SpellTarget::Land,
           "Mass Dispel must target a territory");
    expect(massDispel && massDispel->description.find("every creature") != std::string::npos,
           "Mass Dispel description must match its territory-wide effect");

    const SpellInfo* dispel = findSpell(spellInfos, Spell(Spell::DispelMagic));
    expect(dispel && dispel->target() == SpellTarget::Any,
           "Dispel Magic must target one creature");
    expect(dispel && dispel->description.find("target creature") != std::string::npos,
           "Dispel Magic description must remain single-target");

    expect(creature(107, 3).canReceiveSpell(Spell(Spell::Guidance)),
           "Guidance must accept creatures with a ranged attack");
    expect(!creature(108, 3, 3, 0).canReceiveSpell(Spell(Spell::Guidance)),
           "Guidance must reject creatures without a ranged attack");

    JsonContentFile avatarFile(std::string(FOUR_WINDS_SOURCE_DIR) +
                               "/themes/classic/json/gamedata/avatars.json");
    expect(avatarFile.isValid(), "avatar theme JSON must parse");
    const std::list<AvatarInfo> avatarInfos = avatarFile.toArray().toStdList<AvatarInfo>();

    const AvatarInfo* ziag = findAvatar(avatarInfos, Avatar(Avatar::Ziag));
    expect(ziag && hasSpell(*ziag, Spell(Spell::MassDispel)),
           "Ziag must know Mass Dispel");

    const AvatarInfo* javed = findAvatar(avatarInfos, Avatar(Avatar::Javed));
    expect(javed && hasSpell(*javed, Spell(Spell::MassDispel)),
           "Javed must know Mass Dispel");

    const RuneGameRuleset & classicRuleset = classicRuneGameRuleset();
    expect(classicRuleset.id() == "classic" && classicRuleset.version() == 1,
           "Classic Rune Game ruleset identity must remain stable");
    expect(WinResults::scoreMultiplier(0) == 1, "zero doubles must use x1");
    expect(WinResults::scoreMultiplier(1) == 2, "one double must use x2");
    expect(WinResults::scoreMultiplier(4) == 16, "four doubles must use x16");
    expect(WinResults::scoreMultiplier(5) == 32, "five doubles must reach the limit tier");

    WinRules pointRules;
    pointRules << WinRule(WinRule::Pung, Stone(Stone::Skull1), false)
               << WinRule(WinRule::Chao, Stone(Stone::Sword2), false)
               << WinRule(WinRule::Chao, Stone(Stone::Number2), false)
               << WinRule(WinRule::Chao, Stone(Stone::Skull6), false);
    const WinResults pointHand(Wind::East, Wind::South, Wind::West, pointRules,
                               WinRules(), Stone(Stone::Number5), Stone(Stone::Sword4));
    expect(pointHand.totalPoints() == 24,
           "a terminal Pung must add four points to the 20-point win base");
    expect(pointHand.totalScore() == 24,
           "Mahjong total score must include set and hand points without doubles");

    const AlternateRuneGameRuleset alternateRuleset;
    expect(alternateRuleset.id() == "test-alternate-scoring" && alternateRuleset.version() == 7,
           "an injected Rune Game ruleset must expose independent identity and version");
    expect(pointHand.totalPoints(alternateRuleset) == 34,
           "an injected Rune Game ruleset must control base win points");
    expect(pointHand.totalScore(alternateRuleset) == 102,
           "an injected Rune Game ruleset must control score multiplication");
    expect(pointHand.totalScore() == 24,
           "an injected Rune Game ruleset must not mutate the implicit Classic ruleset");

    WinRules doubledRules;
    doubledRules << WinRule(WinRule::Pung, Stone(Stone::WindEast), false)
                 << WinRule(WinRule::Chao, Stone(Stone::Sword2), false)
                 << WinRule(WinRule::Chao, Stone(Stone::Number2), false)
                 << WinRule(WinRule::Chao, Stone(Stone::Skull6), false);
    const WinResults doubledHand(Wind::East, Wind::South, Wind::East, doubledRules,
                                 WinRules(), Stone(Stone::Number5), Stone(Stone::Sword4));
    expect(doubledHand.totalPoints() == 24,
           "a lucky exposed Wind Pung must retain its four base points");
    expect(doubledHand.totalScore() == 48,
           "Mahjong doubles must multiply the complete accumulated point total");
    expect(doubledHand.totalScore(alternateRuleset) == 200,
           "an injected Rune Game ruleset must control the maximum win score");

    WinRules concealedRules;
    concealedRules << WinRule(WinRule::Pung, Stone(Stone::Skull1), true)
                   << WinRule(WinRule::Pung, Stone(Stone::Sword1), true)
                   << WinRule(WinRule::Pung, Stone(Stone::Number1), true)
                   << WinRule(WinRule::Pung, Stone(Stone::WhiteDragon), true);
    const WinResults limitHand(Wind::East, Wind::West, Wind::South, WinRules(),
                               concealedRules, Stone(Stone::WindNorth), Stone(Stone::Sword4));
    expect(limitHand.totalPoints() == 62,
           "four concealed terminal or honor Pungs must include their point bonuses");
    expect(limitHand.totalScore() == 500,
           "a limit hand must remain capped at 500 after multiplying all points");

    BattleParty party(Clan::Red, Land::Maithaius);
    expect(party.join(creature(101, 4)), "first creature must join party");
    expect(party.movePoint() == 4, "empty party slots must not reduce movement");
    expect(party.join(creature(102, 2)), "second creature must join party");
    expect(party.movePoint() == 2, "party movement must use the slowest creature");

    BattleParty deadParty(Clan::Red, Land::Maithaius);
    expect(deadParty.join(creature(103, 3, 0)), "dead fixture must join party");
    deadParty.removeUnloyalty();
    expect(deadParty.isEmpty(), "zero-loyalty creatures must be removed");

    BattleArmy army;
    BattleParty source(Clan::Red, Land::Maithaius);
    expect(source.join(creature(104, 5)), "teleport fixture must join source party");
    army.push_back(source);

    const BattleCreature* before = army.findBattleUnitConst(104);
    expect(before && army.teleportCreature(*before, Land::Baliphon),
           "teleport must move a valid creature");

    const BattleCreature* after = army.findBattleUnitConst(104);
    expect(after != nullptr, "teleport must preserve battle unit id");
    expect(after && after->freeMovePoint() == 5,
           "teleport must not consume movement points");
    expect(army.findPartyConst(Land::Maithaius) == nullptr,
           "teleport must remove an empty source party");
    expect(army.findPartyConst(Land::Baliphon) != nullptr,
           "teleport must create the destination party");

    BattleArmy duplicateArmy;
    BattleParty duplicateSource(Clan::Red, Land::Maithaius);
    expect(duplicateSource.join(creature(105, 4)), "first duplicate fixture must join");
    expect(duplicateSource.join(creature(106, 3)), "second duplicate fixture must join");
    duplicateArmy.push_back(duplicateSource);

    const BattleCreature* duplicate = duplicateArmy.findBattleUnitConst(106);
    expect(duplicate && duplicateArmy.teleportCreature(*duplicate, Land::Baliphon),
           "teleport must move the selected duplicate");
    const BattleParty* remaining = duplicateArmy.findPartyConst(Land::Maithaius);
    expect(remaining != nullptr, "teleport must keep a non-empty source party");
    expect(remaining && remaining->findBattleUnitConst(105) != nullptr,
           "teleport must leave the other creature in place");
    expect(remaining && remaining->findBattleUnitConst(106) == nullptr,
           "teleport must remove the selected battle unit from its source");

    BattleParty merged(Clan::Red, Land::Maithaius);
    expect(merged.join(combatCreature(Clan::Red, Creature::Shadow, 201, 5, 0, 2, 3)),
           "first merge creature must join");
    expect(merged.join(combatCreature(Clan::Red, Creature::Shadow, 202, 5, 0, 2, 3)),
           "second merge creature must join");
    expect(Battle::mergeDefenseBonus(merged, *merged.findBattleUnitConst(201)) == 1,
           "two matching Merge creatures must grant one defense");

    BattleParty rangedAttackers(Clan::Red, Land::Baliphon);
    BattleParty rangedDefenders(Clan::Yellow, Land::Baliphon);
    expect(rangedAttackers.join(combatCreature(Clan::Red, Creature::Durlock, 210, 0, 2, 0, 1)),
           "ranged attacker fixture must join");
    expect(rangedDefenders.join(combatCreature(Clan::Yellow, Creature::AdventureParty, 211, 0, 2, 0, 1)),
           "ranged defender fixture must join");
    BattleTown quietTown(BattleUnit(BaseStat(0, 0, 100, 100)), Clan::Yellow, Land::Baliphon);
    const BattleStrikes rangedStrikes = Battle::doAttackParty(rangedAttackers, quietTown, &rangedDefenders);
    expect(rangedAttackers.isEmpty() && rangedDefenders.isEmpty(),
           "simultaneous ranged combat must let both lethal shooters fire");
    expect(std::count_if(rangedStrikes.begin(), rangedStrikes.end(), [](const BattleStrike & strike)
           { return strike.type == BattleStrike::Ranger; }) == 2,
           "simultaneous ranged combat must record both shots");

    BattleParty townRangedAttackers(Clan::Red, Land::Baliphon);
    BattleParty townRangedDefenders(Clan::Yellow, Land::Baliphon);
    expect(townRangedAttackers.join(combatCreature(Clan::Red, Creature::Durlock,
                                                    212, 0, 2, 0, 1)),
           "territory ranged attacker fixture must join");
    expect(townRangedDefenders.join(combatCreature(Clan::Yellow, Creature::AdventureParty,
                                                    213, 0, 2, 0, 1)),
           "territory ranged defender fixture must join");
    BattleTown rangedTown(BattleUnit(BaseStat(0, 1, 100, 100)), Clan::Yellow, Land::Baliphon);
    const BattleStrikes townRangedStrikes =
        Battle::doAttackParty(townRangedAttackers, rangedTown, &townRangedDefenders);
    expect(townRangedAttackers.isEmpty() && !townRangedDefenders.isEmpty(),
           "a lethal territory shot must remove its target before creature volleys are planned");
    expect(std::count_if(townRangedStrikes.begin(), townRangedStrikes.end(),
                         [](const BattleStrike & strike)
                         { return strike.type == BattleStrike::Ranger; }) == 1,
           "territory fire must resolve before the simultaneous creature ranged round");

    BattleParty firstStrikeAttackers(Clan::Red, Land::Baliphon);
    BattleParty firstStrikeDefenders(Clan::Yellow, Land::Baliphon);
    expect(firstStrikeAttackers.join(combatCreature(Clan::Red, Creature::Durlock, 221, 1, 20, 0, 2)),
           "First Strike ranged support fixture must join");
    expect(firstStrikeAttackers.join(combatCreature(Clan::Red, Creature::GreatCarol, 220, 10, 0, 0, 1)),
           "First Strike leader fixture must join after its support");
    expect(firstStrikeDefenders.join(combatCreature(Clan::Yellow, Creature::AdventureParty, 222, 10, 20, 0, 1)),
           "First Strike defender fixture must join");
    BattleTown weakTown(BattleUnit(BaseStat(0, 0, 0, 1)), Clan::Yellow, Land::Baliphon);
    const BattleStrikes firstStrikes = Battle::doAttackParty(firstStrikeAttackers, weakTown, &firstStrikeDefenders);
    expect(!weakTown.isAlive() && !firstStrikeAttackers.isEmpty(),
           "First Strike leader must let the attacking party strike first and capture the town");
    expect(std::none_of(firstStrikes.begin(), firstStrikes.end(), [](const BattleStrike & strike)
           { return strike.type == BattleStrike::Ranger; }),
           "First Strike must skip all creature ranged attacks");

    BattleParty swarmAttackers(Clan::Red, Land::Baliphon);
    BattleParty swarmDefenders(Clan::Yellow, Land::Baliphon);
    expect(swarmAttackers.join(combatCreature(Clan::Red, Creature::SkeletonHorde, 230, 10, 0, 0, 10)),
           "Swarm attacker fixture must join");
    expect(swarmDefenders.join(combatCreature(Clan::Yellow, Creature::Durlock, 231, 0, 0, 0, 1)),
           "first Swarm target fixture must join");
    expect(swarmDefenders.join(combatCreature(Clan::Yellow, Creature::Durlock, 232, 0, 0, 0, 1)),
           "second Swarm target fixture must join");
    expect(swarmDefenders.join(combatCreature(Clan::Yellow, Creature::Durlock, 233, 0, 0, 0, 1)),
           "third Swarm target fixture must join");
    BattleTown swarmTown(BattleUnit(BaseStat(0, 0, 0, 1)), Clan::Yellow, Land::Baliphon);
    Battle::doAttackParty(swarmAttackers, swarmTown, &swarmDefenders);
    expect(swarmDefenders.isEmpty(), "Swarm must attack every creature in the defending party");

    BattleCreature regenerating = combatCreature(Clan::Red, Creature::Wraith, 240, 7, 0, 4, 5);
    regenerating.applyDamage(4);
    regenerating.initAdventurePart(Ability::None);
    expect(regenerating.loyalty() == regenerating.baseLoyalty(),
           "Regeneration must restore full loyalty at map phase start");

    GameData::gamers.clear();
    LocalPlayer red;
    red.avatar = Avatar::Orachi;
    red.clan = Clan::Red;
    red.wind = Wind::East;
    red.addLandClaimPoints(Clan::Yellow, 500);
    BattleParty undoParty(Clan::Red, Land::Corzen);
    expect(undoParty.join(combatCreature(Clan::Red, Creature::SkeletonHorde, 260, 5, 0, 1, 3)),
           "Adventure undo fixture must join");
    red.army.push_back(undoParty);
    LocalPlayer yellow;
    yellow.avatar = Avatar::Lakkho;
    yellow.clan = Clan::Yellow;
    yellow.wind = Wind::South;
    GameData::gamers.push_back(red);
    GameData::gamers.push_back(yellow);

    expect(GameData::canClaimLand(GameData::gamers.front(), Land::Zubrus),
           "adjacent empty enemy territory with enough points must be claimable");
    BattleParty hiddenGuard(Clan::Yellow, Land::Zubrus);
    expect(hiddenGuard.join(combatCreature(Clan::Yellow, Creature::Shadow, 250, 5, 0, 2, 3)),
           "hidden claim guard fixture must join");
    GameData::gamers.back().army.push_back(hiddenGuard);
    expect(!GameData::canClaimLand(GameData::gamers.front(), Land::Zubrus),
           "an invisible creature must block Land Claim");
    expect(!GameData::canClaimLand(GameData::gamers.front(), Land::TowerOf4Winds),
           "Tower of Four Winds must never be claimable");

    GameData::gamers.back().army.clear();
    ActionList adventureActions;
    expect(GameData::initAdventure(), "Adventure phase must initialize");
    expect(GameData::adventure2Client(GameData::gamers.front().avatar, adventureActions),
           "Adventure turn must begin and create an undo snapshot");
    ActionRejection wrongTurnRejection;
    ActionList wrongTurnActions;
    expect(!GameData::client2Adventure(GameData::gamers.back().avatar,
                                       ClientAdventureUndo(), wrongTurnActions,
                                       &wrongTurnRejection) &&
           wrongTurnActions.empty() &&
           wrongTurnRejection.reason == ActionRejectReason::WrongTurn,
           "Adventure must expose a stable wrong-turn rejection without emitting actions");
    expect(GameData::client2Adventure(GameData::gamers.front().avatar,
                                      ClientLandClaim(Land::Zubrus), adventureActions),
           "Land Claim action must be accepted during the current turn");
    expect(GameData::client2Adventure(GameData::gamers.front().avatar,
                                      ClientUnitMoved(260, Land::Zubrus), adventureActions),
           "movement after a Land Claim must be accepted");
    expect(GameData::landInfo(Land::Zubrus).clan == Clan(Clan::Red),
           "Land Claim must change territory ownership");
    expect(GameData::gamers.front().army.findPartyConst(Land::Zubrus) != nullptr,
           "movement must change the creature position before undo");
    expect(GameData::client2Adventure(GameData::gamers.front().avatar,
                                      ClientAdventureUndo(), adventureActions),
           "Adventure Undo action must be accepted");
    expect(GameData::landInfo(Land::Zubrus).clan == Clan(Clan::Yellow),
           "Adventure Undo must restore territory ownership");
    expect(GameData::gamers.front().landClaimPoints(Clan::Yellow) == 500,
           "Adventure Undo must refund Land Claim points");
    const BattleParty* restoredParty = GameData::gamers.front().army.findPartyConst(Land::Corzen);
    expect(restoredParty && restoredParty->findBattleUnitConst(260),
           "Adventure Undo must restore the original creature position");
    expect(GameData::gamers.front().army.findPartyConst(Land::Zubrus) == nullptr,
           "Adventure Undo must remove the moved destination party");

    if(failures)
        return 1;

    std::cout << "gameplay regressions: ok\n";
    return 0;
}
