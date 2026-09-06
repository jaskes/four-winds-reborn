#ifndef FOUR_WINDS_NETWORK_ADDRESS_H
#define FOUR_WINDS_NETWORK_ADDRESS_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Multiplayer
{
    struct InviteAddress
    {
        std::string address;
        std::uint16_t port = 19782;
        std::string room;
    };

    bool isNumericIPv4(const std::string&);
    bool parseNetworkPort(const std::string&, std::uint16_t&);
    bool parseInvite(const std::string&, InviteAddress&);
    // Invalid components produce an empty string. No DNS, URL fetching or
    // percent decoding occurs when handling an invitation.
    std::string formatInvite(const InviteAddress&);

    // Enumerate local interfaces away from the SDL thread; construction,
    // polling and destruction never wait for adapter enumeration to finish.
    class LocalIPv4Discovery
    {
        struct State;
        std::shared_ptr<State> state;
    public:
        LocalIPv4Discovery();
        bool poll(std::vector<std::string>& addresses) const;
    };
}

#endif
