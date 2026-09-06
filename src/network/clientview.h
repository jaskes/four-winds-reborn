#ifndef FOUR_WINDS_NETWORK_CLIENTVIEW_H
#define FOUR_WINDS_NETWORK_CLIENTVIEW_H

#include "gamedata.h"

namespace Multiplayer
{
    // Presentation snapshots deliberately have no authoritative wall, RNG or
    // pending battle session. Only the recipient's permitted information travels.
    JsonObject buildClientView(const Avatar & recipient);
    bool applyClientView(const JsonObject &, std::string* error = nullptr);

    // An empty wire object means that the event is private to another player.
    JsonObject eventToWire(const ActionMessage &, const Avatar & recipient);
    bool eventFromWire(const JsonObject &, ActionMessage &, std::string* error = nullptr);
    ActionList filterEvents(const ActionList &, const Avatar & recipient);
}

#endif
