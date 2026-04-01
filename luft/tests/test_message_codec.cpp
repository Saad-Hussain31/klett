#include <gtest/gtest.h>
#include "message_codec.h"
#include "protocol.h"
#include "aircraft_state.h"
#include <cstring>
#include <vector>

using namespace luft;

// ── Encode/Decode Roundtrip ─────────────────────

TEST(MessageCodec, EncodeDecodeRoundtrip)
{
    const uint8_t payload[] = {0x01, 0x02, 0x03, 0x04};
    auto wire = MessageCodec::encode(MessageType::SimCommand, 42, payload, sizeof(payload));

    MessageCodec codec;
    codec.feed(wire.data(), wire.size());

    ASSERT_TRUE(codec.has_message());
    auto msg = codec.pop_message();
    EXPECT_EQ(msg.type, MessageType::SimCommand);
    EXPECT_EQ(msg.sequence, 42);
    ASSERT_EQ(msg.payload.size(), sizeof(payload));
    EXPECT_EQ(std::memcmp(msg.payload.data(), payload, sizeof(payload)), 0);
    EXPECT_FALSE(codec.has_message());
}

TEST(MessageCodec, PartialFeed)
{
    const uint8_t payload[] = {0xAA, 0xBB};
    auto wire = MessageCodec::encode(MessageType::Ack, 1, payload, sizeof(payload));

    MessageCodec codec;
    // Feed header first (8 bytes)
    codec.feed(wire.data(), kHeaderSize);
    EXPECT_FALSE(codec.has_message());

    // Feed remaining bytes
    codec.feed(wire.data() + kHeaderSize, wire.size() - kHeaderSize);
    ASSERT_TRUE(codec.has_message());
    auto msg = codec.pop_message();
    EXPECT_EQ(msg.type, MessageType::Ack);
    EXPECT_EQ(msg.sequence, 1);
    EXPECT_EQ(msg.payload.size(), sizeof(payload));
}

TEST(MessageCodec, MultipleMessagesInOneFeed)
{
    auto wire1 = MessageCodec::encode(MessageType::Telemetry, 1, nullptr, 0);
    auto wire2 = MessageCodec::encode(MessageType::ControlInput, 2, nullptr, 0);
    auto wire3 = MessageCodec::encode(MessageType::SimCommand, 3, nullptr, 0);

    std::vector<uint8_t> combined;
    combined.insert(combined.end(), wire1.begin(), wire1.end());
    combined.insert(combined.end(), wire2.begin(), wire2.end());
    combined.insert(combined.end(), wire3.begin(), wire3.end());

    MessageCodec codec;
    codec.feed(combined.data(), combined.size());

    ASSERT_TRUE(codec.has_message());
    auto m1 = codec.pop_message();
    EXPECT_EQ(m1.type, MessageType::Telemetry);
    EXPECT_EQ(m1.sequence, 1);

    ASSERT_TRUE(codec.has_message());
    auto m2 = codec.pop_message();
    EXPECT_EQ(m2.type, MessageType::ControlInput);
    EXPECT_EQ(m2.sequence, 2);

    ASSERT_TRUE(codec.has_message());
    auto m3 = codec.pop_message();
    EXPECT_EQ(m3.type, MessageType::SimCommand);
    EXPECT_EQ(m3.sequence, 3);

    EXPECT_FALSE(codec.has_message());
}

TEST(MessageCodec, EmptyPayloadMessage)
{
    auto wire = MessageCodec::encode(MessageType::Ack, 100, nullptr, 0);

    MessageCodec codec;
    codec.feed(wire.data(), wire.size());

    ASSERT_TRUE(codec.has_message());
    auto msg = codec.pop_message();
    EXPECT_EQ(msg.type, MessageType::Ack);
    EXPECT_EQ(msg.sequence, 100);
    EXPECT_TRUE(msg.payload.empty());
}

// ── Telemetry Serialization ─────────────────────

TEST(MessageCodec, TelemetrySerializationRoundtrip)
{
    AircraftState state;
    state.position = {100.0, 200.0, -1000.0};
    state.velocity_body = {50.0, 1.0, -2.0};
    state.orientation = Quaternion::from_euler(0.1, 0.2, 0.3);
    state.angular_velocity = {0.01, 0.02, 0.03};
    state.airspeed = 52.0;
    state.alpha = 0.05;
    state.beta = 0.01;
    state.altitude_msl = 1000.0;
    state.thrust_current = 1500.0;
    state.fuel_mass = 80.0;

    auto payload = MessageCodec::serialize_telemetry(state, SimState::Running);

    // Verify we can read back the doubles in order
    const uint8_t *ptr = payload.data();
    auto read_d = [&]() -> double
    {
        double v;
        std::memcpy(&v, ptr, sizeof(double));
        ptr += sizeof(double);
        return v;
    };

    EXPECT_DOUBLE_EQ(read_d(), state.position.x);
    EXPECT_DOUBLE_EQ(read_d(), state.position.y);
    EXPECT_DOUBLE_EQ(read_d(), state.position.z);
    EXPECT_DOUBLE_EQ(read_d(), state.velocity_body.x);
    EXPECT_DOUBLE_EQ(read_d(), state.velocity_body.y);
    EXPECT_DOUBLE_EQ(read_d(), state.velocity_body.z);
    EXPECT_DOUBLE_EQ(read_d(), state.orientation.w);
    EXPECT_DOUBLE_EQ(read_d(), state.orientation.x);
    EXPECT_DOUBLE_EQ(read_d(), state.orientation.y);
    EXPECT_DOUBLE_EQ(read_d(), state.orientation.z);
    EXPECT_DOUBLE_EQ(read_d(), state.angular_velocity.x);
    EXPECT_DOUBLE_EQ(read_d(), state.angular_velocity.y);
    EXPECT_DOUBLE_EQ(read_d(), state.angular_velocity.z);
    EXPECT_DOUBLE_EQ(read_d(), state.airspeed);
    EXPECT_DOUBLE_EQ(read_d(), state.alpha);
    EXPECT_DOUBLE_EQ(read_d(), state.beta);
    EXPECT_DOUBLE_EQ(read_d(), state.altitude_msl);
    EXPECT_DOUBLE_EQ(read_d(), state.thrust_current);
    EXPECT_DOUBLE_EQ(read_d(), state.fuel_mass);

    // Last byte is sim_state
    EXPECT_EQ(*ptr, static_cast<uint8_t>(SimState::Running));
}

// ── ControlInput Deserialization ────────────────

TEST(MessageCodec, ControlInputDeserialization)
{
    // Build a known payload: 6 doubles
    std::vector<uint8_t> data(6 * sizeof(double));
    uint8_t *ptr = data.data();
    auto write_d = [&](double v)
    {
        std::memcpy(ptr, &v, sizeof(double));
        ptr += sizeof(double);
    };

    write_d(0.5);  // elevator
    write_d(-0.3); // aileron
    write_d(0.1);  // rudder
    write_d(0.8);  // throttle
    write_d(0.25); // flaps
    write_d(-0.1); // trim

    ControlInput input;
    ASSERT_TRUE(MessageCodec::deserialize_control_input(data.data(), data.size(), input));
    EXPECT_DOUBLE_EQ(input.elevator, 0.5);
    EXPECT_DOUBLE_EQ(input.aileron, -0.3);
    EXPECT_DOUBLE_EQ(input.rudder, 0.1);
    EXPECT_DOUBLE_EQ(input.throttle, 0.8);
    EXPECT_DOUBLE_EQ(input.flaps, 0.25);
    EXPECT_DOUBLE_EQ(input.trim, -0.1);
}

TEST(MessageCodec, ControlInputTooSmall)
{
    uint8_t data[4] = {};
    ControlInput input;
    EXPECT_FALSE(MessageCodec::deserialize_control_input(data, sizeof(data), input));
}

// ── SimCommand Deserialization ──────────────────

TEST(MessageCodec, SimCommandDeserialization)
{
    uint8_t data[] = {static_cast<uint8_t>(SimCommandType::Pause)};
    SimCommandType cmd;
    ASSERT_TRUE(MessageCodec::deserialize_sim_command(data, sizeof(data), cmd));
    EXPECT_EQ(cmd, SimCommandType::Pause);
}

TEST(MessageCodec, SimCommandAllTypes)
{
    for (uint8_t i = static_cast<uint8_t>(SimCommandType::Start);
         i <= static_cast<uint8_t>(SimCommandType::Stop); ++i)
    {
        uint8_t data[] = {i};
        SimCommandType cmd;
        EXPECT_TRUE(MessageCodec::deserialize_sim_command(data, 1, cmd));
        EXPECT_EQ(static_cast<uint8_t>(cmd), i);
    }
}

TEST(MessageCodec, SimCommandEmptyPayload)
{
    SimCommandType cmd;
    EXPECT_FALSE(MessageCodec::deserialize_sim_command(nullptr, 0, cmd));
}

TEST(MessageCodec, SimCommandInvalidValue)
{
    uint8_t data[] = {0}; // 0 is below Start(1)
    SimCommandType cmd;
    EXPECT_FALSE(MessageCodec::deserialize_sim_command(data, 1, cmd));

    uint8_t data2[] = {255}; // way above Stop(5)
    EXPECT_FALSE(MessageCodec::deserialize_sim_command(data2, 1, cmd));
}

TEST(MessageCodec, OversizedMessageRejected)
{
    // Craft a header with length > kMaxMessageSize
    MessageHeader hdr;
    hdr.length = static_cast<uint32_t>(kMaxMessageSize + 1);
    hdr.type = static_cast<uint16_t>(MessageType::Telemetry);
    hdr.sequence = 0;

    uint8_t buf[kHeaderSize];
    pack_header(buf, hdr);

    MessageCodec codec;
    codec.feed(buf, kHeaderSize);
    // The codec should discard the buffer on invalid length
    EXPECT_FALSE(codec.has_message());
}

TEST(MessageCodec, ByteByByteFeed)
{
    const uint8_t payload[] = {0xDE, 0xAD};
    auto wire = MessageCodec::encode(MessageType::Nack, 7, payload, sizeof(payload));

    MessageCodec codec;
    for (size_t i = 0; i < wire.size(); ++i)
    {
        codec.feed(&wire[i], 1);
    }

    ASSERT_TRUE(codec.has_message());
    auto msg = codec.pop_message();
    EXPECT_EQ(msg.type, MessageType::Nack);
    EXPECT_EQ(msg.sequence, 7);
    EXPECT_EQ(msg.payload.size(), 2u);
}
