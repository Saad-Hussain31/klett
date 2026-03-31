#pragma once

// ZMQ-based transport for the Smart Order Router.
//
// Provides three ZMQ sockets:
//   1. REP socket for order submission & queries (REQ/REP pattern)
//   2. PUB socket for market data distribution (PUB/SUB pattern)
//   3. PUB socket for execution events (PUB/SUB pattern)
//
// The transport runs in its own thread and integrates with Gateway/ApiGateway.

#include "core/types.h"
#include "core/order.h"
#include <zmq.hpp>
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace sor::gateway
{

    class ZmqTransport
    {
    public:
        struct Config
        {
            std::string order_endpoint{"tcp://*:5555"};       // REP: order submission
            std::string market_data_endpoint{"tcp://*:5556"}; // PUB: market data
            std::string execution_endpoint{"tcp://*:5557"};   // PUB: execution events
            int recv_timeout_ms{100};                         // poll timeout for clean shutdown
        };

        using RequestHandler = std::function<std::string(const std::string &)>;

        explicit ZmqTransport(Config config);
        ZmqTransport(); // default constructor with default Config
        ~ZmqTransport();

        ZmqTransport(const ZmqTransport &) = delete;
        ZmqTransport &operator=(const ZmqTransport &) = delete;

        // Set the handler for incoming order requests (JSON in, JSON out).
        void set_request_handler(RequestHandler handler);

        // Start the transport threads.
        bool start();

        // Stop the transport and join threads.
        void stop();

        [[nodiscard]] bool is_running() const;

        // Publish a market data update (topic + JSON payload).
        void publish_market_data(const std::string &topic, const std::string &payload);

        // Publish an execution event (topic + JSON payload).
        void publish_execution_event(const std::string &topic, const std::string &payload);

        // Statistics.
        struct Stats
        {
            uint64_t requests_received{0};
            uint64_t requests_handled{0};
            uint64_t md_published{0};
            uint64_t exec_published{0};
            uint64_t errors{0};
        };
        [[nodiscard]] Stats get_stats() const;

    private:
        void request_loop();

        Config config_;
        std::atomic<bool> running_{false};
        RequestHandler request_handler_;

        std::unique_ptr<zmq::context_t> context_;
        std::unique_ptr<zmq::socket_t> rep_socket_; // REQ/REP for orders
        std::unique_ptr<zmq::socket_t> md_pub_;     // PUB for market data
        std::unique_ptr<zmq::socket_t> exec_pub_;   // PUB for execution events

        std::thread request_thread_;
        std::mutex pub_mutex_; // protects PUB sockets

        Stats stats_;
        mutable std::mutex stats_mutex_;
    };

} // namespace sor::gateway
