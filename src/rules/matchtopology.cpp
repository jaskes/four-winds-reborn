/***************************************************************************
 *   Copyright (C) 2026 by Four Winds Reborn contributors                 *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#include "gameobjects.h"
#include "matchtopology.h"

namespace
{
    class ClassicFreeForAllTopology final : public MatchTopology
    {
    public:
        const std::string & id(void) const override
        {
            static const std::string value(ClassicFreeForAllTopologyId);
            return value;
        }

        int version(void) const override { return ClassicFreeForAllTopologyVersion; }
        int seatCount(void) const override { return 4; }
        int controllerCount(void) const override { return 4; }
        int teamCount(void) const override { return 4; }

        int controllerForWind(int windId) const override
        {
            return Wind::East <= windId && windId <= Wind::North ? windId - Wind::East : -1;
        }

        int teamForWind(int windId) const override
        {
            return controllerForWind(windId);
        }

        int controllerForClan(int clanId) const override
        {
            return Clan::Red <= clanId && clanId <= Clan::Purple ? clanId - Clan::Red : -1;
        }

        int teamForClan(int clanId) const override { return controllerForClan(clanId); }
    };

    // East/South own the western Red/Purple half of the island, while
    // West/North own the eastern Yellow/Aqua half. Player generation keeps
    // those clan pairs on the corresponding wind pair.
    int halfForWind(int windId)
    {
        if(windId == Wind::East || windId == Wind::South) return 0;
        if(windId == Wind::West || windId == Wind::North) return 1;
        return -1;
    }

    int halfForClan(int clanId)
    {
        if(clanId == Clan::Red || clanId == Clan::Purple) return 0;
        if(clanId == Clan::Yellow || clanId == Clan::Aqua) return 1;
        return -1;
    }

    class LegacyDuelTopology : public MatchTopology
    {
    public:
        const std::string & id(void) const override
        {
            static const std::string value(DuelTopologyId);
            return value;
        }

        int version(void) const override { return LegacyDuelTopologyVersion; }
        int seatCount(void) const override { return 4; }
        int controllerCount(void) const override { return 2; }
        int teamCount(void) const override { return 2; }
        int controllerForWind(int windId) const override { return halfForWind(windId); }
        int teamForWind(int windId) const override { return halfForWind(windId); }
        int controllerForClan(int clanId) const override { return halfForClan(clanId); }
        int teamForClan(int clanId) const override { return halfForClan(clanId); }
    };

    class DuelTopology final : public LegacyDuelTopology
    {
    public:
        int version(void) const override { return DuelTopologyVersion; }
        int seatCount(void) const override { return 2; }
        int controllerForWind(int windId) const override
        {
            if(windId == Wind::East) return 0;
            if(windId == Wind::West) return 1;
            return -1;
        }
        int teamForWind(int windId) const override { return controllerForWind(windId); }
    };

    class CoalitionTopology final : public MatchTopology
    {
    public:
        const std::string & id(void) const override
        {
            static const std::string value(CoalitionTopologyId);
            return value;
        }

        int version(void) const override { return CoalitionTopologyVersion; }
        int seatCount(void) const override { return 4; }
        int controllerCount(void) const override { return 4; }
        int teamCount(void) const override { return 2; }
        int controllerForWind(int windId) const override
        {
            return Wind::East <= windId && windId <= Wind::North ? windId - Wind::East : -1;
        }
        int teamForWind(int windId) const override { return halfForWind(windId); }
        int controllerForClan(int clanId) const override
        {
            return Clan::Red <= clanId && clanId <= Clan::Purple ? clanId - Clan::Red : -1;
        }
        int teamForClan(int clanId) const override { return halfForClan(clanId); }
    };

    const MatchTopology*& selectedMatchTopology(void)
    {
        static const MatchTopology* selected = &classicFreeForAllTopology();
        return selected;
    }

    std::string topologyLabel(const std::string & id, int version)
    {
        return id + "@" + std::to_string(version);
    }
}

bool MatchTopology::sharesController(int firstWindId, int secondWindId) const
{
    const int first = controllerForWind(firstWindId);
    return 0 <= first && first == controllerForWind(secondWindId);
}

const std::vector<Wind::wind_t> & MatchTopology::winds(void) const
{
    static const std::vector<Wind::wind_t> four = { Wind::East, Wind::South, Wind::West, Wind::North };
    static const std::vector<Wind::wind_t> two = { Wind::East, Wind::West };
    return seatCount() == 2 ? two : four;
}

bool MatchTopology::hasWind(int windId) const
{
    const auto & seats = winds();
    return std::find(seats.begin(), seats.end(), windId) != seats.end();
}

Wind::wind_t MatchTopology::nextWind(int windId) const
{
    const auto & seats = winds();
    auto it = std::find(seats.begin(), seats.end(), windId);
    return it == seats.end() || ++it == seats.end() ? seats.front() : *it;
}

bool MatchTopology::allied(int firstWindId, int secondWindId) const
{
    const int first = teamForWind(firstWindId);
    return 0 <= first && first == teamForWind(secondWindId);
}

bool MatchTopology::sharesControllerByClan(int firstClanId, int secondClanId) const
{
    const int first = controllerForClan(firstClanId);
    return 0 <= first && first == controllerForClan(secondClanId);
}

bool MatchTopology::alliedByClan(int firstClanId, int secondClanId) const
{
    const int first = teamForClan(firstClanId);
    return 0 <= first && first == teamForClan(secondClanId);
}

const MatchTopology & classicFreeForAllTopology(void)
{
    static const ClassicFreeForAllTopology topology;
    return topology;
}

const MatchTopology & duelTopology(void)
{
    static const DuelTopology topology;
    return topology;
}

const MatchTopology & legacyDuelTopology(void)
{
    static const LegacyDuelTopology topology;
    return topology;
}

const MatchTopology & coalitionTopology(void)
{
    static const CoalitionTopology topology;
    return topology;
}

const MatchTopology & activeMatchTopology(void)
{
    return *selectedMatchTopology();
}

const MatchTopology* findMatchTopology(const std::string & id, int version)
{
    const MatchTopology & classic = classicFreeForAllTopology();
    if(id == classic.id() && version == classic.version()) return &classic;
    const MatchTopology & duel = duelTopology();
    if(id == duel.id() && version == duel.version()) return &duel;
    const MatchTopology & legacyDuel = legacyDuelTopology();
    if(id == legacyDuel.id() && version == legacyDuel.version()) return &legacyDuel;
    const MatchTopology & coalition = coalitionTopology();
    if(id == coalition.id() && version == coalition.version()) return &coalition;
    return nullptr;
}

MatchTopologyIdentity matchTopologyIdentity(const MatchTopology & topology)
{
    return { topology.id(), topology.version() };
}

SWE::JsonObject matchTopologyIdentityJson(const MatchTopology & topology)
{
    SWE::JsonObject result;
    result.addString("id", topology.id());
    result.addInteger("version", topology.version());
    return result;
}

bool resolveMatchTopologyIdentity(const SWE::JsonObject & container,
                                  MatchTopologyIdentity & identity,
                                  bool allowLegacyClassic,
                                  std::string* error)
{
    identity = MatchTopologyIdentity();
    if(!container.hasKey(MatchTopologyIdentityKey))
    {
        if(!allowLegacyClassic)
        {
            if(error) *error = "Match topology metadata is missing";
            return false;
        }
        identity = matchTopologyIdentity(classicFreeForAllTopology());
        if(error) error->clear();
        return true;
    }

    const SWE::JsonObject* encoded = container.getObject(MatchTopologyIdentityKey);
    if(!encoded || !encoded->isString("id") || !encoded->isInteger("version"))
    {
        if(error) *error = "Match topology metadata is invalid";
        return false;
    }

    identity.id = encoded->getString("id");
    identity.version = encoded->getInteger("version");
    if(!identity.isValid())
    {
        if(error) *error = "Match topology metadata is invalid";
        return false;
    }
    if(!findMatchTopology(identity.id, identity.version))
    {
        if(error) *error = "Match topology is unavailable or incompatible: " +
            topologyLabel(identity.id, identity.version);
        return false;
    }

    if(error) error->clear();
    return true;
}

bool selectActiveMatchTopology(const std::string & id, int version, std::string* error)
{
    const MatchTopology* selected = findMatchTopology(id, version);
    if(!selected)
    {
        if(error) *error = "Match topology is unavailable or incompatible: " +
            topologyLabel(id, version);
        return false;
    }
    selectedMatchTopology() = selected;
    if(error) error->clear();
    return true;
}

bool sameMatchTopology(const MatchTopologyIdentity & first,
                       const MatchTopologyIdentity & second)
{
    return first.id == second.id && first.version == second.version;
}
