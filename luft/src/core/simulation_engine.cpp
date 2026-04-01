#include "simulation_engine.h"
#include "logger.h"

#include <cmath>

namespace luft
{

    // ──────────────────────────────────────────────
    // Lifecycle
    // ──────────────────────────────────────────────

    void SimulationEngine::initialize(const Config &config)
    {
        std::lock_guard<std::mutex> lock(state_mutex_);

        time_step_ = config.time_step;

        // ── Aircraft parameters (default Cessna 172 for now) ──
        aircraft_params_ = AircraftParams{};

        // ── Initial aircraft state from config ──
        aircraft_state_ = AircraftState{};
        aircraft_state_.altitude_msl = config.initial_altitude_m;
        // NED: z-down, so altitude is negative z
        aircraft_state_.position = Vec3{0.0, 0.0, -config.initial_altitude_m};

        // Initial orientation from heading (yaw only, level flight)
        double heading_rad = config.initial_heading_deg * kDegToRad;
        aircraft_state_.orientation = Quaternion::from_euler(0.0, 0.0, heading_rad);

        // Initial velocity: forward in body frame
        aircraft_state_.velocity_body = Vec3{config.initial_airspeed_ms, 0.0, 0.0};
        aircraft_state_.airspeed = config.initial_airspeed_ms;

        // Fuel
        aircraft_state_.fuel_mass = config.initial_fuel_kg;

        // Engine: start with thrust roughly matching cruise
        aircraft_state_.thrust_current = 0.0;

        // ── Wind model ──
        Vec3 wind_ned{config.wind_north_ms, config.wind_east_ms, config.wind_down_ms};
        if (config.gust_intensity > 0.0)
        {
            wind_model_.set_with_gusts(wind_ned, config.gust_intensity);
        }
        else
        {
            wind_model_.set_constant(wind_ned);
        }

        // ── Control input ──
        current_input_ = ControlInput{};

        // ── Counters ──
        sim_time_ = 0.0;
        tick_count_ = 0;

        state_ = SimState::Initialized;

        LOG_INFO("SimulationEngine initialized: dt=%.4fs, alt=%.0fm, V=%.1fm/s, hdg=%.1fdeg, fuel=%.1fkg",
                 time_step_, config.initial_altitude_m, config.initial_airspeed_ms,
                 config.initial_heading_deg, config.initial_fuel_kg);
    }

    void SimulationEngine::start()
    {
        std::lock_guard<std::mutex> lock(state_mutex_);

        if (!transition_to(SimState::Running))
        {
            LOG_WARN("SimulationEngine::start() ignored — invalid transition from %s",
                     sim_state_name(state_));
            return;
        }

        LOG_INFO("Simulation started at t=%.3fs", sim_time_);
    }

    void SimulationEngine::pause()
    {
        std::lock_guard<std::mutex> lock(state_mutex_);

        if (!transition_to(SimState::Paused))
        {
            LOG_WARN("SimulationEngine::pause() ignored — invalid transition from %s",
                     sim_state_name(state_));
            return;
        }

        LOG_INFO("Simulation paused at t=%.3fs, tick=%lu", sim_time_, tick_count_);
    }

    void SimulationEngine::resume()
    {
        std::lock_guard<std::mutex> lock(state_mutex_);

        if (!transition_to(SimState::Running))
        {
            LOG_WARN("SimulationEngine::resume() ignored — invalid transition from %s",
                     sim_state_name(state_));
            return;
        }

        LOG_INFO("Simulation resumed at t=%.3fs", sim_time_);
    }

    void SimulationEngine::stop()
    {
        std::lock_guard<std::mutex> lock(state_mutex_);

        if (state_ == SimState::Uninitialized)
        {
            LOG_WARN("SimulationEngine::stop() ignored — not initialized");
            return;
        }

        state_ = SimState::Stopped;
        LOG_INFO("Simulation stopped at t=%.3fs, tick=%lu", sim_time_, tick_count_);
    }

    void SimulationEngine::reset(const Config &config)
    {
        // initialize() acquires the lock itself
        initialize(config);
        LOG_INFO("Simulation reset complete");
    }

    // ──────────────────────────────────────────────
    // Simulation step
    // ──────────────────────────────────────────────

    void SimulationEngine::step()
    {
        std::lock_guard<std::mutex> lock(state_mutex_);

        if (state_ != SimState::Running)
        {
            return;
        }

        // ── Compute environment at current altitude ──
        AtmosphereState atmo = atmosphere_.compute(aircraft_state_.altitude_msl);
        WindState wind = wind_model_.compute(aircraft_state_.altitude_msl, sim_time_);

        // ── Snapshot control input (already stored by set_control_input) ──
        ControlInput input = current_input_;

        // ── Integrate one time step ──
        flight_dynamics_.update(aircraft_state_, aircraft_params_, input, atmo, wind, time_step_);

        // ── Post-step validity checks ──
        if (!check_state_validity())
        {
            state_ = SimState::Error;
            LOG_ERROR("Simulation entered Error state at t=%.3fs — NaN or divergence detected", sim_time_);
            return;
        }

        // ── Clamp altitude to reasonable bounds ──
        // NED: position.z is negative of altitude MSL
        constexpr double kMaxAltitude = 30000.0; // ~100,000 ft, above service ceiling
        constexpr double kMinAltitude = -100.0;  // allow slight below sea level for terrain
        aircraft_state_.altitude_msl = clamp(aircraft_state_.altitude_msl, kMinAltitude, kMaxAltitude);
        aircraft_state_.position.z = -aircraft_state_.altitude_msl;

        // ── Clamp fuel ──
        if (aircraft_state_.fuel_mass < 0.0)
        {
            aircraft_state_.fuel_mass = 0.0;
        }

        // ── Advance time ──
        sim_time_ += time_step_;
        tick_count_++;
    }

    // ──────────────────────────────────────────────
    // Thread-safe input / output
    // ──────────────────────────────────────────────

    void SimulationEngine::set_control_input(const ControlInput &input)
    {
        std::lock_guard<std::mutex> lock(state_mutex_);

        current_input_ = input;
        current_input_.clamp_all();
    }

    AircraftState SimulationEngine::get_state() const
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        return aircraft_state_;
    }

    SimState SimulationEngine::get_sim_state() const
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        return state_;
    }

    double SimulationEngine::get_sim_time() const
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        return sim_time_;
    }

    uint64_t SimulationEngine::get_tick_count() const
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        return tick_count_;
    }

    // ──────────────────────────────────────────────
    // State machine helpers
    // ──────────────────────────────────────────────

    bool SimulationEngine::is_valid_transition(SimState from, SimState to) const
    {
        switch (to)
        {
        case SimState::Initialized:
            return from == SimState::Uninitialized || from == SimState::Stopped;

        case SimState::Running:
            return from == SimState::Initialized || from == SimState::Paused;

        case SimState::Paused:
            return from == SimState::Running;

        case SimState::Stopped:
            // Can stop from any active state
            return from == SimState::Initialized || from == SimState::Running || from == SimState::Paused || from == SimState::Error;

        case SimState::Error:
            // Internal transition only — always allowed from Running
            return from == SimState::Running;

        case SimState::Uninitialized:
            // Cannot transition back to Uninitialized
            return false;
        }

        return false;
    }

    bool SimulationEngine::transition_to(SimState target)
    {
        if (!is_valid_transition(state_, target))
        {
            return false;
        }
        state_ = target;
        return true;
    }

    bool SimulationEngine::check_state_validity()
    {
        const auto &pos = aircraft_state_.position;
        const auto &vel = aircraft_state_.velocity_body;

        // Check for NaN in critical fields
        if (std::isnan(pos.x) || std::isnan(pos.y) || std::isnan(pos.z))
        {
            LOG_ERROR("NaN detected in position: (%.3f, %.3f, %.3f)", pos.x, pos.y, pos.z);
            return false;
        }

        if (std::isnan(vel.x) || std::isnan(vel.y) || std::isnan(vel.z))
        {
            LOG_ERROR("NaN detected in velocity_body: (%.3f, %.3f, %.3f)", vel.x, vel.y, vel.z);
            return false;
        }

        if (std::isnan(aircraft_state_.airspeed) || std::isnan(aircraft_state_.alpha) ||
            std::isnan(aircraft_state_.beta))
        {
            LOG_ERROR("NaN detected in derived quantities: V=%.3f alpha=%.3f beta=%.3f",
                      aircraft_state_.airspeed, aircraft_state_.alpha, aircraft_state_.beta);
            return false;
        }

        const auto &q = aircraft_state_.orientation;
        if (std::isnan(q.w) || std::isnan(q.x) || std::isnan(q.y) || std::isnan(q.z))
        {
            LOG_ERROR("NaN detected in orientation quaternion");
            return false;
        }

        const auto &omega = aircraft_state_.angular_velocity;
        if (std::isnan(omega.x) || std::isnan(omega.y) || std::isnan(omega.z))
        {
            LOG_ERROR("NaN detected in angular velocity");
            return false;
        }

        // Check for extreme divergence (velocity > Mach 3 at sea level ~ 1000 m/s)
        constexpr double kMaxSpeed = 1000.0;
        if (vel.magnitude_sq() > kMaxSpeed * kMaxSpeed)
        {
            LOG_ERROR("Velocity divergence: |V| = %.1f m/s exceeds limit", std::sqrt(vel.magnitude_sq()));
            return false;
        }

        return true;
    }

} // namespace luft
