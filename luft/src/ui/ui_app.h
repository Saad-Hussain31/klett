#pragma once

#include "../core/aircraft_state.h"
#include <array>
#include <cstring>
#include <mutex>

struct SDL_Window;
using SDL_GLContext = void *;

namespace luft
{

    class UiApp
    {
    public:
        UiApp() = default;
        ~UiApp();

        // Non-copyable, non-movable
        UiApp(const UiApp &) = delete;
        UiApp &operator=(const UiApp &) = delete;
        UiApp(UiApp &&) = delete;
        UiApp &operator=(UiApp &&) = delete;

        // ── Lifecycle ──────────────────────────────────

        bool initialize(int width, int height, const char *title);
        void shutdown();
        [[nodiscard]] bool should_close() const { return quit_requested_; }

        // ── Per-frame ──────────────────────────────────

        bool process_events();
        void begin_frame();

        void render_telemetry(const AircraftState &state, SimState sim_state, double sim_time);
        void render_controls(ControlInput &input);
        void render_sim_controls(SimState current_state,
                                 bool *start_clicked,
                                 bool *pause_clicked,
                                 bool *resume_clicked,
                                 bool *reset_clicked,
                                 bool *stop_clicked);
        void render_connection_status(int telemetry_clients, int command_clients);
        void render_log_panel();

        void end_frame();

        // ── Input bridge ───────────────────────────────

        void get_keyboard_control_input(ControlInput &input) const;

        // ── Log capture ────────────────────────────────

        void add_log_entry(const char *text);

    private:
        // SDL / GL handles
        SDL_Window *window_ = nullptr;
        SDL_GLContext gl_context_ = nullptr;
        bool initialized_ = false;
        bool quit_requested_ = false;

        // Keyboard state for flight controls
        struct KeyStates
        {
            bool w = false;     // elevator nose-up
            bool s = false;     // elevator nose-down
            bool a = false;     // aileron left
            bool d = false;     // aileron right
            bool q = false;     // rudder left
            bool e = false;     // rudder right
            bool shift = false; // throttle up
            bool ctrl = false;  // throttle down
            bool f = false;     // flaps toggle (edge-detected)
        };
        KeyStates keys_{};

        // Flaps toggling state (cycles 0.0 -> 0.33 -> 0.66 -> 1.0 -> 0.0)
        bool flap_key_was_down_ = false;
        float flap_step_index_ = 0;

        // Keyboard-driven throttle accumulator (persists across frames)
        double kb_throttle_ = 0.0;

        // ── Log ring buffer ────────────────────────────

        static constexpr int kMaxLogEntries = 200;
        static constexpr int kMaxLineLen = 256;

        struct LogEntry
        {
            char text[kMaxLineLen] = {};
        };

        std::array<LogEntry, kMaxLogEntries> log_buffer_{};
        int log_write_pos_ = 0;
        int log_count_ = 0;
        bool log_scroll_to_bottom_ = true;
        std::mutex log_mutex_;
    };

} // namespace luft
