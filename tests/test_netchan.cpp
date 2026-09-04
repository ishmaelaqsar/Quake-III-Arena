#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "engine_fixture.hpp"
#include "q_shared.h"
#include "qcommon.h"

#ifndef FRAGMENT_SIZE
#define FRAGMENT_SIZE (1400 - 100)
#endif

namespace {

struct CapturedPacket {
    int length;
    std::vector<byte> data;
    netadr_t to;
};

std::vector<CapturedPacket> g_capturedPackets;

void TestSendPacketOverride(int length, const void *data, netadr_t to) {
    CapturedPacket pkt;
    pkt.length = length;
    pkt.data.assign(static_cast<const byte *>(data), static_cast<const byte *>(data) + length);
    pkt.to = to;
    g_capturedPackets.push_back(std::move(pkt));
}

class NetchanFixture : public q3::test::EngineFixture {
protected:
    void SetUp() override {
        q3::test::EngineFixture::SetUp();
        g_capturedPackets.clear();
        Sys_SetSendPacketOverride(TestSendPacketOverride);
        Netchan_Init(0x1234);
    }

    void TearDown() override {
        Sys_SetSendPacketOverride(nullptr);
        g_capturedPackets.clear();
    }
};

} // namespace

TEST_F(NetchanFixture, SmallMessageRoundTrips) {
    netchan_t a{}, b{};
    netadr_t adr_a{}, adr_b{};
    adr_a.type = NA_IP;
    adr_a.port = 1234;
    adr_b.type = NA_IP;
    adr_b.port = 5678;

    Netchan_Setup(NS_CLIENT, &a, adr_b, 0x1234);
    Netchan_Setup(NS_SERVER, &b, adr_a, 0x1234);

    byte send_buf[100];
    for (int i = 0; i < 100; ++i) {
        send_buf[i] = static_cast<byte>(i + 1);
    }

    Netchan_Transmit(&a, 100, send_buf);
    ASSERT_EQ(g_capturedPackets.size(), 1u);

    byte recv_buf[MAX_MSGLEN];
    msg_t msg;
    MSG_Init(&msg, recv_buf, sizeof(recv_buf));
    std::memcpy(msg.data, g_capturedPackets[0].data.data(), g_capturedPackets[0].length);
    msg.cursize = g_capturedPackets[0].length;

    qboolean ok = Netchan_Process(&b, &msg);
    EXPECT_TRUE(ok);
    int payload_len = msg.cursize - msg.readcount;
    EXPECT_EQ(payload_len, 100);
    EXPECT_EQ(std::memcmp(msg.data + msg.readcount, send_buf, 100), 0);
}

TEST_F(NetchanFixture, LargeMessageFragmentsAndReassembles) {
    netchan_t a{}, b{};
    netadr_t adr_a{}, adr_b{};
    adr_a.type = NA_IP;
    adr_a.port = 1234;
    adr_b.type = NA_IP;
    adr_b.port = 5678;

    Netchan_Setup(NS_CLIENT, &a, adr_b, 0x1234);
    Netchan_Setup(NS_SERVER, &b, adr_a, 0x1234);

    const int total_len = 3 * FRAGMENT_SIZE;
    std::vector<byte> send_buf(total_len);
    for (int i = 0; i < total_len; ++i) {
        send_buf[i] = static_cast<byte>((i * 7 + 3) & 0xFF);
    }

    Netchan_Transmit(&a, total_len, send_buf.data());
    ASSERT_EQ(g_capturedPackets.size(), 1u);

    // Send remaining fragments
    std::vector<CapturedPacket> all_packets;
    all_packets.push_back(std::move(g_capturedPackets[0]));
    g_capturedPackets.clear();

    while (a.unsentFragments) {
        Netchan_TransmitNextFragment(&a);
        ASSERT_EQ(g_capturedPackets.size(), 1u);
        all_packets.push_back(std::move(g_capturedPackets[0]));
        g_capturedPackets.clear();
    }

    ASSERT_GT(all_packets.size(), 1u);

    byte recv_buf[MAX_MSGLEN];
    msg_t msg;
    qboolean ok = qfalse;

    for (size_t i = 0; i < all_packets.size(); ++i) {
        MSG_Init(&msg, recv_buf, sizeof(recv_buf));
        std::memcpy(msg.data, all_packets[i].data.data(), all_packets[i].length);
        msg.cursize = all_packets[i].length;

        ok = Netchan_Process(&b, &msg);
        if (i + 1 < all_packets.size()) {
            EXPECT_FALSE(ok);
        }
    }

    EXPECT_TRUE(ok);
    int payload_len = msg.cursize - msg.readcount;
    EXPECT_EQ(payload_len, total_len);
    EXPECT_EQ(std::memcmp(msg.data + msg.readcount, send_buf.data(), total_len), 0);
    EXPECT_EQ(b.incomingSequence, a.outgoingSequence - 1);
}

TEST_F(NetchanFixture, DuplicateSequenceIsRejected) {
    netchan_t a{}, b{};
    netadr_t adr_a{}, adr_b{};
    adr_a.type = NA_IP;
    adr_a.port = 1234;
    adr_b.type = NA_IP;
    adr_b.port = 5678;

    Netchan_Setup(NS_CLIENT, &a, adr_b, 0x1234);
    Netchan_Setup(NS_SERVER, &b, adr_a, 0x1234);

    byte send_buf[32] = {1, 2, 3, 4};
    Netchan_Transmit(&a, 32, send_buf);
    ASSERT_EQ(g_capturedPackets.size(), 1u);

    byte recv_buf[MAX_MSGLEN];
    msg_t msg;
    MSG_Init(&msg, recv_buf, sizeof(recv_buf));
    std::memcpy(msg.data, g_capturedPackets[0].data.data(), g_capturedPackets[0].length);
    msg.cursize = g_capturedPackets[0].length;

    // First process succeeds
    EXPECT_TRUE(Netchan_Process(&b, &msg));

    // Duplicate process of same sequence must be rejected
    MSG_Init(&msg, recv_buf, sizeof(recv_buf));
    std::memcpy(msg.data, g_capturedPackets[0].data.data(), g_capturedPackets[0].length);
    msg.cursize = g_capturedPackets[0].length;
    EXPECT_FALSE(Netchan_Process(&b, &msg));
}

TEST_F(NetchanFixture, OutOfOrderIsDroppedAndCounted) {
    netchan_t a{}, b{};
    netadr_t adr_a{}, adr_b{};
    adr_a.type = NA_IP;
    adr_a.port = 1234;
    adr_b.type = NA_IP;
    adr_b.port = 5678;

    Netchan_Setup(NS_CLIENT, &a, adr_b, 0x1234);
    Netchan_Setup(NS_SERVER, &b, adr_a, 0x1234);

    Cvar_Set("showdrop", "1");

    byte send_buf[32] = {1};
    Netchan_Transmit(&a, 32, send_buf); // seq 0
    Netchan_Transmit(&a, 32, send_buf); // seq 1
    Netchan_Transmit(&a, 32, send_buf); // seq 2
    ASSERT_EQ(g_capturedPackets.size(), 3u);

    byte recv_buf[MAX_MSGLEN];
    msg_t msg;

    // Process packet 0
    MSG_Init(&msg, recv_buf, sizeof(recv_buf));
    std::memcpy(msg.data, g_capturedPackets[0].data.data(), g_capturedPackets[0].length);
    msg.cursize = g_capturedPackets[0].length;
    EXPECT_TRUE(Netchan_Process(&b, &msg));

    g_sysPrintBuffer.clear();

    // Skip packet 1 and process packet 2 directly
    MSG_Init(&msg, recv_buf, sizeof(recv_buf));
    std::memcpy(msg.data, g_capturedPackets[2].data.data(), g_capturedPackets[2].length);
    msg.cursize = g_capturedPackets[2].length;
    EXPECT_TRUE(Netchan_Process(&b, &msg));

    EXPECT_NE(g_sysPrintBuffer.find("Dropped 1 packets"), std::string::npos)
        << "Captured print did not report drop: " << g_sysPrintBuffer;
}
