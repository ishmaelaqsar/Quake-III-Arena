#include <gtest/gtest.h>
#include <cstring>

#include "q_shared.h"
#include "qcommon.h"


TEST(StringTest, StrncpyzSafety) {
    char dest[8];
    Q_strncpyz(dest, "HelloWorld123", sizeof(dest));
    EXPECT_EQ(std::strlen(dest), 7u);
    EXPECT_STREQ(dest, "HelloWo");
    EXPECT_EQ(dest[7], '\0');

    Q_strncpyz(dest, "Hi", sizeof(dest));
    EXPECT_STREQ(dest, "Hi");
    EXPECT_EQ(dest[2], '\0');
}

TEST(StringTest, StrcatSafety) {
    char dest[12] = "Hello";
    Q_strcat(dest, sizeof(dest), " World!");
    EXPECT_EQ(std::strlen(dest), 11u);
    EXPECT_STREQ(dest, "Hello World");
    EXPECT_EQ(dest[11], '\0');
}

TEST(StringTest, CaseInsensitiveCompare) {
    EXPECT_EQ(Q_stricmp("Quake3", "quake3"), 0);
    EXPECT_EQ(Q_stricmp("QUAKE", "quake"), 0);
    EXPECT_NE(Q_stricmp("Quake3", "Quake4"), 0);

    EXPECT_EQ(Q_stricmpn("Quake3Arena", "quake3Test", 6), 0);
    EXPECT_NE(Q_stricmpn("Quake3Arena", "quake4Test", 6), 0);
}

TEST(StringTest, CleanStrColorCodes) {
    char colored[64] = "Hello ^1Red ^2Green ^3Yellow";
    Q_CleanStr(colored);
    EXPECT_STREQ(colored, "Hello Red Green Yellow");
}

TEST(StringTest, ComSprintfFormatting) {
    char buffer[32];
    Com_sprintf(buffer, sizeof(buffer), "Value: %d, String: %s", 42, "test");
    EXPECT_STREQ(buffer, "Value: 42, String: test");

    // Overflow truncation check
    char smallBuf[10];
    Com_sprintf(smallBuf, sizeof(smallBuf), "123456789012345");
    EXPECT_EQ(std::strlen(smallBuf), 9u);
    EXPECT_STREQ(smallBuf, "123456789");
    EXPECT_EQ(smallBuf[9], '\0');
}

TEST(StringTest, InfoStrings) {
    char info[MAX_INFO_STRING] = "";

    Info_SetValueForKey(info, "name", "UnnamedPlayer");
    Info_SetValueForKey(info, "model", "sarge");
    Info_SetValueForKey(info, "rate", "25000");

    EXPECT_STREQ(Info_ValueForKey(info, "name"), "UnnamedPlayer");
    EXPECT_STREQ(Info_ValueForKey(info, "model"), "sarge");
    EXPECT_STREQ(Info_ValueForKey(info, "rate"), "25000");
    EXPECT_STREQ(Info_ValueForKey(info, "nonexistent"), "");

    // Update existing key
    Info_SetValueForKey(info, "name", "Ranger");
    EXPECT_STREQ(Info_ValueForKey(info, "name"), "Ranger");

    // Remove key
    Info_RemoveKey(info, "model");
    EXPECT_STREQ(Info_ValueForKey(info, "model"), "");
    EXPECT_STREQ(Info_ValueForKey(info, "name"), "Ranger");
}

TEST(QShared, QbooleanIsFourBytes) {
    static_assert(sizeof(qboolean) == 4, "qboolean must be 4 bytes");
    EXPECT_EQ(qfalse, 0);
    EXPECT_EQ(qtrue, 1);
}
