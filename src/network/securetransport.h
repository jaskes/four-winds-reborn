#ifndef FOUR_WINDS_SECURE_TRANSPORT_H
#define FOUR_WINDS_SECURE_TRANSPORT_H

#include "tcptransport.h"

namespace Multiplayer
{
    // TLS 1.3 external-PSK plus ephemeral key exchange. The room secret is
    // exactly 32 or 64 hexadecimal characters; passwords and legacy short
    // admission codes are not accepted. No plaintext fallback exists.
    class SecureConnection
    {
        struct Impl;
        std::unique_ptr<Impl> impl;
        explicit SecureConnection(std::unique_ptr<Impl>);
        friend class SecureListener;

    public:
        explicit SecureConnection(const std::string& roomSecretHex,
                                  const TcpLimits& limits = TcpLimits());
        ~SecureConnection();
        SecureConnection(SecureConnection&&) noexcept;
        SecureConnection& operator=(SecureConnection&&) noexcept;
        SecureConnection(const SecureConnection&) = delete;
        SecureConnection& operator=(const SecureConnection&) = delete;

        bool connect(const std::string& host, std::uint16_t port, std::string& error);
        void poll();
        bool send(const std::string& payload, std::string& error);
        bool receive(std::string& payload);
        // connected becomes true only after the PSK-authenticated TLS 1.3
        // handshake completes, not merely when its TCP socket connects.
        bool connected() const;
        bool connecting() const;
        bool closed() const;
        const std::string& error() const;
        void close();
    };

    class SecureListener
    {
        struct Impl;
        std::unique_ptr<Impl> impl;

    public:
        explicit SecureListener(const std::string& roomSecretHex,
                                const TcpLimits& limits = TcpLimits());
        ~SecureListener();
        SecureListener(SecureListener&&) noexcept;
        SecureListener& operator=(SecureListener&&) noexcept;
        SecureListener(const SecureListener&) = delete;
        SecureListener& operator=(const SecureListener&) = delete;

        bool listen(std::uint16_t port, std::string& error);
        std::uint16_t port() const;
        // Accepted connections still need poll() to authenticate. Application
        // messages are unavailable before a successful handshake.
        std::unique_ptr<SecureConnection> accept();
        void close();
    };
}

#endif
