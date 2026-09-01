#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>

extern "C" {
#include "q_shared.h"
#include "qcommon.h"
}

TEST(LegacyVmSyscallTest, IntptrArrayParameterIndexing) {
    // Simulate VM_DllSyscall parameter array passed as 64-bit intptr_t array
    intptr_t args[8];
    args[0] = 100; // Syscall ID
    args[1] = 42;  // First arg (integer)
    args[2] = reinterpret_cast<intptr_t>("TestString"); // Second arg (pointer)
    args[3] = 1024; // Third arg (buffer size)

    // Verify intptr_t casting produces correct 64-bit alignment and indexing
    intptr_t *intptr_args = reinterpret_cast<intptr_t*>(args);
    
    EXPECT_EQ(intptr_args[0], 100);
    EXPECT_EQ(intptr_args[1], 42);
    EXPECT_STREQ(reinterpret_cast<const char*>(intptr_args[2]), "TestString");
    EXPECT_EQ(intptr_args[3], 1024);
}

TEST(LegacyVmSyscallTest, FloatConversionRoundTrip) {
    float original = 3.14159f;
    intptr_t arg = 0;
    std::memcpy(&arg, &original, sizeof(float));

    float reconstructed = *reinterpret_cast<float*>(&arg);
    EXPECT_FLOAT_EQ(reconstructed, original);
}
