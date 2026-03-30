// fix_adapter.cpp -- Mock FIX protocol adapter implementation.
// Translates between internal Order/ExecutionReport structs and FIX 4.4
// tag-value messages.  All "network I/O" is simulated via in-process queues.

#include "connectors/fix_adapter.h"
#include <cinttypes>
#include <cstdio>
#include <sstream>
#include <charconv>
#include <stdexcept>

namespace sor::connectors
{

    // ---------------------------------------------------------------------------
    // Additional FIX tags not exposed in the public header (implementation detail).
    // ---------------------------------------------------------------------------

    namespace fix_tags
    {
        static constexpr int TAG_ORD_TYPE = 40;
        static constexpr int TAG_TIME_IN_FORCE = 59;
        static constexpr int TAG_SENDER_COMP_ID = 49;
        static constexpr int TAG_TARGET_COMP_ID = 56;
        static constexpr int TAG_MSG_SEQ_NUM = 34;
        static constexpr int TAG_ORIG_CL_ORD_ID = 41;
    } // namespace fix_tags

    // ---------------------------------------------------------------------------
    // FIX Side mapping:  1 = Buy, 2 = Sell
    // ---------------------------------------------------------------------------

    static char side_to_fix(Side side)
    {
        return (side == Side::Buy) ? '1' : '2';
    }

    [[maybe_unused]] static Side side_from_fix(const std::string &val)
    {
        return (val == "1") ? Side::Buy : Side::Sell;
    }

    // ---------------------------------------------------------------------------
    // FIX OrdType mapping:  1 = Market, 2 = Limit
    // (IOC and FOK are expressed via TimeInForce, not OrdType, in standard FIX.)
    // ---------------------------------------------------------------------------

    static char ord_type_to_fix(OrderType type)
    {
        switch (type)
        {
        case OrderType::Market:
            return '1';
        case OrderType::Limit:
            return '2';
        case OrderType::IOC:
            return '2'; // limit + TIF=IOC
        case OrderType::FOK:
            return '2'; // limit + TIF=FOK
        }
        return '2';
    }

    [[maybe_unused]] static OrderType ord_type_from_fix(const std::string &val,
                                                        const std::string &tif_val)
    {
        if (val == "1")
            return OrderType::Market;
        // Limit -- but check TIF for IOC/FOK.
        if (tif_val == "3")
            return OrderType::IOC;
        if (tif_val == "4")
            return OrderType::FOK;
        return OrderType::Limit;
    }

    // ---------------------------------------------------------------------------
    // FIX TimeInForce mapping:  0=Day, 1=GTC, 3=IOC, 4=FOK, 6=GTD
    // ---------------------------------------------------------------------------

    static char tif_to_fix(TimeInForce tif, OrderType type)
    {
        // IOC and FOK order types override the TIF field.
        if (type == OrderType::IOC)
            return '3';
        if (type == OrderType::FOK)
            return '4';

        switch (tif)
        {
        case TimeInForce::DAY:
            return '0';
        case TimeInForce::GTC:
            return '1';
        case TimeInForce::IOC:
            return '3';
        case TimeInForce::FOK:
            return '4';
        case TimeInForce::GTD:
            return '6';
        }
        return '1';
    }

    [[maybe_unused]] static TimeInForce tif_from_fix(const std::string &val)
    {
        if (val == "0")
            return TimeInForce::DAY;
        if (val == "1")
            return TimeInForce::GTC;
        if (val == "3")
            return TimeInForce::IOC;
        if (val == "4")
            return TimeInForce::FOK;
        if (val == "6")
            return TimeInForce::GTD;
        return TimeInForce::GTC;
    }

    // ---------------------------------------------------------------------------
    // FIX OrdStatus mapping:
    //   0 = New, 1 = PartiallyFilled, 2 = Filled, 4 = Canceled, 8 = Rejected
    //   A = PendingNew, 6 = PendingCancel, E = PendingReplace, C = Expired
    // ---------------------------------------------------------------------------

    [[maybe_unused]] static std::string ord_status_to_fix(OrderState state)
    {
        switch (state)
        {
        case OrderState::New:
            return "0";
        case OrderState::PendingNew:
            return "A";
        case OrderState::Accepted:
            return "0"; // FIX "New" (acked)
        case OrderState::PartiallyFilled:
            return "1";
        case OrderState::Filled:
            return "2";
        case OrderState::PendingCancel:
            return "6";
        case OrderState::Canceled:
            return "4";
        case OrderState::Rejected:
            return "8";
        case OrderState::Expired:
            return "C";
        case OrderState::PendingReplace:
            return "E";
        }
        return "0";
    }

    static OrderState ord_status_from_fix(const std::string &val)
    {
        if (val == "0")
            return OrderState::Accepted;
        if (val == "A")
            return OrderState::PendingNew;
        if (val == "1")
            return OrderState::PartiallyFilled;
        if (val == "2")
            return OrderState::Filled;
        if (val == "4")
            return OrderState::Canceled;
        if (val == "6")
            return OrderState::PendingCancel;
        if (val == "8")
            return OrderState::Rejected;
        if (val == "C")
            return OrderState::Expired;
        if (val == "E")
            return OrderState::PendingReplace;
        return OrderState::New;
    }

    // ---------------------------------------------------------------------------
    // FIX ExecType mapping (simplified):
    //   0 = New, 1 = PartialFill, 2 = Fill, 4 = Canceled, 8 = Rejected,
    //   F = Trade (FIX 4.4+)
    // ---------------------------------------------------------------------------

    [[maybe_unused]] static std::string exec_type_for_state(OrderState state)
    {
        switch (state)
        {
        case OrderState::Accepted:
            return "0"; // New
        case OrderState::PartiallyFilled:
            return "F"; // Trade (partial)
        case OrderState::Filled:
            return "F"; // Trade (full)
        case OrderState::Canceled:
            return "4";
        case OrderState::Rejected:
            return "8";
        case OrderState::Expired:
            return "C";
        default:
            return "0";
        }
    }

    // ---------------------------------------------------------------------------
    // Numeric conversion helpers
    // ---------------------------------------------------------------------------

    static std::string price_to_str(Price p)
    {
        // Convert fixed-point price to a decimal string with 8 decimal places.
        const bool negative = (p < 0);
        const int64_t abs_p = negative ? -p : p;
        const int64_t whole = abs_p / PRICE_SCALE;
        const int64_t frac = abs_p % PRICE_SCALE;

        char buf[32];
        int len = std::snprintf(buf, sizeof(buf), "%s%" PRId64 ".%08" PRId64,
                                negative ? "-" : "", whole, frac);
        // Trim trailing zeros for cleaner output.
        while (len > 1 && buf[len - 1] == '0' && buf[len - 2] != '.')
            --len;
        return std::string(buf, static_cast<size_t>(len));
    }

    static Price price_from_str(const std::string &s)
    {
        if (s.empty())
            return 0;
        const double d = std::stod(s);
        return to_price(d);
    }

    static std::string qty_to_str(Quantity q)
    {
        return std::to_string(q);
    }

    static Quantity qty_from_str(const std::string &s)
    {
        if (s.empty())
            return 0;
        return static_cast<Quantity>(std::stoll(s));
    }

    static std::string id_to_str(OrderId id)
    {
        return std::to_string(id);
    }

    static OrderId id_from_str(const std::string &s)
    {
        if (s.empty())
            return INVALID_ORDER_ID;
        return static_cast<OrderId>(std::stoull(s));
    }

    // ---------------------------------------------------------------------------
    // FixMessage -- serialization
    // ---------------------------------------------------------------------------

    std::string FixMessage::serialize() const
    {
        std::ostringstream oss;
        bool first = true;
        for (const auto &[tag, value] : fields)
        {
            if (!first)
                oss << '|';
            oss << tag << '=' << value;
            first = false;
        }
        return oss.str();
    }

    FixMessage FixMessage::deserialize(const std::string &raw)
    {
        FixMessage msg{};
        msg.type = Type::Heartbeat; // default; overridden by TAG_MSG_TYPE

        std::istringstream iss(raw);
        std::string token;

        while (std::getline(iss, token, '|'))
        {
            const auto eq = token.find('=');
            if (eq == std::string::npos)
                continue;

            const int tag = std::stoi(token.substr(0, eq));
            std::string value = token.substr(eq + 1);
            msg.fields[tag] = std::move(value);
        }

        // Derive message type from tag 35.
        auto it = msg.fields.find(TAG_MSG_TYPE);
        if (it != msg.fields.end())
        {
            const auto &mt = it->second;
            if (mt == "D")
                msg.type = Type::NewOrderSingle;
            else if (mt == "F")
                msg.type = Type::OrderCancelRequest;
            else if (mt == "8")
                msg.type = Type::ExecutionReport;
            else if (mt == "0")
                msg.type = Type::Heartbeat;
            else if (mt == "A")
                msg.type = Type::Logon;
            else if (mt == "5")
                msg.type = Type::Logout;
        }

        return msg;
    }

    // ---------------------------------------------------------------------------
    // FixAdapter -- construction
    // ---------------------------------------------------------------------------

    FixAdapter::FixAdapter(Config config)
        : config_(std::move(config))
    {
    }

    // ---------------------------------------------------------------------------
    // FixAdapter -- connection lifecycle
    // ---------------------------------------------------------------------------

    bool FixAdapter::connect()
    {
        bool expected = false;
        if (!connected_.compare_exchange_strong(expected, true,
                                                std::memory_order_acq_rel))
        {
            return false; // already connected
        }

        std::lock_guard<std::mutex> lock(mutex_);

        // Send a Logon message.
        FixMessage logon{};
        logon.type = FixMessage::Type::Logon;
        logon.fields[FixMessage::TAG_MSG_TYPE] = "A";
        logon.fields[fix_tags::TAG_SENDER_COMP_ID] = config_.sender_comp_id;
        logon.fields[fix_tags::TAG_TARGET_COMP_ID] = config_.target_comp_id;
        logon.fields[fix_tags::TAG_MSG_SEQ_NUM] = std::to_string(++msg_seq_num_);
        sent_log_.push_back(std::move(logon));

        return true;
    }

    void FixAdapter::disconnect()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);

            if (connected_.load(std::memory_order_acquire))
            {
                // Send a Logout message.
                FixMessage logout{};
                logout.type = FixMessage::Type::Logout;
                logout.fields[FixMessage::TAG_MSG_TYPE] = "5";
                logout.fields[fix_tags::TAG_SENDER_COMP_ID] = config_.sender_comp_id;
                logout.fields[fix_tags::TAG_TARGET_COMP_ID] = config_.target_comp_id;
                logout.fields[fix_tags::TAG_MSG_SEQ_NUM] = std::to_string(++msg_seq_num_);
                sent_log_.push_back(std::move(logout));
            }
        }

        connected_.store(false, std::memory_order_release);
    }

    bool FixAdapter::is_connected() const
    {
        return connected_.load(std::memory_order_acquire);
    }

    // ---------------------------------------------------------------------------
    // FixAdapter -- order operations
    // ---------------------------------------------------------------------------

    bool FixAdapter::send_order(const Order &order)
    {
        if (!is_connected())
            return false;

        std::lock_guard<std::mutex> lock(mutex_);

        active_orders_.emplace(order.id, order);

        FixMessage msg = order_to_fix(order);
        msg.fields[fix_tags::TAG_MSG_SEQ_NUM] = std::to_string(++msg_seq_num_);
        sent_log_.push_back(std::move(msg));

        return true;
    }

    bool FixAdapter::cancel_order(const CancelRequest &request)
    {
        if (!is_connected())
            return false;

        std::lock_guard<std::mutex> lock(mutex_);

        auto it = active_orders_.find(request.order_id);
        if (it == active_orders_.end())
            return false;

        FixMessage msg = cancel_to_fix(request);
        msg.fields[fix_tags::TAG_MSG_SEQ_NUM] = std::to_string(++msg_seq_num_);
        sent_log_.push_back(std::move(msg));

        return true;
    }

    // ---------------------------------------------------------------------------
    // FixAdapter -- metadata & latency
    // ---------------------------------------------------------------------------

    VenueId FixAdapter::venue_id() const
    {
        return config_.venue_id;
    }

    const char *FixAdapter::venue_name() const
    {
        return config_.name.c_str();
    }

    VenueStatus FixAdapter::status() const
    {
        return is_connected() ? VenueStatus::Connected : VenueStatus::Disconnected;
    }

    std::chrono::microseconds FixAdapter::avg_latency() const
    {
        return config_.simulated_latency;
    }

    // ---------------------------------------------------------------------------
    // FixAdapter -- incoming message processing
    // ---------------------------------------------------------------------------

    void FixAdapter::process_incoming()
    {
        std::lock_guard<std::mutex> lock(mutex_);

        while (!incoming_queue_.empty())
        {
            FixMessage msg = std::move(incoming_queue_.front());
            incoming_queue_.pop();

            if (msg.type != FixMessage::Type::ExecutionReport)
                continue;

            ExecutionReport report = exec_from_fix(msg);
            report.venue_id = config_.venue_id;

            // Update tracked active orders.
            const OrderId oid = report.order_id;
            auto it = active_orders_.find(oid);
            if (it != active_orders_.end())
            {
                Order &tracked = it->second;
                tracked.filled_quantity = report.cum_quantity;
                tracked.remaining_quantity = report.leaves_quantity;
                tracked.avg_fill_price = report.avg_price;

                // Remove from tracking if terminal.
                if (report.state == OrderState::Filled ||
                    report.state == OrderState::Canceled ||
                    report.state == OrderState::Rejected ||
                    report.state == OrderState::Expired)
                {
                    active_orders_.erase(it);
                }
            }

            notify_execution(report);
        }
    }

    void FixAdapter::inject_message(FixMessage msg)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        incoming_queue_.push(std::move(msg));
    }

    // ---------------------------------------------------------------------------
    // FixAdapter -- FIX <-> internal translation
    // ---------------------------------------------------------------------------

    FixMessage FixAdapter::order_to_fix(const Order &order) const
    {
        FixMessage msg{};
        msg.type = FixMessage::Type::NewOrderSingle;

        msg.fields[FixMessage::TAG_MSG_TYPE] = "D";
        msg.fields[FixMessage::TAG_CL_ORD_ID] = id_to_str(order.client_order_id);
        msg.fields[FixMessage::TAG_SYMBOL] = order.symbol.to_string();
        msg.fields[FixMessage::TAG_SIDE] = std::string(1, side_to_fix(order.side));
        msg.fields[FixMessage::TAG_ORDER_QTY] = qty_to_str(order.quantity);
        msg.fields[fix_tags::TAG_ORD_TYPE] = std::string(1, ord_type_to_fix(order.type));
        msg.fields[fix_tags::TAG_TIME_IN_FORCE] =
            std::string(1, tif_to_fix(order.tif, order.type));
        msg.fields[fix_tags::TAG_SENDER_COMP_ID] = config_.sender_comp_id;
        msg.fields[fix_tags::TAG_TARGET_COMP_ID] = config_.target_comp_id;

        // Include price for limit-like orders.
        if (order.type != OrderType::Market && order.price != INVALID_PRICE)
        {
            msg.fields[FixMessage::TAG_PRICE] = price_to_str(order.price);
        }

        return msg;
    }

    FixMessage FixAdapter::cancel_to_fix(const CancelRequest &request) const
    {
        FixMessage msg{};
        msg.type = FixMessage::Type::OrderCancelRequest;

        msg.fields[FixMessage::TAG_MSG_TYPE] = "F";
        msg.fields[FixMessage::TAG_CL_ORD_ID] =
            id_to_str(request.client_order_id);
        msg.fields[fix_tags::TAG_ORIG_CL_ORD_ID] =
            id_to_str(request.order_id);
        msg.fields[FixMessage::TAG_SYMBOL] = request.symbol.to_string();
        msg.fields[FixMessage::TAG_SIDE] =
            std::string(1, side_to_fix(request.side));
        msg.fields[fix_tags::TAG_SENDER_COMP_ID] = config_.sender_comp_id;
        msg.fields[fix_tags::TAG_TARGET_COMP_ID] = config_.target_comp_id;

        return msg;
    }

    ExecutionReport FixAdapter::exec_from_fix(const FixMessage &msg) const
    {
        ExecutionReport report{};

        auto get = [&](int tag) -> std::string
        {
            auto it = msg.fields.find(tag);
            return (it != msg.fields.end()) ? it->second : std::string{};
        };

        // Order and execution identifiers.
        const std::string cl_ord_id_str = get(FixMessage::TAG_CL_ORD_ID);
        const std::string order_id_str = get(FixMessage::TAG_ORDER_ID);
        const std::string exec_id_str = get(FixMessage::TAG_EXEC_ID);

        report.order_id = order_id_str.empty()
                              ? id_from_str(cl_ord_id_str)
                              : id_from_str(order_id_str);
        report.exec_id = id_from_str(exec_id_str);

        // State from OrdStatus (tag 39).
        report.state = ord_status_from_fix(get(FixMessage::TAG_ORD_STATUS));

        // Fill details.
        report.last_price = price_from_str(get(FixMessage::TAG_LAST_PX));
        report.last_quantity = qty_from_str(get(FixMessage::TAG_LAST_QTY));
        report.avg_price = price_from_str(get(FixMessage::TAG_AVG_PX));
        report.cum_quantity = qty_from_str(get(FixMessage::TAG_CUM_QTY));
        report.leaves_quantity = qty_from_str(get(FixMessage::TAG_LEAVES_QTY));

        // Informational text.
        const std::string text = get(FixMessage::TAG_TEXT);
        if (!text.empty())
        {
            report.text = text;
        }

        report.timestamp = std::chrono::steady_clock::now();

        return report;
    }

} // namespace sor::connectors
