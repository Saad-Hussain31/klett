#pragma once

#include "protocol.h"
#include "../aircraft_state.h"

#include <cstdint>
#include <cstddef>
#include <vector>
#include <queue>

namespace luft
{

    // ──────────────────────────────────────────────
    // A fully decoded message ready for dispatch
    // ──────────────────────────────────────────────

    struct FramedMessage
    {
        MessageType type;
        uint16_t sequence;
        std::vector<uint8_t> payload;
    };

    // ──────────────────────────────────────────────
    // MessageCodec — framing, serialization, deserialization
    // ──────────────────────────────────────────────

    class MessageCodec
    {
    public:
        // ── Receiving ─────────────────────────────

        /// Feed raw bytes from the socket into the receive buffer.
        void feed(const uint8_t *data, size_t len);

        /// True when at least one complete message has been assembled.
        [[nodiscard]] bool has_message() const;

        /// Pop the next complete message. Undefined if has_message() is false.
        FramedMessage pop_message();

        // ── Sending ───────────────────────────────

        /// Build a complete wire frame: header (network byte order) + payload.
        static std::vector<uint8_t> encode(MessageType type, uint16_t sequence,
                                           const uint8_t *payload, size_t payload_len);

        // ── Telemetry serialization ───────────────

        /// Serialize aircraft state + sim state into a binary payload.
        static std::vector<uint8_t> serialize_telemetry(const AircraftState &state,
                                                        SimState sim_state);

        // ── Deserialization helpers ────────────────

        /// Decode a ControlInput payload.  Returns false on size mismatch.
        static bool deserialize_control_input(const uint8_t *data, size_t len,
                                              ControlInput &out);

        /// Decode a SimCommand payload.  Returns false on size mismatch.
        static bool deserialize_sim_command(const uint8_t *data, size_t len,
                                            SimCommandType &out);

    private:
        /// Try to parse complete messages from the receive buffer.
        void try_parse();

        std::vector<uint8_t> recv_buf_;
        std::queue<FramedMessage> messages_;
    };

} // namespace luft
