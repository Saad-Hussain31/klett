#include "ui/panels/execution.h"
#include "core/types.h"
#include <cstdio>

namespace sor::ui
{

    void ExecutionPanel::on_fill_events(const std::vector<FillEvent> &events)
    {
        for (const auto &ev : events)
        {
            char buf[256];
            std::snprintf(buf, sizeof(buf),
                          "FILL  order=%lu  %s %s  qty=%ld  px=%.2f  cum=%ld  leaves=%ld  venue=%u",
                          static_cast<unsigned long>(ev.order_id),
                          ev.side == Side::Buy ? "BUY" : "SELL",
                          ev.symbol.c_str(),
                          static_cast<long>(ev.quantity),
                          to_double(ev.price),
                          static_cast<long>(ev.cum_quantity),
                          static_cast<long>(ev.leaves_quantity),
                          ev.venue_id);
            entries_.push_back({buf, true});
            if (entries_.size() > MAX_ENTRIES)
                entries_.pop_front();
        }
    }

    void ExecutionPanel::on_completion_events(const std::vector<CompletionEvent> &events)
    {
        for (const auto &ev : events)
        {
            char buf[256];
            std::snprintf(buf, sizeof(buf),
                          "COMPLETE  order=%lu  %s  filled=%ld  avg_px=%.2f",
                          static_cast<unsigned long>(ev.order_id),
                          ev.symbol.c_str(),
                          static_cast<long>(ev.filled_quantity),
                          to_double(ev.avg_fill_price));
            entries_.push_back({buf, false});
            if (entries_.size() > MAX_ENTRIES)
                entries_.pop_front();
        }
    }

    void ExecutionPanel::render()
    {
        ImGui::Begin("Execution Events");

        ImGui::Checkbox("Auto-scroll", &auto_scroll_);
        ImGui::SameLine();
        ImGui::Text("(%zu events)", entries_.size());
        ImGui::Separator();

        if (ImGui::BeginChild("exec_scroll", ImVec2(0, 0), ImGuiChildFlags_None,
                              ImGuiWindowFlags_HorizontalScrollbar))
        {
            for (const auto &entry : entries_)
            {
                ImVec4 color = entry.is_fill
                    ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f)   // green for fills
                    : ImVec4(0.3f, 0.6f, 1.0f, 1.0f);   // blue for completions
                ImGui::TextColored(color, "%s", entry.text.c_str());
            }

            if (auto_scroll_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();

        ImGui::End();
    }

} // namespace sor::ui
