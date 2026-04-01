#pragma once

#include "protocol.h"
#include "socket.h"
#include "message_codec.h"
#include "../aircraft_state.h"

#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <atomic>

namespace luft
{

    // ──────────────────────────────────────────────
    // NetworkService — epoll-driven telemetry publisher
    //                  and command receiver
    // ──────────────────────────────────────────────

    class NetworkService
    {
    public:
        static constexpr int kMaxClients = 8;

        using CommandCallback = std::function<void(SimCommandType)>;
        using ControlCallback = std::function<void(const ControlInput &)>;

        NetworkService();
        ~NetworkService();

        NetworkService(const NetworkService &) = delete;
        NetworkService &operator=(const NetworkService &) = delete;

        /// Bind the telemetry and command listeners.  Returns false on failure.
        bool start(const std::string &telemetry_host, uint16_t telemetry_port,
                   const std::string &command_host, uint16_t command_port);

        /// Tear everything down (close all sockets, destroy epoll).
        void stop();

        /// Drive the event loop — call from a dedicated network thread.
        /// timeout_ms is passed to epoll_wait.
        void poll(int timeout_ms);

        /// Thread-safe: serialize state and enqueue for all telemetry clients.
        void publish_telemetry(const AircraftState &state, SimState sim_state);

        void set_command_callback(CommandCallback cb);
        void set_control_callback(ControlCallback cb);

    private:
        // ── Per-client bookkeeping ────────────────
        struct ClientInfo
        {
            TcpSocket socket;
            MessageCodec codec;
            // Outbound queue: fully framed messages waiting to be written.
            std::vector<uint8_t> send_buf;
            size_t send_offset = 0;
        };

        enum class ListenerKind
        {
            Telemetry,
            Command
        };

        // ── Helpers ──────────────────────────────
        void accept_clients(TcpListener &listener, ListenerKind kind);
        void handle_readable(int fd);
        void dispatch_messages(ClientInfo &client);
        void remove_client(int fd);
        void flush_send_queue(ClientInfo &client);
        void enqueue_to_telemetry_clients(std::vector<uint8_t> frame);

        bool epoll_add(int fd, uint32_t events);
        bool epoll_modify(int fd, uint32_t events);
        void epoll_remove(int fd);

        // ── State ────────────────────────────────
        int epoll_fd_ = -1;

        TcpListener telemetry_listener_;
        TcpListener command_listener_;

        // Client maps — keyed by fd.  Protected by clients_mu_ for the
        // telemetry publish path (cross-thread).  The poll() path is
        // single-threaded and only grabs the lock when it modifies the map.
        std::mutex clients_mu_;
        std::vector<std::pair<int, ClientInfo>> telemetry_clients_;
        std::vector<std::pair<int, ClientInfo>> command_clients_;

        // Telemetry outbound queue — written by sim thread, drained in poll().
        std::mutex outbound_mu_;
        std::vector<uint8_t> outbound_frame_; // latest frame (swap semantics)
        bool outbound_pending_ = false;

        std::atomic<uint16_t> telemetry_seq_{0};

        CommandCallback command_cb_;
        ControlCallback control_cb_;

        std::atomic<bool> running_{false};
    };

} // namespace luft
