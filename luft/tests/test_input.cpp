#include <gtest/gtest.h>
#include "input_manager.h"
#include "aircraft_state.h"
#include "math_types.h"

using namespace luft;

class InputManagerTest : public ::testing::Test
{
protected:
    InputManager mgr;
};

TEST_F(InputManagerTest, DefaultInputIsAllZeros)
{
    auto input = mgr.get_control_input();
    EXPECT_DOUBLE_EQ(input.elevator, 0.0);
    EXPECT_DOUBLE_EQ(input.aileron, 0.0);
    EXPECT_DOUBLE_EQ(input.rudder, 0.0);
    EXPECT_DOUBLE_EQ(input.throttle, 0.0);
    EXPECT_DOUBLE_EQ(input.flaps, 0.0);
    EXPECT_DOUBLE_EQ(input.trim, 0.0);
}

TEST_F(InputManagerTest, SensitivityScaling)
{
    mgr.set_sensitivity(2.0, 0.5, 1.5);

    ControlInput raw;
    raw.elevator = 0.3;
    raw.aileron = 0.4;
    raw.rudder = 0.2;
    raw.throttle = 0.7;
    mgr.update(raw);

    auto out = mgr.get_control_input();
    EXPECT_NEAR(out.elevator, 0.3 * 2.0, 1e-6);
    EXPECT_NEAR(out.aileron, 0.4 * 0.5, 1e-6);
    EXPECT_NEAR(out.rudder, 0.2 * 1.5, 1e-6);
    EXPECT_DOUBLE_EQ(out.throttle, 0.7); // throttle has no sensitivity scaling
}

TEST_F(InputManagerTest, SensitivityClampsProperly)
{
    mgr.set_sensitivity(3.0, 3.0, 3.0);

    ControlInput raw;
    raw.elevator = 0.5; // 0.5 * 3.0 = 1.5, should be clamped to 1.0
    mgr.update(raw);

    auto out = mgr.get_control_input();
    EXPECT_DOUBLE_EQ(out.elevator, 1.0);
}

TEST_F(InputManagerTest, ProcessKeysW)
{
    mgr.setup_default_mappings();

    // SDL key::W = 26, maps to elevator positive increment (0.02)
    mgr.handle_key_event(26, true);
    mgr.process_keys();

    auto out = mgr.get_control_input();
    EXPECT_GT(out.elevator, 0.0);
}

TEST_F(InputManagerTest, ProcessKeysS)
{
    mgr.setup_default_mappings();

    // SDL key::S = 22, maps to elevator negative increment (-0.02)
    mgr.handle_key_event(22, true);
    mgr.process_keys();

    auto out = mgr.get_control_input();
    EXPECT_LT(out.elevator, 0.0);
}

TEST_F(InputManagerTest, ProcessKeysAD)
{
    mgr.setup_default_mappings();

    // A = 4, roll left (negative aileron)
    mgr.handle_key_event(4, true);
    mgr.process_keys();
    auto out = mgr.get_control_input();
    EXPECT_LT(out.aileron, 0.0);

    mgr.reset();

    // D = 7, roll right (positive aileron)
    mgr.handle_key_event(7, true);
    mgr.process_keys();
    out = mgr.get_control_input();
    EXPECT_GT(out.aileron, 0.0);
}

TEST_F(InputManagerTest, ProcessKeysQE)
{
    mgr.setup_default_mappings();

    // Q = 20, left yaw (negative rudder)
    mgr.handle_key_event(20, true);
    mgr.process_keys();
    auto out = mgr.get_control_input();
    EXPECT_LT(out.rudder, 0.0);

    mgr.reset();

    // E = 8, right yaw (positive rudder)
    mgr.handle_key_event(8, true);
    mgr.process_keys();
    out = mgr.get_control_input();
    EXPECT_GT(out.rudder, 0.0);
}

TEST_F(InputManagerTest, ProcessKeysThrottle)
{
    mgr.setup_default_mappings();

    // Plus = 87, throttle up
    mgr.handle_key_event(87, true);
    mgr.process_keys();
    auto out = mgr.get_control_input();
    EXPECT_GT(out.throttle, 0.0);
}

TEST_F(InputManagerTest, ResetZeroesEverything)
{
    mgr.setup_default_mappings();

    // Press some keys
    mgr.handle_key_event(26, true); // W
    mgr.process_keys();

    // Verify non-zero
    auto out = mgr.get_control_input();
    EXPECT_GT(out.elevator, 0.0);

    mgr.reset();
    out = mgr.get_control_input();
    EXPECT_DOUBLE_EQ(out.elevator, 0.0);
    EXPECT_DOUBLE_EQ(out.aileron, 0.0);
    EXPECT_DOUBLE_EQ(out.rudder, 0.0);
    EXPECT_DOUBLE_EQ(out.throttle, 0.0);
}

TEST_F(InputManagerTest, ClampPreventsOutOfRange)
{
    mgr.setup_default_mappings();

    // Hold W for many frames to try to exceed +1.0
    mgr.handle_key_event(26, true);
    for (int i = 0; i < 200; ++i)
    {
        mgr.process_keys();
    }

    auto out = mgr.get_control_input();
    EXPECT_LE(out.elevator, 1.0);
    EXPECT_GE(out.elevator, -1.0);
}

TEST_F(InputManagerTest, KeyReleaseStopsIncrement)
{
    mgr.setup_default_mappings();

    mgr.handle_key_event(26, true); // press W
    mgr.process_keys();
    double val1 = mgr.get_control_input().elevator;

    mgr.handle_key_event(26, false); // release W
    mgr.process_keys();
    double val2 = mgr.get_control_input().elevator;

    // After release, value should not increase further
    EXPECT_DOUBLE_EQ(val1, val2);
}

TEST_F(InputManagerTest, CustomKeyMapping)
{
    // Register a custom key: pressing key 99 adds throttle by 0.1
    mgr.register_key(99, ControlAxis::Throttle, 0.1);
    mgr.handle_key_event(99, true);
    mgr.process_keys();

    auto out = mgr.get_control_input();
    EXPECT_NEAR(out.throttle, 0.1, 1e-6);
}

TEST_F(InputManagerTest, UpdateWithRawInput)
{
    ControlInput raw;
    raw.elevator = 0.5;
    raw.aileron = -0.3;
    raw.rudder = 0.1;
    raw.throttle = 0.9;
    raw.flaps = 0.5;
    raw.trim = -0.2;

    mgr.update(raw);
    auto out = mgr.get_control_input();

    EXPECT_DOUBLE_EQ(out.elevator, 0.5);
    EXPECT_DOUBLE_EQ(out.aileron, -0.3);
    EXPECT_DOUBLE_EQ(out.rudder, 0.1);
    EXPECT_DOUBLE_EQ(out.throttle, 0.9);
    EXPECT_DOUBLE_EQ(out.flaps, 0.5);
    EXPECT_DOUBLE_EQ(out.trim, -0.2);
}
