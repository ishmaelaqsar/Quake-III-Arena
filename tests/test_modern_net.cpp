#include <gtest/gtest.h>
#include "../code/modern/net/bitstream.hpp"
#include "../code/modern/net/transport.hpp"

using namespace q3::net;
using namespace q3::math;

TEST(ModernNetTest, BitWriterAndReaderRoundTrip) {
    BitWriter writer;

    writer.write_bits(7, 3);
    writer.write_byte(0x5A);
    writer.write_short(-1234);
    writer.write_int(987654);
    writer.write_float(45.67f);
    writer.write_string("Hello Modern Quake 3");
    writer.write_vec3(Vec3(1.0f, 2.5f, -3.25f));

    BitReader reader(writer.buffer());

    EXPECT_EQ(reader.read_bits(3), 7u);
    EXPECT_EQ(reader.read_byte(), 0x5A);
    EXPECT_EQ(reader.read_short(), -1234);
    EXPECT_EQ(reader.read_int(), 987654);
    EXPECT_FLOAT_EQ(reader.read_float(), 45.67f);
    EXPECT_EQ(reader.read_string(), "Hello Modern Quake 3");

    Vec3 v = reader.read_vec3();
    EXPECT_FLOAT_EQ(v.x, 1.0f);
    EXPECT_FLOAT_EQ(v.y, 2.5f);
    EXPECT_FLOAT_EQ(v.z, -3.25f);
}

TEST(ModernNetTest, LoopbackMultiplayerCommunication) {
    LoopbackTransport client;
    LoopbackTransport server({"127.0.0.1", 27960});

    BitWriter client_pkt;
    client_pkt.write_string("connect");
    client_pkt.write_int(1337);

    // Client pipes data to server (simulating local loopback / offline split-screen packet)
    client.pipe_to(server, client_pkt.data(), client_pkt.byte_size());

    auto server_received = server.receive();
    ASSERT_TRUE(server_received.has_value());

    BitReader server_reader(server_received->payload);
    EXPECT_EQ(server_reader.read_string(), "connect");
    EXPECT_EQ(server_reader.read_int(), 1337);
}
