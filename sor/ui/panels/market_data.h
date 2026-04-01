#pragma once

#include "ui/sor_controller.h"
#include <imgui.h>

namespace sor::ui
{

    class MarketDataPanel
    {
    public:
        explicit MarketDataPanel(SorController &controller);
        void render();

    private:
        SorController &controller_;
        char add_symbol_buf_[17]{};
        bool show_depth_{false};
    };

} // namespace sor::ui
