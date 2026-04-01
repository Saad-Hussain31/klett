#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

namespace luft
{

    // ──────────────────────────────────────────────
    // TcpSocket — RAII wrapper for a connected TCP fd
    // ──────────────────────────────────────────────

    class TcpSocket
    {
    public:
        explicit TcpSocket(int fd = -1) noexcept;
        ~TcpSocket();

        // Move-only
        TcpSocket(TcpSocket &&other) noexcept;
        TcpSocket &operator=(TcpSocket &&other) noexcept;
        TcpSocket(const TcpSocket &) = delete;
        TcpSocket &operator=(const TcpSocket &) = delete;

        [[nodiscard]] int fd() const noexcept { return fd_; }
        [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }

        void close();

        bool set_nonblocking();
        bool set_reuseaddr();
        bool set_nodelay();

        /// Non-blocking send. Returns bytes sent, 0 if EAGAIN, -1 on hard error.
        ssize_t send_bytes(const uint8_t *data, size_t len);

        /// Non-blocking recv. Returns bytes received, 0 if peer closed,
        /// -1 on hard error, -2 if EAGAIN (no data available).
        ssize_t recv_bytes(uint8_t *buf, size_t len);

    private:
        int fd_;
    };

    // ──────────────────────────────────────────────
    // TcpListener — RAII server socket
    // ──────────────────────────────────────────────

    class TcpListener
    {
    public:
        TcpListener() noexcept;
        ~TcpListener();

        // Move-only
        TcpListener(TcpListener &&other) noexcept;
        TcpListener &operator=(TcpListener &&other) noexcept;
        TcpListener(const TcpListener &) = delete;
        TcpListener &operator=(const TcpListener &) = delete;

        bool bind_and_listen(const std::string &host, uint16_t port, int backlog = 16);

        /// Non-blocking accept. Returns invalid socket if no pending connection.
        TcpSocket accept_connection();

        [[nodiscard]] int fd() const noexcept { return fd_; }
        [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }

        void close();

    private:
        int fd_;
    };

} // namespace luft
