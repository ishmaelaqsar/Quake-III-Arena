#include <gtest/gtest.h>
#include "../code/sys/net/net_compat.h"
#include "q_shared.h"
#include "qcommon.h"

class SysNet : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        Com_InitSmallZoneMemory();
        Cmd_Init();
        Cvar_Init();
        Com_InitZoneMemory();
        Netchan_Init(0);

#ifdef _WIN32
        // Winsock needs starting before any socket call, which the engine does in NET_Init.
        // NET_Init is avoided here because it also binds a UDP port, which a test should not do.
        WSADATA wsaData;
        ASSERT_EQ(WSAStartup(MAKEWORD(2, 2), &wsaData), 0) << "WSAStartup failed";
#endif
    }

    static void TearDownTestSuite() {
#ifdef _WIN32
        WSACleanup();
#endif
    }
};

TEST_F(SysNet, StringToAdrParsesIPv4AndPort) {
    netadr_t adr;
    qboolean ok = NET_StringToAdr("127.0.0.1:27960", &adr);
    EXPECT_TRUE(ok);
    EXPECT_EQ(adr.type, NA_IP);
    EXPECT_EQ(adr.ip[0], 127);
    EXPECT_EQ(adr.ip[1], 0);
    EXPECT_EQ(adr.ip[2], 0);
    EXPECT_EQ(adr.ip[3], 1);
    EXPECT_EQ(BigShort(adr.port), 27960);
}

TEST_F(SysNet, LoopbackPacketRoundTrip) {
    const char msg[] = "test_loopback_payload";
    netadr_t to;
    memset(&to, 0, sizeof(to));
    to.type = NA_LOOPBACK;

    NET_SendPacket(NS_CLIENT, sizeof(msg), msg, to);

    byte buffer[MAX_MSGLEN];
    msg_t netmsg;
    MSG_Init(&netmsg, buffer, sizeof(buffer));

    netadr_t from;
    qboolean received = NET_GetLoopPacket(NS_SERVER, &from, &netmsg);
    EXPECT_TRUE(received);
    EXPECT_EQ(from.type, NA_LOOPBACK);
    EXPECT_EQ(netmsg.cursize, (int)sizeof(msg));
    EXPECT_STREQ((char *)netmsg.data, msg);
}

TEST_F(SysNet, InvalidSocketSentinelIsNotZero) {
    EXPECT_NE(Q3_INVALID_SOCKET, 0);
}

TEST_F(SysNet, ErrorStringIsNonEmpty) {
    // Assert the socket, rather than wrapping the body in an `if`: a failure to create one
    // would otherwise leave the test passing with no assertions evaluated at all.
    socket_t s = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_NE(s, Q3_INVALID_SOCKET) << "could not create a socket: " << NET_ErrorString();

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1);  // port 1 refuses the connection, which sets the error
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    const int rc = connect(s, (struct sockaddr *)&addr, sizeof(addr));
    EXPECT_EQ(rc, -1) << "connecting to port 1 was expected to fail";

    char *errStr = NET_ErrorString();
    ASSERT_NE(errStr, nullptr);
    EXPECT_GT(strlen(errStr), 0u);

    q3_closesocket(s);
}
