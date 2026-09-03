#include <gtest/gtest.h>
#include <iomanip>
#include <sstream>
#include <string>
#include "q_shared.h"
#include "qcommon.h"

extern "C" {
typedef struct {
    unsigned int state[4];
    unsigned int count[2];
    unsigned char buffer[64];
} MD4_CTX;

void MD4Init(MD4_CTX *);
void MD4Update(MD4_CTX *, const unsigned char *, unsigned int);
void MD4Final(unsigned char [16], MD4_CTX *);
}

static std::string ComputeMD4Hex(const std::string &input) {
    MD4_CTX ctx;
    unsigned char digest[16];
    MD4Init(&ctx);
    MD4Update(&ctx, reinterpret_cast<const unsigned char *>(input.data()), input.size());
    MD4Final(digest, &ctx);

    std::ostringstream oss;
    for (int i = 0; i < 16; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(digest[i]);
    }
    return oss.str();
}

TEST(MD4Test, StandardRFC1320Vectors) {
    EXPECT_EQ(ComputeMD4Hex(""), "31d6cfe0d16ae931b73c59d7e0c089c0");
    EXPECT_EQ(ComputeMD4Hex("a"), "bde52cb31de33e46245e05fbdbd6fb24");
    EXPECT_EQ(ComputeMD4Hex("abc"), "a448017aaf21d8525fc10ae87aa6729d");
    EXPECT_EQ(ComputeMD4Hex("message digest"), "d9130a8164549fe818874806e1c7014b");
    EXPECT_EQ(ComputeMD4Hex("abcdefghijklmnopqrstuvwxyz"), "d79e1c308aa5bbcdeea8ed63df412da9");
}

TEST(MD4Test, BlockChecksum) {
    char data1[] = "Quake 3 arena test data";
    char data2[] = "Quake 3 arena test data";
    char data3[] = "Quake 3 arena test data modified";

    unsigned sum1 = Com_BlockChecksum(data1, sizeof(data1));
    unsigned sum2 = Com_BlockChecksum(data2, sizeof(data2));
    unsigned sum3 = Com_BlockChecksum(data3, sizeof(data3));

    EXPECT_EQ(sum1, sum2);
    EXPECT_NE(sum1, sum3);
}
