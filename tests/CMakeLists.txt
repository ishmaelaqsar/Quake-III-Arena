set(TEST_PLATFORM_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../code/sys/sys_dll.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../code/sys/net/sys_net.cpp
    $<IF:$<PLATFORM_ID:Windows>,${CMAKE_CURRENT_SOURCE_DIR}/../code/sys/sys_files_win32.cpp,${CMAKE_CURRENT_SOURCE_DIR}/../code/sys/sys_files_unix.cpp>
)

set(TEST_SOURCES
    test_platform_stubs.cpp
    ${TEST_PLATFORM_SOURCES}
    test_math.cpp
    test_strings.cpp
    test_msg.cpp
    test_huffman.cpp
    test_md4.cpp
    test_cvar_cmd.cpp
    test_legacy_vm_syscalls.cpp
    test_modern_math.cpp
    test_modern_cvar.cpp
    test_modern_net.cpp
    test_http_downloader.cpp
    test_discord_rpc.cpp
    test_vulkan_backend.cpp
    test_modern_scripting.cpp
    test_modern_fs.cpp
    test_modern_multiplayer.cpp
    test_modern_logger.cpp
    test_module_symbols.cpp
    test_sys_paths.cpp
    test_sys_net.cpp
    test_vm.cpp
)

# Shared module that tests/test_vm.cpp loads through VM_Create to exercise the 64-bit virtual
# machine application binary interface. It uses the same name and suffix scheme as the game
# modules so that Sys_LoadDll finds it, and lands in <build>/tests/baseq3 so that the test can
# point fs_basepath at <build>/tests without disturbing the real modules.
add_library(testmodule SHARED vm_testmodule/tm_main.c)
target_include_directories(testmodule PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../code/game
    ${CMAKE_CURRENT_SOURCE_DIR}/../code/qcommon
)
set_target_properties(testmodule PROPERTIES
    PREFIX "" OUTPUT_NAME "testmodule${Q3_ARCH}" SUFFIX "${Q3_DLL_EXT}"
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests/baseq3
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests/baseq3
    C_VISIBILITY_PRESET hidden
)

add_executable(quake3_tests ${TEST_SOURCES})
add_dependencies(quake3_tests testmodule)

target_compile_definitions(quake3_tests PRIVATE
    C_ONLY
    Q3_TEST_BUILD_DIR="${CMAKE_BINARY_DIR}"
)

target_include_directories(quake3_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../code/sys
    ${CMAKE_CURRENT_SOURCE_DIR}/../code/game
    ${CMAKE_CURRENT_SOURCE_DIR}/../code/qcommon
    ${CMAKE_CURRENT_SOURCE_DIR}/../code/botlib
    ${CMAKE_CURRENT_SOURCE_DIR}/../code/server
)

# Link against GoogleTest and engine components
if(TARGET GTest::gtest_main)
    set(GTEST_TARGET GTest::gtest_main)
else()
    set(GTEST_TARGET gtest_main)
endif()

target_link_libraries(quake3_tests PRIVATE
    q3sys
    q3renderer
    q3server
    qcommon
    botlib
    q3jpeg
    ${GTEST_TARGET}
    SDL2::SDL2
    OpenGL::GL
    $<$<NOT:$<PLATFORM_ID:Windows>>:m>
    $<$<PLATFORM_ID:Windows>:ws2_32 winmm shell32>
)

include(GoogleTest)
gtest_discover_tests(quake3_tests)
