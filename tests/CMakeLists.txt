set(TEST_SOURCES
    test_platform_stubs.cpp
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
)

add_executable(quake3_tests ${TEST_SOURCES})

target_compile_definitions(quake3_tests PRIVATE
    DEDICATED
    C_ONLY
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
    target_link_libraries(quake3_tests PRIVATE
        -Wl,--start-group
        q3sys
        q3renderer
        q3server
        qcommon
        botlib
        -Wl,--end-group
        GTest::gtest_main
        m
        dl
        pthread
    )
else()
    target_link_libraries(quake3_tests PRIVATE
        -Wl,--start-group
        q3sys
        q3renderer
        q3server
        qcommon
        botlib
        -Wl,--end-group
        gtest_main
        m
        dl
        pthread
    )
endif()

include(GoogleTest)
gtest_discover_tests(quake3_tests)
