#include "ui_app.h"
#include "../core/logger.h"

#include <SDL.h>
#include <SDL_opengl.h>
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cmath>

namespace luft
{

    // ────────────────────────────────────────────────────
    // Helpers
    // ────────────────────────────────────────────────────

    static constexpr double kMpsToKnots = 1.94384;
    static constexpr double kKeyboardRate = 0.02; // control surface rate per frame from keyboard

    static const char *sim_state_color_label(SimState s)
    {
        switch (s)
        {
        case SimState::Running:
            return "Running";
        case SimState::Paused:
            return "Paused";
        case SimState::Stopped:
            return "Stopped";
        case SimState::Initialized:
            return "Ready";
        case SimState::Uninitialized:
            return "Not Init";
        case SimState::Error:
            return "ERROR";
        }
        return "Unknown";
    }

    static ImVec4 sim_state_color(SimState s)
    {
        switch (s)
        {
        case SimState::Running:
            return {0.2f, 0.9f, 0.2f, 1.0f};
        case SimState::Paused:
            return {1.0f, 0.9f, 0.2f, 1.0f};
        case SimState::Error:
            return {1.0f, 0.2f, 0.2f, 1.0f};
        case SimState::Stopped:
            return {0.6f, 0.6f, 0.6f, 1.0f};
        default:
            return {0.8f, 0.8f, 0.8f, 1.0f};
        }
    }

    // ────────────────────────────────────────────────────
    // Lifecycle
    // ────────────────────────────────────────────────────

    UiApp::~UiApp()
    {
        if (initialized_)
            shutdown();
    }

    bool UiApp::initialize(int width, int height, const char *title)
    {
        if (initialized_)
        {
            LOG_WARN("UiApp::initialize called when already initialized");
            return true;
        }

        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
        {
            LOG_ERROR("SDL_Init failed: %s", SDL_GetError());
            return false;
        }

        // Request OpenGL 3.3 core profile
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);

        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

        auto window_flags = static_cast<SDL_WindowFlags>(
            SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);

        window_ = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                   width, height, window_flags);
        if (!window_)
        {
            LOG_ERROR("SDL_CreateWindow failed: %s", SDL_GetError());
            SDL_Quit();
            return false;
        }

        gl_context_ = SDL_GL_CreateContext(window_);
        if (!gl_context_)
        {
            LOG_ERROR("SDL_GL_CreateContext failed: %s", SDL_GetError());
            SDL_DestroyWindow(window_);
            window_ = nullptr;
            SDL_Quit();
            return false;
        }

        SDL_GL_MakeCurrent(window_, gl_context_);
        SDL_GL_SetSwapInterval(1); // vsync

        // Initialize Dear ImGui
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        ImGuiIO &io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        ImGui::StyleColorsDark();

        // Tighten up the style for a dense instrument-panel look
        ImGuiStyle &style = ImGui::GetStyle();
        style.WindowRounding = 4.0f;
        style.FrameRounding = 2.0f;
        style.FramePadding = ImVec2(6, 3);
        style.ItemSpacing = ImVec2(8, 4);

        const char *glsl_version = "#version 330 core";
        ImGui_ImplSDL2_InitForOpenGL(window_, gl_context_);
        ImGui_ImplOpenGL3_Init(glsl_version);

        initialized_ = true;
        LOG_INFO("UiApp initialized (%dx%d, OpenGL 3.3 core)", width, height);
        return true;
    }

    void UiApp::shutdown()
    {
        if (!initialized_)
            return;

        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();

        if (gl_context_)
        {
            SDL_GL_DeleteContext(gl_context_);
            gl_context_ = nullptr;
        }
        if (window_)
        {
            SDL_DestroyWindow(window_);
            window_ = nullptr;
        }

        SDL_Quit();
        initialized_ = false;
        LOG_INFO("UiApp shut down");
    }

    // ────────────────────────────────────────────────────
    // Per-frame: events
    // ────────────────────────────────────────────────────

    bool UiApp::process_events()
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL2_ProcessEvent(&event);

            if (event.type == SDL_QUIT)
            {
                quit_requested_ = true;
                return false;
            }

            if (event.type == SDL_WINDOWEVENT &&
                event.window.event == SDL_WINDOWEVENT_CLOSE &&
                event.window.windowID == SDL_GetWindowID(window_))
            {
                quit_requested_ = true;
                return false;
            }
        }

        // Snapshot keyboard state (only when ImGui is not capturing keyboard)
        const ImGuiIO &io = ImGui::GetIO();
        if (!io.WantCaptureKeyboard)
        {
            const Uint8 *ks = SDL_GetKeyboardState(nullptr);
            keys_.w = ks[SDL_SCANCODE_W] != 0;
            keys_.s = ks[SDL_SCANCODE_S] != 0;
            keys_.a = ks[SDL_SCANCODE_A] != 0;
            keys_.d = ks[SDL_SCANCODE_D] != 0;
            keys_.q = ks[SDL_SCANCODE_Q] != 0;
            keys_.e = ks[SDL_SCANCODE_E] != 0;
            keys_.shift = ks[SDL_SCANCODE_LSHIFT] != 0 || ks[SDL_SCANCODE_RSHIFT] != 0;
            keys_.ctrl = ks[SDL_SCANCODE_LCTRL] != 0 || ks[SDL_SCANCODE_RCTRL] != 0;
            keys_.f = ks[SDL_SCANCODE_F] != 0;
        }
        else
        {
            keys_ = {};
        }

        // Throttle accumulator (persists between frames)
        if (keys_.shift)
            kb_throttle_ = std::min(kb_throttle_ + kKeyboardRate, 1.0);
        if (keys_.ctrl)
            kb_throttle_ = std::max(kb_throttle_ - kKeyboardRate, 0.0);

        return true;
    }

    // ────────────────────────────────────────────────────
    // Per-frame: ImGui frame
    // ────────────────────────────────────────────────────

    void UiApp::begin_frame()
    {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
    }

    void UiApp::end_frame()
    {
        ImGui::Render();

        const ImGuiIO &io = ImGui::GetIO();
        glViewport(0, 0, static_cast<int>(io.DisplaySize.x), static_cast<int>(io.DisplaySize.y));
        glClearColor(0.06f, 0.06f, 0.08f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window_);
    }

    // ────────────────────────────────────────────────────
    // Input bridge
    // ────────────────────────────────────────────────────

    void UiApp::get_keyboard_control_input(ControlInput &input) const
    {
        // Elevator: W = nose up (+1), S = nose down (-1)
        if (keys_.w)
            input.elevator = std::min(input.elevator + kKeyboardRate, 1.0);
        if (keys_.s)
            input.elevator = std::max(input.elevator - kKeyboardRate, -1.0);

        // Aileron: A = left roll (-1), D = right roll (+1)
        if (keys_.a)
            input.aileron = std::max(input.aileron - kKeyboardRate, -1.0);
        if (keys_.d)
            input.aileron = std::min(input.aileron + kKeyboardRate, 1.0);

        // Rudder: Q = left yaw (-1), E = right yaw (+1)
        if (keys_.q)
            input.rudder = std::max(input.rudder - kKeyboardRate, -1.0);
        if (keys_.e)
            input.rudder = std::min(input.rudder + kKeyboardRate, 1.0);

        // Throttle from persistent accumulator
        input.throttle = kb_throttle_;

        // Return surfaces towards center when keys released
        if (!keys_.w && !keys_.s)
        {
            if (input.elevator > 0.0)
                input.elevator = std::max(input.elevator - kKeyboardRate * 0.5, 0.0);
            if (input.elevator < 0.0)
                input.elevator = std::min(input.elevator + kKeyboardRate * 0.5, 0.0);
        }
        if (!keys_.a && !keys_.d)
        {
            if (input.aileron > 0.0)
                input.aileron = std::max(input.aileron - kKeyboardRate * 0.5, 0.0);
            if (input.aileron < 0.0)
                input.aileron = std::min(input.aileron + kKeyboardRate * 0.5, 0.0);
        }
        if (!keys_.q && !keys_.e)
        {
            if (input.rudder > 0.0)
                input.rudder = std::max(input.rudder - kKeyboardRate * 0.5, 0.0);
            if (input.rudder < 0.0)
                input.rudder = std::min(input.rudder + kKeyboardRate * 0.5, 0.0);
        }
    }

    // ────────────────────────────────────────────────────
    // Render: Telemetry panel
    // ────────────────────────────────────────────────────

    void UiApp::render_telemetry(const AircraftState &state, SimState sim_state, double sim_time)
    {
        ImGui::SetNextWindowSize(ImVec2(340, 0), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Telemetry"))
        {
            ImGui::End();
            return;
        }

        // Euler angles from quaternion
        Vec3 euler = state.orientation.to_euler();
        double roll_deg = euler.x * kRadToDeg;
        double pitch_deg = euler.y * kRadToDeg;
        double yaw_deg = euler.z * kRadToDeg;

        double ias_kts = state.airspeed * kMpsToKnots;

        // Altitude: NED z is down, so altitude = -position.z
        double alt = -state.position.z;

        // Sim state header
        ImGui::TextColored(sim_state_color(sim_state), "%s", sim_state_color_label(sim_state));
        ImGui::SameLine();
        ImGui::Text("  T: %.1f s", sim_time);

        ImGui::Separator();

        // Position
        if (ImGui::CollapsingHeader("Position", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Text("N: %.1f  E: %.1f  Alt: %.0f m", state.position.x, state.position.y, alt);
        }

        // Velocity
        if (ImGui::CollapsingHeader("Velocity", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Text("IAS: %.1f m/s (%.1f kts)", state.airspeed, ias_kts);
            ImGui::Text("Vx: %.1f  Vy: %.1f  Vz: %.1f m/s",
                        state.velocity_body.x, state.velocity_body.y, state.velocity_body.z);
        }

        // Orientation
        if (ImGui::CollapsingHeader("Orientation", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Text("Roll: %.1f%s  Pitch: %.1f%s  Yaw: %.1f%s",
                        roll_deg, "\xc2\xb0",
                        pitch_deg, "\xc2\xb0",
                        yaw_deg, "\xc2\xb0");
        }

        // Aero angles
        if (ImGui::CollapsingHeader("Aerodynamics", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Text("Alpha: %.2f%s  Beta: %.2f%s",
                        state.alpha * kRadToDeg, "\xc2\xb0",
                        state.beta * kRadToDeg, "\xc2\xb0");
            ImGui::Text("Qbar: %.1f Pa", state.dynamic_pressure);
        }

        // Engine
        if (ImGui::CollapsingHeader("Engine", ImGuiTreeNodeFlags_DefaultOpen))
        {
            // Show thrust as percentage of a reference max (2400 N from AircraftParams default)
            constexpr double kRefMaxThrust = 2400.0;
            float thrust_pct = static_cast<float>(state.thrust_current / kRefMaxThrust * 100.0);
            ImGui::Text("Thrust: %.0f N (%.0f%%)", state.thrust_current, static_cast<double>(thrust_pct));

            float fuel_frac = static_cast<float>(state.fuel_mass / 163.0);
            ImGui::Text("Fuel: %.1f kg", state.fuel_mass);
            ImGui::ProgressBar(fuel_frac, ImVec2(-1, 0), "");
        }

        ImGui::End();
    }

    // ────────────────────────────────────────────────────
    // Render: Controls panel
    // ────────────────────────────────────────────────────

    void UiApp::render_controls(ControlInput &input)
    {
        ImGui::SetNextWindowSize(ImVec2(300, 0), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Controls"))
        {
            ImGui::End();
            return;
        }

        // Cast to float for ImGui sliders, write back
        float elevator = static_cast<float>(input.elevator);
        float aileron = static_cast<float>(input.aileron);
        float rudder = static_cast<float>(input.rudder);
        float throttle = static_cast<float>(input.throttle);
        float flaps = static_cast<float>(input.flaps);
        float trim = static_cast<float>(input.trim);

        ImGui::SliderFloat("Elevator", &elevator, -1.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Aileron", &aileron, -1.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Rudder", &rudder, -1.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Throttle", &throttle, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Flaps", &flaps, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Trim", &trim, -1.0f, 1.0f, "%.2f");

        input.elevator = static_cast<double>(elevator);
        input.aileron = static_cast<double>(aileron);
        input.rudder = static_cast<double>(rudder);
        input.throttle = static_cast<double>(throttle);
        input.flaps = static_cast<double>(flaps);
        input.trim = static_cast<double>(trim);

        ImGui::Separator();
        ImGui::TextDisabled("Keys: W/S elevator, A/D aileron");
        ImGui::TextDisabled("Q/E rudder, Shift/Ctrl throttle");

        ImGui::End();
    }

    // ────────────────────────────────────────────────────
    // Render: Sim control buttons
    // ────────────────────────────────────────────────────

    void UiApp::render_sim_controls(SimState current_state,
                                    bool *start_clicked,
                                    bool *pause_clicked,
                                    bool *resume_clicked,
                                    bool *reset_clicked,
                                    bool *stop_clicked)
    {
        ImGui::SetNextWindowSize(ImVec2(260, 0), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Simulation"))
        {
            ImGui::End();
            return;
        }

        // Status indicator
        ImGui::TextColored(sim_state_color(current_state), "State: %s", sim_state_name(current_state));
        ImGui::Separator();

        // Button enable/disable logic based on current state
        bool can_start = (current_state == SimState::Initialized ||
                          current_state == SimState::Stopped);
        bool can_pause = (current_state == SimState::Running);
        bool can_resume = (current_state == SimState::Paused);
        bool can_reset = (current_state != SimState::Uninitialized);
        bool can_stop = (current_state == SimState::Running ||
                         current_state == SimState::Paused);

        if (!can_start)
            ImGui::BeginDisabled();
        if (ImGui::Button("Start", ImVec2(70, 0)) && start_clicked)
            *start_clicked = true;
        if (!can_start)
            ImGui::EndDisabled();

        ImGui::SameLine();
        if (!can_pause)
            ImGui::BeginDisabled();
        if (ImGui::Button("Pause", ImVec2(70, 0)) && pause_clicked)
            *pause_clicked = true;
        if (!can_pause)
            ImGui::EndDisabled();

        ImGui::SameLine();
        if (!can_resume)
            ImGui::BeginDisabled();
        if (ImGui::Button("Resume", ImVec2(70, 0)) && resume_clicked)
            *resume_clicked = true;
        if (!can_resume)
            ImGui::EndDisabled();

        if (!can_reset)
            ImGui::BeginDisabled();
        if (ImGui::Button("Reset", ImVec2(70, 0)) && reset_clicked)
            *reset_clicked = true;
        if (!can_reset)
            ImGui::EndDisabled();

        ImGui::SameLine();
        if (!can_stop)
            ImGui::BeginDisabled();
        if (ImGui::Button("Stop", ImVec2(70, 0)) && stop_clicked)
            *stop_clicked = true;
        if (!can_stop)
            ImGui::EndDisabled();

        ImGui::End();
    }

    // ────────────────────────────────────────────────────
    // Render: Connection status
    // ────────────────────────────────────────────────────

    void UiApp::render_connection_status(int telemetry_clients, int command_clients)
    {
        ImGui::SetNextWindowSize(ImVec2(220, 0), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Network"))
        {
            ImGui::End();
            return;
        }

        ImGui::Text("Telemetry clients: %d", telemetry_clients);
        ImGui::Text("Command clients:   %d", command_clients);

        int total = telemetry_clients + command_clients;
        if (total > 0)
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.2f, 1.0f), "Connected");
        else
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No clients");

        ImGui::End();
    }

    // ────────────────────────────────────────────────────
    // Render: Log panel
    // ────────────────────────────────────────────────────

    void UiApp::render_log_panel()
    {
        ImGui::SetNextWindowSize(ImVec2(500, 200), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Log"))
        {
            ImGui::End();
            return;
        }

        if (ImGui::Button("Clear"))
        {
            std::lock_guard<std::mutex> lock(log_mutex_);
            log_count_ = 0;
            log_write_pos_ = 0;
        }

        ImGui::SameLine();
        bool auto_scroll = log_scroll_to_bottom_;
        ImGui::Checkbox("Auto-scroll", &auto_scroll);
        log_scroll_to_bottom_ = auto_scroll;

        ImGui::Separator();

        ImGui::BeginChild("LogScrollRegion", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

        {
            std::lock_guard<std::mutex> lock(log_mutex_);
            int count = log_count_;
            int start = 0;
            if (count >= kMaxLogEntries)
            {
                // Ring buffer is full; oldest entry is at write_pos_
                start = log_write_pos_;
                count = kMaxLogEntries;
            }

            for (int i = 0; i < count; ++i)
            {
                int idx = (start + i) % kMaxLogEntries;
                const char *line = log_buffer_[static_cast<size_t>(idx)].text;

                // Color by log level prefix
                ImVec4 color = {0.8f, 0.8f, 0.8f, 1.0f};
                if (std::strstr(line, "ERROR") || std::strstr(line, "FATAL"))
                    color = {1.0f, 0.3f, 0.3f, 1.0f};
                else if (std::strstr(line, "WARN"))
                    color = {1.0f, 0.8f, 0.2f, 1.0f};
                else if (std::strstr(line, "DEBUG") || std::strstr(line, "TRACE"))
                    color = {0.6f, 0.6f, 0.6f, 1.0f};

                ImGui::PushStyleColor(ImGuiCol_Text, color);
                ImGui::TextUnformatted(line);
                ImGui::PopStyleColor();
            }
        }

        if (log_scroll_to_bottom_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);

        ImGui::EndChild();
        ImGui::End();
    }

    // ────────────────────────────────────────────────────
    // Log capture
    // ────────────────────────────────────────────────────

    void UiApp::add_log_entry(const char *text)
    {
        if (!text)
            return;

        std::lock_guard<std::mutex> lock(log_mutex_);

        LogEntry &entry = log_buffer_[static_cast<size_t>(log_write_pos_)];
        std::strncpy(entry.text, text, kMaxLineLen - 1);
        entry.text[kMaxLineLen - 1] = '\0';

        log_write_pos_ = (log_write_pos_ + 1) % kMaxLogEntries;
        if (log_count_ < kMaxLogEntries)
            ++log_count_;
    }

} // namespace luft
