# Checklist 01: build portability and CI

## Purpose

Make the tree build and run on Linux x86_64, macOS arm64, and Windows x64 from one CMake
project. Replace the Linux-only platform layer under `code/unix/` with a portable layer under
`code/sys/`. Add CI legs for all three platforms. Nothing else in `docs/plans/` can start
before this checklist lands, because the tree does not compile on the owner's macOS machine
today.

**Status:** In progress. Phases A1 through A8 complete on 2 September 2026 (hygiene, platform macros, CMake restructure, sys platform layer, runtime DEDICATED, LuaJIT/fetch, CI workflow, build docs). Open: A6.3 MinGW verification.

## Prerequisites

- Checklist `00-environment.md` is complete. Every Linux build and test in this file runs
  inside the container through the Makefile targets. You install nothing on the host.
- Stray build files are removed in step A1 of this file, not in checklist 10. Checklist 10
  step 9 points here. Nothing else can start before the tree is clean.
- Owner decisions this file relies on:
  - Targets are Linux x86_64, macOS arm64, and Windows x64 (MSVC plus vcpkg in CI; clang-cl
    works with the same preset).
  - LuaJIT is required. Use the system package first, then a pinned `luajit-cmake` FetchContent
    fallback, then a fatal error with install hints.
  - Home paths stay ioquake3-compatible: `~/.q3a`, `~/Library/Application Support/Quake3`,
    `%APPDATA%\Quake3`.
  - The new platform files are authored as `.cpp` from day one (see checklist 04), with
    `extern "C"` linkage on every `Sys_*` symbol that legacy C code calls.

## Background

The build audit found these defects. Re-verify each anchor before you edit, because line
numbers drift.

| Defect | Anchor |
|---|---|
| `-Wl,--start-group` is GNU ld only. Apple ld fails with `ld: unknown options: --start-group --end-group`. | `CMakeLists.txt:207,214,325,333`, `tests/CMakeLists.txt:36-50` |
| `unix_glw.h` is a hard `#error` on anything that is not Linux or FreeBSD. `linux_qgl.c:40` includes it, and `linux_qgl.c` is in `CLIENT_PLATFORM_SOURCES`. | `code/unix/unix_glw.h:22`, `CMakeLists.txt:319` |
| `qgl.h` gates the macOS branch on `MACOS_X`, which CMake never defines, so macOS falls to `#include <gl.h>`. | `code/renderer/qgl.h:43,65` |
| Core GL comes from `dlopen("libGL.so.1")` with the return value ignored. Extensions come from `SDL_GL_GetProcAddress`. Two loaders, one of them Linux only. | `code/sys/sys_sdl.cpp:190`, `code/unix/linux_qgl.c:3058-3082` |
| `Sys_LoadDll` probes only `.so`, and the module output names hardcode `x86_64`. macOS produces `.dylib` and Windows `.dll`. Apple Silicon and arm64 Linux can never load `qagame`, `cgame`, or `ui`. | `code/unix/unix_main.c:725-739`, `CMakeLists.txt:349,383,437` |
| `q3server` sets `DEDICATED` and `C_ONLY` as `PUBLIC`. The client executable links `q3server`, so every client platform file compiles with `-DDEDICATED`. Consequences: `quake3_modern --version` prints "Dedicated Server", `GLimp_Shutdown()` is compiled out of the crash handler, and `Sys_XTimeToSysTime` is compiled out. | `CMakeLists.txt:199,329`, `code/unix/unix_main.c:1202`, `code/unix/linux_signals.c:44-46`, `code/unix/unix_shared.c:79` |
| `pkg_check_modules(LUAJIT QUIET luajit)` is never checked. `LUAJIT_INCLUDE_DIRS` and `LUAJIT_LIBRARIES` are used unguarded. `sol.hpp` includes `<lua.h>` unconditionally, so a missing LuaJIT fails deep inside a 29k-line header. | `CMakeLists.txt:35-38,180-182`, `code/sys/scripting/sol/sol.hpp:2951,3155` |
| GoogleTest FetchContent has no `URL_HASH`. | `CMakeLists.txt:445-452` |
| `install(DIRECTORY baseq3/ ...)` targets a directory that does not exist in the repo. | `CMakeLists.txt:469` |
| `q_shared.h` has `WIN32`, `MACOS_X`, `__MACOS__`, `__linux__`, and `__FreeBSD__` blocks and no `__APPLE__` branch or generic fallback. On Apple clang `MAC_STATIC`, `ID_INLINE`, `CPUSTRING`, `PATH_SEP`, and the endian macros are undefined. | `code/game/q_shared.h:133-310` |
| `common.c` includes `<winsock.h>` on anything that is not `__linux__` or `MACOS_X`. | `code/qcommon/common.c:28-36` |
| `DEDICATED` never reaches `common.c`, so `q3ded` registers `dedicated` with default `"0"` and `CVAR_LATCH`. It acts as a server only with `+set dedicated 1`. | `code/qcommon/common.c:2409` |
| `q_math.c` routes `VectorNormalize` through `Sys_VectorNormalize` in `q3sys` (C++). This is the qcommon to q3sys link cycle that `--start-group` hides. | `code/game/q_math.c:1094-1099` |
| `vm_x86.c` is a 29-line stub whose `VM_CallCompiled` has no return statement. It is not in any target. | `code/unix/vm_x86.c` |
| `-DARCH_STRING="x86_64"` is defined and used nowhere. | `CMakeLists.txt:29` |
| No CI configuration, no `CMakePresets.json`, no build directory, and no evidence that this configuration ever compiled. | repo root |

## Steps

### Phase A1: repository hygiene

- [x] **A1.1 Remove stray build files and tracked binaries.** Done on 2 September 2026. Run `git rm` on:
  `Screenshot From 2026-09-01 09-17-54.png` (deleted in the working tree, still tracked),
  `code/quake3.sln`, `code/quake3.vcproj`, `code/*.lnt`, `code/*.bat`, `code/Construct`,
  `code/Makefile`, `code/botlib/botlib.vcproj`, `code/botlib/*.mak`, `code/bspc/Conscript`,
  `code/bspc/*.sln`, `code/bspc/*.vcproj`, `code/bspc/*.mak`, every `Conscript`, `*.bat`, and
  `*.vcproj` under `code/cgame`, `code/game`, `code/q3_ui`, `code/ui`, `code/renderer`,
  `code/splines`, `code/unix/Cons_gcc.pm`, `code/unix/Conscript-*`, `code/unix/Makefile`,
  `code/unix/Makefile.Game`, `code/unix/cons`, `code/unix/pcons-2.3.1/`,
  `code/unix/build_setup.sh`, `code/unix/run-target.sh`, `code/unix/extract_ver.pl`,
  `code/unix/q3test.spec.sh`, `code/unix/Quake3.kdelnk`, `code/macosx/` (whole directory),
  `lcc/bin/*.exe`, `lcc/buildnt.bat`, `libs/*/*.vcproj`, `q3asm/*.sln`, `q3asm/*.vcproj`,
  `q3map/*.sln`, `q3map/*.vcproj`, `q3radiant/*.sln`, `q3radiant/*.vcproj`,
  `q3radiant/splines/Splines.vcproj`. Keep the tool sources in `lcc/`, `q3map/`,
  `q3radiant/`, `code/bspc/`, `libs/`, `common/`, and `q3asm/`.
  - **Tests:** none, because this step deletes files. The gate is the grep in Verify.
  - **Verify:**
    ```bash
    git ls-files | grep -Ei '\.(sln|vcproj|bat|lnt|mak|exe)$|Conscript|pcons|code/unix/cons$|code/macosx' | wc -l
    ```
    prints `0`.
- [x] **A1.2 Extend `.gitignore`.** Done on 2 September 2026. Add `build*/`, `.cache/`, `compile_commands.json`, and
  `CMakeUserPresets.json`. `*.pk3` and `build/` are already present.
  - **Tests:** none, because there is no behaviour.
  - **Verify:** `git status --short` after a container build shows no untracked build output.

### Phase A2: platform macro layer

- [x] **A2.1 Rewrite the OS chain in `q_shared.h`.** Done on 2 September 2026. Replace the five blocks at
  `code/game/q_shared.h:133-310` with one ordered chain:
  ```c
  #if defined(_WIN32)
    #define QDECL     __cdecl
    #define PATH_SEP  '\\'
    #define Q_EXPORT  __declspec(dllexport)
  #elif defined(__APPLE__)
    #define QDECL
    #define PATH_SEP  '/'
    #define Q_EXPORT  __attribute__((visibility("default")))
  #elif defined(__linux__) || defined(__FreeBSD__)
    #define QDECL
    #define PATH_SEP  '/'
    #define Q_EXPORT  __attribute__((visibility("default")))
  #else
    #error "unsupported platform"
  #endif
  ```
  After the chain define the shared items once: `MAC_STATIC` (empty), `ID_INLINE` (`inline`,
  or `__inline` on MSVC), `stricmp strcasecmp` on non-Windows, `CPUSTRING` as
  `OS_STRING "-" ARCH_STRING` (both come from CMake in A3.1), and the endian macros from
  `__BYTE_ORDER__` (all three targets are little endian, so `LittleShort` is a no-op and
  `BigShort` calls `ShortSwap`). Drop the PPC `__rlwimi` and `__dcbt` macros and the whole
  `__MACOS__` block.
  - **Tests:** none, because the compiler is the test. The gate is a clean compile of `qcommon` on
    all three CI legs.
  - **Verify:** in the container, `make configure && make build --target qcommon`
    succeeds. On the macOS CI leg the same target compiles.
- [x] **A2.2 Fix `common.c` includes and hunk defaults.** Done on 2 September 2026. Change `code/qcommon/common.c:28-36`
  to `#ifdef _WIN32 #include <winsock2.h> #else #include <netinet/in.h> #endif`. Unify the
  `MACOS_X` hunk megabytes at `common.c:45-50` to one default (64 and 24 work everywhere).
  - **Tests:** none, because there is no observable behaviour change.
  - **Verify:** `qcommon` compiles on the macOS leg.
- [x] **A2.3 Add missing public prototypes to `qcommon.h`.** Done on 2 September 2026. Add `char *FS_BuildOSPath(const
  char *base, const char *game, const char *qpath)` (defined at `code/qcommon/files.c:468`),
  `void Com_InitSmallZoneMemory(void)`, `void Com_InitZoneMemory(void)` (defined at
  `common.c:1376` and `:1388`), `void Sys_Sleep(int msec)`, `qboolean Sys_IsDedicatedBuild(void)`
  (step A5), and `void Sys_ReleaseDisplay(void)` (checklist 02 B3). Remove the hand-written
  externs at `code/unix/unix_main.c:702` and `:1232`, `code/unix/linux_signals.c:32`, and
  `tests/test_cvar_cmd.cpp:7-8` when those files are rewritten or edited.
  - **Tests:** none, because the compiler enforces this once A3.6 enables
    `-Werror=implicit-function-declaration`.
  - **Verify:** `make build 2>&1 | grep -c 'implicit'` prints `0` after A3.6.
- [x] **A2.4 Remove Linux-only behaviour switches that now change macOS.** Done on 2 September 2026. At
  `code/renderer/tr_init.c:868-872` make the `r_ext_texture_env_add` default `"1"` everywhere.
  At `tr_init.c:882-886` make `r_stencilbits` default `"8"` everywhere. At
  `code/client/cl_keys.c:1043` drop the platform gate around the Alt+Enter fullscreen toggle,
  because SDL handles all three platforms. Leave the `id386` assembler gates at
  `snd_mix.c:34` and `tr_shade_calc.c:1022`; they are already off on x86_64.
  - **Tests:** none, because these are cvar defaults. Checklist 08 covers renderer behaviour.
  - **Verify:** `grep -n '__linux__' code/renderer/tr_init.c code/client/cl_keys.c` prints nothing.

### Phase A3: CMake restructure

- [x] **A3.1 Derive architecture and platform strings.** Done on 2 September 2026. Replace `CMakeLists.txt:29` with:
  ```cmake
  string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _q3_proc)
  if(_q3_proc MATCHES "^(x86_64|amd64)$")
    set(Q3_ARCH x86_64)
  elseif(_q3_proc MATCHES "^(arm64|aarch64)$")
    set(Q3_ARCH arm64)
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
  add_compile_definitions(ARCH_STRING="${Q3_ARCH}" OS_STRING="${Q3_OS}" DLL_EXT="${Q3_DLL_EXT}")
  ```
  Use them in the three module targets:
  ```cmake
  set_target_properties(qagame PROPERTIES
    PREFIX "" OUTPUT_NAME "qagame${Q3_ARCH}" SUFFIX "${Q3_DLL_EXT}"
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/baseq3
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/baseq3
    C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden)
  ```
  Repeat for `cgame` and `ui`. `RUNTIME_OUTPUT_DIRECTORY` covers Windows `.dll` files. Set
  `RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}` on `quake3_modern` and `q3ded` so
  `SDL_GetBasePath()` (A4.4) finds `build/baseq3/` next to the binary. Hidden visibility means
  `vmMain` and `dllEntry` need `Q_EXPORT` (A4.10).
  - **Tests:** `tests/test_module_symbols.cpp` (binary `quake3_tests`). Cases:
    `ModuleSymbols.EveryModuleExportsVmMainAndDllEntry` opens each of `qagame`, `cgame`, and
    `ui` from `<build>/baseq3/` with `SDL_LoadObject` and asserts `SDL_LoadFunction` finds
    `vmMain` and `dllEntry`. `ModuleSymbols.FileNameSchemeMatchesPlatform` calls a new pure
    function `void Sys_ModuleFileName(const char *name, char *buf, int bufSize)` (introduced in
    A4.3) and asserts `buf` equals `name + ARCH_STRING + DLL_EXT`. Pass the build directory
    through a compile definition `Q3_TEST_BUILD_DIR`.
  - **Verify:**
    ```bash
    make build && ls build/baseq3/
    ```
    lists `qagamex86_64.so`, `cgamex86_64.so`, and `uix86_64.so` in the container. The macOS
    CI artifact contains `qagamearm64.dylib`, `cgamearm64.dylib`, and `uiarm64.dylib`.
- [x] **A3.2 Default the build type.** Done on 2 September 2026. Add before `project()` effects apply:
  ```cmake
  if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
  endif()
  ```
  CMake defines `NDEBUG` for `Release` and `RelWithDebInfo`. Do not define it by hand.
  - **Tests:** none, because this is configuration.
  - **Verify:** `grep CMAKE_BUILD_TYPE build/CMakeCache.txt` shows `RelWithDebInfo` after a fresh
    configure with no `-D` flag.
- [x] **A3.3 Convert static libraries to OBJECT libraries and delete `--start-group`.** Done on 2 September 2026. Change
  `botlib`, `q3jpeg`, `qcommon`, `q3sys`, `q3server`, `q3client`, and `q3renderer` to
  `add_library(<name> OBJECT ...)`. Link them directly:
  ```cmake
  target_link_libraries(quake3_modern PRIVATE q3client q3renderer q3server q3sys qcommon botlib q3jpeg)
  target_link_libraries(q3ded PRIVATE q3server q3sys qcommon botlib)
  ```
  CMake 3.12 and later places `$<TARGET_OBJECTS>` on the link line, so link cycles vanish and
  Apple ld and MSVC are satisfied. Remove every `-Wl,--start-group` and `-Wl,--end-group` from
  `CMakeLists.txt` and `tests/CMakeLists.txt`. Keep `target_include_directories(q3sys PUBLIC
  ...)` and give `q3sys` `target_link_libraries(q3sys PUBLIC SDL2::SDL2 OpenGL::GL
  luajit::luajit Threads::Threads ${CMAKE_DL_LIBS})`. With OBJECT libraries the `PUBLIC` usage
  requirements still propagate to consumers.
  - **Tests:** none, because the linker is the test. The link of `quake3_tests` covers it.
  - **Verify:** `grep -rn 'start-group' CMakeLists.txt tests/CMakeLists.txt` prints nothing, and
    the macOS CI leg links both executables.
- [x] **A3.4 Remove `DEDICATED` from every target.** Done on 2 September 2026. Delete it from `CMakeLists.txt:199`,
  `:214`, and `tests/CMakeLists.txt:25`. Step A5 replaces it with a runtime query. Keep
  `C_ONLY`; it is consulted only at `q_shared.h:111` for PPC and is harmless.
  - **Tests:** covered by A5.
  - **Verify:** `grep -rn 'DEDICATED' CMakeLists.txt tests/CMakeLists.txt` prints nothing.
- [x] **A3.5 Use imported targets for system libraries.** Done on 2 September 2026. Add `find_package(Threads REQUIRED)`.
  Prefer `SDL2::SDL2` (fall back to `${SDL2_LIBRARIES}` only if the target is absent) and
  `OpenGL::GL`. Replace raw `m dl pthread` with `$<$<NOT:$<PLATFORM_ID:Windows>>:m>`,
  `${CMAKE_DL_LIBS}`, and `Threads::Threads`. On Windows add `ws2_32 winmm shell32`. Define
  `SDL_MAIN_HANDLED` on both executables and call `SDL_SetMainReady()` at the top of `main`
  (A4.2) so one `main` serves both binaries and no `SDL2main` link is needed.
  - **Tests:** none, because the linker is the test.
  - **Verify:** the Windows CI leg links both executables.
- [x] **A3.6 Fix the warning flags.** Done on 2 September 2026. Delete `-Wno-implicit-function-declaration` and
  `-Wno-int-conversion` from `CMakeLists.txt:20-21`. Gate C-only flags with
  `$<$<COMPILE_LANGUAGE:C>:...>` so gcc does not warn "valid for C but not C++" once files
  become `.cpp` (checklist 04). Add
  `-Werror=implicit-function-declaration -Werror=int-conversion -Werror=incompatible-pointer-types`
  for GNU and Clang. Add an MSVC branch: `/W3 /wd4996 /wd4244 /wd4267 /D_CRT_SECURE_NO_WARNINGS
  /D_CRT_NONSTDC_NO_DEPRECATE /utf-8`. Add `option(Q3_WERROR "Treat warnings as errors" OFF)`
  and turn it on in CI for GNU and Clang.
  - **Tests:** none, because the compiler is the test. Checklist 02 B2 fixes the fallout.
  - **Verify:** `make build 2>&1 | grep -E 'implicit|int-conversion' | wc -l` prints `0`.
- [x] **A3.7 Add build options.** Done on 2 September 2026. `set(Q3_SANITIZE "" CACHE STRING "Sanitizers: address,undefined
  or thread")`. When set and the compiler is GNU or Clang, add
  `-fsanitize=${Q3_SANITIZE} -fno-omit-frame-pointer -fno-sanitize-recover=undefined -g` to
  `add_compile_options` and `add_link_options`, and `add_compile_definitions(Q3_SANITIZE)` so
  debug-only assertions compile in. MSVC supports `/fsanitize=address` only. Add
  `option(Q3_USE_CURL "HTTP downloads through libcurl" ON)` (used by checklist 06),
  `option(Q3_FETCH_LUAJIT "Build LuaJIT from source when no system package is found" ON)`
  (A6), and `include(CTest)` for the standard `BUILD_TESTING`.
  - **Tests:** none, because this is configuration. Checklist 03 C8 wires the sanitizer test
    environment.
  - **Verify:** `cmake --preset asan` configures and `grep fsanitize build-asan/CMakeCache.txt` is
    non-empty.
- [x] **A3.8 Pin GoogleTest.** Done on 2 September 2026. Keep `find_package(GTest)` first. Change the FetchContent block
  at `CMakeLists.txt:445-452` to include `URL_HASH SHA256=<hash of v1.14.0.zip>` and
  `FIND_PACKAGE_ARGS` (CMake 3.24 and later) so the system package wins when present. Set
  `set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)`. Compute the hash inside the container with
  `curl -L <url> | sha256sum` and paste it.
  - **Tests:** none, because this is configuration.
  - **Verify:** a configure with the system GTest removed from the image downloads the pinned zip
    and a tampered hash fails the configure.
- [x] **A3.9 Fix the install layout.** Done on 2 September 2026. Install executables to `${CMAKE_INSTALL_BINDIR}` and
  modules to `${CMAKE_INSTALL_BINDIR}/baseq3` to match the runtime layout. Delete
  `install(DIRECTORY baseq3/ ...)` at `CMakeLists.txt:469`; pak files are owner supplied. Add a
  configure-time `message(STATUS "Copy pak0.pk3 to pak8.pk3 into ${CMAKE_BINARY_DIR}/baseq3
  to run from the build tree")`.
  - **Tests:** none, because this is configuration.
  - **Verify:** `make shell -c 'cmake --install build --prefix /tmp/q3'` succeeds and
    `ls /tmp/q3/bin/baseq3` lists the three modules.
- [x] **A3.10 Add `CMakePresets.json`.** Done on 2 September 2026. Configure presets: `dev` (Ninja, RelWithDebInfo,
  `BUILD_TESTING=ON`, binary dir `build`), `debug`, `release`, `asan`
  (`Q3_SANITIZE=address,undefined`, binary dir `build-asan`), `tsan` (`Q3_SANITIZE=thread`,
  binary dir `build-tsan`, used by checklist 05), `msvc` (Visual Studio 17 2022 or Ninja plus
  `cl`, `CMAKE_TOOLCHAIN_FILE=$env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake`), and `mingw`.
  Build presets for each. Test presets with `--output-on-failure --timeout 120`.
  - **Tests:** none, because this is configuration.
  - **Verify:** `cmake --list-presets` inside the container lists all presets, and
    `cmake --preset dev && cmake --build --preset dev && ctest --preset dev` succeeds.

### Phase A4: platform layer rewrite

Author every new file as `.cpp`. Wrap the `Sys_*`, `IN_*`, `NET_*`, and `main` symbols in
`extern "C"` so the legacy C translation units link. Checklist 04 converts the rest of the tree
and then removes the wrappers in its header close-out. Move code verbatim where the audit
found it correct, and change only what each step names.

- [x] **A4.1 Add `code/sys/sys_local.h`.** Done on 2 September 2026. Internal prototypes shared by the platform files:
  `Sys_PlatformInit`, `Sys_PlatformExit`, `Sys_InitSignals`, `Sys_ConsoleInputInit`,
  `Sys_ConsoleInputShutdown`, `Sys_ConsoleInput`, `Sys_QueEvent`, `Sys_SendKeyEvents`,
  `Sys_GetPacket`, `IN_*`, and `Sys_ListFilteredFiles`. This replaces `code/unix/linux_local.h`.
  - **Tests:** none, because it is a header.
  - **Verify:** no file outside `code/sys/` includes `sys_local.h`.
- [x] **A4.2 Add `code/sys/sys_main.cpp`.** Done on 2 September 2026. Portable entry point and services. `main()` calls
  `SDL_SetMainReady()`, merges `argv`, runs `Sys_ParseArgs` (`--version`, `-v`, and a new
  `--help` that prints usage and the `+set` convention), `Sys_PlatformInit`, `Com_Init`,
  `NET_Init`, `Sys_ConsoleInputInit`, `Sys_InitSignals`, then loops on `Com_Frame`. `Sys_Exit`
  always calls `exit()` (the `_exit` workaround at `code/unix/unix_main.c:329` for GL driver
  `atexit` handlers is obsolete) after `Sys_ConsoleInputShutdown` and `SDL_Quit`; delete the
  `assert(ex == 0)` at `unix_main.c:333`. `Sys_Quit`, `Sys_Error` (rewritten in checklist 02
  B3), `Sys_Print`, `Sys_Init` (registers `in_restart`, sets the `arch` cvar to
  `OS_STRING "-" ARCH_STRING`, `username`, calls `IN_Init`). Event queue `Sys_QueEvent` and
  `Sys_GetEvent` verbatim from `unix_main.c:1022-1140`. `Sys_Milliseconds` from
  `SDL_GetPerformanceCounter()` and `SDL_GetPerformanceFrequency()` with the base captured on
  first call, returning `int` milliseconds since start (monotonic, works before `SDL_Init`).
  `Sys_Sleep(int msec)` calls `SDL_Delay`. `Sys_GetClipboardData` calls `SDL_GetClipboardText`,
  copies into a `Z_Malloc` buffer, and `SDL_free`s the original. Stream stubs from
  `unix_main.c:816-836`. `Sys_CheckCD` returns `qtrue`. `Sys_LowPhysicalMemory` and friends are
  no-ops. `Sys_SnapVector` uses `rint`. `Sys_PrintBinVersion` prints `OS_STRING` and
  `ARCH_STRING` and uses `Sys_IsDedicatedBuild()` (A5) for the banner.
  - **Tests:** `tests/test_sys_time.cpp` (binary `quake3_tests`) is owned by checklist 02 B7 and
    covers `Sys_Milliseconds` and `Sys_Sleep`. For this step add
    `SysMain.ParseArgsRecognisesHelp` in `tests/test_sys_args.cpp` if `Sys_ParseArgs` is
    factored into a pure function that returns an action enum; otherwise `Tests: none, because
    main is not unit-testable` and the gate is `quake3_modern --help` in Verify.
  - **Verify:**
    ```bash
    make shell -c './build/quake3_modern --help; ./build/quake3_modern --version'
    ```
    prints usage and a banner that names `linux-x86_64`.
- [x] **A4.3 Add `code/sys/sys_dll.cpp`.** Done on 2 September 2026. `Sys_LoadDll` and `Sys_UnloadDll` through
  `SDL_LoadObject`, `SDL_LoadFunction`, and `SDL_UnloadObject`. Add the pure helper
  `void Sys_ModuleFileName(const char *name, char *buf, int bufSize)` that produces
  `"%s" ARCH_STRING DLL_EXT`. Search order: current directory, `fs_homepath`, `fs_basepath`,
  then the executable directory from `Sys_DefaultInstallPath` so the build tree works from any
  working directory. Checklist 02 B1 changes the entry point and syscall signatures; make
  both edits together if you land them in the same PR.
  - **Tests:** `tests/test_module_symbols.cpp` (A3.1) covers the name scheme and the exports.
  - **Verify:** `make shell -c 'cd /tmp && /src/build/q3ded +set fs_basepath /paks +map q3dm1 +quit'`
    loads `qagame` from the build tree while the working directory is elsewhere.
- [x] **A4.4 Add `code/sys/sys_files_unix.cpp`.** Done on 2 September 2026. Move `Sys_ListFiles`, `Sys_ListFilteredFiles`,
  `Sys_FreeFileList`, `Sys_Cwd`, `Sys_GetCurrentUser`, and `Sys_SetDefault*Path` verbatim from
  `code/unix/unix_shared.c`, with `MACOS_X` replaced by `__APPLE__`. Change `Sys_Mkdir` to
  `mkdir(path, 0755)`. `Sys_DefaultInstallPath` returns `SDL_GetBasePath()` with the trailing
  separator stripped, falling back to `Sys_Cwd`. `Sys_DefaultHomePath` returns `$HOME/.q3a` on
  Linux and `$HOME/Library/Application Support/Quake3` on macOS, creates it with mode `0700`,
  and on failure prints a warning and returns `Sys_DefaultInstallPath()` instead of calling
  `Sys_Error` (the current behaviour at `unix_shared.c:384-404`).
  - **Tests:** `tests/test_sys_paths.cpp` (binary `quake3_tests`). Cases:
    `SysPaths.HomePathUsesHomeEnv` sets `HOME` to a `TempDir` and asserts the result ends in
    `.q3a` on Linux or `Library/Application Support/Quake3` on macOS and that the directory
    exists with mode `0700`; `SysPaths.HomePathFallsBackWhenMkdirFails` points `HOME` at a
    read-only directory and asserts the install path is returned and no exception is thrown;
    `SysPaths.InstallPathIsExecutableDir` asserts the result is a directory that contains the
    test binary; `SysPaths.ListFilesFiltersByExtension` creates three files in a `TempDir` and
    asserts `Sys_ListFiles(dir, ".cfg", ...)` returns the matching one.
  - **Verify:** `ctest --preset dev -R SysPaths` passes in the container.
- [x] **A4.5 Add `code/sys/sys_files_win32.cpp`.** Done on 2 September 2026. Same API with `FindFirstFileA` and
  `FindNextFileA` (mirror the loop structure of `Sys_ListFilteredFiles`), `_mkdir`, `_getcwd`,
  `GetUserNameA`, `SDL_GetPrefPath("", "Quake3")` for the home path (yields
  `%APPDATA%\Quake3\`), and `SDL_GetBasePath` for the install path.
  - **Tests:** `tests/test_sys_paths.cpp` runs on the Windows CI leg with the same cases; the mode
    assertion is skipped on Windows.
  - **Verify:** the Windows CI leg passes `ctest -R SysPaths`.
- [x] **A4.6 Add `code/sys/sys_unix.cpp`.** Done on 2 September 2026. The tty console (`tty_*`, `Hist_*`,
  `Sys_ConsoleInputInit`, `Sys_ConsoleInputShutdown`, `Sys_ConsoleInput`) verbatim from
  `code/unix/unix_main.c:157-665`. `Sys_PlatformInit` does `seteuid(getuid())` and ignores
  `SIGTTIN`, `SIGTTOU`, and `SIGPIPE`. `Sys_InitSignals` is written in checklist 02 B3; add a
  stub that installs nothing so this file links first. Remove `Sys_ConfigureFPU` (i386 only).
  - **Tests:** none, because the tty console needs a terminal. The gate is the `q3ded` smoke in
    Verify.
  - **Verify:**
    ```bash
    make shell -c 'printf "status\nquit\n" | ./build/q3ded +set fs_basepath /paks +map q3dm1'
    ```
    prints a player table and exits `0`.
- [x] **A4.7 Add `code/sys/sys_win32.cpp`.** Done on 2 September 2026. `Sys_ConsoleInput` through `_kbhit()` and
  `_getch()` with a line buffer (dedicated only). `Sys_PlatformInit` calls
  `SetConsoleOutputCP(CP_UTF8)` and `timeBeginPeriod(1)`. `Sys_InitSignals` installs
  `SetUnhandledExceptionFilter` (body in checklist 02 B3). `Sys_ConsoleInputInit` and
  `Sys_ConsoleInputShutdown` are no-ops.
  - **Tests:** none, because there is no runner with a console session. The gate is the Windows
    CI compile and `ctest`.
  - **Verify:** the Windows CI leg builds `q3ded.exe` and `quake3_modern.exe`.
- [x] **A4.8 Add `code/sys/net/sys_net.cpp` and `code/sys/net/net_compat.h`.** Done on 2 September 2026. Move
  `code/unix/unix_net.c` and make it portable. `net_compat.h` defines on `_WIN32`:
  `typedef SOCKET socket_t;`, `#define Q3_INVALID_SOCKET INVALID_SOCKET`,
  `#define q3_closesocket closesocket`, `#define q3_sockerrno() WSAGetLastError()`, and
  non-blocking through `ioctlsocket(s, FIONBIO, &one)`; otherwise `typedef int socket_t;`,
  `Q3_INVALID_SOCKET (-1)`, `close`, `errno`, and `fcntl(O_NONBLOCK)`. `NET_Init` calls
  `WSAStartup(MAKEWORD(2,2))` on Windows and `NET_Shutdown` calls `WSACleanup`.
  `NET_ErrorString` uses `strerror(errno)` or `FormatMessageA(WSAGetLastError())`. Delete the
  `MACOS_X` `NET_GetLocalAddress` stub at `unix_net.c:336-426` and keep the generic
  `gethostname` and `gethostbyname` version. Replace `*(int*)&s->sin_addr` aliasing with
  `memcpy`. Change `ip_socket` to `socket_t` initialised to `Q3_INVALID_SOCKET`; today `0`
  means "closed", which is a valid descriptor on Unix. `NET_Sleep` changes in checklist 02 B7.
  - **Tests:** `tests/test_sys_net.cpp` (binary `quake3_tests`). Cases:
    `SysNet.StringToAdrParsesIPv4AndPort` for `NET_StringToAdr("127.0.0.1:27960")`;
    `SysNet.LoopbackPacketRoundTrip` sends through `NET_SendLoopPacket` and reads with
    `NET_GetLoopPacket`; `SysNet.InvalidSocketSentinelIsNotZero` asserts
    `Q3_INVALID_SOCKET != 0`; `SysNet.ErrorStringIsNonEmpty` after a forced `connect` failure.
  - **Verify:** `ctest --preset dev -R SysNet` passes in the container and on the Windows leg.
- [x] **A4.9 Add `code/qcommon/vm_none.c`.** Done on 2 September 2026. `VM_Compile` is a no-op and `VM_CallCompiled`
  calls `Com_Error(ERR_FATAL, "VM_CallCompiled: no JIT on this platform")` and returns `0`.
  Add it to `QCOMMON_SOURCES`. Delete `code/unix/vm_x86.c` and `code/unix/qasm.h`.
  - **Tests:** none, because the path is unreachable (`vm->compiled` is never set).
  - **Verify:** `qcommon` links without undefined `VM_Compile` or `VM_CallCompiled`.
- [x] **A4.10 Export module entry points and switch the CMake sources.** Done on 2 September 2026. Add `Q_EXPORT` to
  `vmMain` in `code/game/g_main.c:203`, `code/cgame/cg_main.c:46`, `code/q3_ui/ui_main.c:43`,
  and `code/ui/ui_main.c:168`, and to `dllEntry` in `code/game/g_syscalls.c:34`,
  `code/cgame/cg_syscalls.c:34`, and `code/ui/ui_syscalls.c:33`. Checklist 02 B1 changes the
  same signatures, so do both edits at once when possible. In CMake define:
  ```cmake
  set(SYS_PLATFORM_SOURCES
    code/sys/sys_main.cpp code/sys/sys_dll.cpp code/sys/net/sys_net.cpp
    $<IF:$<PLATFORM_ID:Windows>,code/sys/sys_win32.cpp;code/sys/sys_files_win32.cpp,code/sys/sys_unix.cpp;code/sys/sys_files_unix.cpp>)
  ```
  Use it for both executables. Remove `${CMAKE_CURRENT_SOURCE_DIR}/code/unix` from
  `include_directories`. Delete `code/unix/` and, from `code/null/`, `null_glimp.c`,
  `null_main.c`, `null_net.c`, and `mac_net.c`. Keep `null_client.c`, `null_input.c`, and
  `null_snddma.c`.
  - **Tests:** `tests/test_module_symbols.cpp` (A3.1) fails if `Q_EXPORT` is missing under hidden
    visibility.
  - **Verify:**
    ```bash
    git ls-files code/unix code/macosx | wc -l
    ```
    prints `0`. In the container, `./build/quake3_modern +set fs_basepath /paks +set r_fullscreen 0 +map q3dm1`
    renders under `xvfb-run` (checklist 00 smoke). On the macOS artifact,
    `nm -gU baseq3/qagamearm64.dylib | grep -E 'vmMain|dllEntry'` shows both exported.
- [x] **A4.11 Fix `http_downloader.cpp` platform code until checklist 06 replaces it.** Done on 2 September 2026. Use
  `socket_t` and a `close_socket()` helper from `net_compat.h`, call `WSAStartup` once through
  `std::once_flag` on Windows, `shutdown(SHUT_RDWR)` before close, and set `SO_RCVTIMEO` so a
  hung worker cannot block exit. Skip this step if checklist 06 N1.4 has already landed.
  - **Tests:** none here, because checklist 06 rewrites the downloader and its tests.
  - **Verify:** the Windows CI leg compiles `http_downloader.cpp`.

### Phase A5: `DEDICATED` at runtime

- [x] **A5.1 Add `Sys_IsDedicatedBuild`.** Done on 2 September 2026. In `code/null/null_client.c` add
  `qboolean Sys_IsDedicatedBuild(void) { return qtrue; }` and
  `void Sys_ReleaseDisplay(void) {}`. In `code/client/cl_main.c` add
  `qboolean Sys_IsDedicatedBuild(void) { return qfalse; }`. `Sys_ReleaseDisplay` for the client
  lives in `sys_sdl.cpp` (checklist 02 B3). Add both stubs to `tests/test_platform_stubs.cpp`
  (`Sys_IsDedicatedBuild` returns `qtrue`).
  - **Tests:** none, because the function is a constant per binary. Covered by A5.2.
  - **Verify:** both executables link.
- [x] **A5.2 Use it for the `dedicated` default.** Done on 2 September 2026. Change `code/qcommon/common.c:2409-2413` to
  `com_dedicated = Cvar_Get("dedicated", Sys_IsDedicatedBuild() ? "1" : "0",
  Sys_IsDedicatedBuild() ? CVAR_ROM : CVAR_LATCH);`. Leave the other `#ifndef DEDICATED`
  blocks unconditional; they already compile that way because qcommon never had the define,
  and the null stubs satisfy `CL_ShutdownCGame` and friends. Delete the `#else` CD key default
  at `common.c:2244` (checklist 02 B9 removes CD keys).
  - **Tests:** `tests/test_cvar_cmd.cpp` case `Dedicated.DefaultFollowsBuildKind` asserts that
    after `Com_Init`-free registration through the same expression the cvar is `1` and
    `CVAR_ROM` when the stub returns `qtrue`.
  - **Verify:**
    ```bash
    make shell -c './build/quake3_modern --version; ./build/q3ded +set dedicated 0 +cvarlist dedicated +quit'
    ```
    prints "Full Executable" for the client, and `q3ded` reports `dedicated` as `1` and refuses
    the change.

### Phase A6: LuaJIT acquisition

- [x] **A6.1 Replace the LuaJIT lookup.** Done on 2 September 2026. Replace `CMakeLists.txt:35-38` and the uses at
  `:180,182` with:
  ```cmake
  option(Q3_FETCH_LUAJIT "Build LuaJIT from source when no system package is found" ON)
  find_package(PkgConfig QUIET)
  if(PKG_CONFIG_FOUND)
    pkg_check_modules(LUAJIT IMPORTED_TARGET luajit)
  endif()
  if(LUAJIT_FOUND)
    add_library(luajit::luajit ALIAS PkgConfig::LUAJIT)
  elseif(Q3_FETCH_LUAJIT)
    include(FetchContent)
    FetchContent_Declare(luajit
      GIT_REPOSITORY https://github.com/zhaozg/luajit-cmake
      GIT_TAG        <pinned commit sha>
      GIT_SUBMODULES_RECURSE ON)
    FetchContent_MakeAvailable(luajit)
    add_library(luajit::luajit ALIAS libluajit)   # confirm the wrapper's target name
  else()
    message(FATAL_ERROR
      "LuaJIT not found. Install it (brew install luajit | apt install libluajit-5.1-dev | vcpkg install luajit) "
      "or configure with -DQ3_FETCH_LUAJIT=ON.")
  endif()
  target_link_libraries(q3sys PUBLIC luajit::luajit)
  ```
  Follow-up found on 3 September 2026: this lookup fails on Windows. `pkg_check_modules` needs
  pkg-config, which the `windows-2022` runner does not have, so `LUAJIT_FOUND` is false even
  with the vcpkg port installed and the build falls through to FetchContent, which then breaks.
  Add a `find_path`/`find_library` attempt between the two, because the vcpkg toolchain makes
  those work without pkg-config. See `docs/plans/00-environment.md`.

  `PUBLIC` is required because `script_engine.hpp` includes `sol.hpp`, which tests include.
  LuaJIT 2.1 is required for arm64; the wrapper tracks it. Pin the commit SHA when you land
  the step and record it in `docs/building.md`.
  - **Tests:** none, because this is configuration. `ctest -R ModernScripting` proves the link.
  - **Verify:** in a container variant without `libluajit-5.1-dev`, `cmake --preset dev` completes
    through FetchContent and `ctest --preset dev -R ModernScripting` passes. With
    `-DQ3_FETCH_LUAJIT=OFF` and no system package, configure fails with the hint text.

- [x] **A6.2 Add `Q3_FETCH_DEPS` for hosts with no packages (macOS native build).** Done on 2 September 2026. Add
  `option(Q3_FETCH_DEPS "Fetch SDL2, LuaJIT, and GoogleTest into the build tree" OFF)`. When it
  is ON: skip `find_package(SDL2)` and `FetchContent_Declare(SDL2 GIT_REPOSITORY
  https://github.com/libsdl-org/SDL.git GIT_TAG release-2.32.8)` with `SDL_TEST OFF`,
  `SDL_SHARED OFF`, `SDL_STATIC ON`, then use the `SDL2::SDL2-static` target; force
  `Q3_FETCH_LUAJIT ON` (A6.1); let the existing GoogleTest FetchContent run. On macOS link the
  system `libcurl` (`find_package(CURL)` finds `/usr/lib/libcurl.dylib`; nothing to fetch). The
  root `Makefile` targets `native-configure`, `native-build`, and `native-test` already pass
  `-DQ3_FETCH_DEPS=ON`; they exist so that a Mac with only Xcode, CMake, and Ninja builds and
  tests without a `brew install`. Owner decision 16 in `README.md`.
  - **Tests:** none, because this is configuration. `ctest` in the native build proves it.
  - **Verify:** on a Mac with no Homebrew SDL2 or LuaJIT, `make native-test` configures (network
    needed for the fetch), builds, and passes `ctest`. On Linux in the container,
    `-DQ3_FETCH_DEPS=ON` also works and produces the same test results as the packaged build.

- [ ] **A6.3 Make the MinGW cross build configure.** `cmake/toolchain-mingw-w64.cmake` and
  `docker/Dockerfile.mingw` exist (checklist 00 step 7). The image provides SDL2, curl
  (Schannel), and a static LuaJIT under `/opt/mingw-deps` with a `luajit.pc`, and sets
  `PKG_CONFIG_LIBDIR` so `pkg_check_modules(LUAJIT luajit)` finds it. Confirm that A3 and A6.1
  work under that toolchain: `SDL2::SDL2` resolves from `sdl2-config.cmake`, `OpenGL::GL` maps
  to `opengl32`, `ws2_32 winmm shell32` are linked, `WIN32` selects `sys_win32.cpp` and
  `sys_files_win32.cpp` (A4), and `Q3_DLL_EXT` is `.dll`. `CMAKE_CROSSCOMPILING_EMULATOR` is
  `wine`, so `gtest_discover_tests` and `ctest` run the `.exe` files under Wine.
  - **Tests:** the whole suite runs under Wine through `make win-test`.
  - **Verify:** `make win-build` produces `build-win64/quake3_modern.exe`, `q3ded.exe`,
    `baseq3/qagamex86_64.dll`, `cgamex86_64.dll`, `uix86_64.dll`; `make win-test` passes;
    `x86_64-w64-mingw32-objdump -p build-win64/tests/quake3_tests.exe | grep 'DLL Name'` lists
    only `SDL2.dll`, `libcurl`, the MinGW runtime DLLs, and Windows system DLLs.

### Phase A7: continuous integration

- [x] **A7.1 Add `.github/workflows/ci.yml`.** Done on 2 September 2026. Add a `linux-mingw` job that builds `docker/Dockerfile.mingw` and runs `make win-test` with `CI=1`, so the Windows cross build and Wine tests run on every push next to the MSVC leg. Jobs: `linux` on `ubuntu-24.04` runs inside the
  image from checklist 00 with presets `dev` and `asan`, `Q3_WERROR=ON`, `ctest` with
  `--gtest_shuffle` through `EXTRA_ARGS`, and the headless smoke script; `macos` on `macos-15`
  (arm64) runs `brew install ninja sdl2 luajit`, preset `dev`, and `ctest`; `windows` on
  `windows-2022` installs `sdl2 luajit gtest curl` through vcpkg, preset `msvc`, builds, and
  runs `ctest`. Upload `quake3_modern`, `q3ded`, and `baseq3/*` as artifacts per leg so the
  owner can run a real-GPU check without a local install. Set `concurrency:
  cancel-in-progress: true`. Add `PROPERTIES TIMEOUT 60` to `gtest_discover_tests`.
  - **Tests:** none, because CI is the test runner. Checklist 03 C8 adds the sanitizer environment.
  - **Verify:** all three legs are green on the PR, and the sanitizer leg passes with
    `ASAN_OPTIONS=detect_leaks=1:strict_string_checks=1` and `UBSAN_OPTIONS=print_stacktrace=1`.

### Phase A8: build documentation

- [x] **A8.1 Write `docs/building.md`.** Done on 2 September 2026. Sections: prerequisites per platform (container for
  Linux, `brew` list for macOS, vcpkg list for Windows), presets and what each does, where to
  put pak files (`build/baseq3/` or `+set fs_basepath`), home paths per platform, module
  naming (`qagame<arch><ext>`), the sanitizer presets, and the pinned `luajit-cmake` SHA.
  Update `docs/architecture.md:7-8` (sys layer file list) and the VM ABI paragraph at `:17`
  after checklist 02 B1. Checklist 10 owns the root `README.md` that links here.
  - **Tests:** none, because this is documentation. Checklist 10 adds the link check.
  - **Verify:** a reader who follows only `docs/building.md` reaches a green `ctest` in the
    container.

## Correction on 3 September 2026

`tests/test_module_symbols.cpp` was rewritten. As first written it held two cases with identical
bodies, and both called `GTEST_SKIP()` when a module file was absent, so the test passed on any
build where the modules had not been produced. This test is the tripwire for C++ name mangling
on `vmMain` and `dllEntry`, so it now fails with the list of paths it tried instead of skipping,
and the duplicate case is gone. Keep that property: a guard that can only skip guards nothing.

## Test map

| Test file | Binary | Cases | Added by |
|---|---|---|---|
| `tests/test_module_symbols.cpp` | `quake3_tests` | `FileNameSchemeMatchesPlatform`, `EveryModuleExportsUnmangledEntryPoints` | A3.1, A4.3, A4.10; hardened 3 September 2026 |
| `tests/test_sys_paths.cpp` | `quake3_tests` | `HomePathUsesHomeEnv`, `HomePathFallsBackWhenMkdirFails`, `InstallPathIsExecutableDir`, `ListFilesFiltersByExtension` | A4.4, A4.5 |
| `tests/test_sys_net.cpp` | `quake3_tests` | `StringToAdrParsesIPv4AndPort`, `LoopbackPacketRoundTrip`, `InvalidSocketSentinelIsNotZero`, `ErrorStringIsNonEmpty` | A4.8 |
| `tests/test_sys_args.cpp` | `quake3_tests` | `ParseArgsRecognisesHelp` (only if `Sys_ParseArgs` is factored pure) | A4.2 |
| `tests/test_cvar_cmd.cpp` | `quake3_tests` | `Dedicated.DefaultFollowsBuildKind` | A5.2 |
| `tests/test_platform_stubs.cpp` | both | `Sys_IsDedicatedBuild`, `Sys_ReleaseDisplay` stubs | A5.1 |

## Out of scope

- The Windows runtime smoke. Hosted runners have no GPU, so the owner runs the Windows
  artifact by hand when wanted.
- The GL function loader that replaces `linux_qgl.c` for the renderer. Checklist 08 R1.1 owns
  it and depends on `code/unix/` being gone.
- Crash handler bodies, `Sys_Error`, frame pacing, and first-run messages. Checklist 02 owns
  them; this file only creates the files they land in.
- Renaming legacy `.c` files. Checklist 04 owns it.

## Follow-ons

- A `mingw` CI leg once the MSVC leg is stable.
- `CMAKE_EXPORT_COMPILE_COMMANDS` and a `compile_commands.json` symlink for editor tooling.

## Done criteria

- In the container, `make configure && make build` produces
  `build/quake3_modern`, `build/q3ded`, and `build/baseq3/{qagame,cgame,ui}x86_64.so`, and
  `make test` is green.
- The macOS CI artifact contains `baseq3/{qagame,cgame,ui}arm64.dylib`, and `nm -gU` shows
  `vmMain` and `dllEntry` exported from each.
- `quake3_modern --version` prints "Full Executable". `q3ded` reports `dedicated 1` as
  `CVAR_ROM`.
- `q3ded +map q3dm1` accepts `status` on the tty inside the container.
- The headless smoke from checklist 00 renders `q3dm1` from the build tree.
- CI is green on the Linux (`dev`, `asan`), macOS, and Windows legs with `Q3_WERROR=ON` on GNU
  and Clang.
- `git ls-files code/unix code/macosx` is empty, and the stray-file grep in A1.1 prints `0`.
- Every row of the test map exists and passes under `ctest --preset dev` and
  `ctest --preset asan`.

## Last step

- [ ] Delete this file and remove its row from `docs/plans/README.md`.
