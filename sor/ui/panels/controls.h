#pragma once

#include "ui/sor_controller.h"
#include <imgui.h>

namespace sor::ui
{

    class ControlsPanel
    {
    public:
        explicit ControlsPanel(SorController &controller);
        void render();

    private:
        SorController &controller_;
    };

} // namespace sor::ui
