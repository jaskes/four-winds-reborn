#include "securetransport.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <deque>
#include <utility>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/ssl.h>
#include <psa/crypto.h>

#if !defined(MBEDTLS_SSL_PROTO_TLS1_3) || !defined(MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_PSK_EPHEMERAL_ENABLED)
#error Four Winds multiplayer requires TLS 1.3 PSK ephemeral key exchange
#endif

namespace Multiplayer
{
namespace
{
    using Clock = std::chrono::steady_clock;
    constexpr std::size_t RecordChunkBytes = 16384;

    bool validLimits(const TcpLimits& limits)
    {
        return limits.maximumFrameBytes > 0 && limits.maximumFrameBytes <= 1024 * 1024 &&
            limits.maximumQueuedBytes >= limits.maximumFrameBytes + 4 &&
            limits.maximumQueuedBytes <= 16 * 1024 * 1024 &&
            limits.maximumQueuedFrames > 0 && limits.maximumQueuedFrames <= 4096 &&
            limits.bytesPerPoll > 0 && limits.bytesPerPoll <= 1024 * 1024 &&
            limits.connectTimeout.count() > 0 && limits.frameTimeout.count() > 0 &&
            limits.writeTimeout.count() > 0;
    }

    TcpLimits encryptedLimits(const TcpLimits& limits)
    {
        TcpLimits result = limits;
        // Cipher records and handshake messages need room independent from
        // the application's configured message size. Both layers stay bounded.
        result.maximumFrameBytes = 64 * 1024;
        result.maximumQueuedBytes = std::max<std::size_t>(limits.maximumQueuedBytes, 256 * 1024);
        result.maximumQueuedFrames = std::max<std::size_t>(limits.maximumQueuedFrames, 64);
        return result;
    }

    int hex(char ch)
    {
        if(ch >= '0' && ch <= '9') return ch - '0';
        if(ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
        if(ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
        return -1;
    }

    bool decodeSecret(const std::string& text, std::array<unsigned char, 32>& bytes)
    {
        if(text.size() != 32 && text.size() != 64) return false;
        for(std::size_t index = 0; index < text.size(); index += 2)
        {
            const int high = hex(text[index]), low = hex(text[index + 1]);
            if(high < 0 || low < 0) return false;
            bytes[index / 2] = static_cast<unsigned char>((high << 4) | low);
        }
        return true;
    }

    bool initializePsa()
    {
        // TLS 1.3 uses PSA even when older TLS primitives use the classic API.
        // Initialization is process-wide; never free another connection's PSA.
        static const psa_status_t status = psa_crypto_init();
        return status == PSA_SUCCESS;
    }

    bool retryTls(int code)
    {
        return code == MBEDTLS_ERR_SSL_WANT_READ || code == MBEDTLS_ERR_SSL_WANT_WRITE ||
            code == MBEDTLS_ERR_SSL_CRYPTO_IN_PROGRESS;
    }

    std::string tlsError(const char* operation, int code)
    {
        return std::string(operation) + " failed (TLS error " + std::to_string(code) + ")";
    }
}

struct SecureConnection::Impl
{
    std::string secret;
    TcpLimits limits;
    std::unique_ptr<TcpConnection> raw;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config configuration;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context random;
    bool established = false;
    bool terminal = true;
    std::string failure;
    Clock::time_point handshakeStarted{};
    Clock::time_point frameStarted{};
    bool incomingStarted = false;
    Clock::time_point writeStarted{};
    std::string encryptedInput;
    std::size_t encryptedOffset = 0;
    std::array<unsigned char, 4> header{};
    std::size_t headerBytes = 0;
    std::size_t expectedBytes = 0;
    std::string partial;
    std::deque<std::string> incoming;
    std::size_t incomingBytes = 0;
    std::deque<std::string> outgoing;
    std::size_t outgoingBytes = 0;
    std::size_t outgoingOffset = 0;
    std::size_t writeAttemptBytes = 0;

    Impl(const std::string& roomSecret, const TcpLimits& settings,
         std::unique_ptr<TcpConnection> socket = nullptr)
        : secret(roomSecret), limits(settings), raw(std::move(socket))
    {
        if(!raw) raw = std::make_unique<TcpConnection>(encryptedLimits(limits));
        mbedtls_ssl_init(&ssl);
        mbedtls_ssl_config_init(&configuration);
        mbedtls_entropy_init(&entropy);
        mbedtls_ctr_drbg_init(&random);
    }

    ~Impl()
    {
        mbedtls_ssl_free(&ssl);
        mbedtls_ssl_config_free(&configuration);
        mbedtls_ctr_drbg_free(&random);
        mbedtls_entropy_free(&entropy);
        if(!secret.empty()) mbedtls_platform_zeroize(secret.data(), secret.size());
    }

    void disconnect(const std::string& message)
    {
        terminal = true;
        established = false;
        failure = message;
        raw->close();
        encryptedInput.clear();
        encryptedOffset = 0;
        partial.clear();
        headerBytes = expectedBytes = 0;
        incomingStarted = false;
        outgoing.clear();
        outgoingBytes = outgoingOffset = writeAttemptBytes = 0;
        // Complete authenticated application frames survive an EOF/error.
        // Incomplete plaintext never escapes to the session layer.
    }

    static int writeBio(void* context, const unsigned char* bytes, std::size_t length)
    {
        auto& self = *static_cast<Impl*>(context);
        if(self.raw->closed()) return MBEDTLS_ERR_SSL_CONN_EOF;
        const auto amount = std::min(length, RecordChunkBytes);
        std::string error;
        if(!self.raw->send(std::string(reinterpret_cast<const char*>(bytes), amount), error))
            return MBEDTLS_ERR_SSL_WANT_WRITE;
        return static_cast<int>(amount);
    }

    static int readBio(void* context, unsigned char* bytes, std::size_t length)
    {
        auto& self = *static_cast<Impl*>(context);
        if(self.encryptedOffset == self.encryptedInput.size())
        {
            self.encryptedInput.clear();
            self.encryptedOffset = 0;
            if(!self.raw->receive(self.encryptedInput))
                return self.raw->closed() ? 0 : MBEDTLS_ERR_SSL_WANT_READ;
        }
        const auto amount = std::min(length, self.encryptedInput.size() - self.encryptedOffset);
        if(amount && self.established && !self.incomingStarted)
        {
            // The deadline includes a fragmented TLS record, before it can
            // decrypt into even the first byte of an application header.
            self.incomingStarted = true;
            self.frameStarted = Clock::now();
        }
        std::memcpy(bytes, self.encryptedInput.data() + self.encryptedOffset, amount);
        self.encryptedOffset += amount;
        return static_cast<int>(amount);
    }

    bool configure(bool server, std::string& error)
    {
        error.clear();
        std::array<unsigned char, 32> key{};
        const auto reject = [&](const std::string& reason)
        {
            mbedtls_platform_zeroize(key.data(), key.size());
            disconnect(reason);
            error = reason;
            return false;
        };
        if(!decodeSecret(secret, key)) return reject("Room secret must contain 32 or 64 hexadecimal characters");
        if(!validLimits(limits)) return reject("Invalid secure transport limits");
        if(!initializePsa()) return reject("Unable to initialize the TLS cryptographic provider");
        constexpr unsigned char Purpose[] = "Four Winds private room TLS 1.3";
        int code = mbedtls_ctr_drbg_seed(&random, mbedtls_entropy_func, &entropy, Purpose, sizeof(Purpose) - 1);
        if(code != 0) return reject(tlsError("TLS entropy initialization", code));
        code = mbedtls_ssl_config_defaults(&configuration,
            server ? MBEDTLS_SSL_IS_SERVER : MBEDTLS_SSL_IS_CLIENT,
            MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
        if(code != 0) return reject(tlsError("TLS configuration", code));
        mbedtls_ssl_conf_rng(&configuration, mbedtls_ctr_drbg_random, &random);
        mbedtls_ssl_conf_min_tls_version(&configuration, MBEDTLS_SSL_VERSION_TLS1_3);
        mbedtls_ssl_conf_max_tls_version(&configuration, MBEDTLS_SSL_VERSION_TLS1_3);
        mbedtls_ssl_conf_tls13_key_exchange_modes(&configuration,
            MBEDTLS_SSL_TLS1_3_KEY_EXCHANGE_MODE_PSK_EPHEMERAL);
        // External PSKs use SHA-256 in this API. One explicit AEAD suite and
        // ephemeral X25519/P-256 groups eliminate downgrade ambiguity.
        static const int Suites[] = {MBEDTLS_TLS1_3_AES_128_GCM_SHA256, 0};
        static const std::uint16_t Groups[] = {
            MBEDTLS_SSL_IANA_TLS_GROUP_X25519, MBEDTLS_SSL_IANA_TLS_GROUP_SECP256R1, 0};
        mbedtls_ssl_conf_ciphersuites(&configuration, Suites);
        mbedtls_ssl_conf_groups(&configuration, Groups);
#if defined(MBEDTLS_SSL_EARLY_DATA)
        mbedtls_ssl_conf_early_data(&configuration, MBEDTLS_SSL_EARLY_DATA_DISABLED);
#endif
#if defined(MBEDTLS_SSL_SESSION_TICKETS)
        if(server) mbedtls_ssl_conf_new_session_tickets(&configuration, 0);
        else
        {
            mbedtls_ssl_conf_session_tickets(&configuration, MBEDTLS_SSL_SESSION_TICKETS_DISABLED);
            mbedtls_ssl_conf_tls13_enable_signal_new_session_tickets(&configuration,
                MBEDTLS_SSL_TLS1_3_SIGNAL_NEW_SESSION_TICKETS_DISABLED);
        }
#endif
#if defined(MBEDTLS_SSL_RENEGOTIATION)
        mbedtls_ssl_conf_renegotiation(&configuration, MBEDTLS_SSL_RENEGOTIATION_DISABLED);
#endif
        // The public identity names this protocol, never the room secret.
        constexpr unsigned char Identity[] = "four-winds-room-v1";
        code = mbedtls_ssl_conf_psk(&configuration, key.data(), secret.size() / 2,
                                   Identity, sizeof(Identity) - 1);
        mbedtls_platform_zeroize(key.data(), key.size());
        if(code != 0) return reject(tlsError("TLS room authentication configuration", code));
        code = mbedtls_ssl_setup(&ssl, &configuration);
        if(code != 0) return reject(tlsError("TLS setup", code));
        mbedtls_ssl_set_bio(&ssl, this, writeBio, readBio, nullptr);
        terminal = false;
        handshakeStarted = Clock::now();
        return true;
    }

    bool consume(const unsigned char* bytes, std::size_t length, Clock::time_point now)
    {
        while(length > 0)
        {
            if(headerBytes < header.size())
            {
                if(!incomingStarted) { incomingStarted = true; frameStarted = now; }
                const auto amount = std::min(length, header.size() - headerBytes);
                std::copy_n(bytes, amount, header.begin() + headerBytes);
                bytes += amount; length -= amount; headerBytes += amount;
                if(headerBytes < header.size()) continue;
                expectedBytes = (static_cast<std::size_t>(header[0]) << 24) |
                    (static_cast<std::size_t>(header[1]) << 16) |
                    (static_cast<std::size_t>(header[2]) << 8) | header[3];
                if(expectedBytes == 0 || expectedBytes > limits.maximumFrameBytes)
                { disconnect("Authenticated frame length is outside the allowed range"); return false; }
                if(incoming.size() >= limits.maximumQueuedFrames ||
                   incomingBytes + expectedBytes + 4 > limits.maximumQueuedBytes)
                { disconnect("Authenticated message queue limit exceeded"); return false; }
                partial.reserve(expectedBytes);
            }
            const auto amount = std::min(length, expectedBytes - partial.size());
            partial.append(reinterpret_cast<const char*>(bytes), amount);
            bytes += amount; length -= amount;
            if(partial.size() == expectedBytes)
            {
                incomingBytes += partial.size() + 4;
                incoming.emplace_back(std::move(partial));
                partial.clear();
                headerBytes = expectedBytes = 0;
                incomingStarted = false;
            }
        }
        return true;
    }
};

SecureConnection::SecureConnection(const std::string& secret, const TcpLimits& limits)
    : impl(std::make_unique<Impl>(secret, limits)) {}
SecureConnection::SecureConnection(std::unique_ptr<Impl> state) : impl(std::move(state)) {}
SecureConnection::~SecureConnection() = default;
SecureConnection::SecureConnection(SecureConnection&&) noexcept = default;
SecureConnection& SecureConnection::operator=(SecureConnection&&) noexcept = default;

bool SecureConnection::connect(const std::string& host, std::uint16_t port, std::string& error)
{
    if(!impl) { error = "Secure connection has been moved"; return false; }
    auto replacement = std::make_unique<Impl>(impl->secret, impl->limits);
    impl = std::move(replacement);
    if(!impl->configure(false, error)) return false;
    if(!impl->raw->connect(host, port, error))
    {
        impl->disconnect(error);
        return false;
    }
    return true;
}

void SecureConnection::poll()
{
    if(closed()) return;
    auto& state = *impl;
    state.raw->poll();
    const auto now = Clock::now();
    if(!state.established)
    {
        if(now - state.handshakeStarted >= state.limits.connectTimeout)
        { state.disconnect("TLS room authentication timed out"); return; }
        if(state.raw->connecting()) return;
        if(state.raw->closed()) { state.disconnect("Connection closed during TLS room authentication"); return; }
        for(int step = 0; step < 16 && !mbedtls_ssl_is_handshake_over(&state.ssl); ++step)
        {
            const int code = mbedtls_ssl_handshake_step(&state.ssl);
            if(retryTls(code)) break;
            if(code != 0) { state.disconnect(tlsError("TLS room authentication", code)); return; }
        }
        state.raw->poll();
        if(!mbedtls_ssl_is_handshake_over(&state.ssl)) return;
        if(mbedtls_ssl_get_version_number(&state.ssl) != MBEDTLS_SSL_VERSION_TLS1_3 ||
           mbedtls_ssl_get_ciphersuite_id_from_ssl(&state.ssl) != MBEDTLS_TLS1_3_AES_128_GCM_SHA256)
        { state.disconnect("TLS connection did not negotiate the required protocol"); return; }
        state.established = true;
    }
    if(state.incomingStarted && now - state.frameStarted >= state.limits.frameTimeout)
    { state.disconnect("Authenticated frame timed out"); return; }
    if(!state.outgoing.empty() && now - state.writeStarted >= state.limits.writeTimeout)
    { state.disconnect("Authenticated outgoing frame timed out"); return; }

    std::size_t budget = state.limits.bytesPerPoll;
    // A final TCP read may contain authenticated application data followed by
    // EOF. Drain that data before reporting closure even when a local write is
    // queued; trying that write first would discard the unread final message.
    for(int operations = 0; !state.raw->closed() && !state.outgoing.empty() && budget > 0 && operations < 64; ++operations)
    {
        const auto& front = state.outgoing.front();
        if(state.writeAttemptBytes == 0)
            state.writeAttemptBytes = std::min({budget, RecordChunkBytes, front.size() - state.outgoingOffset});
        const int written = mbedtls_ssl_write(&state.ssl,
            reinterpret_cast<const unsigned char*>(front.data() + state.outgoingOffset), state.writeAttemptBytes);
        // WANT_WRITE/READ retries must preserve the identical pointer and
        // length, even when a later poll has a different remaining budget.
        if(retryTls(written)) break;
        if(written <= 0) { state.disconnect(tlsError("Encrypted send", written)); return; }
        state.writeAttemptBytes = 0;
        state.outgoingOffset += static_cast<std::size_t>(written);
        budget -= std::min(budget, static_cast<std::size_t>(written));
        if(state.outgoingOffset == front.size())
        {
            state.outgoingBytes -= front.size();
            state.outgoing.pop_front();
            state.outgoingOffset = 0;
            state.writeStarted = now;
        }
    }
    std::array<unsigned char, RecordChunkBytes> buffer{};
    budget = state.limits.bytesPerPoll;
    for(int operations = 0; budget > 0 && operations < 64; ++operations)
    {
        const int received = mbedtls_ssl_read(&state.ssl, buffer.data(), std::min(budget, buffer.size()));
        if(retryTls(received)) break;
        if(received <= 0)
        {
            state.disconnect(received == 0 || received == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY ||
                             received == MBEDTLS_ERR_SSL_CONN_EOF ?
                "Encrypted connection closed" : tlsError("Encrypted receive", received));
            return;
        }
        budget -= static_cast<std::size_t>(received);
        if(!state.consume(buffer.data(), static_cast<std::size_t>(received), now)) return;
    }
    state.raw->poll();
}

bool SecureConnection::send(const std::string& payload, std::string& error)
{
    error.clear();
    if(!connected()) error = "Room authentication is not complete";
    else if(payload.empty() || payload.size() > impl->limits.maximumFrameBytes)
        error = "Authenticated outgoing frame length is outside the allowed range";
    else if(impl->outgoing.size() >= impl->limits.maximumQueuedFrames ||
            impl->outgoingBytes + payload.size() + 4 > impl->limits.maximumQueuedBytes)
        error = "Authenticated outgoing message queue limit exceeded";
    if(!error.empty()) return false;
    const auto size = static_cast<std::uint32_t>(payload.size());
    std::string frame(4, '\0');
    for(std::size_t index = 0; index < 4; ++index) frame[index] = static_cast<char>(size >> (24 - index * 8));
    frame += payload;
    if(impl->outgoing.empty()) impl->writeStarted = Clock::now();
    impl->outgoingBytes += frame.size();
    impl->outgoing.emplace_back(std::move(frame));
    return true;
}

bool SecureConnection::receive(std::string& payload)
{
    if(!impl || impl->incoming.empty()) return false;
    impl->incomingBytes -= impl->incoming.front().size() + 4;
    payload = std::move(impl->incoming.front());
    impl->incoming.pop_front();
    return true;
}
bool SecureConnection::connected() const { return impl && impl->established && !impl->terminal; }
bool SecureConnection::connecting() const { return impl && !impl->terminal && !impl->established; }
bool SecureConnection::closed() const { return !impl || impl->terminal; }
const std::string& SecureConnection::error() const
{
    static const std::string empty;
    return impl ? impl->failure : empty;
}
void SecureConnection::close()
{
    if(!impl) return;
    impl->disconnect(std::string());
    impl->incoming.clear();
    impl->incomingBytes = 0;
}

struct SecureListener::Impl
{
    std::string secret;
    TcpLimits limits;
    TcpListener listener;
    Impl(const std::string& roomSecret, const TcpLimits& settings)
        : secret(roomSecret), limits(settings), listener(encryptedLimits(settings)) {}
    ~Impl() { if(!secret.empty()) mbedtls_platform_zeroize(secret.data(), secret.size()); }
};
SecureListener::SecureListener(const std::string& secret, const TcpLimits& limits)
    : impl(std::make_unique<Impl>(secret, limits)) {}
SecureListener::~SecureListener() = default;
SecureListener::SecureListener(SecureListener&&) noexcept = default;
SecureListener& SecureListener::operator=(SecureListener&&) noexcept = default;
bool SecureListener::listen(std::uint16_t port, std::string& error)
{
    error.clear();
    if(!impl) { error = "Secure listener has been moved"; return false; }
    std::array<unsigned char, 32> key{};
    const bool valid = decodeSecret(impl->secret, key);
    mbedtls_platform_zeroize(key.data(), key.size());
    if(!valid) { error = "Room secret must contain 32 or 64 hexadecimal characters"; return false; }
    if(!validLimits(impl->limits)) { error = "Invalid secure transport limits"; return false; }
    if(!initializePsa()) { error = "Unable to initialize the TLS cryptographic provider"; return false; }
    return impl->listener.listen(port, error);
}
std::uint16_t SecureListener::port() const { return impl ? impl->listener.port() : 0; }
std::unique_ptr<SecureConnection> SecureListener::accept()
{
    if(!impl) return nullptr;
    auto raw = impl->listener.accept();
    if(!raw) return nullptr;
    auto state = std::make_unique<SecureConnection::Impl>(impl->secret, impl->limits, std::move(raw));
    std::string error;
    state->configure(true, error);
    return std::unique_ptr<SecureConnection>(new SecureConnection(std::move(state)));
}
void SecureListener::close() { if(impl) impl->listener.close(); }
}
