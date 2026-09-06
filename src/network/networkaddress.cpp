#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <iphlpapi.h>
#else
#include <netinet/in.h>
#include <net/if.h>
#ifdef __ANDROID__
#include <array>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#else
#include <ifaddrs.h>
#endif
#endif

#include "networkaddress.h"

#include <algorithm>
#include <atomic>
#include <array>
#include <cstring>
#include <thread>

namespace Multiplayer
{
namespace
{
    bool octets(const std::string& value, std::array<unsigned, 4>& result)
    {
        if(value.size() < 7 || value.size() > 15) return false;
        std::size_t cursor = 0;
        for(unsigned index = 0; index < result.size(); ++index)
        {
            const auto start = cursor;
            unsigned number = 0;
            while(cursor < value.size() && value[cursor] >= '0' && value[cursor] <= '9')
            {
                number = number * 10 + unsigned(value[cursor++] - '0');
                if(cursor - start > 3 || number > 255) return false;
            }
            if(cursor == start || (cursor - start > 1 && value[start] == '0')) return false;
            result[index] = number;
            if(index + 1 == result.size()) return cursor == value.size();
            if(cursor == value.size() || value[cursor++] != '.') return false;
        }
        return false;
    }

    bool validRoom(const std::string& room)
    {
        if(room.size() != 32 && room.size() != 64) return false;
        return std::all_of(room.begin(), room.end(), [](char value) {
            return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') ||
                   (value >= 'A' && value <= 'F');
        });
    }

    void appendAddress(std::vector<std::string>& output, const sockaddr* address)
    {
        if(!address || address->sa_family != AF_INET) return;
        const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(address);
        const auto* bytes = reinterpret_cast<const unsigned char*>(&ipv4->sin_addr);
        // Loopback/unspecified/multicast addresses cannot be advertised to a
        // second device. Link-local addresses remain useful on ad-hoc LANs.
        if(bytes[0] == 0 || bytes[0] == 127 || bytes[0] >= 224) return;
        output.push_back(std::to_string(bytes[0]) + "." + std::to_string(bytes[1]) + "." +
                         std::to_string(bytes[2]) + "." + std::to_string(bytes[3]));
    }

    std::vector<std::string> enumerateAddresses()
    {
        std::vector<std::string> result;
#ifdef _WIN32
        ULONG length = 16384;
        std::vector<unsigned char> buffer(length);
        ULONG status = ERROR_BUFFER_OVERFLOW;
        // An interface can change between the size query and enumeration.
        for(int attempt = 0; attempt < 3 && status == ERROR_BUFFER_OVERFLOW; ++attempt)
        {
            buffer.resize(length);
            status = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                GAA_FLAG_SKIP_DNS_SERVER, nullptr,
                reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()), &length);
            if(length > 1024 * 1024) return result;
        }
        if(status == NO_ERROR)
            for(auto* adapter = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()); adapter; adapter = adapter->Next)
            {
                if(adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
                for(auto* address = adapter->FirstUnicastAddress; address; address = address->Next)
                    appendAddress(result, address->Address.lpSockaddr);
            }
#elif defined(__ANDROID__)
        // getifaddrs is only exported from API 24; supported Android API 23
        // devices use the IPv4 interface ioctl instead of a newer libc symbol.
        const int descriptor = socket(AF_INET, SOCK_DGRAM, 0);
        if(descriptor >= 0)
        {
            std::array<ifreq, 128> entries{};
            ifconf config{};
            config.ifc_len = static_cast<int>(sizeof(entries));
            config.ifc_req = entries.data();
            if(ioctl(descriptor, SIOCGIFCONF, &config) == 0)
                for(int index = 0; index < config.ifc_len / static_cast<int>(sizeof(ifreq)); ++index)
                {
                    ifreq flags = entries[index];
                    if(ioctl(descriptor, SIOCGIFFLAGS, &flags) == 0 &&
                       (flags.ifr_flags & IFF_UP) && !(flags.ifr_flags & IFF_LOOPBACK))
                        appendAddress(result, &entries[index].ifr_addr);
                }
            close(descriptor);
        }
#else
        ifaddrs* interfaces = nullptr;
        if(getifaddrs(&interfaces) == 0)
        {
            for(auto* item = interfaces; item; item = item->ifa_next)
                if((item->ifa_flags & IFF_UP) && !(item->ifa_flags & IFF_LOOPBACK))
                    appendAddress(result, item->ifa_addr);
            freeifaddrs(interfaces);
        }
#endif
        std::sort(result.begin(), result.end());
        result.erase(std::unique(result.begin(), result.end()), result.end());
        // RFC1918 is the common Wi-Fi/Ethernet invitation, link-local last.
        const auto priority = [](const std::string& address) {
            std::array<unsigned, 4> parts{};
            octets(address, parts);
            if(parts[0] == 192 && parts[1] == 168) return 0;
            if(parts[0] == 10 || (parts[0] == 172 && parts[1] >= 16 && parts[1] <= 31)) return 1;
            return parts[0] == 169 && parts[1] == 254 ? 3 : 2;
        };
        std::stable_sort(result.begin(), result.end(), [&](const auto& a, const auto& b) { return priority(a) < priority(b); });
        return result;
    }
}

bool isNumericIPv4(const std::string& address)
{
    std::array<unsigned, 4> result{};
    return octets(address, result);
}

bool parseNetworkPort(const std::string& value, std::uint16_t& port)
{
    if(value.empty() || value.size() > 5) return false;
    unsigned number = 0;
    for(char digit : value)
    {
        if(digit < '0' || digit > '9') return false;
        number = number * 10 + unsigned(digit - '0');
    }
    if(number == 0 || number > 65535) return false;
    port = static_cast<std::uint16_t>(number);
    return true;
}

bool parseInvite(const std::string& value, InviteAddress& output)
{
    constexpr auto prefix = "fourwinds://";
    if(value.size() > 110 || value.compare(0, std::strlen(prefix), prefix) != 0) return false;
    const auto colon = value.find(':', std::strlen(prefix));
    const auto separator = value.find("/#", colon);
    if(colon == std::string::npos || separator == std::string::npos) return false;
    InviteAddress parsed;
    parsed.address = value.substr(std::strlen(prefix), colon - std::strlen(prefix));
    parsed.room = value.substr(separator + 2);
    if(!isNumericIPv4(parsed.address) || !validRoom(parsed.room) ||
       !parseNetworkPort(value.substr(colon + 1, separator - colon - 1), parsed.port)) return false;
    std::transform(parsed.room.begin(), parsed.room.end(), parsed.room.begin(), [](char ch) {
        return ch >= 'A' && ch <= 'F' ? static_cast<char>(ch - 'A' + 'a') : ch;
    });
    output = std::move(parsed);
    return true;
}

std::string formatInvite(const InviteAddress& invitation)
{
    if(!isNumericIPv4(invitation.address) || !invitation.port || !validRoom(invitation.room)) return {};
    return "fourwinds://" + invitation.address + ':' + std::to_string(invitation.port) + "/#" + invitation.room;
}

struct LocalIPv4Discovery::State
{
    std::atomic<bool> ready{false};
    std::vector<std::string> addresses;
};

LocalIPv4Discovery::LocalIPv4Discovery() : state(std::make_shared<State>())
{
    try
    {
        std::thread([result = state] {
            try { result->addresses = enumerateAddresses(); } catch(...) {}
            result->ready.store(true, std::memory_order_release);
        }).detach();
    }
    catch(...) { state->ready.store(true, std::memory_order_release); }
}

bool LocalIPv4Discovery::poll(std::vector<std::string>& addresses) const
{
    if(!state->ready.load(std::memory_order_acquire)) return false;
    addresses = state->addresses;
    return true;
}
}
