#include <gtest/gtest.h>

extern "C" {
#include "q_shared.h"
#include "qcommon.h"
}

class CvarCmdFixture : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        Com_InitSmallZoneMemory();
        Cmd_Init();
        Cvar_Init();
        Com_InitZoneMemory();
    }
};

TEST_F(CvarCmdFixture, CvarGetAndSet) {
    cvar_t *cv = Cvar_Get("test_cvar", "100", CVAR_ARCHIVE);
    ASSERT_NE(cv, nullptr);
    EXPECT_STREQ(cv->string, "100");
    EXPECT_EQ(cv->integer, 100);
    EXPECT_FLOAT_EQ(cv->value, 100.0f);

    Cvar_Set("test_cvar", "250");
    EXPECT_STREQ(Cvar_VariableString("test_cvar"), "250");
    EXPECT_EQ(Cvar_VariableIntegerValue("test_cvar"), 250);
    EXPECT_FLOAT_EQ(Cvar_VariableValue("test_cvar"), 250.0f);
}

TEST_F(CvarCmdFixture, CommandTokenization) {
    Cmd_TokenizeString("set test_var \"hello world\" 123");
    EXPECT_EQ(Cmd_Argc(), 4);
    EXPECT_STREQ(Cmd_Argv(0), "set");
    EXPECT_STREQ(Cmd_Argv(1), "test_var");
    EXPECT_STREQ(Cmd_Argv(2), "hello world");
    EXPECT_STREQ(Cmd_Argv(3), "123");
}

TEST_F(CvarCmdFixture, DefaultFollowsBuildKind) {
    cvar_t *cv = Cvar_Get("test_dedicated_var", Sys_IsDedicatedBuild() ? "1" : "0",
                          Sys_IsDedicatedBuild() ? CVAR_ROM : CVAR_LATCH);
    ASSERT_NE(cv, nullptr);
    EXPECT_STREQ(cv->string, "1");
    EXPECT_EQ(cv->integer, 1);
    EXPECT_TRUE(cv->flags & CVAR_ROM);
}
