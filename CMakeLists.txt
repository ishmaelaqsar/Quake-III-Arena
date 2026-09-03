cmake_minimum_required(VERSION 3.16)

# Default build type before project() (A3.2)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

project(quake3 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Build options (A3.6, A3.7, A6.1, A6.2)
option(Q3_WERROR "Treat warnings as errors" OFF)
option(Q3_USE_CURL "HTTP downloads through libcurl" ON)
option(Q3_FETCH_DEPS "Fetch SDL2, LuaJIT, and GoogleTest into the build tree" OFF)
option(Q3_FETCH_LUAJIT "Build LuaJIT from source when no system package is found" ON)
set(Q3_SANITIZE "" CACHE STRING "Sanitizers: address,undefined or thread")

# Compile warnings and sanitizer flags (A3.6, A3.7)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -pipe
        -Wall
        -fno-strict-aliasing
        -Wno-unused-function
        -Wno-unused-variable
        -Wno-unused-but-set-variable
        -Wno-deprecated-declarations
        $<$<COMPILE_LANGUAGE:C>:-Werror=implicit-function-declaration>
        $<$<COMPILE_LANGUAGE:C>:-Werror=int-conversion>
        $<$<COMPILE_LANGUAGE:C>:-Werror=incompatible-pointer-types>
        -Wno-format-security
    )
    if(Q3_WERROR)
        add_compile_options(-Werror)
    endif()
    # A shipped Release build carries no verbose logging at all: LOG_DEBUG and LOG_INFO compile
    # to nothing. Debug and RelWithDebInfo keep them, filtered at run time by com_logLevel, so a
    # developer gets them with one cvar and CI keeps the diagnostics. Warnings and errors are
    # always compiled, in every build type: a release binary that reports nothing is worse than
    # a slightly larger one.
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_definitions(Q3_LOG_STRIP_VERBOSE)
    endif()

    if(Q3_SANITIZE)
        # -fno-sanitize=alignment: the zone allocator hands out four-byte-aligned blocks and
        # reads memblock_t through them (code/qcommon/common.cpp:957), and the netcode misaligns
        # by design. Those reports are known and would drown out real findings. Remove this
        # exclusion when the C++ migration replaces the punning, not before.
        add_compile_options(-fsanitize=${Q3_SANITIZE} -fno-sanitize=alignment
                            -fno-omit-frame-pointer -fno-sanitize-recover=undefined -g)
        add_link_options(-fsanitize=${Q3_SANITIZE})
        add_compile_definitions(Q3_SANITIZE)
    endif()
elseif(MSVC)
    add_compile_options(/W3 /wd4996 /wd4244 /wd4267 /D_CRT_SECURE_NO_WARNINGS /D_CRT_NONSTDC_NO_DEPRECATE /utf-8)
    if(Q3_SANITIZE STREQUAL "address")
        add_compile_options(/fsanitize=address)
    endif()
endif()

# Derive architecture and platform strings (A3.1)
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _q3_proc)
if(_q3_proc MATCHES "^(x86_64|amd64)$")
    set(Q3_ARCH x86_64)
elseif(_q3_proc MATCHES "^(arm64|aarch64)$")
    set(Q3_ARCH arm64)
elseif(_q3_proc MATCHES "^(i[3-6]86|x86)$")
    set(Q3_ARCH x86)
else()
    message(FATAL_ERROR "Unsupported CMAKE_SYSTEM_PROCESSOR: ${CMAKE_SYSTEM_PROCESSOR}")
endif()

if(WIN32)
    set(Q3_OS win)
    set(Q3_DLL_EXT ".dll")
elseif(APPLE)
    set(Q3_OS macos)
    set(Q3_DLL_EXT ".dylib")
else()
    set(Q3_OS linux)
    set(Q3_DLL_EXT ".so")
endif()

add_compile_definitions(
    _GNU_SOURCE
    ARCH_STRING="${Q3_ARCH}"
    OS_STRING="${Q3_OS}"
    DLL_EXT="${Q3_DLL_EXT}"
)

# Packages and dependencies (A3.5, A6.1, A6.2)
find_package(Threads REQUIRED)

if(Q3_FETCH_DEPS)
    set(Q3_FETCH_LUAJIT ON CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    include(FetchContent)
    FetchContent_Declare(SDL2
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-2.32.8
    )
    FetchContent_MakeAvailable(SDL2)
    if(TARGET SDL2::SDL2-static)
        add_library(SDL2::SDL2 ALIAS SDL2::SDL2-static)
    endif()
else()
    find_package(SDL2 REQUIRED)
endif()

if(NOT TARGET SDL2::SDL2)
    add_library(SDL2::SDL2 INTERFACE IMPORTED)
    set_target_properties(SDL2::SDL2 PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_DIRS}"
        INTERFACE_LINK_LIBRARIES "${SDL2_LIBRARIES}"
    )
endif()

find_package(OpenGL REQUIRED)
if(NOT TARGET OpenGL::GL)
    add_library(OpenGL::GL INTERFACE IMPORTED)
    set_target_properties(OpenGL::GL PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${OPENGL_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES "${OPENGL_LIBRARIES}"
    )
endif()

# LuaJIT lookup and fallback (A6.1)
#
# Three ways of finding it, in order, because no single one covers every platform:
#
#   1. pkg-config. Covers a Linux distribution package and Homebrew, and carries the include and
#      library paths for us.
#   2. find_path and find_library. Needed on Windows: vcpkg installs the port but the runner has
#      no pkg-config, so step 1 silently finds nothing. These searches do work there, because the
#      vcpkg toolchain adds its installed tree to the CMake search paths. They also cover a
#      manual install and the cross prefix in docker/Dockerfile.mingw.
#   3. Build it from source with FetchContent, when Q3_FETCH_LUAJIT is on.
#
# The header is named luajit.h in every layout; only its directory differs, hence the suffixes.
# The library is libluajit-5.1 on Unix layouts and lua51 with vcpkg.
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(LUAJIT IMPORTED_TARGET luajit)
endif()

if(NOT TARGET PkgConfig::LUAJIT)
    find_path(LUAJIT_INCLUDE_DIR
        NAMES luajit.h
        PATH_SUFFIXES luajit-2.1 luajit luajit-2.0
    )
    find_library(LUAJIT_LIBRARY
        NAMES luajit-5.1 luajit lua51 libluajit-5.1 libluajit
    )
    mark_as_advanced(LUAJIT_INCLUDE_DIR LUAJIT_LIBRARY)
endif()

if(TARGET PkgConfig::LUAJIT)
    add_library(luajit::luajit ALIAS PkgConfig::LUAJIT)
    message(STATUS "LuaJIT: found through pkg-config")
elseif(LUAJIT_INCLUDE_DIR AND LUAJIT_LIBRARY)
    add_library(luajit::luajit UNKNOWN IMPORTED)
    set_target_properties(luajit::luajit PROPERTIES
        IMPORTED_LOCATION "${LUAJIT_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${LUAJIT_INCLUDE_DIR}"
    )
    message(STATUS "LuaJIT: found at ${LUAJIT_LIBRARY}")
elseif(Q3_FETCH_LUAJIT)
    include(FetchContent)
    FetchContent_Declare(luajit
        GIT_REPOSITORY https://github.com/zhaozg/luajit-cmake
        GIT_TAG 94444a6c9bde77a768822f5cd00139161c9de412
        GIT_SUBMODULES_RECURSE ON
    )
    # The wrapper reads LUAJIT_DIR while it configures, which happens inside
    # FetchContent_MakeAvailable, so it has to be set beforehand. It used to be derived from
    # luajit_SOURCE_DIR, which is empty until after that call, so LUAJIT_DIR became "/LuaJIT"
    # and the wrapper failed with `file COPY cannot find "/LuaJIT/src/jit"`. FETCHCONTENT_BASE_DIR
    # gives the same path and is known in advance.
    if(NOT FETCHCONTENT_BASE_DIR)
        set(FETCHCONTENT_BASE_DIR "${CMAKE_BINARY_DIR}/_deps")
    endif()
    set(LUAJIT_DIR "${FETCHCONTENT_BASE_DIR}/luajit-src/LuaJIT" CACHE PATH "" FORCE)
    FetchContent_MakeAvailable(luajit)
    if(TARGET luajit::lib)
        add_library(luajit::luajit ALIAS luajit::lib)
    elseif(TARGET libluajit)
        add_library(luajit::luajit ALIAS libluajit)
    elseif(TARGET luajit)
        add_library(luajit::luajit ALIAS luajit)
    else()
        message(FATAL_ERROR "LuaJIT was fetched but exposed no known target; inspect ${LUAJIT_DIR}")
    endif()
    message(STATUS "LuaJIT: built from source in ${LUAJIT_DIR}")
else()
    message(FATAL_ERROR
        "LuaJIT not found. Install it (brew install luajit | apt install libluajit-5.1-dev | "
        "vcpkg install luajit --triplet x64-windows) or configure with -DQ3_FETCH_LUAJIT=ON.")
endif()

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/code/sys
    ${CMAKE_CURRENT_SOURCE_DIR}/code/sys/scripting
    ${CMAKE_CURRENT_SOURCE_DIR}/code/game
    ${CMAKE_CURRENT_SOURCE_DIR}/code/qcommon
    ${CMAKE_CURRENT_SOURCE_DIR}/code/botlib
    ${CMAKE_CURRENT_SOURCE_DIR}/code/server
    ${CMAKE_CURRENT_SOURCE_DIR}/code/client
    ${CMAKE_CURRENT_SOURCE_DIR}/code/renderer
    ${CMAKE_CURRENT_SOURCE_DIR}/code/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/code/q3_ui
    ${CMAKE_CURRENT_SOURCE_DIR}/code/jpeg-6
)

# ---------------------------------------------------------------------------
# Botlib
# ---------------------------------------------------------------------------
set(BOTLIB_SOURCES
    code/botlib/be_aas_bspq3.c
    code/botlib/be_aas_cluster.c
    code/botlib/be_aas_debug.c
    code/botlib/be_aas_entity.c
    code/botlib/be_aas_file.c
    code/botlib/be_aas_main.c
    code/botlib/be_aas_move.c
    code/botlib/be_aas_optimize.c
    code/botlib/be_aas_reach.c
    code/botlib/be_aas_route.c
    code/botlib/be_aas_routealt.c
    code/botlib/be_aas_sample.c
    code/botlib/be_ai_char.c
    code/botlib/be_ai_chat.c
    code/botlib/be_ai_gen.c
    code/botlib/be_ai_goal.c
    code/botlib/be_ai_move.c
    code/botlib/be_ai_weap.c
    code/botlib/be_ai_weight.c
    code/botlib/be_ea.c
    code/botlib/be_interface.c
    code/botlib/l_crc.c
    code/botlib/l_libvar.c
    code/botlib/l_log.c
    code/botlib/l_memory.c
    code/botlib/l_precomp.c
    code/botlib/l_script.c
    code/botlib/l_struct.c
)
add_library(botlib OBJECT ${BOTLIB_SOURCES})
target_compile_definitions(botlib PRIVATE BOTLIB)

# ---------------------------------------------------------------------------
# JPEG-6 Library
# ---------------------------------------------------------------------------
set(JPEG_SOURCES
    code/jpeg-6/jcapimin.c
    code/jpeg-6/jchuff.c
    code/jpeg-6/jcinit.c
    code/jpeg-6/jccoefct.c
    code/jpeg-6/jccolor.c
    code/jpeg-6/jfdctflt.c
    code/jpeg-6/jcdctmgr.c
    code/jpeg-6/jcphuff.c
    code/jpeg-6/jcmainct.c
    code/jpeg-6/jcmarker.c
    code/jpeg-6/jcmaster.c
    code/jpeg-6/jcomapi.c
    code/jpeg-6/jcparam.c
    code/jpeg-6/jcprepct.c
    code/jpeg-6/jcsample.c
    code/jpeg-6/jdapimin.c
    code/jpeg-6/jdapistd.c
    code/jpeg-6/jdatasrc.c
    code/jpeg-6/jdcoefct.c
    code/jpeg-6/jdcolor.c
    code/jpeg-6/jddctmgr.c
    code/jpeg-6/jdhuff.c
    code/jpeg-6/jdinput.c
    code/jpeg-6/jdmainct.c
    code/jpeg-6/jdmarker.c
    code/jpeg-6/jdmaster.c
    code/jpeg-6/jdpostct.c
    code/jpeg-6/jdsample.c
    code/jpeg-6/jdtrans.c
    code/jpeg-6/jerror.c
    code/jpeg-6/jidctflt.c
    code/jpeg-6/jmemmgr.c
    code/jpeg-6/jmemnobs.c
    code/jpeg-6/jutils.c
)
add_library(q3jpeg OBJECT ${JPEG_SOURCES})

# ---------------------------------------------------------------------------
# QCommon Library
# ---------------------------------------------------------------------------
set(QCOMMON_SOURCES
    code/qcommon/cm_load.cpp
    code/qcommon/cm_patch.cpp
    code/qcommon/cm_polylib.cpp
    code/qcommon/cm_test.cpp
    code/qcommon/cm_trace.cpp
    code/qcommon/cmd.cpp
    code/qcommon/common.cpp
    code/qcommon/cvar.cpp
    code/qcommon/files.cpp
    code/qcommon/md4.c
    code/qcommon/msg.cpp
    code/qcommon/net_chan.cpp
    code/qcommon/huffman.cpp
    code/qcommon/unzip.c
    code/qcommon/vm.cpp
    code/qcommon/vm_interpreted.cpp
    code/qcommon/vm_none.cpp
    code/game/q_math.cpp
    code/game/q_shared.cpp
)
add_library(qcommon OBJECT ${QCOMMON_SOURCES})
target_compile_definitions(qcommon PUBLIC C_ONLY)

# ---------------------------------------------------------------------------
# Cross-Platform System Layer Library (q3sys)
# ---------------------------------------------------------------------------
set(SYS_SOURCES
    code/sys/cvar/cvar_manager.cpp
    code/sys/net/bitstream.cpp
    code/sys/net/http_downloader.cpp
    code/sys/rpc/discord_rpc.cpp
    code/sys/scripting/script_engine.cpp
    code/sys/fs/vfs.cpp
    code/sys/multiplayer/session.cpp
    code/sys/threading/thread_affinity.cpp
    code/sys/threading/main_thread_queue.cpp
    code/sys/sys_api.cpp
)
add_library(q3sys OBJECT ${SYS_SOURCES})
target_include_directories(q3sys PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/code/sys
    ${CMAKE_CURRENT_SOURCE_DIR}/code/game
    ${CMAKE_CURRENT_SOURCE_DIR}/code/qcommon
)
target_link_libraries(q3sys PUBLIC
    luajit::luajit
    SDL2::SDL2
    OpenGL::GL
    Threads::Threads
    ${CMAKE_DL_LIBS}
)

# ---------------------------------------------------------------------------
# Server Library
# ---------------------------------------------------------------------------
set(SERVER_SOURCES
    code/server/sv_bot.cpp
    code/server/sv_ccmds.cpp
    code/server/sv_client.cpp
    code/server/sv_game.cpp
    code/server/sv_init.cpp
    code/server/sv_main.cpp
    code/server/sv_net_chan.cpp
    code/server/sv_snapshot.cpp
    code/server/sv_world.cpp
)
add_library(q3server OBJECT ${SERVER_SOURCES})
target_compile_definitions(q3server PUBLIC C_ONLY)

# ---------------------------------------------------------------------------
# Platform Sources
# ---------------------------------------------------------------------------
set(SYS_PLATFORM_SOURCES
    code/sys/sys_main.cpp
    code/sys/sys_dll.cpp
    code/sys/net/sys_net.cpp
    $<IF:$<PLATFORM_ID:Windows>,code/sys/sys_win32.cpp;code/sys/sys_files_win32.cpp,code/sys/sys_unix.cpp;code/sys/sys_files_unix.cpp>
)

# ---------------------------------------------------------------------------
# Dedicated Server Executable (q3ded)
# ---------------------------------------------------------------------------
add_executable(q3ded
    ${SYS_PLATFORM_SOURCES}
    code/null/null_client.cpp
    code/null/null_input.cpp
    code/null/null_snddma.cpp
)
target_compile_definitions(q3ded PRIVATE C_ONLY SDL_MAIN_HANDLED)
target_link_libraries(q3ded PRIVATE
    q3server
    q3sys
    qcommon
    botlib
    $<$<NOT:$<PLATFORM_ID:Windows>>:m>
    $<$<PLATFORM_ID:Windows>:ws2_32 winmm shell32>
)
set_target_properties(q3ded PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}>"
)

# ---------------------------------------------------------------------------
# Client Library
# ---------------------------------------------------------------------------
set(CLIENT_SOURCES
    code/client/cl_cgame.cpp
    code/client/cl_cin.cpp
    code/client/cl_console.cpp
    code/client/cl_input.cpp
    code/client/cl_keys.cpp
    code/client/cl_main.cpp
    code/client/cl_net_chan.cpp
    code/client/cl_parse.cpp
    code/client/cl_scrn.cpp
    code/client/cl_ui.cpp
    code/client/snd_adpcm.cpp
    code/client/snd_dma.cpp
    code/client/snd_mem.cpp
    code/client/snd_mix.cpp
    code/client/snd_wavelet.cpp
    code/sys/sys_sdl.cpp
)
add_library(q3client OBJECT ${CLIENT_SOURCES})
target_link_libraries(q3client PUBLIC SDL2::SDL2 OpenGL::GL)

# ---------------------------------------------------------------------------
# Renderer Library
# ---------------------------------------------------------------------------
set(RENDERER_SOURCES
    code/renderer/tr_animation.cpp
    code/renderer/tr_backend.cpp
    code/renderer/tr_bsp.cpp
    code/renderer/tr_cmds.cpp
    code/renderer/tr_curve.cpp
    code/renderer/tr_flares.cpp
    code/renderer/tr_font.cpp
    code/renderer/tr_image.cpp
    code/renderer/tr_init.cpp
    code/renderer/tr_light.cpp
    code/renderer/tr_main.cpp
    code/renderer/tr_marks.cpp
    code/renderer/tr_mesh.cpp
    code/renderer/tr_model.cpp
    code/renderer/tr_noise.cpp
    code/renderer/tr_scene.cpp
    code/renderer/tr_shade.cpp
    code/renderer/tr_shade_calc.cpp
    code/renderer/vulkan/vk_backend.cpp
    code/renderer/tr_shader.cpp
    code/renderer/tr_shadows.cpp
    code/renderer/tr_sky.cpp
    code/renderer/tr_surface.cpp
    code/renderer/tr_world.cpp
)
add_library(q3renderer OBJECT ${RENDERER_SOURCES})

# ---------------------------------------------------------------------------
# Modern Client Executable (quake3_modern)
# ---------------------------------------------------------------------------
add_executable(quake3_modern ${SYS_PLATFORM_SOURCES})
target_compile_definitions(quake3_modern PRIVATE SDL_MAIN_HANDLED)
target_link_libraries(quake3_modern PRIVATE
    q3client
    q3renderer
    q3server
    q3sys
    qcommon
    botlib
    q3jpeg
    $<$<NOT:$<PLATFORM_ID:Windows>>:m>
    $<$<PLATFORM_ID:Windows>:ws2_32 winmm shell32>
)
set_target_properties(quake3_modern PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}>"
)

# ---------------------------------------------------------------------------
# Game Module (qagame)
# ---------------------------------------------------------------------------
set(GAME_SOURCES
    code/game/ai_chat.c
    code/game/ai_cmd.c
    code/game/ai_dmnet.c
    code/game/ai_dmq3.c
    code/game/ai_main.c
    code/game/ai_team.c
    code/game/ai_vcmd.c
    code/game/bg_misc.c
    code/game/bg_pmove.c
    code/game/bg_slidemove.c
    code/game/g_active.c
    code/game/g_arenas.c
    code/game/g_bot.c
    code/game/g_client.c
    code/game/g_cmds.c
    code/game/g_combat.c
    code/game/g_items.c
    code/game/g_main.c
    code/game/g_mem.c
    code/game/g_misc.c
    code/game/g_missile.c
    code/game/g_mover.c
    code/game/g_session.c
    code/game/g_spawn.c
    code/game/g_svcmds.c
    code/game/g_syscalls.c
    code/game/g_target.c
    code/game/g_team.c
    code/game/g_trigger.c
    code/game/g_utils.c
    code/game/g_weapon.c
    code/game/q_math.cpp
    code/game/q_shared.cpp
)
add_library(qagame SHARED ${GAME_SOURCES})
target_compile_definitions(qagame PRIVATE GAME_DLL)
target_link_libraries(qagame PRIVATE $<$<NOT:$<PLATFORM_ID:Windows>>:m>)
set_target_properties(qagame PROPERTIES
    PREFIX "" OUTPUT_NAME "qagame${Q3_ARCH}" SUFFIX "${Q3_DLL_EXT}"
    LIBRARY_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}/baseq3>"
    RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}/baseq3>"
    C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden
)

# ---------------------------------------------------------------------------
# Client Game Module (cgame)
# ---------------------------------------------------------------------------
set(CGAME_SOURCES
    code/game/bg_misc.c
    code/game/bg_pmove.c
    code/game/bg_slidemove.c
    code/cgame/cg_consolecmds.c
    code/cgame/cg_draw.c
    code/cgame/cg_drawtools.c
    code/cgame/cg_effects.c
    code/cgame/cg_ents.c
    code/cgame/cg_event.c
    code/cgame/cg_info.c
    code/cgame/cg_localents.c
    code/cgame/cg_main.c
    code/cgame/cg_marks.c
    code/cgame/cg_players.c
    code/cgame/cg_playerstate.c
    code/cgame/cg_predict.c
    code/cgame/cg_scoreboard.c
    code/cgame/cg_servercmds.c
    code/cgame/cg_snapshot.c
    code/cgame/cg_syscalls.c
    code/cgame/cg_view.c
    code/cgame/cg_weapons.c
    code/game/q_math.cpp
    code/game/q_shared.cpp
)
add_library(cgame SHARED ${CGAME_SOURCES})
target_compile_definitions(cgame PRIVATE CGAME_DLL)
target_link_libraries(cgame PRIVATE $<$<NOT:$<PLATFORM_ID:Windows>>:m>)
set_target_properties(cgame PROPERTIES
    PREFIX "" OUTPUT_NAME "cgame${Q3_ARCH}" SUFFIX "${Q3_DLL_EXT}"
    LIBRARY_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}/baseq3>"
    RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}/baseq3>"
    C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden
)

# ---------------------------------------------------------------------------
# UI Module (q3_ui)
# ---------------------------------------------------------------------------
set(UI_SOURCES
    code/game/bg_misc.c
    code/game/q_math.cpp
    code/game/q_shared.cpp
    code/q3_ui/ui_addbots.c
    code/q3_ui/ui_atoms.c
    code/q3_ui/ui_cinematics.c
    code/q3_ui/ui_confirm.c
    code/q3_ui/ui_connect.c
    code/q3_ui/ui_controls2.c
    code/q3_ui/ui_credits.c
    code/q3_ui/ui_demo2.c
    code/q3_ui/ui_display.c
    code/q3_ui/ui_gameinfo.c
    code/q3_ui/ui_ingame.c
    code/q3_ui/ui_loadconfig.c
    code/q3_ui/ui_main.c
    code/q3_ui/ui_menu.c
    code/q3_ui/ui_mfield.c
    code/q3_ui/ui_mods.c
    code/q3_ui/ui_network.c
    code/q3_ui/ui_options.c
    code/q3_ui/ui_playermodel.c
    code/q3_ui/ui_players.c
    code/q3_ui/ui_playersettings.c
    code/q3_ui/ui_preferences.c
    code/q3_ui/ui_qmenu.c
    code/q3_ui/ui_removebots.c
    code/q3_ui/ui_saveconfig.c
    code/q3_ui/ui_serverinfo.c
    code/q3_ui/ui_servers2.c
    code/q3_ui/ui_setup.c
    code/q3_ui/ui_sound.c
    code/q3_ui/ui_sparena.c
    code/q3_ui/ui_specifyserver.c
    code/q3_ui/ui_splevel.c
    code/q3_ui/ui_sppostgame.c
    code/q3_ui/ui_spreset.c
    code/q3_ui/ui_spskill.c
    code/q3_ui/ui_startserver.c
    code/ui/ui_syscalls.c
    code/q3_ui/ui_team.c
    code/q3_ui/ui_teamorders.c
    code/q3_ui/ui_video.c
)
add_library(ui SHARED ${UI_SOURCES})
target_compile_definitions(ui PRIVATE UI_DLL)
target_link_libraries(ui PRIVATE $<$<NOT:$<PLATFORM_ID:Windows>>:m>)
set_target_properties(ui PROPERTIES
    PREFIX "" OUTPUT_NAME "ui${Q3_ARCH}" SUFFIX "${Q3_DLL_EXT}"
    LIBRARY_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}/baseq3>"
    RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}/baseq3>"
    C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden
)

# ---------------------------------------------------------------------------
# Unit Testing with GoogleTest
# ---------------------------------------------------------------------------
include(CTest)

if(BUILD_TESTING)
    find_package(GTest QUIET)
    if(NOT GTest_FOUND)
        include(FetchContent)
        FetchContent_Declare(
            googletest
            URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
            URL_HASH SHA256=1f357c27ca988c3f7c6b4bf68a9395005ac6761f034046e9dde0896e3aba00e4
            FIND_PACKAGE_ARGS 1.14.0
        )
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    endif()

    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Installation & Standalone Packaging Targets (A3.9)
# ---------------------------------------------------------------------------
include(GNUInstallDirs)

install(TARGETS quake3_modern q3ded
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(TARGETS qagame cgame ui
    LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR}/baseq3
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/baseq3
)

message(STATUS "Copy pak0.pk3 to pak8.pk3 into ${CMAKE_BINARY_DIR}/baseq3 to run from the build tree")
