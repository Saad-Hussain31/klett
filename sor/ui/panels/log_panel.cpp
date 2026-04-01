#include "ui/panels/log_panel.h"

namespace sor::ui
{

    void LogPanel::on_log_messages(const std::vector<LogMessage> &messages)
    {
        for (const auto &msg : messages)
        {
            entries_.push_back({msg.text, msg.level});
            if (entries_.size() > MAX_ENTRIES)
                entries_.pop_front();
        }
    }

    void LogPanel::render()
    {
        ImGui::Begin("Log");

        // Level filters
        ImGui::Checkbox("Trace", &show_trace_);  ImGui::SameLine();
        ImGui::Checkbox("Debug", &show_debug_);  ImGui::SameLine();
        ImGui::Checkbox("Info", &show_info_);     ImGui::SameLine();
        ImGui::Checkbox("Warn", &show_warn_);     ImGui::SameLine();
        ImGui::Checkbox("Error", &show_error_);   ImGui::SameLine();
        ImGui::Checkbox("Critical", &show_critical_); ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &auto_scroll_);

        filter_.Draw("Filter", -100.0f);
        ImGui::Separator();

        if (ImGui::BeginChild("log_scroll", ImVec2(0, 0), ImGuiChildFlags_None,
                              ImGuiWindowFlags_HorizontalScrollbar))
        {
            for (const auto &entry : entries_)
            {
                // Level filter (spdlog levels: 0=trace, 1=debug, 2=info, 3=warn, 4=error, 5=critical)
                switch (entry.level)
                {
                case 0: if (!show_trace_) continue; break;
                case 1: if (!show_debug_) continue; break;
                case 2: if (!show_info_) continue; break;
                case 3: if (!show_warn_) continue; break;
                case 4: if (!show_error_) continue; break;
                case 5: if (!show_critical_) continue; break;
                }

                if (!filter_.PassFilter(entry.text.c_str()))
                    continue;

                ImVec4 color;
                switch (entry.level)
                {
                case 0: color = {0.5f, 0.5f, 0.5f, 1.0f}; break; // trace: grey
                case 1: color = {0.6f, 0.6f, 0.8f, 1.0f}; break; // debug: light blue
                case 2: color = {1.0f, 1.0f, 1.0f, 1.0f}; break; // info: white
                case 3: color = {1.0f, 1.0f, 0.2f, 1.0f}; break; // warn: yellow
                case 4: color = {1.0f, 0.3f, 0.3f, 1.0f}; break; // error: red
                case 5: color = {1.0f, 0.0f, 0.0f, 1.0f}; break; // critical: bright red
                default: color = {1.0f, 1.0f, 1.0f, 1.0f}; break;
                }

                ImGui::TextColored(color, "%s", entry.text.c_str());
            }

            if (auto_scroll_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();

        ImGui::End();
    }

} // namespace sor::ui
