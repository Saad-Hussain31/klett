#pragma once

#include "ui/sor_controller.h"

#include <string>

struct SDL_Window;
using SDL_GLContext = void *;

namespace sor::ui
{

    class OrderEntryPanel;
    class OrderBookPanel;
    class MarketDataPanel;
    class ExecutionPanel;
    class LogPanel;
    class ControlsPanel;

    class SorUI
    {
    public:
        explicit SorUI(SorController &controller);
        ~SorUI();

        SorUI(const SorUI &) = delete;
        SorUI &operator=(const SorUI &) = delete;

        bool initialize(const std::string &title = "SOR Trading UI",
                        int width = 1600, int height = 900);

        /// Blocking render loop. Returns when the window is closed.
        void run();

        void shutdown();

    private:
        void render_frame();

        SorController &controller_;

        SDL_Window *window_{nullptr};
        SDL_GLContext gl_context_{nullptr};

        std::unique_ptr<OrderEntryPanel> order_entry_;
        std::unique_ptr<OrderBookPanel> order_book_;
        std::unique_ptr<MarketDataPanel> market_data_;
        std::unique_ptr<ExecutionPanel> execution_;
        std::unique_ptr<LogPanel> log_panel_;
        std::unique_ptr<ControlsPanel> controls_;
    };

} // namespace sor::ui
