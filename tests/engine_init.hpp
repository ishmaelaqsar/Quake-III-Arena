#pragma once

// One-time engine initialisation shared by the tests that call into qcommon.
//
// GoogleTest runs SetUpTestSuite once per fixture class, so two fixtures that each initialised
// the zone, the command system, and the cvar system would initialise them twice. Call
// EnsureEngineInitialised() instead: it is idempotent and safe under --gtest_shuffle.
//
// Checklist 03 step C1 replaces this header with the fuller tests/engine_fixture.hpp.

#include "q_shared.h"
#include "qcommon.h"

inline void EnsureEngineInitialised() {
    static bool initialised = false;
    if (initialised) {
        return;
    }
    initialised = true;

    Com_InitSmallZoneMemory();
    Cmd_Init();
    Cvar_Init();
    Com_InitZoneMemory();
}
