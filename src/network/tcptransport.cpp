#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <bcrypt.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "tcptransport.h"

#include <algorithm>
#include <array>
#include <deque>
#include <utility>

namespace Multiplayer
{
namespace
{
    using Clock = std::chrono::steady_clock;
#ifdef _WIN32
    using Socket = SOCKET;
    using SocketLength = int;
    constexpr Socket InvalidSocket = INVALID_SOCKET;
    int lastSocketError() { return WSAGetLastError(); }
    void closeSocket(Socket socket) { closesocket(socket); }
    bool wouldBlock(int code) { return code == WSAEWOULDBLOCK; }
    bool interrupted(int code) { return code == WSAEINTR; }
    bool connectPending(int code) { return wouldBlock(code) || code == WSAEINPROGRESS; }
    bool initializeSockets()
    {
        struct Runtime
        {
            bool ready;
            Runtime() { WSADATA data{}; ready = WSAStartup(MAKEWORD(2, 2), &data) == 0; }
            ~Runtime() { if(ready) WSACleanup(); }
        };
        static Runtime runtime;
        return runtime.ready;
    }
#else
    using Socket = int;
    using SocketLength = socklen_t;
    constexpr Socket InvalidSocket = -1;
    int lastSocketError() { return errno; }
    void closeSocket(Socket socket) { ::close(socket); }
    bool wouldBlock(int code) { return code == EAGAIN || code == EWOULDBLOCK; }
    bool interrupted(int code) { return code == EINTR; }
    bool connectPending(int code) { return code == EINPROGRESS || interrupted(code); }
    bool initializeSockets() { return true; }
#endif

    std::string socketError(const char * operation, int code = lastSocketError())
    {
        // Numeric diagnostics are stable across hosts; never include payloads,
        // session secrets, or platform error strings containing user input.
        return std::string(operation) + " failed (socket error " + std::to_string(code) + ")";
    }

    bool configureSocket(Socket socket)
    {
#ifdef _WIN32
        u_long nonblocking = 1;
        if(ioctlsocket(socket, FIONBIO, &nonblocking) != 0) return false;
#else
        const int flags = fcntl(socket, F_GETFL, 0);
        if(flags < 0 || fcntl(socket, F_SETFL, flags | O_NONBLOCK) < 0) return false;
        const int descriptorFlags = fcntl(socket, F_GETFD, 0);
        if(descriptorFlags < 0 || fcntl(socket, F_SETFD, descriptorFlags | FD_CLOEXEC) < 0)
            return false;
#endif
#ifdef SO_NOSIGPIPE
        const int noSignal = 1;
        if(setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE,
                      reinterpret_cast<const char *>(&noSignal), sizeof(noSignal)) != 0)
            return false;
#endif
        const int noDelay = 1;
        return setsockopt(socket, IPPROTO_TCP, TCP_NODELAY,
                          reinterpret_cast<const char *>(&noDelay), sizeof(noDelay)) == 0;
    }

    bool validLimits(const TcpLimits & limits)
    {
        return limits.maximumFrameBytes > 0 && limits.maximumFrameBytes <= 1024 * 1024 &&
            limits.maximumQueuedBytes >= limits.maximumFrameBytes + 4 &&
            limits.maximumQueuedBytes <= 16 * 1024 * 1024 &&
            limits.maximumQueuedFrames > 0 && limits.maximumQueuedFrames <= 4096 &&
            limits.bytesPerPoll > 0 && limits.bytesPerPoll <= 1024 * 1024 &&
            limits.connectTimeout.count() > 0 && limits.frameTimeout.count() > 0 &&
            limits.writeTimeout.count() > 0;
    }

    int socketSend(Socket socket, const char * bytes, std::size_t length)
    {
        int flags = 0;
#ifdef MSG_NOSIGNAL
        flags |= MSG_NOSIGNAL;
#endif
        return static_cast<int>(::send(socket, bytes, static_cast<int>(length), flags));
    }
}

struct TcpConnection::Impl
{
    TcpLimits limits;
    Socket socket = InvalidSocket;
    bool pending = false;
    std::string failure;
    std::array<unsigned char, 4> header{};
    std::size_t headerBytes = 0;
    std::size_t expectedBytes = 0;
    std::string partial;
    std::deque<std::string> incoming;
    std::size_t incomingBytes = 0;
    std::deque<std::string> outgoing;
    std::size_t outgoingBytes = 0;
    std::size_t outgoingOffset = 0;
    Clock::time_point connectStarted{};
    Clock::time_point frameStarted{};
    Clock::time_point writeStarted{};

    explicit Impl(const TcpLimits & configuration) : limits(configuration) {}
    ~Impl() { if(socket != InvalidSocket) closeSocket(socket); }

    void disconnect(const std::string & message)
    {
        if(socket != InvalidSocket) closeSocket(socket);
        socket = InvalidSocket;
        pending = false;
        failure = message;
        headerBytes = expectedBytes = 0;
        partial.clear();
        outgoing.clear();
        outgoingBytes = outgoingOffset = 0;
        // Already completed input survives EOF so a final acknowledgement can
        // be consumed even when FIN arrived in the same poll as its frame.
    }

    bool consume(const char * bytes, std::size_t length, Clock::time_point now)
    {
        while(length > 0)
        {
            if(headerBytes < header.size())
            {
                if(headerBytes == 0) frameStarted = now;
                const auto amount = std::min(length, header.size() - headerBytes);
                std::copy_n(bytes, amount, header.begin() + headerBytes);
                bytes += amount;
                length -= amount;
                headerBytes += amount;
                if(headerBytes < header.size()) continue;
                expectedBytes = (static_cast<std::size_t>(header[0]) << 24) |
                    (static_cast<std::size_t>(header[1]) << 16) |
                    (static_cast<std::size_t>(header[2]) << 8) | header[3];
                if(expectedBytes == 0 || expectedBytes > limits.maximumFrameBytes)
                {
                    disconnect("Incoming frame length is outside the allowed range");
                    return false;
                }
                if(incoming.size() >= limits.maximumQueuedFrames ||
                   incomingBytes + expectedBytes + 4 > limits.maximumQueuedBytes)
                {
                    disconnect("Incoming message queue limit exceeded");
                    return false;
                }
                partial.reserve(expectedBytes);
            }
            const auto amount = std::min(length, expectedBytes - partial.size());
            partial.append(bytes, amount);
            bytes += amount;
            length -= amount;
            if(partial.size() == expectedBytes)
            {
                incomingBytes += partial.size() + 4;
                incoming.emplace_back(std::move(partial));
                partial.clear();
                headerBytes = expectedBytes = 0;
            }
        }
        return true;
    }
};

TcpConnection::TcpConnection(const TcpLimits & limits) : impl(new Impl(limits)) {}
TcpConnection::TcpConnection(std::unique_ptr<Impl> state) : impl(std::move(state)) {}
TcpConnection::~TcpConnection() = default;
TcpConnection::TcpConnection(TcpConnection &&) noexcept = default;
TcpConnection & TcpConnection::operator=(TcpConnection &&) noexcept = default;

bool TcpConnection::connect(const std::string & host, std::uint16_t port, std::string & error)
{
    const TcpLimits limits = impl ? impl->limits : TcpLimits();
    impl.reset(new Impl(limits));
    error.clear();
    auto reject = [&](const std::string & message)
    {
        impl->disconnect(message);
        error = message;
        return false;
    };
    if(!validLimits(limits)) return reject("Invalid TCP transport limits");
    if(!initializeSockets()) return reject("Unable to initialize socket support");
    if(port == 0) return reject("Connection port must be between 1 and 65535");
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    const std::string numericHost = host == "localhost" ? "127.0.0.1" : host;
    if(numericHost.empty() || numericHost.size() > 15 ||
       numericHost.find_first_not_of("0123456789.") != std::string::npos ||
       inet_pton(AF_INET, numericHost.c_str(), &address.sin_addr) != 1)
        return reject("Enter an IPv4 address or localhost; hostname lookup is unavailable");
    impl->socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(impl->socket == InvalidSocket) return reject(socketError("socket"));
    if(!configureSocket(impl->socket)) return reject(socketError("socket configuration"));
    const int result = ::connect(impl->socket, reinterpret_cast<sockaddr *>(&address), sizeof(address));
    if(result != 0)
    {
        const int code = lastSocketError();
        if(!connectPending(code)) return reject(socketError("connect", code));
        impl->pending = true;
        impl->connectStarted = Clock::now();
    }
    return true;
}

void TcpConnection::poll()
{
    if(closed()) return;
    auto & state = *impl;
    const auto now = Clock::now();
    if(state.pending)
    {
        if(now - state.connectStarted >= state.limits.connectTimeout)
        {
            state.disconnect("Connection attempt timed out");
            return;
        }
#ifndef _WIN32
        // select only represents descriptors below FD_SETSIZE on POSIX.
        if(state.socket >= FD_SETSIZE)
        {
            state.disconnect("Socket descriptor exceeds the select limit");
            return;
        }
#endif
        fd_set writable, exceptional;
        FD_ZERO(&writable);
        FD_ZERO(&exceptional);
        FD_SET(state.socket, &writable);
        FD_SET(state.socket, &exceptional);
        timeval timeout{};
#ifdef _WIN32
        const int count = select(0, nullptr, &writable, &exceptional, &timeout);
#else
        const int count = select(state.socket + 1, nullptr, &writable, &exceptional, &timeout);
#endif
        if(count < 0)
        {
            const int code = lastSocketError();
            if(!interrupted(code)) state.disconnect(socketError("connect readiness", code));
            return;
        }
        if(count == 0) return;
        int connectError = 0;
        SocketLength errorSize = sizeof(connectError);
        if(getsockopt(state.socket, SOL_SOCKET, SO_ERROR,
                      reinterpret_cast<char *>(&connectError), &errorSize) != 0)
        {
            state.disconnect(socketError("connect status"));
            return;
        }
        if(connectError != 0)
        {
            state.disconnect(socketError("connect", connectError));
            return;
        }
        state.pending = false;
    }
    if(state.headerBytes != 0 && now - state.frameStarted >= state.limits.frameTimeout)
    {
        state.disconnect("Incoming frame timed out");
        return;
    }
    if(!state.outgoing.empty() && now - state.writeStarted >= state.limits.writeTimeout)
    {
        state.disconnect("Outgoing frame timed out");
        return;
    }

    std::size_t writeBudget = state.limits.bytesPerPoll;
    for(int operations = 0; !state.outgoing.empty() && writeBudget > 0 && operations < 64; ++operations)
    {
        const std::string & front = state.outgoing.front();
        const auto amount = std::min(writeBudget, front.size() - state.outgoingOffset);
        const int sent = socketSend(state.socket, front.data() + state.outgoingOffset, amount);
        if(sent < 0)
        {
            const int code = lastSocketError();
            if(wouldBlock(code)) break;
            if(interrupted(code)) continue;
            state.disconnect(socketError("send", code));
            return;
        }
        if(sent == 0)
        {
            state.disconnect("Peer disconnected while sending");
            return;
        }
        state.outgoingOffset += static_cast<std::size_t>(sent);
        writeBudget -= static_cast<std::size_t>(sent);
        if(state.outgoingOffset == front.size())
        {
            state.outgoingBytes -= front.size();
            state.outgoing.pop_front();
            state.outgoingOffset = 0;
            state.writeStarted = now;
        }
    }

    std::array<char, 16384> buffer{};
    std::size_t readBudget = state.limits.bytesPerPoll;
    for(int operations = 0; readBudget > 0 && operations < 64; ++operations)
    {
        auto amount = std::min(readBudget, buffer.size());
        if(state.limits.receiveBackpressure)
        {
            // Reserve capacity for a complete maximum-sized frame before
            // accepting its header. A locally full queue then pauses between
            // frames, without starting or extending a peer's frame deadline.
            if(state.headerBytes == 0 &&
               (state.incoming.size() >= state.limits.maximumQueuedFrames ||
                state.incomingBytes + state.limits.maximumFrameBytes + 4 > state.limits.maximumQueuedBytes))
                break;
            // A recv() must not run past the reserved frame into the next
            // header, whose capacity has not yet been checked.
            amount = std::min(amount, state.headerBytes < state.header.size() ?
                state.header.size() - state.headerBytes : state.expectedBytes - state.partial.size());
        }
        const int received = static_cast<int>(::recv(state.socket, buffer.data(), static_cast<int>(amount), 0));
        if(received < 0)
        {
            const int code = lastSocketError();
            if(wouldBlock(code)) break;
            if(interrupted(code)) continue;
            state.disconnect(socketError("receive", code));
            return;
        }
        if(received == 0)
        {
            state.disconnect(state.headerBytes ? "Peer disconnected during a frame" : "Peer disconnected");
            return;
        }
        readBudget -= static_cast<std::size_t>(received);
        if(!state.consume(buffer.data(), static_cast<std::size_t>(received), now)) return;
    }
}

bool TcpConnection::send(const std::string & payload, std::string & error)
{
    error.clear();
    if(!connected()) error = "Connection is not established";
    else if(payload.empty() || payload.size() > impl->limits.maximumFrameBytes)
        error = "Outgoing frame length is outside the allowed range";
    else if(impl->outgoing.size() >= impl->limits.maximumQueuedFrames ||
            impl->outgoingBytes + payload.size() + 4 > impl->limits.maximumQueuedBytes)
        error = "Outgoing message queue limit exceeded";
    if(!error.empty()) return false;
    const auto length = static_cast<std::uint32_t>(payload.size());
    std::string frame(4, '\0');
    frame[0] = static_cast<char>(length >> 24);
    frame[1] = static_cast<char>(length >> 16);
    frame[2] = static_cast<char>(length >> 8);
    frame[3] = static_cast<char>(length);
    frame += payload;
    if(impl->outgoing.empty()) impl->writeStarted = Clock::now();
    impl->outgoingBytes += frame.size();
    impl->outgoing.emplace_back(std::move(frame));
    return true;
}

bool TcpConnection::receive(std::string & payload)
{
    if(!impl || impl->incoming.empty()) return false;
    impl->incomingBytes -= impl->incoming.front().size() + 4;
    payload = std::move(impl->incoming.front());
    impl->incoming.pop_front();
    return true;
}

std::size_t TcpConnection::partialFrameBytes() const
{ return impl ? impl->headerBytes + impl->partial.size() : 0; }
bool TcpConnection::connected() const { return impl && impl->socket != InvalidSocket && !impl->pending; }
bool TcpConnection::connecting() const { return impl && impl->pending; }
bool TcpConnection::closed() const { return !impl || impl->socket == InvalidSocket; }
const std::string & TcpConnection::error() const
{
    static const std::string empty;
    return impl ? impl->failure : empty;
}
void TcpConnection::close()
{
    if(!impl) return;
    impl->disconnect(std::string());
    impl->incoming.clear();
    impl->incomingBytes = 0;
}

struct TcpListener::Impl
{
    TcpLimits limits;
    Socket socket = InvalidSocket;
    std::uint16_t boundPort = 0;
    explicit Impl(const TcpLimits & configuration) : limits(configuration) {}
    ~Impl() { if(socket != InvalidSocket) closeSocket(socket); }
};

TcpListener::TcpListener(const TcpLimits & limits) : impl(new Impl(limits)) {}
TcpListener::~TcpListener() = default;
TcpListener::TcpListener(TcpListener &&) noexcept = default;
TcpListener & TcpListener::operator=(TcpListener &&) noexcept = default;

bool TcpListener::listen(std::uint16_t port, std::string & error)
{
    if(!impl) impl.reset(new Impl(TcpLimits()));
    close();
    error.clear();
    auto reject = [&](const std::string & message)
    {
        close();
        error = message;
        return false;
    };
    if(!validLimits(impl->limits)) return reject("Invalid TCP transport limits");
    if(!initializeSockets()) return reject("Unable to initialize socket support");
    impl->socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(impl->socket == InvalidSocket) return reject(socketError("socket"));
    if(!configureSocket(impl->socket)) return reject(socketError("socket configuration"));
    const int enabled = 1;
#ifdef _WIN32
    constexpr int reuseOption = SO_EXCLUSIVEADDRUSE;
#else
    constexpr int reuseOption = SO_REUSEADDR;
#endif
    if(setsockopt(impl->socket, SOL_SOCKET, reuseOption,
                  reinterpret_cast<const char *>(&enabled), sizeof(enabled)) != 0)
        return reject(socketError("listener configuration"));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if(::bind(impl->socket, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0)
        return reject(socketError("bind"));
    if(::listen(impl->socket, 4) != 0) return reject(socketError("listen"));
    SocketLength addressLength = sizeof(address);
    if(getsockname(impl->socket, reinterpret_cast<sockaddr *>(&address), &addressLength) != 0)
        return reject(socketError("bound address"));
    impl->boundPort = ntohs(address.sin_port);
    return true;
}

std::uint16_t TcpListener::port() const { return impl ? impl->boundPort : 0; }

std::unique_ptr<TcpConnection> TcpListener::accept()
{
    if(!impl || impl->socket == InvalidSocket) return nullptr;
    const Socket accepted = ::accept(impl->socket, nullptr, nullptr);
    if(accepted == InvalidSocket) return nullptr;
    if(!configureSocket(accepted))
    {
        closeSocket(accepted);
        return nullptr;
    }
    auto state = std::make_unique<TcpConnection::Impl>(impl->limits);
    state->socket = accepted;
    return std::unique_ptr<TcpConnection>(new TcpConnection(std::move(state)));
}

void TcpListener::close()
{
    if(!impl) return;
    if(impl->socket != InvalidSocket) closeSocket(impl->socket);
    impl->socket = InvalidSocket;
    impl->boundPort = 0;
}

bool secureRandomHex(std::size_t bytes, std::string & result, std::string & error)
{
    result.clear();
    error.clear();
    std::array<unsigned char, 256> random{};
    if(bytes == 0 || bytes > random.size())
    {
        error = "Random token size is outside the allowed range";
        return false;
    }
#ifdef _WIN32
    if(BCryptGenRandom(nullptr, random.data(), static_cast<ULONG>(bytes),
                       BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
    {
        error = "Operating system random source failed";
        return false;
    }
#else
    // /dev/urandom is available on every supported Unix target, including
    // Android API 23 where the libc getrandom entry point is not yet present.
    const int descriptor = ::open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if(descriptor < 0)
    {
        error = "Unable to open operating system random source";
        return false;
    }
    std::size_t offset = 0;
    unsigned interruptions = 0;
    while(offset < bytes)
    {
        const auto count = ::read(descriptor, random.data() + offset, bytes - offset);
        if(count < 0 && errno == EINTR && ++interruptions < 128) continue;
        if(count <= 0)
        {
            ::close(descriptor);
            error = "Operating system random source failed";
            return false;
        }
        offset += static_cast<std::size_t>(count);
    }
    ::close(descriptor);
#endif
    constexpr char Hex[] = "0123456789abcdef";
    result.reserve(bytes * 2);
    for(std::size_t index = 0; index < bytes; ++index)
    {
        result += Hex[random[index] >> 4];
        result += Hex[random[index] & 15];
    }
    return true;
}
}
