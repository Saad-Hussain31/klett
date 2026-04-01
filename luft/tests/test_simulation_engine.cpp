#include <gtest/gtest.h>
#include "simulation_engine.h"
#include "config.h"
#include "aircraft_state.h"
#include "math_types.h"
#include <thread>
#include <atomic>

using namespace luft;

class SimulationEngineTest : public ::testing::Test
{
protected:
    SimulationEngine engine;
    Config cfg;

    void SetUp() override
    {
        cfg = default_config();
    }
};

TEST_F(SimulationEngineTest, InitialStateIsUninitialized)
{
    EXPECT_EQ(engine.get_sim_state(), SimState::Uninitialized);
}

TEST_F(SimulationEngineTest, InitializeTransitionsToInitialized)
{
    engine.initialize(cfg);
    EXPECT_EQ(engine.get_sim_state(), SimState::Initialized);
}

TEST_F(SimulationEngineTest, StartTransitionsToRunning)
{
    engine.initialize(cfg);
    engine.start();
    EXPECT_EQ(engine.get_sim_state(), SimState::Running);
}

TEST_F(SimulationEngineTest, PauseTransitionsToPaused)
{
    engine.initialize(cfg);
    engine.start();
    engine.pause();
    EXPECT_EQ(engine.get_sim_state(), SimState::Paused);
}

TEST_F(SimulationEngineTest, ResumeFromPausedToRunning)
{
    engine.initialize(cfg);
    engine.start();
    engine.pause();
    engine.resume();
    EXPECT_EQ(engine.get_sim_state(), SimState::Running);
}

TEST_F(SimulationEngineTest, StopFromRunning)
{
    engine.initialize(cfg);
    engine.start();
    engine.stop();
    EXPECT_EQ(engine.get_sim_state(), SimState::Stopped);
}

TEST_F(SimulationEngineTest, StopFromPaused)
{
    engine.initialize(cfg);
    engine.start();
    engine.pause();
    engine.stop();
    EXPECT_EQ(engine.get_sim_state(), SimState::Stopped);
}

TEST_F(SimulationEngineTest, InvalidTransitionStartFromRunning)
{
    engine.initialize(cfg);
    engine.start();
    // start() from Running should be ignored (requires Initialized or Paused)
    engine.start();
    EXPECT_EQ(engine.get_sim_state(), SimState::Running);
}

TEST_F(SimulationEngineTest, InvalidTransitionPauseFromInitialized)
{
    engine.initialize(cfg);
    // pause() from Initialized should be ignored
    engine.pause();
    EXPECT_EQ(engine.get_sim_state(), SimState::Initialized);
}

TEST_F(SimulationEngineTest, ResumeFromInitializedTransitionsToRunning)
{
    engine.initialize(cfg);
    // resume() calls transition_to(Running), and Initialized -> Running is valid
    engine.resume();
    EXPECT_EQ(engine.get_sim_state(), SimState::Running);
}

TEST_F(SimulationEngineTest, InvalidTransitionPauseFromStopped)
{
    engine.initialize(cfg);
    engine.start();
    engine.stop();
    // pause() from Stopped should be ignored (only Running -> Paused)
    engine.pause();
    EXPECT_EQ(engine.get_sim_state(), SimState::Stopped);
}

TEST_F(SimulationEngineTest, StopFromUninitializedIgnored)
{
    engine.stop();
    EXPECT_EQ(engine.get_sim_state(), SimState::Uninitialized);
}

TEST_F(SimulationEngineTest, StepOnlyAdvancesInRunning)
{
    engine.initialize(cfg);
    engine.step(); // should be no-op since state is Initialized
    EXPECT_DOUBLE_EQ(engine.get_sim_time(), 0.0);
    EXPECT_EQ(engine.get_tick_count(), 0u);
}

TEST_F(SimulationEngineTest, StepIncrementsSimTime)
{
    engine.initialize(cfg);
    engine.start();
    engine.step();
    EXPECT_NEAR(engine.get_sim_time(), cfg.time_step, 1e-12);
}

TEST_F(SimulationEngineTest, StepIncrementsTickCount)
{
    engine.initialize(cfg);
    engine.start();
    engine.step();
    EXPECT_EQ(engine.get_tick_count(), 1u);
    engine.step();
    EXPECT_EQ(engine.get_tick_count(), 2u);
}

TEST_F(SimulationEngineTest, StepDoesNotAdvanceWhenPaused)
{
    engine.initialize(cfg);
    engine.start();
    engine.step();
    double time_after_one = engine.get_sim_time();
    engine.pause();
    engine.step();
    EXPECT_DOUBLE_EQ(engine.get_sim_time(), time_after_one);
}

TEST_F(SimulationEngineTest, ResetReInitializesState)
{
    engine.initialize(cfg);
    engine.start();
    for (int i = 0; i < 100; ++i)
        engine.step();
    EXPECT_GT(engine.get_sim_time(), 0.0);

    engine.reset(cfg);
    EXPECT_EQ(engine.get_sim_state(), SimState::Initialized);
    EXPECT_DOUBLE_EQ(engine.get_sim_time(), 0.0);
    EXPECT_EQ(engine.get_tick_count(), 0u);
}

TEST_F(SimulationEngineTest, GetStateReturnsValidState)
{
    engine.initialize(cfg);
    AircraftState state = engine.get_state();
    EXPECT_NEAR(state.altitude_msl, cfg.initial_altitude_m, 1e-6);
    EXPECT_NEAR(state.airspeed, cfg.initial_airspeed_ms, 1e-6);
}

TEST_F(SimulationEngineTest, SetControlInputApplied)
{
    engine.initialize(cfg);
    engine.start();

    ControlInput input;
    input.throttle = 0.8;
    input.elevator = 0.5;
    engine.set_control_input(input);

    // After a step, the sim should use the input (no direct access, but no crash)
    engine.step();
    EXPECT_EQ(engine.get_sim_state(), SimState::Running);
}

TEST_F(SimulationEngineTest, ThreadSafetySetControlAndGetState)
{
    engine.initialize(cfg);
    engine.start();

    std::atomic<bool> done{false};
    std::atomic<int> errors{0};

    // Writer thread: sets control input
    std::thread writer([&]()
                       {
        for (int i = 0; i < 1000 && !done; ++i) {
            ControlInput input;
            input.throttle = static_cast<double>(i % 10) / 10.0;
            input.elevator = static_cast<double>(i % 20 - 10) / 10.0;
            engine.set_control_input(input);
        }
        done = true; });

    // Reader thread: reads state
    std::thread reader([&]()
                       {
        for (int i = 0; i < 1000 && !done; ++i) {
            AircraftState state = engine.get_state();
            if (std::isnan(state.position.x) || std::isnan(state.position.y)) {
                errors++;
            }
        } });

    // Main thread: steps the simulation
    for (int i = 0; i < 100; ++i)
    {
        engine.step();
    }

    done = true;
    writer.join();
    reader.join();

    EXPECT_EQ(errors.load(), 0);
}

TEST_F(SimulationEngineTest, MultipleStepsAccumulateTime)
{
    engine.initialize(cfg);
    engine.start();

    int n = 50;
    for (int i = 0; i < n; ++i)
    {
        engine.step();
    }

    EXPECT_NEAR(engine.get_sim_time(), n * cfg.time_step, 1e-9);
    EXPECT_EQ(engine.get_tick_count(), static_cast<uint64_t>(n));
}
