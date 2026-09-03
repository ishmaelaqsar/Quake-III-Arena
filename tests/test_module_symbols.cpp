/*
Tests that every game module is loadable and exports its entry points under the name the engine
looks up (checklist 01 phase A3, checklist 04 phase P0).

This is the tripwire for C++ name mangling. `Sys_LoadDll` resolves "vmMain" and "dllEntry" by
string, so if a module is ever compiled as C++ without C linkage on those two functions, the
symbols mangle, the lookup returns null, and the engine silently falls back to the bytecode
virtual machine. These tests fail rather than skip when a module is missing, because a test that
skips cannot catch that regression.
*/

#include <gtest/gtest.h>
#include <SDL.h>

#include <string>
#include <vector>

#include "q_shared.h"
#include "qcommon.h"

extern "C" void Sys_ModuleFileName(const char *name, char *buf, int bufSize);

namespace {

const char *const kModules[] = {"qagame", "cgame", "ui"};

std::string ModuleFileName(const char *name) {
    char buf[MAX_OSPATH];
    Sys_ModuleFileName(name, buf, sizeof(buf));
    return buf;
}

// The modules are written to <build>/baseq3. Older layouts left them in <build>, so try both
// and report every path that was tried when the module is genuinely absent.
void *OpenModule(const std::string &fileName, std::string *triedPaths) {
    const std::string candidates[] = {
        std::string(Q3_TEST_BUILD_DIR) + "/baseq3/" + fileName,
        std::string(Q3_TEST_BUILD_DIR) + "/" + fileName,
    };

    for (const std::string &path : candidates) {
        if (void *handle = SDL_LoadObject(path.c_str())) {
            return handle;
        }
        triedPaths->append("\n  ").append(path).append(": ").append(SDL_GetError());
    }
    return nullptr;
}

}  // namespace

// The file name scheme must match what the build produces, or Sys_LoadDll never finds a module.
TEST(ModuleSymbols, FileNameSchemeMatchesPlatform) {
    EXPECT_EQ(ModuleFileName("qagame"), std::string("qagame") + ARCH_STRING + DLL_EXT);
    EXPECT_EQ(ModuleFileName("cgame"), std::string("cgame") + ARCH_STRING + DLL_EXT);
}

// Every module loads and exports both entry points under their unmangled names.
TEST(ModuleSymbols, EveryModuleExportsUnmangledEntryPoints) {
    for (const char *moduleName : kModules) {
        const std::string fileName = ModuleFileName(moduleName);
        std::string tried;

        void *handle = OpenModule(fileName, &tried);
        ASSERT_NE(handle, nullptr) << "could not load " << fileName << ". Tried:" << tried;

        // Resolved by string, exactly as Sys_LoadDll does it. A mangled symbol fails here.
        EXPECT_NE(SDL_LoadFunction(handle, "vmMain"), nullptr)
            << "vmMain is not exported unmangled from " << fileName
            << "; the module may have been compiled as C++ without extern \"C\"";
        EXPECT_NE(SDL_LoadFunction(handle, "dllEntry"), nullptr)
            << "dllEntry is not exported unmangled from " << fileName
            << "; the module may have been compiled as C++ without extern \"C\"";

        SDL_UnloadObject(handle);
    }
}
