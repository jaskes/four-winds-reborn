#ifndef FOUR_WINDS_NETWORK_STATUS_H
#define FOUR_WINDS_NETWORK_STATUS_H
#include "matchsession.h"

inline std::string networkStatusText()
{
    const auto& match = Multiplayer::session();
    const auto& status = match.status();
    if(status == "Connected") return _("Connected");
    if(status == "Connecting...") return _("Connecting...");
    if(status == "Reconnecting...") return _("Reconnecting...");
    if(status == "Could not join room; check address and invitation") return _("Could not join room; check address and invitation");
    if(status == "Waiting for disconnected player") return _("Waiting for disconnected player");
    if(status == "Connected; waiting for host") return _("Connected; waiting for host");
    if(status == "State changed; choose again") return _("State changed; choose again");
    if(status == "Room code or game version does not match") return _("Room code or game version does not match");
    if(status == "Room is full or reconnect token is invalid") return _("Room is full or reconnect token is invalid");
    if(match.active() && match.isHost() && !match.started())
        return StringFormat(_("%1 / %2 players connected")).arg(match.occupiedSeats()).arg(match.requiredSeats());
    return status;
}
#endif
