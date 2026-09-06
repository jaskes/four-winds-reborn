#ifndef FOUR_WINDS_COMMAND_WIRE_H
#define FOUR_WINDS_COMMAND_WIRE_H
#include <memory>
#include <string>
#include "actions.h"
namespace Multiplayer
{
    std::unique_ptr<ClientMessage> commandFromWire(const JsonObject&, std::string& error);
}
#endif
