#pragma once

#include "aircraft_state.h"
#include "atmosphere.h"
#include "wind_model.h"
#include "aerodynamics.h"
#include "engine_model.h"
#include "flight_dynamics.h"
#include "config.h"

#include <mutex>
#include <cstdint>

namespace luft
{

    // ──────────────────────────────────────────────
    // SimulationEngine — the heart of the flight simulator.
    //
    // Owns the full physics pipeline: atmosphere, wind, aero,
    // engine, and flight dynamics integration.  Provides a
    // thread-safe interface for control input (from network or
    // UI threads) and state readback.
    //
    // Lifecycle:
    //   Uninitialized -> initialize() -> Initialized
    //   Initialized   -> start()      -> Running
    //   Running       -> pause()      -> Paused
    //   Paused        -> resume()     -> Running
    //   Running|Paused|Initialized -> stop() -> Stopped
    //   Stopped       -> reset()      -> Initialized
    //   Any           -> Error (internal, on NaN / divergence)
    // ──────────────────────────────────────────────

    class SimulationEngine
    {
    public:
        SimulationEngine() = default;
        ~SimulationEngine() = default;

        // Non-copyable, non-movable (owns mutex)
        SimulationEngine(const SimulationEngine &) = delete;
        SimulationEngine &operator=(const SimulationEngine &) = delete;
        SimulationEngine(SimulationEngine &&) = delete;
        SimulationEngine &operator=(SimulationEngine &&) = delete;

        // ── Lifecycle ────────────────────────────────

        /// Configure the engine from a validated Config.
        /// Transitions: Uninitialized|Stopped -> Initialized
        void initialize(const Config &config);

        /// Begin running the simulation.
        /// Transitions: Initialized|Paused -> Running
        void start();

        /// Pause the simulation (state is preserved).
        /// Transitions: Running -> Paused
        void pause();

        /// Resume from pause.
        /// Transitions: Paused -> Running
        void resume();

        /// Stop the simulation entirely.
        /// Transitions: any (except Uninitialized) -> Stopped
        void stop();

        /// Reset to initial conditions and re-initialize.
        /// Transitions: any -> Initialized
        void reset(const Config &config);

        // ── Simulation step ─────────────────────────

        /// Advance the simulation by one fixed time step.
        /// No-op if state != Running.
        void step();

        // ── Thread-safe input/output ────────────────

        /// Set control surfaces.  Thread-safe: may be called from
        /// the network thread or UI thread concurrently with step().
        void set_control_input(const ControlInput &input);

        /// Snapshot the current aircraft state.  Thread-safe.
        [[nodiscard]] AircraftState get_state() const;

        /// Query simulation status.
        [[nodiscard]] SimState get_sim_state() const;
        [[nodiscard]] double get_sim_time() const;
        [[nodiscard]] uint64_t get_tick_count() const;

    private:
        // ── Subsystems ───────────────────────────────
        Atmosphere atmosphere_;
        WindModel wind_model_;
        Aerodynamics aerodynamics_; // unused directly — FlightDynamics owns its own
        EngineModel engine_model_;  // unused directly — FlightDynamics owns its own
        FlightDynamics flight_dynamics_;

        // ── Simulation state ─────────────────────────
        AircraftState aircraft_state_{};
        AircraftParams aircraft_params_{};
        ControlInput current_input_{};

        SimState state_ = SimState::Uninitialized;
        double sim_time_ = 0.0;
        uint64_t tick_count_ = 0;
        double time_step_ = 0.01; // overwritten by config

        // ── Thread safety ────────────────────────────
        // Protects aircraft_state_ and current_input_ for cross-thread access.
        mutable std::mutex state_mutex_;

        // ── Helpers ──────────────────────────────────
        bool transition_to(SimState target);
        bool is_valid_transition(SimState from, SimState to) const;
        bool check_state_validity();
    };

} // namespace luft
