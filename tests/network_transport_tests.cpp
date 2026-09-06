#define SDL_MAIN_HANDLED
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "network/tcptransport.h"
#include "network/wirejson.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    using namespace Multiplayer;
    using Clock = std::chrono::steady_clock;

    void require(bool condition, const std::string & message)
    {
        if(!condition) throw std::runtime_error(message);
    }

    void eventually(const std::function<bool()> & ready, const std::string & message,
                    int milliseconds = 3000)
    {
        const auto deadline = Clock::now() + std::chrono::milliseconds(milliseconds);
        do
        {
            if(ready()) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        while(Clock::now() < deadline);
        throw std::runtime_error(message);
    }

    std::string framed(const std::string & payload)
    {
        const auto size = static_cast<std::uint32_t>(payload.size());
        std::string result;
        for(int shift : {24, 16, 8, 0}) result += static_cast<char>(size >> shift);
        return result + payload;
    }

    struct Pair
    {
        TcpListener listener;
        TcpConnection client;
        std::unique_ptr<TcpConnection> server;

        explicit Pair(const TcpLimits & limits = TcpLimits()) : listener(limits), client(limits)
        {
            std::string error;
            require(listener.listen(0, error), "Listener failed: " + error);
            require(listener.port() != 0, "Ephemeral port not reported");
            require(client.connect("localhost", listener.port(), error), "Connect failed: " + error);
            eventually([&]
            {
                client.poll();
                if(!server) server = listener.accept();
                return client.connected() && server;
            }, "Loopback connection did not complete");
        }

        void poll() { client.poll(); server->poll(); }
    };

    class RawPeer
    {
#ifdef _WIN32
        SOCKET socket = INVALID_SOCKET;
#else
        int socket = -1;
#endif
    public:
        explicit RawPeer(std::uint16_t port)
        {
            socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#ifdef _WIN32
            require(socket != INVALID_SOCKET, "Raw socket failed");
#else
            require(socket >= 0, "Raw socket failed");
#endif
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_port = htons(port);
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            require(::connect(socket, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0,
                    "Raw loopback connect failed");
            const int noDelay = 1;
            require(setsockopt(socket, IPPROTO_TCP, TCP_NODELAY,
                               reinterpret_cast<const char *>(&noDelay), sizeof(noDelay)) == 0,
                    "Raw socket TCP_NODELAY failed");
        }
        ~RawPeer() { close(); }
        RawPeer(const RawPeer &) = delete;
        RawPeer & operator=(const RawPeer &) = delete;

        void send(const std::string & bytes)
        {
            // Only small, bounded writes to a freshly accepted local socket.
            std::size_t offset = 0;
            while(offset < bytes.size())
            {
                const int sent = static_cast<int>(::send(socket, bytes.data() + offset,
                                                         static_cast<int>(bytes.size() - offset), 0));
                require(sent > 0, "Raw socket send failed");
                offset += static_cast<std::size_t>(sent);
            }
        }

        void close()
        {
#ifdef _WIN32
            if(socket != INVALID_SOCKET) closesocket(socket);
            socket = INVALID_SOCKET;
#else
            if(socket >= 0) ::close(socket);
            socket = -1;
#endif
        }
    };

    std::unique_ptr<TcpConnection> accept(TcpListener & listener)
    {
        std::unique_ptr<TcpConnection> connection;
        eventually([&] { connection = listener.accept(); return !!connection; }, "Raw accept timed out");
        return connection;
    }

    void testRoundTripAndMoves()
    {
        Pair pair;
        require(!pair.listener.accept(), "Accept must return immediately without another peer");
        std::string error, message;
        for(int index = 0; index < 40; ++index)
            require(pair.client.send("{\"sequence\":" + std::to_string(index) + "}", error), error);
        int index = 0;
        eventually([&]
        {
            pair.poll();
            while(pair.server->receive(message))
            {
                require(message == "{\"sequence\":" + std::to_string(index++) + "}", "Frame ordering changed");
            }
            return index == 40;
        }, "Queued roundtrip timed out");
        TcpConnection moved(std::move(pair.client));
        require(pair.client.closed(), "Moved-from connection must be safely closed");
        require(!pair.client.send("{}", error), "Moved-from connection accepted a message");
        require(pair.server->send("{\"ok\":true}", error), error);
        eventually([&]
        {
            pair.server->poll();
            moved.poll();
            return moved.receive(message);
        }, "Moved connection lost its socket");
        require(message == "{\"ok\":true}", "Reply payload changed");
        moved.close();
        moved.close();
        eventually([&] { pair.server->poll(); return pair.server->closed(); }, "Peer EOF not detected");
    }

    void testFragmentedAndCoalesced()
    {
        TcpListener listener;
        std::string error, message;
        require(listener.listen(0, error), error);
        RawPeer raw(listener.port());
        auto server = accept(listener);
        const std::string first = framed("{\"text\":\"fragmented\"}");
        for(std::size_t index = 0; index + 1 < first.size(); ++index)
        {
            raw.send(first.substr(index, 1));
            server->poll();
            require(!server->receive(message), "Partial frame escaped to the application");
        }
        raw.send(first.substr(first.size() - 1) + framed("{}") + framed("{\"last\":true}"));
        std::vector<std::string> messages;
        eventually([&]
        {
            server->poll();
            while(server->receive(message)) messages.push_back(message);
            return messages.size() == 3;
        }, "Coalesced frames were not decoded");
        require(messages == std::vector<std::string>{"{\"text\":\"fragmented\"}", "{}", "{\"last\":true}"},
                "Fragmented/coalesced payloads changed");
    }

    void testPartialIoAndBackpressure()
    {
        TcpLimits limits;
        limits.maximumFrameBytes = 128 * 1024;
        limits.maximumQueuedBytes = 2 * (limits.maximumFrameBytes + 4);
        limits.maximumQueuedFrames = 2;
        limits.bytesPerPoll = 73;
        Pair pair(limits);
        std::string error, message;
        const std::string payload(limits.maximumFrameBytes, 'x');
        require(pair.client.send(payload, error), error);
        require(pair.client.send(payload, error), error);
        require(!pair.client.send("{}", error), "Outbound queue exceeded its configured byte/frame limit");
        require(pair.client.connected(), "Recoverable backpressure closed the connection");
        int received = 0;
        // Tight polling deliberately forces thousands of partial frame writes
        // and reads, rather than depending on OS socket-buffer sizes.
        const auto deadline = Clock::now() + std::chrono::seconds(5);
        while(received < 2 && Clock::now() < deadline)
        {
            pair.poll();
            while(pair.server->receive(message))
            {
                require(message == payload, "Large partial-I/O payload was corrupted");
                ++received;
            }
        }
        require(received == 2, "Large frames did not complete under a small poll budget");
        require(pair.client.send("{}", error), "Drained queue did not recover");
        eventually([&] { pair.poll(); return pair.server->receive(message); }, "Post-backpressure frame missing");
    }

    void testInvalidLengthsAndInboundBudget()
    {
        for(const std::string & header : {std::string(4, '\0'), std::string("\0\x10\0\1", 4)})
        {
            TcpListener listener;
            std::string error;
            require(listener.listen(0, error), error);
            RawPeer raw(listener.port());
            auto server = accept(listener);
            raw.send(header);
            eventually([&] { server->poll(); return server->closed(); }, "Illegal frame length was accepted");
            require(server->error().find("length") != std::string::npos, "Length failure diagnostic missing");
        }
        TcpLimits limits;
        limits.maximumQueuedFrames = 2;
        TcpListener listener(limits);
        std::string error;
        require(listener.listen(0, error), error);
        RawPeer raw(listener.port());
        auto server = accept(listener);
        raw.send(framed("{}") + framed("{}") + framed("{}"));
        eventually([&] { server->poll(); return server->closed(); }, "Inbound queue limit was ignored");
        require(server->error().find("queue") != std::string::npos, "Queue failure diagnostic missing");
    }

    void testEofAndDeadlines()
    {
        TcpLimits limits;
        TcpListener listener(limits);
        std::string error, message;
        require(listener.listen(0, error), error);
        {
            RawPeer raw(listener.port());
            auto server = accept(listener);
            raw.send(framed("{\"finished\":true}"));
            raw.close();
            eventually([&] { server->poll(); return server->closed(); }, "EOF not observed");
            require(server->receive(message) && message == "{\"finished\":true}", "EOF discarded a complete final frame");
            require(!server->receive(message), "Final frame was delivered twice");
        }
        {
            RawPeer raw(listener.port());
            auto server = accept(listener);
            raw.send(framed("{\"unfinished\":true}").substr(0, 8));
            raw.close();
            eventually([&] { server->poll(); return server->closed(); }, "Partial-frame EOF not observed");
            require(!server->receive(message), "Truncated frame was delivered");
            require(server->error().find("during a frame") != std::string::npos, "Truncation diagnostic missing");
        }
        for(const bool payloadDrip : {false, true})
        {
            TcpLimits dripLimits;
            dripLimits.frameTimeout = std::chrono::milliseconds(35);
            TcpListener dripListener(dripLimits);
            require(dripListener.listen(0, error), error);
            RawPeer raw(dripListener.port());
            auto server = accept(dripListener);
            // Prove that an empty early poll does not establish the timer.
            // The first byte can reach a loopback socket only on a later poll.
            server->poll();
            require(server->partialFrameBytes() == 0, "Idle connection reported a partial frame");
            const std::string first = payloadDrip ? framed(std::string(64, 'x')).substr(0, 5) : std::string(1, '\0');
            raw.send(first);
            eventually([&]
            {
                server->poll();
                require(!server->closed(), "Connection closed before its first partial frame was observed");
                return server->partialFrameBytes() == first.size();
            }, "First partial frame bytes did not reach the receiver");
            const auto receivedAt = Clock::now();
            std::this_thread::sleep_until(receivedAt + std::chrono::milliseconds(20));
            server->poll();
            // A loaded runner may already overshoot the complete deadline.
            // That is a valid timeout, and we must not write to its closed peer.
            if(!server->closed())
            {
                raw.send(std::string(1, payloadDrip ? 'y' : '\0'));
                eventually([&]
                {
                    server->poll();
                    return server->closed() || server->partialFrameBytes() == first.size() + 1;
                }, "Later partial frame byte did not reach the receiver");
            }
            std::this_thread::sleep_until(receivedAt + dripLimits.frameTimeout + std::chrono::milliseconds(5));
            server->poll();
            require(server->closed() && server->error().find("timed out") != std::string::npos,
                    payloadDrip ? "Slow payload drip extended the frame deadline" :
                                  "Slow header drip extended the frame deadline");
        }
        limits.writeTimeout = std::chrono::milliseconds(15);
        Pair pair(limits);
        // Unlike receive(), send() starts the queued-write deadline
        // synchronously, so no socket-delivery observation is needed here.
        require(pair.client.send("{}", error), error);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        pair.client.poll();
        require(pair.client.closed() && pair.client.error().find("Outgoing frame timed out") != std::string::npos,
                "Unsent frame deadline was ignored");
    }

    void testInvalidConnectAndReconnect()
    {
        TcpConnection connection;
        std::string error;
        require(!connection.connect("example.invalid", 1234, error), "Unexpected blocking DNS support");
        require(connection.closed() && error.find("IPv4") != std::string::npos, "Invalid address diagnostic missing");
        require(!connection.connect(std::string("127.0.0.1\0hidden", 16), 1234, error), "NUL-suffixed address accepted");
        require(!connection.connect("127.0.0.1", 0, error), "Zero connection port accepted");
        TcpListener listener;
        require(listener.listen(0, error), error);
        const auto unavailablePort = listener.port();
        listener.close();
        if(connection.connect("127.0.0.1", unavailablePort, error))
            eventually([&] { connection.poll(); return connection.closed(); }, "Connection refusal was not reported");
        require(!connection.error().empty(), "Connection refusal has no error");
        require(listener.listen(0, error), error);
        for(int attempt = 0; attempt < 3; ++attempt)
        {
            require(connection.connect("127.0.0.1", listener.port(), error), error);
            auto server = accept(listener);
            eventually([&] { connection.poll(); return connection.connected(); }, "Reconnect failed");
            require(connection.error().empty(), "Previous connection error leaked into reconnect");
            require(connection.send("{}", error), error);
            std::string message;
            eventually([&]
            {
                connection.poll(); server->poll(); return server->receive(message);
            }, "Reconnected message missing");
            connection.close();
        }
    }

    void testSecureRandom()
    {
        std::string first, second, error;
        require(secureRandomHex(32, first, error), "OS random source failed: " + error);
        require(secureRandomHex(32, second, error), "OS random source failed: " + error);
        require(first.size() == 64 && first.find_first_not_of("0123456789abcdef") == std::string::npos,
                "Random token has an incorrect encoding");
        require(first != second, "Random source repeated a 256-bit token");
        require(!secureRandomHex(0, first, error) && first.empty(), "Empty entropy request accepted");
        require(!secureRandomHex(257, first, error) && first.empty(), "Unbounded entropy request accepted");
    }

    void testJsonSemantics()
    {
        SWE::JsonObject object;
        std::string error;
        const std::string valid = R"({"text":"\u0412\u0435\u0442\u0435\u0440 \ud83c\udf2c","integer":-2147483648,"maximum":2147483647,"real":1e2,"yes":true,"no":false,"null":null,"array":[null,{},[],0,"a\n\t\\\"\/"]})";
        require(parseWireObject(valid, object, error), error);
        require(object.getString("text") == u8"Ветер 🌬", "Unicode escape decoding changed text");
        require(object.isInteger("integer") && object.getInteger("integer") == (-2147483647 - 1), "Integer minimum changed");
        require(object.isInteger("maximum") && object.getInteger("maximum") == 2147483647, "Integer maximum changed");
        require(object.isDouble("real") && object.getDouble("real") == 100, "Exponent number changed type or value");
        require(object.isBoolean("yes") && object.getBoolean("yes"), "Boolean true changed type");
        require(object.isBoolean("no") && !object.getBoolean("no"), "Boolean false changed type");
        require(object.isNull("null"), "Null changed type");
        require(object.getArray("array") && object.getArray("array")->size() == 5, "Array lost a null/container entry");
        SWE::JsonObject roundTrip;
        require(parseWireObject(object.toString(), roundTrip, error), "SWE serialized values did not parse: " + error);
        require(roundTrip.getString("text") == object.getString("text"), "UTF-8 roundtrip failed");
        require(parseWireObject(" \r\n { } \t ", object, error) && object.size() == 0, "Empty root object rejected");
    }

    void testJsonRejections()
    {
        std::vector<std::string> invalid = {
            "", "[]", "null", "{", "{}{}", "{} true", "{} /* comment */", "{\"x\":1,}",
            "{\"x\":[1,]}", "{x:1}", "{\"x\":True}", "{\"x\":trueish}", "{\"x\":NaN}",
            "{\"x\":Infinity}", "{\"x\":01}", "{\"x\":0x10}", "{\"x\":+1}", "{\"x\":1.}",
            "{\"x\":.1}", "{\"x\":1e}", "{\"x\":--1}", "{\"x\":2147483648}", "{\"x\":-2147483649}",
            "{\"x\":1e999}", "{\"x\":1 \"y\":2}", "{\"x\" 1}", "{\"x\":1,\"x\":2}",
            R"({"x":1,"\u0078":2})", R"({"obj":{"a":1,"a":2}})", R"({"x":"\uD800"})",
            R"({"x":"\uDC00"})", R"({"x":"\uD800\u0041"})", R"({"x":"\uZZZZ"})",
            R"({"x":"\q"})", "{\"x\":\"\n\"}", std::string("{\"x\":\"") + '\0' + "\"}"
        };
        for(const auto & bytes : {std::string("\xc0\xaf", 2), std::string("\xed\xa0\x80", 3),
                                  std::string("\xf4\x90\x80\x80", 4), std::string("\xe2\x28\xa1", 3),
                                  std::string("\x80", 1), std::string("\xf0\x9f", 2)})
            invalid.push_back("{\"x\":\"" + bytes + "\"}");
        invalid.push_back("{\"x\":" + std::string(40, '[') + "0" + std::string(40, ']') + "}");
        invalid.push_back("{\"x\":\"" + std::string(1024 * 1024, 'x') + "\"}");
        std::string many = "{\"x\":[0";
        for(int index = 0; index < 65536; ++index) many += ",0";
        invalid.push_back(many + "]}");
        SWE::JsonObject unchanged;
        unchanged.addString("sentinel", "unchanged");
        const auto baseline = unchanged.toString();
        for(std::size_t index = 0; index < invalid.size(); ++index)
        {
            std::string error;
            require(!parseWireObject(invalid[index], unchanged, error), "Malformed JSON accepted, case " + std::to_string(index));
            require(!error.empty(), "Malformed JSON rejected without a diagnostic");
            require(unchanged.toString() == baseline, "Failed parsing mutated the output object");
        }
    }

    int echoServer()
    {
        TcpListener listener;
        std::string error, message;
        require(listener.listen(0, error), error);
        std::cout << "PORT " << listener.port() << std::endl;
        auto peer = accept(listener);
        for(int index = 0; index < 3; ++index)
        {
            eventually([&] { peer->poll(); return peer->receive(message); }, "Process client message timed out");
            SWE::JsonObject parsed;
            require(parseWireObject(message, parsed, error), error);
            require(parsed.isInteger("sequence") && parsed.getInteger("sequence") == index, "Process message order changed");
            require(peer->send(message, error), error);
            peer->poll();
        }
        eventually([&] { peer->poll(); return peer->closed(); }, "Process client failed to close");
        std::cout << "SERVER_OK" << std::endl;
        return 0;
    }

    int echoClient(std::uint16_t port)
    {
        TcpConnection peer;
        std::string error, message;
        require(peer.connect("127.0.0.1", port, error), error);
        eventually([&] { peer.poll(); return peer.connected(); }, "Process server connection timed out");
        for(int index = 0; index < 3; ++index)
        {
            SWE::JsonObject object;
            object.addInteger("sequence", index);
            object.addString("text", u8"Руны — один игрок, одна рука");
            const std::string sent = object.toString();
            require(peer.send(sent, error), error);
            eventually([&] { peer.poll(); return peer.receive(message); }, "Process echo timed out");
            require(message == sent, "Cross-process UTF-8 payload changed");
        }
        peer.close();
        std::cout << "CLIENT_OK" << std::endl;
        return 0;
    }
}

int main(int argc, char ** argv)
{
    try
    {
        if(argc == 2 && std::string(argv[1]) == "--echo-server") return echoServer();
        if(argc == 3 && std::string(argv[1]) == "--echo-client")
        {
            const int port = std::stoi(argv[2]);
            require(port > 0 && port <= 65535, "Invalid process-test port");
            return echoClient(static_cast<std::uint16_t>(port));
        }
        require(argc == 1, "Unknown transport-test argument");
        testJsonSemantics();
        testJsonRejections();
        testRoundTripAndMoves();
        testFragmentedAndCoalesced();
        testPartialIoAndBackpressure();
        testInvalidLengthsAndInboundBudget();
        testEofAndDeadlines();
        testInvalidConnectAndReconnect();
        testSecureRandom();
        std::cout << "Network transport and strict wire JSON: all checks passed" << std::endl;
        return 0;
    }
    catch(const std::exception & exception)
    {
        std::cerr << "Network transport test failed: " << exception.what() << std::endl;
        return 1;
    }
}
