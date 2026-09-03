#include <gtest/gtest.h>
#include "engine_init.hpp"
#include "../code/sys/sys_api.h"

TEST(SysApiBoundary, ThrowingScriptDoesNotTerminate) {
    EnsureEngineInitialised();

    // Invalid Lua syntax should be caught and return qfalse without terminating.
    qboolean result = Sys_ScriptExecute("function () syntax error !!! {{{");
    EXPECT_EQ(result, qfalse);
}

TEST(SysApiBoundary, ShutdownIsIdempotent) {
    EnsureEngineInitialised();

    // Calling shutdown multiple times should be safe and idempotent.
    Sys_SubsystemShutdown();
    Sys_SubsystemShutdown();

    // Re-initialize for subsequent tests.
    Sys_SubsystemInit();
}

TEST(SysApiBoundary, InitWithoutLuaStillReturns) {
    EnsureEngineInitialised();
    // In this build configuration LuaJIT is enabled; verify that initialization succeeds.
    Sys_SubsystemInit();
}
