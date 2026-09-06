#include <chrono>
#include <iostream>
#include <set>
#include <stdexcept>
#include <thread>

#include "networkaddress.h"

int runNetworkAddressTests()
{
    using namespace Multiplayer;
    try
    {
        const auto require = [](bool value, const char* message) {
            if(!value) throw std::runtime_error(message);
        };
        for(const char* valid : {"127.0.0.1", "192.168.1.20", "10.0.0.1", "172.31.255.254"})
            require(isNumericIPv4(valid), "valid numeric IPv4 rejected");
        for(const char* invalid : {"", "localhost", "::1", "192.168.1", "192.168.1.1.1", "192.168.001.1",
             "192.168.1.256", "192.168.1.-1", "192.168.1.+1", " 127.0.0.1", "127.0.0.1\n", "0x7f.0.0.1"})
            require(!isNumericIPv4(invalid), "ambiguous or nonnumeric IPv4 accepted");
        std::uint16_t port = 19782;
        require(parseNetworkPort("1", port) && port == 1, "minimum port rejected");
        require(parseNetworkPort("65535", port) && port == 65535, "maximum port rejected");
        for(const char* invalid : {"", "0", "65536", "9999999999", "-1", "+1", "1.0", " 80", "80 "})
        {
            port = 19782;
            require(!parseNetworkPort(invalid, port) && port == 19782, "invalid port accepted or changed output");
        }
        for(const int length : {32, 64})
        {
            const InviteAddress original{"192.168.1.20", 65535, std::string(length, 'f')};
            InviteAddress parsed;
            require(parseInvite(formatInvite(original), parsed), "valid invitation did not round trip");
            require(parsed.address == original.address && parsed.port == original.port && parsed.room == original.room,
                "invitation round trip changed endpoint or secret");
        }
        InviteAddress parsed;
        require(parseInvite("fourwinds://127.0.0.1:19782/#01ABCDEF01ABCDEF01ABCDEF01ABCDEF", parsed) &&
                parsed.room == "01abcdef01abcdef01abcdef01abcdef",
            "uppercase hex invitation not normalized");
        require(!parseInvite("fourwinds://127.0.0.1:19782/#01234567", parsed), "obsolete short room secret accepted");
        for(const char* invalid : {"https://127.0.0.1:19782/#0123456789abcdef0123456789abcdef", "fourwinds://host.example:19782/#0123456789abcdef0123456789abcdef",
            "fourwinds://127.0.0.1:0/#0123456789abcdef0123456789abcdef", "fourwinds://127.0.0.1:65536/#0123456789abcdef0123456789abcdef",
            "fourwinds://127.0.0.1:19782/#0123456", "fourwinds://127.0.0.1:19782/#012345678",
            "fourwinds://127.0.0.1:19782/#0123456789abcdef0123456789abcdeg", "fourwinds://127.0.0.1:19782/#0123456789abcdef0123456789abcdef?x=1",
            "fourwinds://127.0.0.1:19782/#0123456789abcdef0123456789abcdef\n", "fourwinds://127.0.0.1:19782/#0123456789abcdef0123456789abcdef/extra",
            "fourwinds://user@127.0.0.1:19782/#0123456789abcdef0123456789abcdef", "fourwinds://127.0.0.1:19782#0123456789abcdef0123456789abcdef",
            "fourwinds://127.0.0.1:19782/path#0123456789abcdef0123456789abcdef", "fourwinds://127.0.0.1:19782/#%300123456789abcdef0123456789abcdef",
            "fourwinds://[::1]:19782/#0123456789abcdef0123456789abcdef", "fourwinds://127.0.0.1:19782/#0123456789abcdef0123456789abcdef\t"})
        {
            const auto previous = parsed;
            require(!parseInvite(invalid, parsed), "malformed invitation accepted");
            require(parsed.address == previous.address && parsed.port == previous.port && parsed.room == previous.room,
                "rejected invitation changed join inputs");
        }
        require(formatInvite({"localhost", 19782, "0123456789abcdef0123456789abcdef"}).empty() &&
                formatInvite({"127.0.0.1", 0, "0123456789abcdef0123456789abcdef"}).empty() &&
                formatInvite({"127.0.0.1", 19782, "invalid"}).empty(), "invalid invitation was formatted");

        LocalIPv4Discovery discovery;
        std::vector<std::string> addresses;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while(!discovery.poll(addresses) && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        require(discovery.poll(addresses), "local adapter enumeration did not complete");
        require(std::set<std::string>(addresses.begin(), addresses.end()).size() == addresses.size(),
                "local addresses contain duplicates");
        for(const auto& address : addresses)
            require(isNumericIPv4(address) && address.compare(0, 4, "127.") != 0 && address != "0.0.0.0",
                    "discovery advertised a non-LAN address");
        std::cout << "network address tests: ok\n";
        return 0;
    }
    catch(const std::exception& error)
    {
        std::cerr << "network address tests: " << error.what() << '\n';
        return 1;
    }
}
