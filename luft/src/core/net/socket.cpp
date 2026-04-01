#include "socket.h"
#include "../logger.h"

#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

namespace luft
{

    // ──────────────────────────────────────────────
    // TcpSocket
    // ──────────────────────────────────────────────

    TcpSocket::TcpSocket(int fd) noexcept : fd_(fd) {}

    TcpSocket::~TcpSocket() { close(); }

    TcpSocket::TcpSocket(TcpSocket &&other) noexcept : fd_(other.fd_)
    {
        other.fd_ = -1;
    }

    TcpSocket &TcpSocket::operator=(TcpSocket &&other) noexcept
    {
        if (this != &other)
        {
            close();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    void TcpSocket::close()
    {
        if (fd_ >= 0)
        {
            ::close(fd_);
            fd_ = -1;
        }
    }

    bool TcpSocket::set_nonblocking()
    {
        int flags = ::fcntl(fd_, F_GETFL, 0);
        if (flags == -1)
        {
            LOG_ERROR("fcntl F_GETFL failed: %s", std::strerror(errno));
            return false;
        }
        if (::fcntl(fd_, F_SETFL, flags | O_NONBLOCK) == -1)
        {
            LOG_ERROR("fcntl F_SETFL O_NONBLOCK failed: %s", std::strerror(errno));
            return false;
        }
        return true;
    }

    bool TcpSocket::set_reuseaddr()
    {
        int opt = 1;
        if (::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1)
        {
            LOG_ERROR("setsockopt SO_REUSEADDR failed: %s", std::strerror(errno));
            return false;
        }
        return true;
    }

    bool TcpSocket::set_nodelay()
    {
        int opt = 1;
        if (::setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) == -1)
        {
            LOG_ERROR("setsockopt TCP_NODELAY failed: %s", std::strerror(errno));
            return false;
        }
        return true;
    }

    ssize_t TcpSocket::send_bytes(const uint8_t *data, size_t len)
    {
        while (true)
        {
            ssize_t n = ::send(fd_, data, len, MSG_NOSIGNAL);
            if (n >= 0)
                return n;

            int err = errno;
            if (err == EINTR)
                continue;
            if (err == EAGAIN || err == EWOULDBLOCK)
                return 0;
            if (err == EPIPE || err == ECONNRESET)
            {
                LOG_WARN("send: connection lost (fd %d): %s", fd_, std::strerror(err));
                return -1;
            }
            LOG_ERROR("send failed (fd %d): %s", fd_, std::strerror(err));
            return -1;
        }
    }

    ssize_t TcpSocket::recv_bytes(uint8_t *buf, size_t len)
    {
        while (true)
        {
            ssize_t n = ::recv(fd_, buf, len, 0);
            if (n > 0)
                return n;
            if (n == 0)
                return 0; // peer closed

            int err = errno;
            if (err == EINTR)
                continue;
            if (err == EAGAIN || err == EWOULDBLOCK)
                return -2;
            if (err == ECONNRESET)
            {
                LOG_WARN("recv: connection reset (fd %d)", fd_);
                return -1;
            }
            LOG_ERROR("recv failed (fd %d): %s", fd_, std::strerror(err));
            return -1;
        }
    }

    // ──────────────────────────────────────────────
    // TcpListener
    // ──────────────────────────────────────────────

    TcpListener::TcpListener() noexcept : fd_(-1) {}

    TcpListener::~TcpListener() { close(); }

    TcpListener::TcpListener(TcpListener &&other) noexcept : fd_(other.fd_)
    {
        other.fd_ = -1;
    }

    TcpListener &TcpListener::operator=(TcpListener &&other) noexcept
    {
        if (this != &other)
        {
            close();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    void TcpListener::close()
    {
        if (fd_ >= 0)
        {
            ::close(fd_);
            fd_ = -1;
        }
    }

    bool TcpListener::bind_and_listen(const std::string &host, uint16_t port, int backlog)
    {
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0)
        {
            LOG_ERROR("socket() failed: %s", std::strerror(errno));
            return false;
        }

        // Set reuse and non-blocking on the listener itself
        {
            int opt = 1;
            if (::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1)
            {
                LOG_ERROR("setsockopt SO_REUSEADDR failed: %s", std::strerror(errno));
                close();
                return false;
            }
        }

        int flags = ::fcntl(fd_, F_GETFL, 0);
        if (flags == -1 || ::fcntl(fd_, F_SETFL, flags | O_NONBLOCK) == -1)
        {
            LOG_ERROR("fcntl non-blocking on listener failed: %s", std::strerror(errno));
            close();
            return false;
        }

        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);

        if (host.empty() || host == "0.0.0.0")
        {
            addr.sin_addr.s_addr = INADDR_ANY;
        }
        else
        {
            if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1)
            {
                LOG_ERROR("invalid bind address: %s", host.c_str());
                close();
                return false;
            }
        }

        if (::bind(fd_, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) == -1)
        {
            LOG_ERROR("bind(%s:%u) failed: %s", host.c_str(), port, std::strerror(errno));
            close();
            return false;
        }

        if (::listen(fd_, backlog) == -1)
        {
            LOG_ERROR("listen() failed: %s", std::strerror(errno));
            close();
            return false;
        }

        LOG_INFO("listening on %s:%u (fd %d)", host.c_str(), port, fd_);
        return true;
    }

    TcpSocket TcpListener::accept_connection()
    {
        struct sockaddr_in peer_addr{};
        socklen_t peer_len = sizeof(peer_addr);

        while (true)
        {
            int client_fd = ::accept4(fd_, reinterpret_cast<struct sockaddr *>(&peer_addr),
                                      &peer_len, SOCK_NONBLOCK | SOCK_CLOEXEC);
            if (client_fd >= 0)
            {
                char ip_buf[INET_ADDRSTRLEN];
                ::inet_ntop(AF_INET, &peer_addr.sin_addr, ip_buf, sizeof(ip_buf));
                LOG_INFO("accepted connection from %s:%u (fd %d)",
                         ip_buf, ntohs(peer_addr.sin_port), client_fd);
                return TcpSocket(client_fd);
            }

            int err = errno;
            if (err == EINTR)
                continue;
            if (err == EAGAIN || err == EWOULDBLOCK)
            {
                return TcpSocket(-1); // no pending connection
            }
            LOG_ERROR("accept4() failed: %s", std::strerror(err));
            return TcpSocket(-1);
        }
    }

} // namespace luft
