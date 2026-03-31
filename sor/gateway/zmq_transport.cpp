#include "gateway/zmq_transport.h"
#include "infra/logging.h"

#include <chrono>

namespace sor::gateway
{

    ZmqTransport::ZmqTransport()
        : config_{}
    {
    }

    ZmqTransport::ZmqTransport(Config config)
        : config_(std::move(config))
    {
    }

    ZmqTransport::~ZmqTransport()
    {
        stop();
    }

    void ZmqTransport::set_request_handler(RequestHandler handler)
    {
        request_handler_ = std::move(handler);
    }

    bool ZmqTransport::start()
    {
        if (running_.load(std::memory_order_acquire))
            return false;

        try
        {
            context_ = std::make_unique<zmq::context_t>(1);

            // REP socket for order submission
            rep_socket_ = std::make_unique<zmq::socket_t>(*context_, zmq::socket_type::rep);
            rep_socket_->set(zmq::sockopt::rcvtimeo, config_.recv_timeout_ms);
            rep_socket_->bind(config_.order_endpoint);

            // PUB socket for market data
            md_pub_ = std::make_unique<zmq::socket_t>(*context_, zmq::socket_type::pub);
            md_pub_->bind(config_.market_data_endpoint);

            // PUB socket for execution events
            exec_pub_ = std::make_unique<zmq::socket_t>(*context_, zmq::socket_type::pub);
            exec_pub_->bind(config_.execution_endpoint);

            running_.store(true, std::memory_order_release);
            request_thread_ = std::thread(&ZmqTransport::request_loop, this);

            SOR_LOG_INFO("ZMQ transport started: orders={} market_data={} execution={}",
                         config_.order_endpoint, config_.market_data_endpoint,
                         config_.execution_endpoint);
            return true;
        }
        catch (const zmq::error_t &e)
        {
            SOR_LOG_ERROR("ZMQ transport start failed: {}", e.what());
            return false;
        }
    }

    void ZmqTransport::stop()
    {
        if (!running_.exchange(false, std::memory_order_acq_rel))
            return;

        if (request_thread_.joinable())
            request_thread_.join();

        // Close sockets before context destruction
        {
            std::lock_guard lk(pub_mutex_);
            exec_pub_.reset();
            md_pub_.reset();
        }
        rep_socket_.reset();
        context_.reset();

        SOR_LOG_INFO("ZMQ transport stopped");
    }

    bool ZmqTransport::is_running() const
    {
        return running_.load(std::memory_order_acquire);
    }

    void ZmqTransport::publish_market_data(const std::string &topic,
                                            const std::string &payload)
    {
        std::lock_guard lk(pub_mutex_);
        if (!md_pub_ || !running_.load(std::memory_order_relaxed))
            return;

        try
        {
            // Multi-part: [topic][payload]
            md_pub_->send(zmq::buffer(topic), zmq::send_flags::sndmore);
            md_pub_->send(zmq::buffer(payload), zmq::send_flags::none);

            std::lock_guard slk(stats_mutex_);
            ++stats_.md_published;
        }
        catch (const zmq::error_t &e)
        {
            SOR_LOG_WARN("ZMQ market data publish failed: {}", e.what());
            std::lock_guard slk(stats_mutex_);
            ++stats_.errors;
        }
    }

    void ZmqTransport::publish_execution_event(const std::string &topic,
                                                const std::string &payload)
    {
        std::lock_guard lk(pub_mutex_);
        if (!exec_pub_ || !running_.load(std::memory_order_relaxed))
            return;

        try
        {
            exec_pub_->send(zmq::buffer(topic), zmq::send_flags::sndmore);
            exec_pub_->send(zmq::buffer(payload), zmq::send_flags::none);

            std::lock_guard slk(stats_mutex_);
            ++stats_.exec_published;
        }
        catch (const zmq::error_t &e)
        {
            SOR_LOG_WARN("ZMQ execution publish failed: {}", e.what());
            std::lock_guard slk(stats_mutex_);
            ++stats_.errors;
        }
    }

    ZmqTransport::Stats ZmqTransport::get_stats() const
    {
        std::lock_guard lk(stats_mutex_);
        return stats_;
    }

    void ZmqTransport::request_loop()
    {
        while (running_.load(std::memory_order_acquire))
        {
            zmq::message_t request;
            auto result = rep_socket_->recv(request, zmq::recv_flags::none);

            if (!result.has_value())
                continue; // timeout, check running_ flag

            {
                std::lock_guard lk(stats_mutex_);
                ++stats_.requests_received;
            }

            std::string body(static_cast<const char *>(request.data()), request.size());
            std::string response;

            if (request_handler_)
            {
                try
                {
                    response = request_handler_(body);
                }
                catch (const std::exception &e)
                {
                    response = R"({"status":"error","message":"internal error"})";
                    SOR_LOG_ERROR("ZMQ request handler exception: {}", e.what());
                    std::lock_guard lk(stats_mutex_);
                    ++stats_.errors;
                }
            }
            else
            {
                response = R"({"status":"error","message":"no handler configured"})";
            }

            rep_socket_->send(zmq::buffer(response), zmq::send_flags::none);

            {
                std::lock_guard lk(stats_mutex_);
                ++stats_.requests_handled;
            }
        }
    }

} // namespace sor::gateway
