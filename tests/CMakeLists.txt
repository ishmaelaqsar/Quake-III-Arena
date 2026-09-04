set(TEST_PLATFORM_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../code/sys/sys_dll.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../code/sys/net/sys_net.cpp
    $<IF:$<PLATFORM_ID:Windows>,${CMAKE_CURRENT_SOURCE_DIR}/../code/sys/sys_files_win32.cpp,${CMAKE_CURRENT_SOURCE_DIR}/../code/sys/sys_files_unix.cpp>
)

# Shared module that tests/test_vm.cpp loads through VM_Create to exercise the 64-bit virtual
# machine application binary interface.
add_library(testmodule SHARED vm_testmodule/tm_main.c)
target_include_directories(testmodule PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../code/game
    ${CMAKE_CURRENT_SOURCE_DIR}/../code/qcommon
)
set_target_properties(testmodule PROPERTIES
    PREFIX "" OUTPUT_NAME "testmodule${Q3_ARCH}" SUFFIX "${Q3_DLL_EXT}"
    LIBRARY_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}/tests/baseq3>"
    RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}/tests/baseq3>"
    C_VISIBILITY_PRESET hidden
)

# Link against GoogleTest
if(TARGET GTest::gtest_main)
    set(GTEST_TARGET GTest::gtest_main)
else()
    set(GTEST_TARGET gtest_main)
endif()

# ---------------------------------------------------------------------------
# q3sys_tests: pure C++ tests for code/sys
# ---------------------------------------------------------------------------
set(Q3SYS_TEST_SOURCES
    test_platform_stubs.cpp
    ${TEST_PLATFORM_SOURCES}
    test_modern_logger.cpp
    test_modern_fs.cpp
    test_modern_net.cpp
    test_modern_scripting.cpp
    test_modern_cvar.cpp
    test_modern_multiplayer.cpp
    test_http_downloader.cpp
    test_discord_rpc.cpp
    test_modern_math.cpp
    test_threading_queue.cpp
    test_job_system.cpp
    test_download_policy.cpp
    test_vulkan_backend.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../code/renderer/vulkan/vk_backend.cpp
)

add_executable(q3sys_tests ${Q3SYS_TEST_SOURCES})

target_compile_definitions(q3sys_tests PRIVATE
    C_ONLY
    Q3_TEST_BUILD_DIR="${CMAKE_BINARY_DIR}"
    Q3_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)

target_include_directories(q3sys_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../code/sys
    ${CMAKE_CURRENT_SOURCE_DIR}/../code/game
    ${CMAKE_CURRENT_SOURCE_DIR}/../code/qcommon
    ${CMAKE_CURRENT_SOURCE_DIR}/../code/server
)

target_link_libraries(q3sys_tests PRIVATE
    q3sys
    qcommon
    q3server
    botlib
    luajit::luajit
    SDL2::SDL2
    Threads::Threads
    ${GTEST_TARGET}
    $<$<NOT:$<PLATFORM_ID:Windows>>:m>
    $<$<PLATFORM_ID:Windows>:ws2_32 winmm shell32>
)

# ---------------------------------------------------------------------------
# quake3_tests: engine tests
# ---------------------------------------------------------------------------
set(QUAKE3_TEST_SOURCES
    test_platform_stubs.cpp
    ${TEST_PLATFORM_SOURCES}
    ${CMAKE_CURRENT_SOURCE_DIR}/../code/client/snd_adpcm.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../code/client/snd_wavelet.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../code/client/snd_mem.cpp
    test_math.cpp
    test_strings.cpp
    test_msg.cpp
    test_huffman.cpp
    test_md4.cpp
    test_cvar_cmd.cpp
    test_module_symbols.cpp
    test_sys_paths.cpp
    test_sys_net.cpp
    test_vm.cpp
    test_sys_api_boundary.cpp
    test_sys_time.cpp
    test_zip_writer.cpp
    test_files.cpp
    test_netchan.cpp
    test_collision.cpp
    test_sound.cpp
)

add_executable(quake3_tests ${QUAKE3_TEST_SOURCES})
add_dependencies(quake3_tests testmodule)

target_compile_definitions(quake3_tests PRIVATE
    C_ONLY
    Q3_TEST_BUILD_DIR="${CMAKE_BINARY_DIR}"
    Q3_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)

target_include_directories(quake3_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../code/sys
    ${CMAKE_CURRENT_SOURCE_DIR}/../code/game
    ${CMAKE_CURRENT_SOURCE_DIR}/../code/qcommon
    ${CMAKE_CURRENT_SOURCE_DIR}/../code/botlib
    ${CMAKE_CURRENT_SOURCE_DIR}/../code/server
)

target_link_libraries(quake3_tests PRIVATE
    q3sys
    q3server
    qcommon
    botlib
    ${GTEST_TARGET}
    SDL2::SDL2
    $<$<NOT:$<PLATFORM_ID:Windows>>:m>
    $<$<PLATFORM_ID:Windows>:ws2_32 winmm shell32>
)

if(Q3_SANITIZE)
    # detect_leaks=0 on every platform, not only Apple. The zone and hunk allocators
    # deliberately never free at exit, so leak detection reports every engine allocation and
    # buries real findings. This used to say 1 off Apple while the continuous integration
    # workflow passed detect_leaks=0 with a comment explaining why it had to be off; a CTest
    # ENVIRONMENT property sets the variable in the child, so this file won and the workflow's
    # rationale was dead. One source of truth, and it is this one.
    set(TEST_ENV "ASAN_OPTIONS=detect_leaks=0:strict_string_checks=1:abort_on_error=1;UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1")
    set(TEST_PROPERTIES PROPERTIES TIMEOUT 60 ENVIRONMENT "${TEST_ENV}")
    target_compile_options(quake3_tests PRIVATE -fno-sanitize=alignment)
else()
    set(TEST_PROPERTIES PROPERTIES TIMEOUT 60)
endif()

# Repeating a case is what shakes out the timing dependence the threading tests introduce, and
# checklist 03 step C8.2 asks for it on the sanitizer legs. It is `ctest --repeat-until-fail 3`
# there, not the `--gtest_repeat=3` that step named. gtest_discover_tests already gives every
# case its own process, and --gtest_repeat re-runs inside that process, which needs each case to
# be idempotent against engine globals. It is not: running it that way failed six cases that
# mutate the cvar registry, the cached home path, or the VM table. --repeat-until-fail re-runs
# the process, which is the property the step is after and needs no test to change. See C8.3 for
# the six, which are worth fixing on their own merits.

include(GoogleTest)
# DISCOVERY_TIMEOUT: the default is 5 seconds, which the Windows cross leg cannot meet. There
# CMAKE_CROSSCOMPILING_EMULATOR is wine, and the first wine call in a fresh container also
# initialises the prefix, so listing the cases spent the whole budget on setup and discovery
# failed with "Process terminated due to timeout" before a single test ran.
# --gtest_shuffle used to be passed here and did nothing: DISCOVERY_MODE PRE_TEST gives every
# case its own process, so there is no intra-process order to shuffle. Randomising which order
# the processes run in is ctest --schedule-random, which the workflow passes.
gtest_discover_tests(q3sys_tests ${TEST_PROPERTIES}
                     DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 120)
gtest_discover_tests(quake3_tests ${TEST_PROPERTIES}
                     DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 120)
