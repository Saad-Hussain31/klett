#include "ui/panels/order_book.h"
#include "core/types.h"

namespace sor::ui
{

    static const char *state_str(OrderState s)
    {
        switch (s)
        {
        case OrderState::New:
            return "New";
        case OrderState::PendingNew:
            return "PendingNew";
        case OrderState::Accepted:
            return "Accepted";
        case OrderState::PartiallyFilled:
            return "Partial";
        case OrderState::Filled:
            return "Filled";
        case OrderState::PendingCancel:
            return "PendingCancel";
        case OrderState::Canceled:
            return "Canceled";
        case OrderState::Rejected:
            return "Rejected";
        case OrderState::Expired:
            return "Expired";
        case OrderState::PendingReplace:
            return "PendingReplace";
        }
        return "?";
    }

    static ImVec4 state_color(OrderState s)
    {
        switch (s)
        {
        case OrderState::Filled:
            return {0.2f, 1.0f, 0.2f, 1.0f};
        case OrderState::PartiallyFilled:
            return {1.0f, 1.0f, 0.2f, 1.0f};
        case OrderState::Rejected:
            return {1.0f, 0.3f, 0.3f, 1.0f};
        case OrderState::Canceled:
            return {0.6f, 0.6f, 0.6f, 1.0f};
        default:
            return {1.0f, 1.0f, 1.0f, 1.0f};
        }
    }

    static const char *side_str(Side s)
    {
        return s == Side::Buy ? "Buy" : "Sell";
    }

    static const char *type_str(OrderType t)
    {
        switch (t)
        {
        case OrderType::Limit:
            return "Limit";
        case OrderType::Market:
            return "Market";
        case OrderType::IOC:
            return "IOC";
        case OrderType::FOK:
            return "FOK";
        }
        return "?";
    }

    static const char *strategy_str(RoutingStrategy s)
    {
        switch (s)
        {
        case RoutingStrategy::BestPrice:
            return "BestPrice";
        case RoutingStrategy::LiquiditySweep:
            return "Sweep";
        case RoutingStrategy::SmartIOC:
            return "SmartIOC";
        case RoutingStrategy::VWAP:
            return "VWAP";
        }
        return "?";
    }

    OrderBookPanel::OrderBookPanel(SorController &controller)
        : controller_(controller) {}

    void OrderBookPanel::render()
    {
        ImGui::Begin("Order Blotter");

        ImGui::Checkbox("Show Children", &show_children_);
        ImGui::Separator();

        const int col_count = 11;
        if (ImGui::BeginTable("orders", col_count,
                              ImGuiTableFlags_Borders |
                                  ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_Resizable |
                                  ImGuiTableFlags_ScrollY))
        {
            ImGui::TableSetupColumn("ID");
            ImGui::TableSetupColumn("Symbol");
            ImGui::TableSetupColumn("Side");
            ImGui::TableSetupColumn("Type");
            ImGui::TableSetupColumn("State");
            ImGui::TableSetupColumn("Qty");
            ImGui::TableSetupColumn("Filled");
            ImGui::TableSetupColumn("Remaining");
            ImGui::TableSetupColumn("AvgPx");
            ImGui::TableSetupColumn("Venue");
            ImGui::TableSetupColumn("Strategy");
            ImGui::TableHeadersRow();

            auto order_ids = controller_.get_tracked_order_ids();

            for (OrderId oid : order_ids)
            {
                auto snap = controller_.get_order_snapshot(oid);
                if (!snap)
                    continue;
                const Order &o = *snap;

                ImGui::TableNextRow();
                ImVec4 color = state_color(o.state);

                ImGui::TableNextColumn();
                ImGui::TextColored(color, "%lu", static_cast<unsigned long>(o.id));
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(o.symbol.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(side_str(o.side));
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(type_str(o.type));
                ImGui::TableNextColumn();
                ImGui::TextColored(color, "%s", state_str(o.state));
                ImGui::TableNextColumn();
                ImGui::Text("%ld", static_cast<long>(o.quantity));
                ImGui::TableNextColumn();
                ImGui::Text("%ld", static_cast<long>(o.filled_quantity));
                ImGui::TableNextColumn();
                ImGui::Text("%ld", static_cast<long>(o.remaining_quantity));
                ImGui::TableNextColumn();
                ImGui::Text("%.2f", to_double(o.avg_fill_price));
                ImGui::TableNextColumn();
                ImGui::Text("%u", o.target_venue);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(strategy_str(o.strategy));

                // Right-click cancel
                if (o.is_active())
                {
                    char popup_id[32];
                    std::snprintf(popup_id, sizeof(popup_id), "ctx_%lu", static_cast<unsigned long>(o.id));
                    if (ImGui::BeginPopupContextItem(popup_id))
                    {
                        if (ImGui::MenuItem("Cancel Order"))
                            controller_.cancel_order(o.id);
                        ImGui::EndPopup();
                    }
                }

                // Show children
                if (show_children_)
                {
                    auto children = controller_.get_children(oid);
                    for (OrderId cid : children)
                    {
                        auto csnap = controller_.get_order_snapshot(cid);
                        if (!csnap)
                            continue;
                        const Order &c = *csnap;
                        ImVec4 ccolor = state_color(c.state);

                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::TextColored({0.6f, 0.6f, 0.8f, 1.0f}, "  %lu", static_cast<unsigned long>(c.id));
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(c.symbol.c_str());
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(side_str(c.side));
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(type_str(c.type));
                        ImGui::TableNextColumn();
                        ImGui::TextColored(ccolor, "%s", state_str(c.state));
                        ImGui::TableNextColumn();
                        ImGui::Text("%ld", static_cast<long>(c.quantity));
                        ImGui::TableNextColumn();
                        ImGui::Text("%ld", static_cast<long>(c.filled_quantity));
                        ImGui::TableNextColumn();
                        ImGui::Text("%ld", static_cast<long>(c.remaining_quantity));
                        ImGui::TableNextColumn();
                        ImGui::Text("%.2f", to_double(c.avg_fill_price));
                        ImGui::TableNextColumn();
                        ImGui::Text("%u", c.target_venue);
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(strategy_str(c.strategy));
                    }
                }
            }
            ImGui::EndTable();
        }

        ImGui::End();
    }

} // namespace sor::ui
