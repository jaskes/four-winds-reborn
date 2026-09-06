#include "network/securetransport.h"

#include <array>
#include <chrono>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
    using namespace Multiplayer;
    using Clock = std::chrono::steady_clock;
    const std::string Secret = "0123456789abcdef0123456789abcdef";
    void require(bool condition, const std::string& error)
    { if(!condition) throw std::runtime_error(error); }

    void eventually(const std::function<bool()>& predicate, const std::string& error, int milliseconds = 4000)
    {
        const auto deadline = Clock::now() + std::chrono::milliseconds(milliseconds);
        while(Clock::now() < deadline) if(predicate()) return;
        throw std::runtime_error(error);
    }

    struct Pair
    {
        SecureListener listener;
        SecureConnection client;
        std::unique_ptr<SecureConnection> server;
        std::string error;
        Pair(const TcpLimits& clientLimits = TcpLimits(), const TcpLimits& serverLimits = TcpLimits(),
             const std::string& clientSecret = Secret, const std::string& serverSecret = Secret)
            : listener(serverSecret, serverLimits), client(clientSecret, clientLimits)
        {
            require(listener.listen(0, error), error);
            require(client.connect("localhost", listener.port(), error), error);
            require(!client.connected(), "TCP connection was exposed as authenticated before TLS completed");
            require(!client.send("must not be early data", error), "Application write accepted before authentication");
        }
        void poll()
        {
            client.poll();
            if(!server) server = listener.accept();
            if(server) server->poll();
        }
        void authenticate()
        {
            eventually([&]
            {
                poll();
                require(!client.closed(), "Client authentication: " + client.error());
                require(!server || !server->closed(), "Server authentication: " + (server ? server->error() : "none"));
                return client.connected() && server && server->connected();
            }, "TLS handshake timed out");
        }
    };

    struct Relay
    {
        SecureListener listener;
        TcpListener interception;
        SecureConnection client;
        std::unique_ptr<SecureConnection> server;
        std::unique_ptr<TcpConnection> downstream;
        TcpConnection upstream;
        std::string error;
        std::vector<std::string> clientRecords;
        std::vector<std::string> serverRecords;
        std::size_t remainingChunks = std::numeric_limits<std::size_t>::max();
        bool tamper = false;

        explicit Relay(const TcpLimits& serverLimits = TcpLimits()) : listener(Secret, serverLimits), client(Secret)
        {
            require(listener.listen(0, error) && interception.listen(0, error), error);
            require(client.connect("localhost", interception.port(), error), error);
            require(upstream.connect("localhost", listener.port(), error), error);
            eventually([&]
            {
                poll();
                require(!client.closed(), "Relay client handshake: " + client.error());
                require(!server || !server->closed(), "Relay server handshake: " + (server ? server->error() : "none"));
                return client.connected() && server && server->connected();
            }, "Intercepted TLS handshake timed out");
        }

        void poll()
        {
            client.poll();
            if(!downstream) downstream = interception.accept();
            if(!server) server = listener.accept();
            upstream.poll();
            if(downstream && upstream.connected())
            {
                downstream->poll();
                std::string wire;
                while(downstream->receive(wire))
                {
                    clientRecords.push_back(wire);
                    if(remainingChunks == 0) continue;
                    --remainingChunks;
                    if(tamper && !wire.empty()) { wire.back() ^= 1; tamper = false; }
                    require(upstream.send(wire, error), "Relay client forwarding: " + error);
                }
                while(upstream.receive(wire))
                {
                    serverRecords.push_back(wire);
                    require(downstream->send(wire, error), "Relay server forwarding: " + error);
                }
                downstream->poll(); upstream.poll();
            }
            if(server) server->poll();
        }
    };

    unsigned word(const std::string& bytes, std::size_t offset)
    {
        require(offset + 2 <= bytes.size(), "Truncated TLS wire field");
        return (static_cast<unsigned char>(bytes[offset]) << 8) | static_cast<unsigned char>(bytes[offset + 1]);
    }

    void verifyClientHelloPolicy(const std::string& hello)
    {
        require(hello.size() > 44 && hello[0] == 22 && hello[5] == 1, "Capture does not begin with TLS ClientHello");
        std::size_t position = 5 + 4 + 2 + 32;
        require(position < hello.size(), "Missing session ID");
        position += 1 + static_cast<unsigned char>(hello[position]);
        position += 2 + word(hello, position);
        require(position < hello.size(), "Missing compression methods");
        position += 1 + static_cast<unsigned char>(hello[position]);
        const auto end = position + 2 + word(hello, position);
        position += 2;
        require(end <= hello.size(), "ClientHello extensions exceeded capture");
        bool version = false, ephemeral = false, identity = false;
        while(position < end)
        {
            const auto type = word(hello, position), size = word(hello, position + 2);
            position += 4;
            require(position + size <= end, "Invalid TLS extension length");
            const auto content = hello.substr(position, size);
            if(type == 43) { require(content == std::string("\2\3\4", 3), "TLS version downgrade was offered"); version = true; }
            if(type == 45) { require(content == std::string("\1\1", 2), "Non-ephemeral PSK mode was offered"); ephemeral = true; }
            if(type == 41) identity = content.find("four-winds-room-v1") != std::string::npos;
            require(type != 42, "0-RTT early data was advertised");
            position += size;
        }
        require(version && ephemeral && identity, "TLS ClientHello omitted the required authenticated ephemeral policy");
    }

    void testConfidentialityAndReplay()
    {
        Relay relay;
        require(!relay.clientRecords.empty(), "No handshake bytes captured");
        verifyClientHelloPolicy(relay.clientRecords.front());
        const auto before = relay.clientRecords.size();
        const std::string payload = u8"{\"privateHand\":\"Северный ветер, руна 8, только хозяину\",\"unique\":\"FWR_CONFIDENTIAL_92381\"}";
        require(relay.client.send(payload, relay.error), relay.error);
        std::string received;
        eventually([&] { relay.poll(); return relay.server->receive(received); }, "Encrypted message was not delivered");
        require(received == payload, "Authenticated UTF-8 message was corrupted");
        std::string capture;
        for(const auto& frame : relay.clientRecords) capture += frame;
        for(const auto& frame : relay.serverRecords) capture += frame;
        require(capture.find(Secret) == std::string::npos && capture.find("FWR_CONFIDENTIAL_92381") == std::string::npos &&
                capture.find("privateHand") == std::string::npos && capture.find(u8"Северный") == std::string::npos,
                "Wire capture exposed the room secret or private gameplay plaintext");
        require(relay.clientRecords.size() == before + 1, "Small application message must form one captured TLS chunk");
        require(relay.upstream.send(relay.clientRecords.back(), relay.error), relay.error);
        eventually([&] { relay.upstream.poll(); relay.server->poll(); return relay.server->closed(); }, "Replayed ciphertext was accepted");
        require(!relay.server->receive(received), "Replayed application data escaped TLS sequence authentication");
    }

    void testTamperingAndWrongSecret()
    {
        {
            Relay relay;
            relay.tamper = true;
            require(relay.client.send("{\"move\":\"authenticated-message\"}", relay.error), relay.error);
            eventually([&] { relay.poll(); return relay.server->closed(); }, "Modified TLS ciphertext was accepted");
            std::string message;
            require(!relay.server->receive(message), "Tampered ciphertext produced application data");
        }
        {
            Pair pair(TcpLimits(), TcpLimits(), std::string(32, 'f'), Secret);
            eventually([&] { pair.poll(); return pair.client.closed() || (pair.server && pair.server->closed()); }, "Wrong room secret authenticated");
            std::string message;
            require(!pair.client.connected() && pair.server && !pair.server->connected() &&
                    !pair.server->receive(message), "Wrong secret obtained an authenticated channel");
        }
    }

    void testLargeDuplexAndBounds()
    {
        TcpLimits limits;
        limits.maximumFrameBytes = 65536;
        limits.maximumQueuedBytes = 2 * (limits.maximumFrameBytes + 4);
        limits.maximumQueuedFrames = 2;
        limits.bytesPerPoll = 97;
        Pair pair(limits, limits);
        pair.authenticate();
        const std::string first(65536, 'a'), second(65536, 'b');
        require(pair.client.send(first, pair.error) && pair.client.send(second, pair.error), pair.error);
        require(!pair.client.send("overflow", pair.error) && pair.client.connected(), "Queue backpressure was not bounded/recoverable");
        require(pair.server->send(second, pair.error), pair.error);
        std::vector<std::string> serverMessages, clientMessages;
        eventually([&]
        {
            pair.poll();
            require(!pair.client.closed() && !pair.server->closed(), "Partial TLS duplex I/O closed a healthy connection");
            std::string payload;
            while(pair.server->receive(payload)) serverMessages.push_back(payload);
            while(pair.client.receive(payload)) clientMessages.push_back(payload);
            return serverMessages.size() == 2 && clientMessages.size() == 1;
        }, "Partial encrypted duplex I/O failed", 10000);
        require(serverMessages == std::vector<std::string>{first, second} && clientMessages.front() == second,
                "TLS partial-write retry duplicated, reordered or damaged an application frame");
        require(pair.client.send("after drain", pair.error), "Secure queue did not recover after backpressure");
        require(!pair.client.send(std::string(65537, 'x'), pair.error) && !pair.client.send("", pair.error),
                "Outgoing application size limits were ignored");
    }

    void testAuthenticatedOversizeAndEof()
    {
        TcpLimits small;
        small.maximumFrameBytes = 64;
        Pair oversized(TcpLimits(), small);
        oversized.authenticate();
        require(oversized.client.send(std::string(512, 'x'), oversized.error), oversized.error);
        eventually([&] { oversized.poll(); return oversized.server->closed(); }, "Authenticated oversized frame accepted");
        std::string payload;
        require(!oversized.server->receive(payload) && oversized.server->error().find("length") != std::string::npos,
                "Oversized authenticated frame was exposed to the application");

        Pair eof;
        eof.authenticate();
        require(eof.client.send("final acknowledged data", eof.error), eof.error);
        eof.client.poll();
        eof.client.close();
        require(eof.server->send("outbound data loses its peer", eof.error), eof.error);
        eventually([&] { eof.server->poll(); return eof.server->closed(); }, "Secure EOF was not detected");
        require(eof.server->receive(payload) && payload == "final acknowledged data", "EOF discarded a complete authenticated frame");
        require(!eof.server->receive(payload), "EOF duplicated its final application frame");
    }

    void testDeadlinesAndNoFallback()
    {
        TcpLimits limits;
        limits.connectTimeout = std::chrono::milliseconds(30);
        TcpListener silent;
        std::string error, payload;
        require(silent.listen(0, error), error);
        SecureConnection client(Secret, limits);
        require(client.connect("localhost", silent.port(), error), error);
        std::unique_ptr<TcpConnection> socket;
        eventually([&] { client.poll(); socket = silent.accept(); return !!socket; }, "Silent peer connection");
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        client.poll();
        require(client.closed() && client.error().find("timed out") != std::string::npos,
                "Silent peer bypassed the TLS handshake deadline");

        SecureListener listener(Secret);
        require(listener.listen(0, error), error);
        TcpConnection plaintext;
        require(plaintext.connect("localhost", listener.port(), error), error);
        std::unique_ptr<SecureConnection> server;
        eventually([&]
        {
            plaintext.poll();
            if(!server) server = listener.accept();
            return plaintext.connected() && server;
        }, "Plaintext attack connection");
        require(plaintext.send("{\"kind\":\"hello\",\"room\":\"" + Secret + "\"}", error), error);
        eventually([&] { plaintext.poll(); server->poll(); return server->closed(); }, "Plaintext input negotiated a fallback");
        require(!server->receive(payload), "Plaintext bypassed authentication");

        limits = TcpLimits();
        limits.frameTimeout = std::chrono::milliseconds(80);
        for(const std::size_t chunks : {1, 2})
        {
            Relay relay(limits);
            // One 16 KiB TLS record occupies two outer TCP frames. One chunk
            // stalls inside TLS; two finish the record but leave the larger
            // inner application frame incomplete. Both need a fixed deadline.
            relay.remainingChunks = chunks;
            require(relay.client.send(std::string(50000, 'q'), error), error);
            eventually([&] { relay.poll(); return relay.remainingChunks == 0; }, "Partial authenticated frame fixture");
            for(int i = 0; i < 8; ++i) relay.poll();
            require(!relay.server->receive(payload), "Partial authenticated frame was delivered");
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            relay.server->poll();
            require(relay.server->closed() && relay.server->error().find("frame timed out") != std::string::npos,
                    "Stalled encrypted or inner authenticated frame bypassed its deadline");
        }
    }

    void testSecretValidation()
    {
        std::string error;
        for(const std::string& secret : std::vector<std::string>{"", "1234abcd", std::string(31, 'a'), std::string(32, 'g'), std::string(65, 'a')})
        {
            SecureListener listener(secret);
            SecureConnection client(secret);
            require(!listener.listen(0, error) && !client.connect("localhost", 19782, error),
                    "Weak or malformed room secret accepted");
        }
        Pair longSecret(TcpLimits(), TcpLimits(), std::string(64, 'b'), std::string(64, 'b'));
        longSecret.authenticate();
        SecureConnection moved(std::move(longSecret.client));
        require(longSecret.client.closed() && moved.connected(), "Secure move lost or duplicated connection ownership");
    }
}

int main()
{
    try
    {
        testSecretValidation();
        testConfidentialityAndReplay();
        testTamperingAndWrongSecret();
        testLargeDuplexAndBounds();
        testAuthenticatedOversizeAndEof();
        testDeadlinesAndNoFallback();
        std::cout << "TLS 1.3 private-room security and transport regressions: all checks passed\n";
        return 0;
    }
    catch(const std::exception& exception)
    {
        std::cerr << "Secure transport regression failed: " << exception.what() << '\n';
        return 1;
    }
}
