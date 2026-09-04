#include <gtest/gtest.h>
#include "engine_init.hpp"
#include "../code/sys/sys_api.h"
#include "../code/sys/threading/job_system.hpp"

extern "C" void Com_Shutdown(void);

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

// Regression for checklist 02 step B5.2. Sys_SubsystemShutdown existed and was correct, but
// nothing in the engine called it, so the job system's workers, the download, and the script
// engine outlived Com_Shutdown. Drive the engine entry point rather than Sys_SubsystemShutdown:
// a direct call is exactly what let the defect pass for a day.
TEST(SysApiBoundary, ComShutdownStopsTheJobSystem) {
    EnsureEngineInitialised();
    Sys_SubsystemInit();
    EXPECT_GT(q3::threading::JobSystem::instance().worker_count(), 0u);

    Com_Shutdown();

    EXPECT_EQ(q3::threading::JobSystem::instance().worker_count(), 0u);
}
