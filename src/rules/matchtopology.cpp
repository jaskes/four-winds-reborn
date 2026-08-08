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

bool MatchTopology::allied(int firstWindId, int secondWindId) const
{
    const int first = teamForWind(firstWindId);
    return 0 <= first && first == teamForWind(secondWindId);
}

const MatchTopology & classicFreeForAllTopology(void)
{
    static const ClassicFreeForAllTopology topology;
    return topology;
}

const MatchTopology & activeMatchTopology(void)
{
    return *selectedMatchTopology();
}

const MatchTopology* findMatchTopology(const std::string & id, int version)
{
    const MatchTopology & classic = classicFreeForAllTopology();
    return id == classic.id() && version == classic.version() ? &classic : nullptr;
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
