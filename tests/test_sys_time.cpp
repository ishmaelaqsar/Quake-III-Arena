#include <gtest/gtest.h>
#include "q_shared.h"
#include "qcommon.h"

extern "C" {
void Sys_Sleep(int msec);
void NET_Sleep(int msec);
}

TEST(SysTime, MillisecondsNeverDecreases) {
    int prev = Sys_Milliseconds();
    for (int i = 0; i < 1000; i++) {
        int current = Sys_Milliseconds();
        EXPECT_GE(current, prev);
        prev = current;
    }
}

TEST(SysTime, SleepAdvancesClock) {
    int start = Sys_Milliseconds();
    Sys_Sleep(5);
    int elapsed = Sys_Milliseconds() - start;
    EXPECT_GE(elapsed, 4);
}

TEST(SysTime, NetSleepZeroReturns) {
    int start = Sys_Milliseconds();
    NET_Sleep(0);
    int elapsed = Sys_Milliseconds() - start;
    EXPECT_LE(elapsed, 5);
}
