#ifdef SOR_HAS_LIVE_FEED

#include "market_data/alpaca/alpaca_provider.h"
#include "infra/logging.h"

#include <nlohmann/json.hpp>

namespace sor::market_data::alpaca
{

    AlpacaProvider::AlpacaProvider(Config config)
        : config_(std::move(config)),
          normalizer_(config_.venue_id),
          quality_(std::chrono::seconds(5))
    {
    }

    AlpacaProvider::~AlpacaProvider()
    {
        disconnect();
    }

    bool AlpacaProvider::connect()
    {
        if (connected_.load())
            return true;

        should_run_.store(true);

        ws_.setUrl(config_.ws_url);
        ws_.setOnMessageCallback(
            [this](const ix::WebSocketMessagePtr &msg) {
                on_ws_message(msg);
            });

        // Configure automatic reconnection
        ws_.enableAutomaticReconnection();
        ws_.setMaxWaitBetweenReconnectionRetries(
            static_cast<uint32_t>(config_.reconnect_delay.count() * 1000));

        ws_.start();
        SOR_LOG_INFO("[AlpacaProvider] Connecting to {}", config_.ws_url);

        // Wait for connection + auth (up to 10 seconds)
        for (int i = 0; i < 100 && should_run_.load(); ++i)
        {
            if (authenticated_.load())
            {
                SOR_LOG_INFO("[AlpacaProvider] Connected and authenticated");
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (!authenticated_.load())
        {
            SOR_LOG_ERROR("[AlpacaProvider] Failed to authenticate within timeout");
            ws_.stop();
            return false;
        }

        return true;
    }

    void AlpacaProvider::disconnect()
    {
        should_run_.store(false);
        ws_.stop();
        connected_.store(false);
        authenticated_.store(false);
        SOR_LOG_INFO("[AlpacaProvider] Disconnected");
    }

    bool AlpacaProvider::is_connected() const
    {
        return connected_.load() && authenticated_.load();
    }

    void AlpacaProvider::subscribe(const Symbol &symbol)
    {
        std::string sym_str = symbol.to_string();
        {
            std::lock_guard lock(sub_mutex_);
            pending_symbols_.insert(sym_str);
        }

        if (authenticated_.load())
        {
            send_subscriptions();
        }
    }

    void AlpacaProvider::unsubscribe(const Symbol &symbol)
    {
        std::string sym_str = symbol.to_string();
        {
            std::lock_guard lock(sub_mutex_);
            pending_symbols_.erase(sym_str);
            subscribed_symbols_.erase(sym_str);
        }

        if (authenticated_.load())
        {
            nlohmann::json unsub;
            unsub["action"] = "unsubscribe";
            unsub["quotes"] = nlohmann::json::array({sym_str});
            ws_.send(unsub.dump());
            SOR_LOG_DEBUG("[AlpacaProvider] Unsubscribed from {}", sym_str);
        }
    }

    void AlpacaProvider::set_aggregator(MarketDataAggregator &aggregator)
    {
        aggregator_ = &aggregator;
    }

    void AlpacaProvider::on_ws_message(const ix::WebSocketMessagePtr &msg)
    {
        switch (msg->type)
        {
        case ix::WebSocketMessageType::Open:
            connected_.store(true);
            SOR_LOG_INFO("[AlpacaProvider] WebSocket connected");
            send_auth();
            break;

        case ix::WebSocketMessageType::Close:
            connected_.store(false);
            authenticated_.store(false);
            quality_.on_reconnection();
            SOR_LOG_WARN("[AlpacaProvider] WebSocket closed: {} {}",
                         msg->closeInfo.code, msg->closeInfo.reason);
            break;

        case ix::WebSocketMessageType::Error:
            quality_.on_parse_error();
            SOR_LOG_ERROR("[AlpacaProvider] WebSocket error: {}", msg->errorInfo.reason);
            break;

        case ix::WebSocketMessageType::Message:
        {
            quality_.on_message_received();

            const auto &text = msg->str;

            // Check for control messages (auth/subscription responses)
            try
            {
                auto json = nlohmann::json::parse(text);
                if (json.is_array() && !json.empty())
                {
                    const auto &first = json[0];
                    if (first.contains("T"))
                    {
                        const auto &t = first["T"].get_ref<const std::string &>();
                        if (t == "success" && first.contains("msg"))
                        {
                            const auto &m = first["msg"].get_ref<const std::string &>();
                            if (m == "connected")
                            {
                                SOR_LOG_DEBUG("[AlpacaProvider] Received 'connected' message");
                            }
                            else if (m == "authenticated")
                            {
                                authenticated_.store(true);
                                SOR_LOG_INFO("[AlpacaProvider] Authenticated successfully");
                                send_subscriptions();
                            }
                            return;
                        }
                        else if (t == "error")
                        {
                            int code = first.value("code", 0);
                            std::string err_msg = first.value("msg", "unknown");
                            SOR_LOG_ERROR("[AlpacaProvider] Error {}: {}", code, err_msg);
                            quality_.on_parse_error();
                            return;
                        }
                        else if (t == "subscription")
                        {
                            SOR_LOG_DEBUG("[AlpacaProvider] Subscription confirmed");
                            return;
                        }
                    }
                }
            }
            catch (...)
            {
                // Fall through to normalizer processing
            }

            // Process quote/trade data through normalizer
            if (aggregator_)
            {
                normalizer_.on_message(text,
                                       [this](VenueId vid, const Symbol &sym, const OrderBook &book) {
                                           aggregator_->on_book_update(vid, sym, book);
                                           quality_.on_quote_processed();
                                       });
            }
            break;
        }

        default:
            break;
        }
    }

    void AlpacaProvider::send_auth()
    {
        nlohmann::json auth;
        auth["action"] = "auth";
        auth["key"] = config_.api_key;
        auth["secret"] = config_.api_secret;
        ws_.send(auth.dump());
        SOR_LOG_DEBUG("[AlpacaProvider] Sent auth request");
    }

    void AlpacaProvider::send_subscriptions()
    {
        std::vector<std::string> to_subscribe;
        {
            std::lock_guard lock(sub_mutex_);
            for (const auto &sym : pending_symbols_)
            {
                if (subscribed_symbols_.find(sym) == subscribed_symbols_.end())
                {
                    to_subscribe.push_back(sym);
                }
            }
        }

        if (to_subscribe.empty())
            return;

        nlohmann::json sub;
        sub["action"] = "subscribe";
        sub["quotes"] = to_subscribe;

        ws_.send(sub.dump());

        {
            std::lock_guard lock(sub_mutex_);
            for (const auto &sym : to_subscribe)
            {
                subscribed_symbols_.insert(sym);
            }
        }

        SOR_LOG_INFO("[AlpacaProvider] Subscribed to {} symbols", to_subscribe.size());
    }

} // namespace sor::market_data::alpaca

#endif // SOR_HAS_LIVE_FEED
