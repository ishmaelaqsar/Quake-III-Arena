#include <gtest/gtest.h>
#include <vector>

extern "C" {
#include "q_shared.h"
#include "qcommon.h"
}

TEST(MsgTest, BasicByteShortLongFloat) {
    byte buffer[1024];
    msg_t msg;

    MSG_Init(&msg, buffer, sizeof(buffer));

    MSG_WriteByte(&msg, 0xAB);
    MSG_WriteShort(&msg, 0x1234);
    MSG_WriteLong(&msg, 0x76543210);
    MSG_WriteFloat(&msg, 3.14159f);
    MSG_WriteString(&msg, "Quake 3 C++17");

    MSG_BeginReading(&msg);

    EXPECT_EQ(MSG_ReadByte(&msg), 0xAB);
    EXPECT_EQ(MSG_ReadShort(&msg), 0x1234);
    EXPECT_EQ(MSG_ReadLong(&msg), 0x76543210);
    EXPECT_FLOAT_EQ(MSG_ReadFloat(&msg), 3.14159f);
    EXPECT_STREQ(MSG_ReadString(&msg), "Quake 3 C++17");
}

TEST(MsgTest, BitOperations) {
    byte buffer[1024];
    msg_t msg;

    MSG_Init(&msg, buffer, sizeof(buffer));

    MSG_WriteBits(&msg, 5, 3);    // 3 bits: 5 (101 in binary)
    MSG_WriteBits(&msg, 13, 4);   // 4 bits: 13 (1101 in binary)
    MSG_WriteBits(&msg, 255, 8);  // 8 bits: 255 (11111111 in binary)

    MSG_BeginReading(&msg);

    EXPECT_EQ(MSG_ReadBits(&msg, 3), 5);
    EXPECT_EQ(MSG_ReadBits(&msg, 4), 13);
    EXPECT_EQ(MSG_ReadBits(&msg, 8), 255);
}

TEST(MsgTest, OutOfBandMessages) {
    byte buffer[1024];
    msg_t msg;

    MSG_InitOOB(&msg, buffer, sizeof(buffer));
    EXPECT_TRUE(msg.oob);

    MSG_WriteLong(&msg, -1);
    MSG_WriteString(&msg, "getinfo");

    MSG_BeginReadingOOB(&msg);
    EXPECT_EQ(MSG_ReadLong(&msg), -1);
    EXPECT_STREQ(MSG_ReadString(&msg), "getinfo");
}
