#include "ui/sor_ui.h"
#include "ui/sor_controller.h"

#include "ui/panels/order_entry.h"
#include "ui/panels/order_book.h"
#include "ui/panels/market_data.h"
#include "ui/panels/execution.h"
#include "ui/panels/log_panel.h"
#include "ui/panels/controls.h"

#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_opengl3.h>

#include <SDL.h>
#if defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include <iostream>

namespace sor::ui
{

    SorUI::SorUI(SorController &controller)
        : controller_(controller) {}

    SorUI::~SorUI()
    {
        shutdown();
    }

    bool SorUI::initialize(const std::string &title, int width, int height)
    {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
        {
            std::cerr << "SDL_Init error: " << SDL_GetError() << "\n";
            return false;
        }

        // GL 3.3 Core
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

        window_ = SDL_CreateWindow(
            title.c_str(),
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            width, height,
            SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);

        if (!window_)
        {
            std::cerr << "SDL_CreateWindow error: " << SDL_GetError() << "\n";
            return false;
        }

        gl_context_ = SDL_GL_CreateContext(window_);
        SDL_GL_MakeCurrent(window_, gl_context_);
        SDL_GL_SetSwapInterval(1); // vsync

        // ImGui setup
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGuiIO &io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        ImGui::StyleColorsDark();

        // Adjust style
        ImGuiStyle &style = ImGui::GetStyle();
        style.WindowRounding = 4.0f;
        style.FrameRounding = 2.0f;
        style.GrabRounding = 2.0f;

        ImGui_ImplSDL2_InitForOpenGL(window_, gl_context_);
        ImGui_ImplOpenGL3_Init("#version 330");

        // Create panels
        order_entry_ = std::make_unique<OrderEntryPanel>(controller_);
        order_book_ = std::make_unique<OrderBookPanel>(controller_);
        market_data_ = std::make_unique<MarketDataPanel>(controller_);
        execution_ = std::make_unique<ExecutionPanel>();
        log_panel_ = std::make_unique<LogPanel>();
        controls_ = std::make_unique<ControlsPanel>(controller_);

        return true;
    }

    void SorUI::run()
    {
        bool running = true;

        while (running)
        {
            SDL_Event event;
            while (SDL_PollEvent(&event))
            {
                ImGui_ImplSDL2_ProcessEvent(&event);
                if (event.type == SDL_QUIT)
                    running = false;
                if (event.type == SDL_WINDOWEVENT &&
                    event.window.event == SDL_WINDOWEVENT_CLOSE &&
                    event.window.windowID == SDL_GetWindowID(window_))
                    running = false;
            }

            render_frame();
        }
    }

    void SorUI::render_frame()
    {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // Full-viewport dockspace
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

        // Drain events once per frame
        auto fills = controller_.drain_fill_events();
        auto completions = controller_.drain_completion_events();
        auto logs = controller_.drain_log_messages();

        // Feed events to panels
        if (!fills.empty())
            execution_->on_fill_events(fills);
        if (!completions.empty())
            execution_->on_completion_events(completions);
        if (!logs.empty())
            log_panel_->on_log_messages(logs);

        // Render all panels
        order_entry_->render();
        order_book_->render();
        market_data_->render();
        execution_->render();
        log_panel_->render();
        controls_->render();

        // Render
        ImGui::Render();
        int display_w, display_h;
        SDL_GL_GetDrawableSize(window_, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        SDL_GL_SwapWindow(window_);
    }

    void SorUI::shutdown()
    {
        order_entry_.reset();
        order_book_.reset();
        market_data_.reset();
        execution_.reset();
        log_panel_.reset();
        controls_.reset();

        if (gl_context_)
        {
            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplSDL2_Shutdown();
            ImGui::DestroyContext();

            SDL_GL_DeleteContext(gl_context_);
            gl_context_ = nullptr;
        }

        if (window_)
        {
            SDL_DestroyWindow(window_);
            window_ = nullptr;
        }

        SDL_Quit();
    }

} // namespace sor::ui
