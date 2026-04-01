#include "ui/panels/controls.h"
#include "core/types.h"

namespace sor::ui
{

    ControlsPanel::ControlsPanel(SorController &controller)
        : controller_(controller) {}

    void ControlsPanel::render()
    {
        ImGui::Begin("Controls");

        // Gateway status
        bool running = controller_.is_running();
        ImGui::TextColored(running ? ImVec4(0.2f,1.0f,0.2f,1.0f) : ImVec4(1.0f,0.3f,0.3f,1.0f),
                           "Gateway: %s", running ? "RUNNING" : "STOPPED");

        // Kill switch
        bool kill_active = controller_.is_kill_switch_active();
        ImGui::TextColored(kill_active ? ImVec4(1.0f,0.0f,0.0f,1.0f) : ImVec4(0.2f,1.0f,0.2f,1.0f),
                           "Kill Switch: %s", kill_active ? "ACTIVE" : "OFF");
        ImGui::SameLine();
        if (ImGui::Button(kill_active ? "Deactivate" : "Activate"))
            controller_.toggle_kill_switch();

        ImGui::Separator();

        // Gateway stats
        auto gw_stats = controller_.get_gateway_stats();
        ImGui::Text("Gateway Statistics");
        if (ImGui::BeginTable("gw_stats", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
        {
            ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::Text("Orders Submitted");
            ImGui::TableNextColumn(); ImGui::Text("%lu", static_cast<unsigned long>(gw_stats.orders_submitted));
            ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::Text("Orders Routed");
            ImGui::TableNextColumn(); ImGui::Text("%lu", static_cast<unsigned long>(gw_stats.orders_routed));
            ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::Text("Orders Completed");
            ImGui::TableNextColumn(); ImGui::Text("%lu", static_cast<unsigned long>(gw_stats.orders_completed));
            ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::Text("Orders Rejected");
            ImGui::TableNextColumn(); ImGui::Text("%lu", static_cast<unsigned long>(gw_stats.orders_rejected));
            ImGui::EndTable();
        }

        ImGui::Separator();

        // Execution stats
        auto exec_stats = controller_.get_execution_stats();
        ImGui::Text("Execution Statistics");
        if (ImGui::BeginTable("exec_stats", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
        {
            ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::Text("Total Fills");
            ImGui::TableNextColumn(); ImGui::Text("%lu", static_cast<unsigned long>(exec_stats.total_fills));
            ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::Text("Partial Fills");
            ImGui::TableNextColumn(); ImGui::Text("%lu", static_cast<unsigned long>(exec_stats.total_partial_fills));
            ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::Text("Total Rejects");
            ImGui::TableNextColumn(); ImGui::Text("%lu", static_cast<unsigned long>(exec_stats.total_rejects));
            ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::Text("Total Cancels");
            ImGui::TableNextColumn(); ImGui::Text("%lu", static_cast<unsigned long>(exec_stats.total_cancels));
            ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::Text("Reroutes");
            ImGui::TableNextColumn(); ImGui::Text("%lu", static_cast<unsigned long>(exec_stats.reroutes));
            ImGui::EndTable();
        }

        ImGui::Separator();

        // Position info per watched symbol
        ImGui::Text("Positions");
        for (const auto &sym : controller_.watched_symbols())
        {
            auto pos = controller_.get_position(sym);
            if (pos.net_quantity == 0 && pos.open_order_count == 0) continue;

            if (ImGui::TreeNode(sym.c_str()))
            {
                ImGui::Text("Net Qty:     %ld", static_cast<long>(pos.net_quantity));
                ImGui::Text("Avg Entry:   %.2f", to_double(pos.avg_entry_price));
                ImGui::Text("Realized PnL: %.2f", to_double(pos.realized_pnl));
                ImGui::Text("Open Orders: %d", pos.open_order_count);
                ImGui::TreePop();
            }
        }

        ImGui::End();
    }

} // namespace sor::ui
