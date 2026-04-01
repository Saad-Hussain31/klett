#include <gtest/gtest.h>
#include "protocol.h"
#include "socket.h"
#include <cstring>
#include <arpa/inet.h>

using namespace luft;

// ── Header Pack/Unpack ──────────────────────────

TEST(Protocol, PackUnpackHeaderRoundtrip)
{
    MessageHeader hdr;
    hdr.length = 1024;
    hdr.type = static_cast<uint16_t>(MessageType::Telemetry);
    hdr.sequence = 42;

    uint8_t buf[kHeaderSize];
    pack_header(buf, hdr);

    MessageHeader result = unpack_header(buf);
    EXPECT_EQ(result.length, 1024u);
    EXPECT_EQ(result.type, static_cast<uint16_t>(MessageType::Telemetry));
    EXPECT_EQ(result.sequence, 42);
}

TEST(Protocol, PackUnpackMultipleValues)
{
    for (uint32_t len : {8u, 100u, 65536u})
    {
        for (uint16_t seq : {0, 1, 1000, 65535})
        {
            MessageHeader hdr;
            hdr.length = len;
            hdr.type = static_cast<uint16_t>(MessageType::ControlInput);
            hdr.sequence = seq;

            uint8_t buf[kHeaderSize];
            pack_header(buf, hdr);

            MessageHeader result = unpack_header(buf);
            EXPECT_EQ(result.length, len);
            EXPECT_EQ(result.type, static_cast<uint16_t>(MessageType::ControlInput));
            EXPECT_EQ(result.sequence, seq);
        }
    }
}

TEST(Protocol, NetworkByteOrder)
{
    MessageHeader hdr;
    hdr.length = 0x01020304;
    hdr.type = 0x0506;
    hdr.sequence = 0x0708;

    uint8_t buf[kHeaderSize];
    pack_header(buf, hdr);

    // Verify network byte order (big-endian) for length
    EXPECT_EQ(buf[0], 0x01);
    EXPECT_EQ(buf[1], 0x02);
    EXPECT_EQ(buf[2], 0x03);
    EXPECT_EQ(buf[3], 0x04);

    // Type
    EXPECT_EQ(buf[4], 0x05);
    EXPECT_EQ(buf[5], 0x06);

    // Sequence
    EXPECT_EQ(buf[6], 0x07);
    EXPECT_EQ(buf[7], 0x08);
}

TEST(Protocol, HeaderSizeIs8Bytes)
{
    EXPECT_EQ(kHeaderSize, 8u);
    EXPECT_EQ(sizeof(MessageHeader), 8u);
}

// ── TcpListener Tests ───────────────────────────

TEST(TcpListener, BindToLocalhostValidFd)
{
    TcpListener listener;
    // Use port 0 to let the OS choose an available port
    bool ok = listener.bind_and_listen("127.0.0.1", 0);
    ASSERT_TRUE(ok);
    EXPECT_TRUE(listener.valid());
    EXPECT_GE(listener.fd(), 0);
    listener.close();
}

TEST(TcpListener, AcceptOnEmptyListenerReturnsInvalid)
{
    TcpListener listener;
    bool ok = listener.bind_and_listen("127.0.0.1", 0);
    ASSERT_TRUE(ok);

    // No one is connecting, so accept should return invalid socket
    TcpSocket sock = listener.accept_connection();
    EXPECT_FALSE(sock.valid());
    EXPECT_EQ(sock.fd(), -1);

    listener.close();
}

TEST(TcpListener, CloseInvalidatesListener)
{
    TcpListener listener;
    listener.bind_and_listen("127.0.0.1", 0);
    EXPECT_TRUE(listener.valid());
    listener.close();
    EXPECT_FALSE(listener.valid());
    EXPECT_EQ(listener.fd(), -1);
}

// ── TcpSocket Tests ─────────────────────────────

TEST(TcpSocket, DefaultConstructorInvalid)
{
    TcpSocket sock;
    EXPECT_FALSE(sock.valid());
    EXPECT_EQ(sock.fd(), -1);
}

TEST(TcpSocket, MoveConstructor)
{
    TcpSocket a(42);
    TcpSocket b(std::move(a));
    EXPECT_EQ(b.fd(), 42);
    EXPECT_EQ(a.fd(), -1);
    // Prevent closing invalid fds in destructors
    b = TcpSocket(-1);
}

TEST(TcpSocket, MoveAssignment)
{
    TcpSocket a(100);
    TcpSocket b;
    b = std::move(a);
    EXPECT_EQ(b.fd(), 100);
    EXPECT_EQ(a.fd(), -1);
    // Prevent closing invalid fds
    b = TcpSocket(-1);
}
