/*
Tests for the 64-bit virtual machine application binary interface (checklist 02 phase B1).

These tests load the real shared module built from tests/vm_testmodule/tm_main.c through
VM_Create with VMI_NATIVE, then call into it with VM_Call. The important case is
HeapPointerSurvivesRoundTrip: a heap address does not fit in 32 bits on the platforms this fork
targets, so any `int` left in the call path truncates it and the engine reads a wild pointer.
The failure is silent, which is why it is tested rather than reviewed.
*/

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

#include "engine_init.hpp"
#include "q_shared.h"
#include "qcommon.h"

namespace {

// Must match the command numbers in tests/vm_testmodule/tm_main.c.
enum TestModuleCommand {
    TM_ADD = 0,
    TM_SYSCALL_ECHO = 1,
    TM_SYSCALL_POINTER = 2,
};

void *g_pointerToReturn = nullptr;
intptr_t g_lastEchoArgument = 0;

// Engine-side syscall handler, with the same signature the real handlers use.
intptr_t TestSyscalls(intptr_t *args) {
    switch (args[0]) {
        case TM_SYSCALL_ECHO:
            g_lastEchoArgument = args[1];
            return args[1] * 2;
        case TM_SYSCALL_POINTER:
            return reinterpret_cast<intptr_t>(g_pointerToReturn);
        default:
            return -1;
    }
}

class VmAbiFixture : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        EnsureEngineInitialised();

        // Sys_LoadDll searches <path>/<fs_game>/<name><arch><ext>. The test module is built
        // into <build>/tests/baseq3, so point the filesystem cvars at that directory.
        Cvar_Set("fs_basepath", Q3_TEST_BUILD_DIR "/tests");
        Cvar_Set("fs_game", "baseq3");

        s_vm = VM_Create("testmodule", TestSyscalls, VMI_NATIVE);
    }

    static void TearDownTestSuite() {
        if (s_vm != nullptr) {
            VM_Free(s_vm);
            s_vm = nullptr;
        }
    }

    void SetUp() override {
        ASSERT_NE(s_vm, nullptr)
            << "VM_Create could not load the test module from " << Q3_TEST_BUILD_DIR
            << "/tests/baseq3. Build the testmodule target.";
    }

    static vm_t *s_vm;
};

vm_t *VmAbiFixture::s_vm = nullptr;

// Arguments reach the module in the right order and the return value comes back.
TEST_F(VmAbiFixture, AddCommandReturnsSum) {
    EXPECT_EQ(VM_Call(s_vm, TM_ADD, 3, 4), 7);
    EXPECT_EQ(VM_Call(s_vm, TM_ADD, -5, 5), 0);
}

// An argument survives the module -> engine direction, and the handler's return survives the
// engine -> module direction.
TEST_F(VmAbiFixture, SyscallArgumentAndReturnSurvive) {
    g_lastEchoArgument = 0;
    EXPECT_EQ(VM_Call(s_vm, TM_SYSCALL_ECHO, 21), 42);
    EXPECT_EQ(g_lastEchoArgument, 21);
}

// The regression test for the truncation bug: a full-width pointer travels engine -> module
// (as a syscall return) and module -> engine (as the vmMain return) without losing bits.
TEST_F(VmAbiFixture, HeapPointerSurvivesRoundTrip) {
    void *allocated = std::malloc(64);
    ASSERT_NE(allocated, nullptr);
    g_pointerToReturn = allocated;

    const intptr_t returned = VM_Call(s_vm, TM_SYSCALL_POINTER);

    EXPECT_EQ(returned, reinterpret_cast<intptr_t>(allocated))
        << "pointer changed in the round trip; a 32-bit type is left in the call path";
    EXPECT_EQ(reinterpret_cast<void *>(returned), allocated);

    std::free(allocated);
    g_pointerToReturn = nullptr;
}

// A pointer with bits set above the low 32 must survive too, which is what actually fails when
// an `int` is left in the path. The value is never dereferenced.
TEST_F(VmAbiFixture, HighAddressSurvivesRoundTrip) {
    if (sizeof(void *) <= 4) {
        GTEST_SKIP() << "32-bit platform, no high addresses to test";
    }

    void *const highAddress = reinterpret_cast<void *>(static_cast<intptr_t>(0x0000555500001234LL));
    g_pointerToReturn = highAddress;

    const intptr_t returned = VM_Call(s_vm, TM_SYSCALL_POINTER);

    EXPECT_EQ(returned, reinterpret_cast<intptr_t>(highAddress))
        << "high address bits were lost in the round trip";
    g_pointerToReturn = nullptr;
}

TEST_F(VmAbiFixture, MissingModuleReturnsNull) {
    vm_t *missing = nullptr;
    try {
        missing = VM_Create("nonexistent_module", TestSyscalls, VMI_NATIVE);
    } catch (const q3::test::SysErrorException &) {
        missing = nullptr;
    }
    EXPECT_EQ(missing, nullptr);
}

}  // namespace
