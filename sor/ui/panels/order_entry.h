#pragma once

#include "ui/sor_controller.h"
#include <imgui.h>
#include <string>

namespace sor::ui
{

    class OrderEntryPanel
    {
    public:
        explicit OrderEntryPanel(SorController &controller);
        void render();

    private:
        SorController &controller_;

        // Form state
        char symbol_buf_[17]{"AAPL"};
        int side_idx_{0};
        int type_idx_{0};
        int tif_idx_{0};
        int strategy_idx_{0};
        int quantity_{100};
        double price_{150.0};
        std::string status_text_;
    };

} // namespace sor::ui
