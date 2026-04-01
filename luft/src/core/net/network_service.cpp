#include "network_service.h"
#include "../logger.h"

#include <sys/epoll.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <algorithm>

namespace luft
{

    // ──────────────────────────────────────────────
    // Construction / destruction
    // ──────────────────────────────────────────────

    NetworkService::NetworkService() = default;

    NetworkService::~NetworkService() { stop(); }

    // ──────────────────────────────────────────────
    // Lifecycle
    // ──────────────────────────────────────────────

    bool NetworkService::start(const std::string &telemetry_host, uint16_t telemetry_port,
                               const std::string &command_host, uint16_t command_port)
    {
        epoll_fd_ = ::epoll_create1(EPOLL_CLOEXEC);
        if (epoll_fd_ < 0)
        {
            LOG_ERROR("epoll_create1 failed: %s", std::strerror(errno));
            return false;
        }

        if (!telemetry_listener_.bind_and_listen(telemetry_host, telemetry_port))
        {
            stop();
            return false;
        }
        if (!epoll_add(telemetry_listener_.fd(), EPOLLIN))
        {
            stop();
            return false;
        }

        if (!command_listener_.bind_and_listen(command_host, command_port))
        {
            stop();
            return false;
        }
        if (!epoll_add(command_listener_.fd(), EPOLLIN))
        {
            stop();
            return false;
        }

        running_.store(true, std::memory_order_release);
        LOG_INFO("network service started  telemetry=%s:%u  command=%s:%u",
                 telemetry_host.c_str(), telemetry_port,
                 command_host.c_str(), command_port);
        return true;
    }

    void NetworkService::stop()
    {
        running_.store(false, std::memory_order_release);

        {
            std::lock_guard<std::mutex> lock(clients_mu_);
            for (auto &[fd, ci] : telemetry_clients_)
            {
                epoll_remove(fd);
                ci.socket.close();
            }
            telemetry_clients_.clear();

            for (auto &[fd, ci] : command_clients_)
            {
                epoll_remove(fd);
                ci.socket.close();
            }
            command_clients_.clear();
        }

        telemetry_listener_.close();
        command_listener_.close();

        if (epoll_fd_ >= 0)
        {
            ::close(epoll_fd_);
            epoll_fd_ = -1;
        }

        LOG_INFO("network service stopped");
    }

    // ──────────────────────────────────────────────
    // Callbacks
    // ──────────────────────────────────────────────

    void NetworkService::set_command_callback(CommandCallback cb)
    {
        command_cb_ = std::move(cb);
    }

    void NetworkService::set_control_callback(ControlCallback cb)
    {
        control_cb_ = std::move(cb);
    }

    // ──────────────────────────────────────────────
    // Event loop — called from the network thread
    // ──────────────────────────────────────────────

    void NetworkService::poll(int timeout_ms)
    {
        if (epoll_fd_ < 0)
            return;

        // ── Drain the outbound telemetry queue ───
        {
            std::vector<uint8_t> frame;
            {
                std::lock_guard<std::mutex> lock(outbound_mu_);
                if (outbound_pending_)
                {
                    frame.swap(outbound_frame_);
                    outbound_pending_ = false;
                }
            }
            if (!frame.empty())
            {
                std::lock_guard<std::mutex> lock(clients_mu_);
                for (auto &[fd, ci] : telemetry_clients_)
                {
                    ci.send_buf.insert(ci.send_buf.end(), frame.begin(), frame.end());
                }
            }
        }

        // ── Flush pending writes on telemetry clients ───
        {
            std::lock_guard<std::mutex> lock(clients_mu_);
            for (auto &[fd, ci] : telemetry_clients_)
            {
                flush_send_queue(ci);
            }
        }

        // ── epoll_wait ───────────────────────────
        static constexpr int kMaxEvents = 64;
        struct epoll_event events[kMaxEvents];

        int nfds = ::epoll_wait(epoll_fd_, events, kMaxEvents, timeout_ms);
        if (nfds < 0)
        {
            if (errno != EINTR)
                LOG_ERROR("epoll_wait failed: %s", std::strerror(errno));
            return;
        }

        for (int i = 0; i < nfds; ++i)
        {
            int fd = events[i].data.fd;
            uint32_t ev = events[i].events;

            // ── Listener fds ────────────────────
            if (fd == telemetry_listener_.fd())
            {
                accept_clients(telemetry_listener_, ListenerKind::Telemetry);
                continue;
            }
            if (fd == command_listener_.fd())
            {
                accept_clients(command_listener_, ListenerKind::Command);
                continue;
            }

            // ── Error / hangup ──────────────────
            if (ev & (EPOLLERR | EPOLLHUP))
            {
                LOG_INFO("client fd %d: hangup/error", fd);
                remove_client(fd);
                continue;
            }

            // ── Readable ────────────────────────
            if (ev & EPOLLIN)
            {
                handle_readable(fd);
            }
        }
    }

    // ──────────────────────────────────────────────
    // Accept new clients
    // ──────────────────────────────────────────────

    void NetworkService::accept_clients(TcpListener &listener, ListenerKind kind)
    {
        for (;;)
        {
            TcpSocket sock = listener.accept_connection();
            if (!sock.valid())
                return;

            int fd = sock.fd();
            sock.set_nodelay();

            if (!epoll_add(fd, EPOLLIN | EPOLLET))
            {
                LOG_WARN("epoll_add for client fd %d failed, dropping", fd);
                return;
            }

            const char *label = (kind == ListenerKind::Telemetry) ? "telemetry" : "command";

            std::lock_guard<std::mutex> lock(clients_mu_);
            auto &list = (kind == ListenerKind::Telemetry) ? telemetry_clients_ : command_clients_;

            if (static_cast<int>(list.size()) >= kMaxClients)
            {
                LOG_WARN("max %s clients reached (%d), rejecting fd %d",
                         label, kMaxClients, fd);
                epoll_remove(fd);
                // sock destructor closes fd
                return;
            }

            ClientInfo ci;
            ci.socket = std::move(sock);
            list.emplace_back(fd, std::move(ci));
            LOG_INFO("%s client connected (fd %d, total %zu)", label, fd, list.size());
        }
    }

    // ──────────────────────────────────────────────
    // Handle readable fd
    // ──────────────────────────────────────────────

    void NetworkService::handle_readable(int fd)
    {
        // Find the client in either list
        ClientInfo *client = nullptr;
        bool is_command = false;

        {
            std::lock_guard<std::mutex> lock(clients_mu_);
            for (auto &[cfd, ci] : command_clients_)
            {
                if (cfd == fd)
                {
                    client = &ci;
                    is_command = true;
                    break;
                }
            }
            if (!client)
            {
                for (auto &[cfd, ci] : telemetry_clients_)
                {
                    if (cfd == fd)
                    {
                        client = &ci;
                        break;
                    }
                }
            }
        }

        if (!client)
        {
            LOG_WARN("readable event for unknown fd %d", fd);
            epoll_remove(fd);
            return;
        }

        // Edge-triggered: read until EAGAIN
        uint8_t buf[4096];
        for (;;)
        {
            ssize_t n = client->socket.recv_bytes(buf, sizeof(buf));
            if (n > 0)
            {
                client->codec.feed(buf, static_cast<size_t>(n));
                continue;
            }
            if (n == 0)
            {
                // Peer closed
                LOG_INFO("client fd %d disconnected", fd);
                remove_client(fd);
                return;
            }
            if (n == -2)
            {
                // EAGAIN — done for now
                break;
            }
            // Hard error
            LOG_WARN("read error on fd %d, removing client", fd);
            remove_client(fd);
            return;
        }

        if (is_command)
        {
            dispatch_messages(*client);
        }
    }

    // ──────────────────────────────────────────────
    // Dispatch decoded command messages
    // ──────────────────────────────────────────────

    void NetworkService::dispatch_messages(ClientInfo &client)
    {
        while (client.codec.has_message())
        {
            FramedMessage msg = client.codec.pop_message();

            switch (msg.type)
            {
            case MessageType::SimCommand:
            {
                SimCommandType cmd;
                if (MessageCodec::deserialize_sim_command(msg.payload.data(),
                                                          msg.payload.size(), cmd))
                {
                    if (command_cb_)
                        command_cb_(cmd);
                }
                break;
            }
            case MessageType::ControlInput:
            {
                ControlInput input;
                if (MessageCodec::deserialize_control_input(msg.payload.data(),
                                                            msg.payload.size(), input))
                {
                    if (control_cb_)
                        control_cb_(input);
                }
                break;
            }
            default:
                LOG_WARN("unexpected message type 0x%04x from command client",
                         static_cast<unsigned>(msg.type));
                break;
            }
        }
    }

    // ──────────────────────────────────────────────
    // Remove a client from all tracking
    // ──────────────────────────────────────────────

    void NetworkService::remove_client(int fd)
    {
        epoll_remove(fd);

        std::lock_guard<std::mutex> lock(clients_mu_);

        auto remove_from = [fd](std::vector<std::pair<int, ClientInfo>> &list)
        {
            auto it = std::find_if(list.begin(), list.end(),
                                   [fd](const auto &p)
                                   { return p.first == fd; });
            if (it != list.end())
            {
                it->second.socket.close();
                list.erase(it);
                return true;
            }
            return false;
        };

        if (!remove_from(telemetry_clients_))
            remove_from(command_clients_);
    }

    // ──────────────────────────────────────────────
    // Telemetry publishing (called from sim thread)
    // ──────────────────────────────────────────────

    void NetworkService::publish_telemetry(const AircraftState &state, SimState sim_state)
    {
        uint16_t seq = telemetry_seq_.fetch_add(1, std::memory_order_relaxed);

        auto payload = MessageCodec::serialize_telemetry(state, sim_state);
        auto frame = MessageCodec::encode(MessageType::Telemetry, seq,
                                          payload.data(), payload.size());

        std::lock_guard<std::mutex> lock(outbound_mu_);
        outbound_frame_.swap(frame);
        outbound_pending_ = true;
    }

    // ──────────────────────────────────────────────
    // Flush queued send data to a client
    // ──────────────────────────────────────────────

    void NetworkService::flush_send_queue(ClientInfo &client)
    {
        while (client.send_offset < client.send_buf.size())
        {
            size_t remaining = client.send_buf.size() - client.send_offset;
            ssize_t n = client.socket.send_bytes(
                client.send_buf.data() + client.send_offset, remaining);

            if (n > 0)
            {
                client.send_offset += static_cast<size_t>(n);
                continue;
            }
            if (n == 0)
            {
                // EAGAIN — socket buffer full, try again next poll()
                break;
            }
            // Hard error — caller will detect on next read or epoll event
            LOG_WARN("send error on fd %d, clearing send buffer", client.socket.fd());
            client.send_buf.clear();
            client.send_offset = 0;
            return;
        }

        // If everything was sent, reclaim the buffer
        if (client.send_offset >= client.send_buf.size())
        {
            client.send_buf.clear();
            client.send_offset = 0;
        }
    }

    // ──────────────────────────────────────────────
    // Epoll helpers
    // ──────────────────────────────────────────────

    bool NetworkService::epoll_add(int fd, uint32_t events)
    {
        struct epoll_event ev{};
        ev.events = events;
        ev.data.fd = fd;
        if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) == -1)
        {
            LOG_ERROR("epoll_ctl ADD fd %d failed: %s", fd, std::strerror(errno));
            return false;
        }
        return true;
    }

    bool NetworkService::epoll_modify(int fd, uint32_t events)
    {
        struct epoll_event ev{};
        ev.events = events;
        ev.data.fd = fd;
        if (::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) == -1)
        {
            LOG_ERROR("epoll_ctl MOD fd %d failed: %s", fd, std::strerror(errno));
            return false;
        }
        return true;
    }

    void NetworkService::epoll_remove(int fd)
    {
        // EPOLL_CTL_DEL ignores the event pointer (since Linux 2.6.9)
        ::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
    }

    // ──────────────────────────────────────────────
    // Enqueue a telemetry frame directly to all
    // connected telemetry clients (used internally)
    // ──────────────────────────────────────────────

    void NetworkService::enqueue_to_telemetry_clients(std::vector<uint8_t> frame)
    {
        std::lock_guard<std::mutex> lock(clients_mu_);
        for (auto &[fd, ci] : telemetry_clients_)
        {
            ci.send_buf.insert(ci.send_buf.end(), frame.begin(), frame.end());
        }
    }

} // namespace luft
