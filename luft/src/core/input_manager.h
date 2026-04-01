#pragma once

#include "aircraft_state.h"

#include <unordered_map>
#include <vector>
#include <mutex>

namespace luft
{

    // ──────────────────────────────────────────────
    // Control axis identifiers for key mapping
    // ──────────────────────────────────────────────

    enum class ControlAxis
    {
        Elevator,
        Aileron,
        Rudder,
        Throttle,
        Flaps,
        Trim
    };

    // ──────────────────────────────────────────────
    // Key mapping — binds a key code to a control axis
    // ──────────────────────────────────────────────

    struct KeyMapping
    {
        ControlAxis axis;
        double increment; // applied per update while key is held
    };

    // ──────────────────────────────────────────────
    // InputManager — maps raw input to ControlInput
    // ──────────────────────────────────────────────

    class InputManager
    {
    public:
        InputManager() = default;

        // Apply external control input (e.g., from joystick/network) with sensitivity
        void update(const ControlInput &raw_input);

        // Get current processed control input
        [[nodiscard]] ControlInput get_control_input() const;

        // Configurable sensitivity gains
        void set_sensitivity(double elevator, double aileron, double rudder);

        // Zero all inputs
        void reset();

        // Key mapping
        void register_key(int key_code, ControlAxis axis, double increment);
        void handle_key_event(int key_code, bool pressed);

        // Apply held-key increments to the current control state (call once per frame)
        void process_keys();

        // Register default WASD + utility key bindings
        void setup_default_mappings();

    private:
        ControlInput current_{};

        double elevator_sensitivity_ = 1.0;
        double aileron_sensitivity_ = 1.0;
        double rudder_sensitivity_ = 1.0;

        // key_code -> KeyMapping
        std::unordered_map<int, KeyMapping> key_map_;

        // Currently held keys
        std::unordered_map<int, bool> key_state_;

        mutable std::mutex mutex_;

        void apply_axis_increment(ControlAxis axis, double increment);
    };

} // namespace luft
