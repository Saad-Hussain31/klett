#include "message_codec.h"
#include "../logger.h"

#include <cstring>
#include <algorithm>

namespace luft
{

    // ──────────────────────────────────────────────
    // Receive side
    // ──────────────────────────────────────────────

    void MessageCodec::feed(const uint8_t *data, size_t len)
    {
        recv_buf_.insert(recv_buf_.end(), data, data + len);
        try_parse();
    }

    bool MessageCodec::has_message() const
    {
        return !messages_.empty();
    }

    FramedMessage MessageCodec::pop_message()
    {
        FramedMessage msg = std::move(messages_.front());
        messages_.pop();
        return msg;
    }

    void MessageCodec::try_parse()
    {
        while (recv_buf_.size() >= kHeaderSize)
        {
            MessageHeader hdr = unpack_header(recv_buf_.data());

            // Sanity checks
            if (hdr.length < kHeaderSize || hdr.length > kMaxMessageSize)
            {
                LOG_ERROR("message_codec: invalid message length %u, discarding buffer", hdr.length);
                recv_buf_.clear();
                return;
            }

            // Not enough bytes for the full message yet
            if (recv_buf_.size() < hdr.length)
                return;

            size_t payload_len = hdr.length - kHeaderSize;

            FramedMessage msg;
            msg.type = static_cast<MessageType>(hdr.type);
            msg.sequence = hdr.sequence;
            if (payload_len > 0)
            {
                msg.payload.assign(recv_buf_.data() + kHeaderSize,
                                   recv_buf_.data() + kHeaderSize + payload_len);
            }

            messages_.push(std::move(msg));

            // Erase consumed bytes
            recv_buf_.erase(recv_buf_.begin(),
                            recv_buf_.begin() + static_cast<ptrdiff_t>(hdr.length));
        }
    }

    // ──────────────────────────────────────────────
    // Send side
    // ──────────────────────────────────────────────

    std::vector<uint8_t> MessageCodec::encode(MessageType type, uint16_t sequence,
                                              const uint8_t *payload, size_t payload_len)
    {
        uint32_t total = static_cast<uint32_t>(kHeaderSize + payload_len);
        std::vector<uint8_t> buf(total);

        MessageHeader hdr;
        hdr.length = total;
        hdr.type = static_cast<uint16_t>(type);
        hdr.sequence = sequence;
        pack_header(buf.data(), hdr);

        if (payload_len > 0)
            std::memcpy(buf.data() + kHeaderSize, payload, payload_len);

        return buf;
    }

    // ──────────────────────────────────────────────
    // Binary helpers — pack/unpack doubles and enums
    // ──────────────────────────────────────────────

    namespace
    {

        inline void write_double(std::vector<uint8_t> &buf, double v)
        {
            const auto *p = reinterpret_cast<const uint8_t *>(&v);
            buf.insert(buf.end(), p, p + sizeof(double));
        }

        inline double read_double(const uint8_t *&ptr)
        {
            double v;
            std::memcpy(&v, ptr, sizeof(double));
            ptr += sizeof(double);
            return v;
        }

    } // anonymous namespace

    // ──────────────────────────────────────────────
    // Telemetry serialization
    //
    // Layout (all IEEE-754 doubles, native byte order):
    //   position.x/y/z          3 doubles  = 24 bytes
    //   velocity_body.x/y/z     3 doubles  = 24 bytes
    //   orientation.w/x/y/z     4 doubles  = 32 bytes
    //   angular_velocity.x/y/z  3 doubles  = 24 bytes
    //   airspeed                 1 double   =  8 bytes
    //   alpha                    1 double   =  8 bytes
    //   beta                     1 double   =  8 bytes
    //   altitude_msl             1 double   =  8 bytes
    //   thrust_current           1 double   =  8 bytes
    //   fuel_mass                1 double   =  8 bytes
    //   sim_state                1 uint8    =  1 byte
    //   -------------------------------------------
    //   total                               153 bytes
    // ──────────────────────────────────────────────

    static constexpr size_t kTelemetryPayloadSize =
        18 * sizeof(double) + sizeof(uint8_t); // 145 bytes

    std::vector<uint8_t> MessageCodec::serialize_telemetry(const AircraftState &state,
                                                           SimState sim_state)
    {
        std::vector<uint8_t> buf;
        buf.reserve(kTelemetryPayloadSize);

        // Position
        write_double(buf, state.position.x);
        write_double(buf, state.position.y);
        write_double(buf, state.position.z);

        // Body-frame velocity
        write_double(buf, state.velocity_body.x);
        write_double(buf, state.velocity_body.y);
        write_double(buf, state.velocity_body.z);

        // Orientation quaternion
        write_double(buf, state.orientation.w);
        write_double(buf, state.orientation.x);
        write_double(buf, state.orientation.y);
        write_double(buf, state.orientation.z);

        // Angular velocity
        write_double(buf, state.angular_velocity.x);
        write_double(buf, state.angular_velocity.y);
        write_double(buf, state.angular_velocity.z);

        // Scalar derived quantities
        write_double(buf, state.airspeed);
        write_double(buf, state.alpha);
        write_double(buf, state.beta);
        write_double(buf, state.altitude_msl);

        // Engine / fuel
        write_double(buf, state.thrust_current);
        write_double(buf, state.fuel_mass);

        // Sim state
        buf.push_back(static_cast<uint8_t>(sim_state));

        return buf;
    }

    // ──────────────────────────────────────────────
    // ControlInput deserialization
    //
    // Layout:
    //   elevator, aileron, rudder, throttle, flaps, trim
    //   6 doubles = 48 bytes
    // ──────────────────────────────────────────────

    static constexpr size_t kControlInputPayloadSize = 6 * sizeof(double);

    bool MessageCodec::deserialize_control_input(const uint8_t *data, size_t len,
                                                 ControlInput &out)
    {
        if (len < kControlInputPayloadSize)
        {
            LOG_WARN("control_input payload too small: %zu < %zu", len, kControlInputPayloadSize);
            return false;
        }

        const uint8_t *ptr = data;
        out.elevator = read_double(ptr);
        out.aileron = read_double(ptr);
        out.rudder = read_double(ptr);
        out.throttle = read_double(ptr);
        out.flaps = read_double(ptr);
        out.trim = read_double(ptr);
        return true;
    }

    // ──────────────────────────────────────────────
    // SimCommand deserialization — 1 byte
    // ──────────────────────────────────────────────

    bool MessageCodec::deserialize_sim_command(const uint8_t *data, size_t len,
                                               SimCommandType &out)
    {
        if (len < 1)
        {
            LOG_WARN("sim_command payload empty");
            return false;
        }

        uint8_t raw = data[0];
        if (raw < static_cast<uint8_t>(SimCommandType::Start) ||
            raw > static_cast<uint8_t>(SimCommandType::Stop))
        {
            LOG_WARN("sim_command unknown subtype: %u", raw);
            return false;
        }

        out = static_cast<SimCommandType>(raw);
        return true;
    }

} // namespace luft
