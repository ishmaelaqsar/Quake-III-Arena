#include <gtest/gtest.h>
#include <SDL.h>

extern "C" {
#include "q_shared.h"
#include "qcommon.h"

void Sys_ModuleFileName(const char *name, char *buf, int bufSize);
}

TEST(ModuleSymbols, FileNameSchemeMatchesPlatform) {
    char buf[256];
    Sys_ModuleFileName("qagame", buf, sizeof(buf));
    std::string expected = std::string("qagame") + ARCH_STRING + DLL_EXT;
    EXPECT_STREQ(buf, expected.c_str());
}

TEST(ModuleSymbols, EveryModuleExportsVmMainAndDllEntry) {
    const char *modules[] = { "qagame", "cgame", "ui" };

    for (const char *modName : modules) {
        char fname[256];
        Sys_ModuleFileName(modName, fname, sizeof(fname));

        std::string fullPath = std::string(Q3_TEST_BUILD_DIR) + "/baseq3/" + fname;
        void *libHandle = SDL_LoadObject(fullPath.c_str());
        if (!libHandle) {
            // Check in build directory directly as fallback
            std::string altPath = std::string(Q3_TEST_BUILD_DIR) + "/" + fname;
            libHandle = SDL_LoadObject(altPath.c_str());
        }

        if (!libHandle) {
            // Module might not be built in this specific test run
            GTEST_SKIP() << "Module " << fname << " not found in " << fullPath;
            continue;
        }

        void *vmMain = SDL_LoadFunction(libHandle, "vmMain");
        void *dllEntry = SDL_LoadFunction(libHandle, "dllEntry");

        EXPECT_NE(vmMain, nullptr) << "vmMain missing from " << modName;
        EXPECT_NE(dllEntry, nullptr) << "dllEntry missing from " << modName;

        SDL_UnloadObject(libHandle);
    }
}
