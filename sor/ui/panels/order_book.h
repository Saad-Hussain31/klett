#pragma once

#include "ui/sor_controller.h"
#include <imgui.h>

namespace sor::ui
{

    class OrderBookPanel
    {
    public:
        explicit OrderBookPanel(SorController &controller);
        void render();

    private:
        SorController &controller_;
        bool show_children_{true};
    };

} // namespace sor::ui
