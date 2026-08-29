#include <gtest/gtest.h>
#include <cstring>
#include <vector>

extern "C" {
#include "q_shared.h"
#include "qcommon.h"
}

TEST(HuffmanTest, CompressAndDecompress) {
    const char *testText = "Quake III Arena modernization with C++17 and GoogleTest. "
                           "The quick brown fox jumps over the lazy dog. "
                           "Testing repetitive data data data data 1234567890.";
    int textLen = (int)std::strlen(testText) + 1;

    byte buffer[2048];
    // Copy the text starting at offset 12 (simulating netchan header)
    int offset = 12;
    std::memcpy(buffer + offset, testText, textLen);

    msg_t msg;
    MSG_Init(&msg, buffer, sizeof(buffer));
    msg.cursize = offset + textLen;

    Huff_Compress(&msg, offset);
    EXPECT_GT(msg.cursize, offset);

    Huff_Decompress(&msg, offset);
    EXPECT_EQ(msg.cursize, offset + textLen);
    EXPECT_STREQ((char *)(msg.data + offset), testText);
}
