cmake_minimum_required(VERSION 3.16)
project(quake3 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Compile options
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -pipe
        -Wall
        -Wno-unused-function
        -Wno-unused-variable
        -Wno-unused-but-set-variable
        -Wno-deprecated-declarations
        -Wno-implicit-function-declaration
        -Wno-int-conversion
        -Wno-format-security
    )
endif()

# Global definitions
add_definitions(
    -D_GNU_SOURCE
    -DARCH_STRING="x86_64"
)

# Packages
find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/code/sys
    ${CMAKE_CURRENT_SOURCE_DIR}/code/game
    ${CMAKE_CURRENT_SOURCE_DIR}/code/qcommon
    ${CMAKE_CURRENT_SOURCE_DIR}/code/botlib
    ${CMAKE_CURRENT_SOURCE_DIR}/code/server
    ${CMAKE_CURRENT_SOURCE_DIR}/code/client
    ${CMAKE_CURRENT_SOURCE_DIR}/code/renderer
    ${CMAKE_CURRENT_SOURCE_DIR}/code/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/code/q3_ui
    ${CMAKE_CURRENT_SOURCE_DIR}/code/jpeg-6
    ${CMAKE_CURRENT_SOURCE_DIR}/code/unix
    ${SDL2_INCLUDE_DIRS}
    ${OPENGL_INCLUDE_DIR}
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
add_library(botlib STATIC ${BOTLIB_SOURCES})
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
add_library(q3jpeg STATIC ${JPEG_SOURCES})

# ---------------------------------------------------------------------------
# QCommon Library
# ---------------------------------------------------------------------------
set(QCOMMON_SOURCES
    code/qcommon/cm_load.c
    code/qcommon/cm_patch.c
    code/qcommon/cm_polylib.c
    code/qcommon/cm_test.c
    code/qcommon/cm_trace.c
    code/qcommon/cmd.c
    code/qcommon/common.c
    code/qcommon/cvar.c
    code/qcommon/files.c
    code/qcommon/md4.c
    code/qcommon/msg.c
    code/qcommon/net_chan.c
    code/qcommon/huffman.c
    code/qcommon/unzip.c
    code/qcommon/vm.c
    code/qcommon/vm_interpreted.c
    code/game/q_math.c
    code/game/q_shared.c
)
add_library(qcommon STATIC ${QCOMMON_SOURCES})
target_compile_definitions(qcommon PUBLIC C_ONLY)

# ---------------------------------------------------------------------------
# Cross-Platform System Layer Library (q3sys)
# ---------------------------------------------------------------------------
set(SYS_SOURCES
    code/sys/cvar/cvar_manager.cpp
    code/sys/net/bitstream.cpp
    code/sys/scripting/script_engine.cpp
    code/sys/fs/vfs.cpp
    code/sys/multiplayer/session.cpp
    code/sys/sys_sdl.cpp
    code/sys/sys_api.cpp
)
add_library(q3sys STATIC ${SYS_SOURCES})
target_include_directories(q3sys PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/code/sys
    ${CMAKE_CURRENT_SOURCE_DIR}/code/game
    ${CMAKE_CURRENT_SOURCE_DIR}/code/qcommon
)
target_link_libraries(q3sys PUBLIC qcommon ${SDL2_LIBRARIES} ${OPENGL_LIBRARIES})

# ---------------------------------------------------------------------------
# Server Library
# ---------------------------------------------------------------------------
set(SERVER_SOURCES
    code/server/sv_bot.c
    code/server/sv_ccmds.c
    code/server/sv_client.c
    code/server/sv_game.c
    code/server/sv_init.c
    code/server/sv_main.c
    code/server/sv_net_chan.c
    code/server/sv_snapshot.c
    code/server/sv_world.c
)
add_library(q3server STATIC ${SERVER_SOURCES})
target_compile_definitions(q3server PUBLIC DEDICATED C_ONLY)

# ---------------------------------------------------------------------------
# Dedicated Server Executable (q3ded)
# ---------------------------------------------------------------------------
set(DED_PLATFORM_SOURCES
    code/unix/linux_signals.c
    code/unix/unix_main.c
    code/unix/unix_net.c
    code/unix/unix_shared.c
    code/null/null_client.c
    code/null/null_input.c
    code/null/null_snddma.c
)
add_executable(q3ded ${DED_PLATFORM_SOURCES})
target_compile_definitions(q3ded PRIVATE DEDICATED C_ONLY)
target_link_libraries(q3ded PRIVATE
    -Wl,--start-group
    q3server
    q3sys
    qcommon
    botlib
    -Wl,--end-group
    m
    dl
    pthread
)

# ---------------------------------------------------------------------------
# Client Library
# ---------------------------------------------------------------------------
set(CLIENT_SOURCES
    code/client/cl_cgame.c
    code/client/cl_cin.c
    code/client/cl_console.c
    code/client/cl_input.c
    code/client/cl_keys.c
    code/client/cl_main.c
    code/client/cl_net_chan.c
    code/client/cl_parse.c
    code/client/cl_scrn.c
    code/client/cl_ui.c
    code/client/snd_adpcm.c
    code/client/snd_dma.c
    code/client/snd_mem.c
    code/client/snd_mix.c
    code/client/snd_wavelet.c
)
add_library(q3client STATIC ${CLIENT_SOURCES})

# ---------------------------------------------------------------------------
# Renderer Library
# ---------------------------------------------------------------------------
set(RENDERER_SOURCES
    code/renderer/tr_animation.c
    code/renderer/tr_backend.c
    code/renderer/tr_bsp.c
    code/renderer/tr_cmds.c
    code/renderer/tr_curve.c
    code/renderer/tr_flares.c
    code/renderer/tr_font.c
    code/renderer/tr_image.c
    code/renderer/tr_init.c
    code/renderer/tr_light.c
    code/renderer/tr_main.c
    code/renderer/tr_marks.c
    code/renderer/tr_mesh.c
    code/renderer/tr_model.c
    code/renderer/tr_noise.c
    code/renderer/tr_scene.c
    code/renderer/tr_shade.c
    code/renderer/tr_shade_calc.c
    code/renderer/tr_shader.c
    code/renderer/tr_shadows.c
    code/renderer/tr_sky.c
    code/renderer/tr_surface.c
    code/renderer/tr_world.c
)
add_library(q3renderer STATIC ${RENDERER_SOURCES})

# ---------------------------------------------------------------------------
# Modern Client Executable (quake3_modern)
# ---------------------------------------------------------------------------
set(CLIENT_PLATFORM_SOURCES
    code/unix/linux_signals.c
    code/unix/linux_qgl.c
    code/unix/unix_main.c
    code/unix/unix_net.c
    code/unix/unix_shared.c
)
add_executable(quake3_modern ${CLIENT_PLATFORM_SOURCES})
target_link_libraries(quake3_modern PRIVATE
    -Wl,--start-group
    q3sys
    q3client
    q3renderer
    q3server
    qcommon
    botlib
    q3jpeg
    -Wl,--end-group
    ${SDL2_LIBRARIES}
    ${OPENGL_LIBRARIES}
    m
    dl
    pthread
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
    code/game/q_math.c
    code/game/q_shared.c
)
add_library(qagame SHARED ${GAME_SOURCES})
target_compile_definitions(qagame PRIVATE GAME_DLL)
target_link_libraries(qagame PRIVATE m)
set_target_properties(qagame PROPERTIES PREFIX "" OUTPUT_NAME "qagamex86_64")

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
    code/game/q_math.c
    code/game/q_shared.c
)
add_library(cgame SHARED ${CGAME_SOURCES})
target_compile_definitions(cgame PRIVATE CGAME_DLL)
target_link_libraries(cgame PRIVATE m)
set_target_properties(cgame PROPERTIES PREFIX "" OUTPUT_NAME "cgamex86_64")

# ---------------------------------------------------------------------------
# UI Module (q3_ui)
# ---------------------------------------------------------------------------
set(UI_SOURCES
    code/game/bg_misc.c
    code/game/q_math.c
    code/game/q_shared.c
    code/q3_ui/ui_addbots.c
    code/q3_ui/ui_atoms.c
    code/q3_ui/ui_cdkey.c
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
target_link_libraries(ui PRIVATE m)
set_target_properties(ui PROPERTIES PREFIX "" OUTPUT_NAME "uix86_64")

# ---------------------------------------------------------------------------
# Unit Testing with GoogleTest
# ---------------------------------------------------------------------------
enable_testing()

find_package(GTest QUIET)
if(NOT GTest_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

add_subdirectory(tests)

# ---------------------------------------------------------------------------
# Installation & Standalone Packaging Targets
# ---------------------------------------------------------------------------
include(GNUInstallDirs)

install(TARGETS quake3_modern q3ded
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(TARGETS qagame cgame ui
    LIBRARY DESTINATION ${CMAKE_INSTALL_DATADIR}/quake3/baseq3
)

install(DIRECTORY baseq3/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/quake3/baseq3
    FILES_MATCHING PATTERN "*.pk3"
)
