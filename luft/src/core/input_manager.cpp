#include "input_manager.h"
#include "logger.h"

namespace luft
{

    // ──────────────────────────────────────────────
    // Default SDL key codes (matching SDL_Scancode values)
    // Defined here to avoid an SDL dependency in the header
    // ──────────────────────────────────────────────

    namespace key
    {
        // Letter keys (SDL scancodes)
        constexpr int W = 26;
        constexpr int S = 22;
        constexpr int A = 4;
        constexpr int D = 7;
        constexpr int Q = 20;
        constexpr int E = 8;
        constexpr int F = 9;
        constexpr int T = 23;
        constexpr int G = 10;
        // Symbols
        constexpr int Plus = 87;  // SDL_SCANCODE_KP_PLUS
        constexpr int Minus = 86; // SDL_SCANCODE_KP_MINUS
    } // namespace key

    // ──────────────────────────────────────────────
    // update — apply external raw input with sensitivity
    // ──────────────────────────────────────────────

    void InputManager::update(const ControlInput &raw_input)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        current_.elevator = raw_input.elevator * elevator_sensitivity_;
        current_.aileron = raw_input.aileron * aileron_sensitivity_;
        current_.rudder = raw_input.rudder * rudder_sensitivity_;
        current_.throttle = raw_input.throttle;
        current_.flaps = raw_input.flaps;
        current_.trim = raw_input.trim;
        current_.clamp_all();
    }

    ControlInput InputManager::get_control_input() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return current_;
    }

    void InputManager::set_sensitivity(double elevator, double aileron, double rudder)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        elevator_sensitivity_ = elevator;
        aileron_sensitivity_ = aileron;
        rudder_sensitivity_ = rudder;
        LOG_DEBUG("Input sensitivity: elev=%.2f ail=%.2f rud=%.2f", elevator, aileron, rudder);
    }

    void InputManager::reset()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        current_ = ControlInput{};
        key_state_.clear();
    }

    // ──────────────────────────────────────────────
    // Key mapping
    // ──────────────────────────────────────────────

    void InputManager::register_key(int key_code, ControlAxis axis, double increment)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        key_map_[key_code] = KeyMapping{axis, increment};
    }

    void InputManager::handle_key_event(int key_code, bool pressed)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        key_state_[key_code] = pressed;
    }

    void InputManager::process_keys()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto &[code, held] : key_state_)
        {
            if (!held)
                continue;

            auto it = key_map_.find(code);
            if (it != key_map_.end())
            {
                apply_axis_increment(it->second.axis, it->second.increment);
            }
        }
        current_.clamp_all();
    }

    void InputManager::apply_axis_increment(ControlAxis axis, double increment)
    {
        switch (axis)
        {
        case ControlAxis::Elevator:
            current_.elevator += increment;
            break;
        case ControlAxis::Aileron:
            current_.aileron += increment;
            break;
        case ControlAxis::Rudder:
            current_.rudder += increment;
            break;
        case ControlAxis::Throttle:
            current_.throttle += increment;
            break;
        case ControlAxis::Flaps:
            current_.flaps += increment;
            break;
        case ControlAxis::Trim:
            current_.trim += increment;
            break;
        }
    }

    // ──────────────────────────────────────────────
    // Default key bindings
    // ──────────────────────────────────────────────

    void InputManager::setup_default_mappings()
    {
        constexpr double kAxisRate = 0.02; // per-frame increment for control surfaces
        constexpr double kThrottleRate = 0.01;
        constexpr double kTrimRate = 0.005;
        constexpr double kFlapStep = 0.25; // flap notch step

        // W/S = elevator (W = pitch up = positive, S = pitch down)
        register_key(key::W, ControlAxis::Elevator, kAxisRate);
        register_key(key::S, ControlAxis::Elevator, -kAxisRate);

        // A/D = aileron (A = roll left = negative, D = roll right)
        register_key(key::A, ControlAxis::Aileron, -kAxisRate);
        register_key(key::D, ControlAxis::Aileron, kAxisRate);

        // Q/E = rudder (Q = left yaw, E = right yaw)
        register_key(key::Q, ControlAxis::Rudder, -kAxisRate);
        register_key(key::E, ControlAxis::Rudder, kAxisRate);

        // +/- = throttle
        register_key(key::Plus, ControlAxis::Throttle, kThrottleRate);
        register_key(key::Minus, ControlAxis::Throttle, -kThrottleRate);

        // F = flaps (toggle step up)
        register_key(key::F, ControlAxis::Flaps, kFlapStep);

        // T/G = trim (T = trim up, G = trim down)
        register_key(key::T, ControlAxis::Trim, kTrimRate);
        register_key(key::G, ControlAxis::Trim, -kTrimRate);

        LOG_INFO("Default key mappings registered");
    }

} // namespace luft
