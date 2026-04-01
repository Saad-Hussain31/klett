#include "ui/panels/order_entry.h"
#include "core/types.h"
#include <cstdio>

namespace sor::ui
{

    static const char *const SIDE_NAMES[] = {"Buy", "Sell"};
    static const char *const TYPE_NAMES[] = {"Limit", "Market", "IOC", "FOK"};
    static const char *const TIF_NAMES[] = {"GTC", "IOC", "FOK", "GTD", "DAY"};
    static const char *const STRATEGY_NAMES[] = {"BestPrice", "LiquiditySweep", "SmartIOC", "VWAP"};

    OrderEntryPanel::OrderEntryPanel(SorController &controller)
        : controller_(controller) {}

    void OrderEntryPanel::render()
    {
        ImGui::Begin("Order Entry");

        ImGui::InputText("Symbol", symbol_buf_, sizeof(symbol_buf_));

        ImGui::Combo("Side", &side_idx_, SIDE_NAMES, IM_ARRAYSIZE(SIDE_NAMES));
        ImGui::Combo("Type", &type_idx_, TYPE_NAMES, IM_ARRAYSIZE(TYPE_NAMES));
        ImGui::Combo("TIF", &tif_idx_, TIF_NAMES, IM_ARRAYSIZE(TIF_NAMES));
        ImGui::Combo("Strategy", &strategy_idx_, STRATEGY_NAMES, IM_ARRAYSIZE(STRATEGY_NAMES));

        ImGui::InputInt("Quantity", &quantity_);
        if (quantity_ < 1) quantity_ = 1;

        // Disable price for Market orders
        bool is_market = (type_idx_ == 1);
        if (is_market) ImGui::BeginDisabled();
        ImGui::InputDouble("Price", &price_, 0.01, 1.0, "%.2f");
        if (is_market) ImGui::EndDisabled();

        // Quick-fill from NBBO
        Symbol sym(symbol_buf_);
        auto nbbo = controller_.get_nbbo(sym);
        if (nbbo.valid())
        {
            ImGui::Text("NBBO: %.2f x %.2f", to_double(nbbo.best_bid), to_double(nbbo.best_ask));
            ImGui::SameLine();
            if (ImGui::SmallButton("Bid"))
                price_ = to_double(nbbo.best_bid);
            ImGui::SameLine();
            if (ImGui::SmallButton("Ask"))
                price_ = to_double(nbbo.best_ask);
            ImGui::SameLine();
            if (ImGui::SmallButton("Mid"))
                price_ = to_double(nbbo.mid_price());
        }

        ImGui::Separator();

        if (ImGui::Button("Submit Order", ImVec2(-1, 30)))
        {
            OrderParams params;
            params.symbol = symbol_buf_;
            params.side = static_cast<Side>(side_idx_);
            params.quantity = quantity_;
            params.type = static_cast<OrderType>(type_idx_);
            params.price = is_market ? 0.0 : price_;
            params.tif = static_cast<TimeInForce>(tif_idx_);
            params.strategy = static_cast<RoutingStrategy>(strategy_idx_);

            OrderId id = controller_.submit_order(params);
            if (id != INVALID_ORDER_ID)
            {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "Order %lu submitted", static_cast<unsigned long>(id));
                status_text_ = buf;
            }
            else
            {
                status_text_ = "Order submission failed";
            }
        }

        if (!status_text_.empty())
        {
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", status_text_.c_str());
        }

        ImGui::End();
    }

} // namespace sor::ui
