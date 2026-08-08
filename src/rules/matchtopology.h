/***************************************************************************
 *   Copyright (C) 2026 by Four Winds Reborn contributors                 *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#ifndef FOUR_WINDS_MATCH_TOPOLOGY_H
#define FOUR_WINDS_MATCH_TOPOLOGY_H

#include <string>

#include "swe/swe_json.h"

constexpr const char MatchTopologyIdentityKey[] = "matchTopology";
constexpr const char ClassicFreeForAllTopologyId[] = "classic-ffa";
constexpr int ClassicFreeForAllTopologyVersion = 1;
constexpr const char DuelTopologyId[] = "duel";
constexpr int DuelTopologyVersion = 1;
constexpr const char CoalitionTopologyId[] = "coalition";
constexpr int CoalitionTopologyVersion = 1;

// Match topology owns player/control relationships, independently of the
// Rune Game ruleset. Runtime ownership is keyed by clan because player winds
// rotate between Rune Game rounds while clans remain stable for the match.
class MatchTopology
{
public:
    virtual ~MatchTopology() = default;

    virtual const std::string & id(void) const = 0;
    virtual int version(void) const = 0;
    virtual int seatCount(void) const = 0;
    virtual int controllerCount(void) const = 0;
    virtual int teamCount(void) const = 0;

    // Wind identifiers use the stable serialized Wind values. Invalid winds
    // return -1 rather than silently joining a controller or team.
    virtual int controllerForWind(int windId) const = 0;
    virtual int teamForWind(int windId) const = 0;
    virtual int controllerForClan(int clanId) const = 0;
    virtual int teamForClan(int clanId) const = 0;

    bool sharesController(int firstWindId, int secondWindId) const;
    bool allied(int firstWindId, int secondWindId) const;
    bool sharesControllerByClan(int firstClanId, int secondClanId) const;
    bool alliedByClan(int firstClanId, int secondClanId) const;
};

struct MatchTopologyIdentity
{
    std::string id;
    int version = 0;

    bool isValid(void) const { return !id.empty() && 0 < version; }
};

const MatchTopology & classicFreeForAllTopology(void);
const MatchTopology & duelTopology(void);
const MatchTopology & coalitionTopology(void);
const MatchTopology & activeMatchTopology(void);
const MatchTopology* findMatchTopology(const std::string & id, int version);

MatchTopologyIdentity matchTopologyIdentity(const MatchTopology &);
SWE::JsonObject matchTopologyIdentityJson(const MatchTopology &);
bool resolveMatchTopologyIdentity(const SWE::JsonObject & container,
                                  MatchTopologyIdentity & identity,
                                  bool allowLegacyClassic,
                                  std::string* error = nullptr);
bool selectActiveMatchTopology(const std::string & id, int version,
                               std::string* error = nullptr);
bool sameMatchTopology(const MatchTopologyIdentity &, const MatchTopologyIdentity &);

#endif
