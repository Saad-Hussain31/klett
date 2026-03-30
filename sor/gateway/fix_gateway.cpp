#include "gateway/fix_gateway.h"
#include <sstream>
#include <chrono>
#include <cstdlib>

namespace sor::gateway
{

    // ---------------------------------------------------------------------------
    // Construction
    // ---------------------------------------------------------------------------

    FixGateway::FixGateway(Gateway &gateway)
        : gateway_(gateway)
    {
        // Register a fill callback so we can generate outbound FIX reports.
        gateway_.execution_handler().set_fill_callback(
            [this](const Order &order, const ExecutionReport &report)
            {
                if (send_callback_)
                {
                    send_callback_(build_execution_report(order, report));
                }
            });
    }

    // ---------------------------------------------------------------------------
    // Inbound FIX message handling
    // ---------------------------------------------------------------------------

    void FixGateway::on_fix_message(const std::string &raw_fix)
    {
        // Parse the MsgType (tag 35).
        const std::string msg_type = get_tag(raw_fix, "35");

        if (msg_type == "D")
        {
            // NewOrderSingle.
            Order order{};

            // ClOrdID (tag 11) -> client_order_id.
            const std::string cl_ord_id = get_tag(raw_fix, "11");
            if (!cl_ord_id.empty())
            {
                order.client_order_id = static_cast<OrderId>(std::stoull(cl_ord_id));
            }

            // Symbol (tag 55).
            order.symbol = Symbol(get_tag(raw_fix, "55"));

            // Side (tag 54): 1 = Buy, 2 = Sell.
            const std::string side_str = get_tag(raw_fix, "54");
            order.side = (side_str == "2") ? Side::Sell : Side::Buy;

            // OrderQty (tag 38).
            const std::string qty_str = get_tag(raw_fix, "38");
            if (!qty_str.empty())
            {
                order.quantity = std::stoll(qty_str);
                order.remaining_quantity = order.quantity;
            }

            // Price (tag 44).
            const std::string price_str = get_tag(raw_fix, "44");
            if (!price_str.empty())
            {
                order.price = to_price(std::stod(price_str));
            }

            // OrdType (tag 40): 1 = Market, 2 = Limit.
            const std::string ord_type = get_tag(raw_fix, "40");
            if (ord_type == "1")
            {
                order.type = OrderType::Market;
            }
            else
            {
                order.type = OrderType::Limit;
            }

            // TimeInForce (tag 59): 0 = Day, 1 = GTC, 3 = IOC, 4 = FOK.
            const std::string tif_str = get_tag(raw_fix, "59");
            if (tif_str == "0")
                order.tif = TimeInForce::DAY;
            else if (tif_str == "1")
                order.tif = TimeInForce::GTC;
            else if (tif_str == "3")
                order.tif = TimeInForce::IOC;
            else if (tif_str == "4")
                order.tif = TimeInForce::FOK;

            // ClientID (tag 109).
            const std::string client_id = get_tag(raw_fix, "109");
            if (!client_id.empty())
            {
                order.client_id = ClientId(client_id);
            }

            gateway_.submit_order(std::move(order));
        }
        else if (msg_type == "F")
        {
            // OrderCancelRequest.
            CancelRequest cancel{};

            const std::string orig_cl = get_tag(raw_fix, "41");
            if (!orig_cl.empty())
            {
                cancel.order_id = static_cast<OrderId>(std::stoull(orig_cl));
            }

            const std::string cl_ord = get_tag(raw_fix, "11");
            if (!cl_ord.empty())
            {
                cancel.client_order_id = static_cast<OrderId>(std::stoull(cl_ord));
            }

            cancel.symbol = Symbol(get_tag(raw_fix, "55"));

            const std::string side_str = get_tag(raw_fix, "54");
            cancel.side = (side_str == "2") ? Side::Sell : Side::Buy;

            gateway_.cancel_order(std::move(cancel));
        }
        // Other message types are silently ignored in this simplified gateway.
    }

    void FixGateway::set_send_callback(std::function<void(const std::string &)> cb)
    {
        send_callback_ = std::move(cb);
    }

    // ---------------------------------------------------------------------------
    // FIX tag parsing (simplified SOH-delimited)
    // ---------------------------------------------------------------------------

    std::string FixGateway::get_tag(const std::string &msg, const std::string &tag)
    {
        // FIX messages use SOH (0x01) as delimiter.  For logging/testing,
        // the pipe character '|' is also accepted.
        const std::string search_soh = tag + "=";

        for (char delim : {'\x01', '|'})
        {
            std::string search = std::string(1, delim) + search_soh;

            // Check if the message starts with this tag (no leading delimiter).
            size_t pos = std::string::npos;
            if (msg.compare(0, search_soh.size(), search_soh) == 0)
            {
                pos = search_soh.size();
            }
            else
            {
                auto found = msg.find(search);
                if (found != std::string::npos)
                {
                    pos = found + search.size();
                }
            }

            if (pos != std::string::npos)
            {
                auto end = msg.find(delim, pos);
                if (end == std::string::npos)
                {
                    end = msg.size();
                }
                return msg.substr(pos, end - pos);
            }
        }

        return {};
    }

    // ---------------------------------------------------------------------------
    // Outbound FIX message construction
    // ---------------------------------------------------------------------------

    std::string FixGateway::build_execution_report(
        const Order &order, const ExecutionReport &report)
    {
        // Build a simplified FIX 4.2-style ExecutionReport (MsgType=8).
        // Uses SOH (0x01) as delimiter.
        constexpr char SOH = '\x01';

        std::ostringstream oss;
        oss << "35=8" << SOH;
        oss << "11=" << order.client_order_id << SOH;
        oss << "37=" << order.id << SOH;
        oss << "17=" << report.exec_id << SOH;
        oss << "55=" << order.symbol.c_str() << SOH;
        oss << "54=" << (order.side == Side::Buy ? "1" : "2") << SOH;

        // OrdStatus (tag 39).
        switch (report.state)
        {
        case OrderState::New:
            oss << "39=A" << SOH;
            break;
        case OrderState::Accepted:
            oss << "39=0" << SOH;
            break;
        case OrderState::PartiallyFilled:
            oss << "39=1" << SOH;
            break;
        case OrderState::Filled:
            oss << "39=2" << SOH;
            break;
        case OrderState::Canceled:
            oss << "39=4" << SOH;
            break;
        case OrderState::Rejected:
            oss << "39=8" << SOH;
            break;
        case OrderState::Expired:
            oss << "39=C" << SOH;
            break;
        case OrderState::PendingCancel:
            oss << "39=6" << SOH;
            break;
        case OrderState::PendingNew:
            oss << "39=A" << SOH;
            break;
        case OrderState::PendingReplace:
            oss << "39=E" << SOH;
            break;
        }

        oss << "31=" << to_double(report.last_price) << SOH;
        oss << "32=" << report.last_quantity << SOH;
        oss << "14=" << report.cum_quantity << SOH;
        oss << "6=" << to_double(report.avg_price) << SOH;
        oss << "151=" << report.leaves_quantity << SOH;

        if (!report.text.empty())
        {
            oss << "58=" << report.text.c_str() << SOH;
        }

        return oss.str();
    }

} // namespace sor::gateway
