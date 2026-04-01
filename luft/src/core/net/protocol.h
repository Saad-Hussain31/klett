#pragma once

#include <cstdint>
#include <cstring>
#include <arpa/inet.h>

namespace luft
{

    // ──────────────────────────────────────────────
    // Message types carried over the wire
    // ──────────────────────────────────────────────

    enum class MessageType : uint16_t
    {
        Telemetry = 0x0001,    // server -> client: aircraft state
        ControlInput = 0x0002, // client -> server: control commands
        SimCommand = 0x0003,   // client -> server: start/stop/pause/reset
        ConfigUpdate = 0x0004, // client -> server: config change
        Ack = 0x0010,          // server -> client: acknowledgment
        Nack = 0x0011,         // server -> client: negative ack
    };

    // ──────────────────────────────────────────────
    // Simulation command subtypes
    // ──────────────────────────────────────────────

    enum class SimCommandType : uint8_t
    {
        Start = 1,
        Pause = 2,
        Resume = 3,
        Reset = 4,
        Stop = 5,
    };

    // ──────────────────────────────────────────────
    // Wire header — 8 bytes, packed, network byte order
    // ──────────────────────────────────────────────

    struct __attribute__((packed)) MessageHeader
    {
        uint32_t length;   // total message length including header
        uint16_t type;     // MessageType
        uint16_t sequence; // sequence number for ordering
    };

    static_assert(sizeof(MessageHeader) == 8, "MessageHeader must be exactly 8 bytes");

    static constexpr size_t kHeaderSize = sizeof(MessageHeader);
    static constexpr size_t kMaxMessageSize = 65536;

    // ──────────────────────────────────────────────
    // Header serialization helpers (network byte order)
    // ──────────────────────────────────────────────

    inline void pack_header(uint8_t *dst, const MessageHeader &hdr)
    {
        MessageHeader net;
        net.length = htonl(hdr.length);
        net.type = htons(hdr.type);
        net.sequence = htons(hdr.sequence);
        std::memcpy(dst, &net, kHeaderSize);
    }

    inline MessageHeader unpack_header(const uint8_t *src)
    {
        MessageHeader net;
        std::memcpy(&net, src, kHeaderSize);
        MessageHeader hdr;
        hdr.length = ntohl(net.length);
        hdr.type = ntohs(net.type);
        hdr.sequence = ntohs(net.sequence);
        return hdr;
    }

} // namespace luft
