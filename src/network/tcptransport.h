#ifndef FOUR_WINDS_TCP_TRANSPORT_H
#define FOUR_WINDS_TCP_TRANSPORT_H

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace Multiplayer
{
    // Uses the OS cryptographic random source, independent of gameplay RNG.
    // Generates 1..256 random bytes encoded as lowercase hexadecimal.
    bool secureRandomHex(std::size_t bytes, std::string & result, std::string & error);

    // Limits are per connection. Shorter deadlines also make fault tests fast
    // without exposing clock injection to the session layer.
    struct TcpLimits
    {
        std::size_t maximumFrameBytes = 1024 * 1024;
        std::size_t maximumQueuedBytes = 2 * 1024 * 1024;
        std::size_t maximumQueuedFrames = 128;
        std::size_t bytesPerPoll = 256 * 1024;
        std::chrono::milliseconds connectTimeout{10000};
        std::chrono::milliseconds frameTimeout{15000};
        std::chrono::milliseconds writeTimeout{15000};
        // Internal framed streams such as TLS ciphertext may pause socket
        // reads until receive() drains their bounded completed-frame queue.
        // Ordinary application transports keep rejecting queue overflow.
        bool receiveBackpressure = false;
    };

    // Main-thread, nonblocking, length-prefixed payload transport. It accepts
    // UTF-8 JSON as opaque bytes; parseWireObject validates received messages.
    // No GameData/SDL calls, DNS lookups, threads or gameplay state live here.
    class TcpConnection
    {
        struct Impl;
        std::unique_ptr<Impl> impl;
        explicit TcpConnection(std::unique_ptr<Impl>);
        friend class TcpListener;

    public:
        explicit TcpConnection(const TcpLimits & limits = TcpLimits());
        ~TcpConnection();
        TcpConnection(TcpConnection &&) noexcept;
        TcpConnection & operator=(TcpConnection &&) noexcept;
        TcpConnection(const TcpConnection &) = delete;
        TcpConnection & operator=(const TcpConnection &) = delete;

        // True means the connection attempt started (possibly still pending).
        // Supports IPv4 literals and localhost, with ports 1..65535.
        bool connect(const std::string & host, std::uint16_t port, std::string & error);
        void poll();
        bool send(const std::string & payload, std::string & error);
        bool receive(std::string & payload);
        // Read-only diagnostic: received header/payload bytes of the current
        // incomplete frame, excluding already completed queued messages.
        std::size_t partialFrameBytes() const;
        bool connected() const;
        bool connecting() const;
        bool closed() const;
        const std::string & error() const;
        void close();
    };

    class TcpListener
    {
        struct Impl;
        std::unique_ptr<Impl> impl;

    public:
        explicit TcpListener(const TcpLimits & limits = TcpLimits());
        ~TcpListener();
        TcpListener(TcpListener &&) noexcept;
        TcpListener & operator=(TcpListener &&) noexcept;
        TcpListener(const TcpListener &) = delete;
        TcpListener & operator=(const TcpListener &) = delete;

        // Binds all IPv4 interfaces. Port zero requests an ephemeral port.
        bool listen(std::uint16_t port, std::string & error);
        std::uint16_t port() const;
        // nullptr means no connection is ready; it never blocks.
        std::unique_ptr<TcpConnection> accept();
        void close();
    };
}

#endif
