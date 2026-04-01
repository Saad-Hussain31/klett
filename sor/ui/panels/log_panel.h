#pragma once

#include "ui/sor_controller.h"
#include <imgui.h>
#include <deque>
#include <string>

namespace sor::ui
{

    class LogPanel
    {
    public:
        void render();
        void on_log_messages(const std::vector<LogMessage> &messages);

    private:
        struct Entry
        {
            std::string text;
            int level;
        };

        std::deque<Entry> entries_;
        bool auto_scroll_{true};
        bool show_trace_{false};
        bool show_debug_{true};
        bool show_info_{true};
        bool show_warn_{true};
        bool show_error_{true};
        bool show_critical_{true};
        ImGuiTextFilter filter_;
        static constexpr size_t MAX_ENTRIES = 2000;
    };

} // namespace sor::ui
