#pragma once

#include "ui/sor_controller.h"
#include <imgui.h>
#include <deque>

namespace sor::ui
{

    class ExecutionPanel
    {
    public:
        void render();
        void on_fill_events(const std::vector<FillEvent> &events);
        void on_completion_events(const std::vector<CompletionEvent> &events);

    private:
        struct Entry
        {
            std::string text;
            bool is_fill; // true=fill, false=completion
        };

        std::deque<Entry> entries_;
        bool auto_scroll_{true};
        static constexpr size_t MAX_ENTRIES = 500;
    };

} // namespace sor::ui
