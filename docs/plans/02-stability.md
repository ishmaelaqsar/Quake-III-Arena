# Checklist 02: stability

## Purpose

Fix the defects that make the engine crash, leak, stall, or lie to the user. This covers the
half-finished 64-bit virtual machine (VM) ABI, the missing crash handler, the unsafe logger,
the `sys_api` boundary, the VFS hook that bypasses the file system, the busy-wait frame loop,
the hostile first-run message, and the CD key and authorize server gates that no longer serve
a purpose in a GPL build.

**Status:** In progress. Phases B1 and B2 complete on 2 September 2026 (64-bit VM ABI, prototype fallout).

## Prerequisites

- Checklist `01-build-portability.md` is complete. The platform files under `code/sys/` exist
  and the tree builds on all three CI legs.
- Checklist `00-environment.md` provides the container. Every command below runs through
  the Makefile targets unless it names a CI artifact.
- Checklist `04-cxx-migration.md` prep PR (C-P0) can land before or after this file. Step B1
  (VM ABI) is done in C, before any file is renamed to `.cpp`, because it edits the same
  `*_syscalls.c` files that the rename touches.
- Owner decisions this file relies on:
  - Remove the VFS `FS_ReadFile` and `FS_WriteFile` hooks rather than fix them in place.
  - `com_skipIntro` defaults to `1`.
  - Windows crash reports use a message box only. DbgHelp minidumps are behind an option that
    defaults off.
  - `LOG_WARN` and `LOG_ERROR` are always compiled; `LOG_DEBUG` and `LOG_INFO` are stripped
    only by an explicit define, never by `NDEBUG`.

## Background

| Defect | Anchor |
|---|---|
| `VM_Call` stores arguments in `int args[16]` and reads them with `va_arg(ap, int)`. The entry point type is `int (*)(int, ...)`. `vmMain` takes `int` parameters. Pointers passed through are truncated to 32 bits. | `code/qcommon/vm.c:673,694,698-701`, `code/qcommon/vm_local.h:138`, `code/game/g_main.c:203` |
| `VM_DllSyscall` returns `int`, the `systemCall` pointer type is `int (*)(int *)`, and `cl_ui.c` returns `(intptr_t)strncpy(...)` from an `int` function. `vm.c:345` casts `intptr_t[]` to `int*`. | `code/qcommon/vm.c:333,345`, `code/qcommon/vm_local.h:130`, `code/client/cl_ui.c:1047` |
| The client installs no signal handler. `InitSig()` is called only under `DEDICATED` (`unix_main.c:1267`) or from the unbuilt `linux_glimp.c`. The handler that exists calls `printf`, `GLimp_Shutdown`, and `Sys_Exit` from signal context, uses `signal()` instead of `sigaction`, and stores its flag in a plain `qboolean`. No backtrace anywhere. | `code/unix/linux_signals.c:30-61` |
| `Sys_Exit` calls `_exit` under `NDEBUG`. `Sys_Error` formats with `vsprintf` into 1024 bytes, prints to stderr, and never releases fullscreen or the mouse grab. `floating_point_exception_handler` swallows `SIGFPE` and re-arms. | `code/unix/unix_main.c:329,393-416,457-460` |
| The logger has no mutex, compiles every `LOG_*` macro including `LOG_ERROR` to nothing under `NDEBUG`, prints the absolute `__FILE__` path, builds an `ostringstream` per call, and calls the console sink from whatever thread logs. | `code/sys/logger/logger.hpp:35-72` |
| `Sys_SubsystemInit` allocates the script engine with `new` and never frees it. Every `extern "C"` function in `sys_api.cpp` can throw. `Sys_VFS_ReadFile` truncates the size to `int`. | `code/sys/sys_api.cpp:9-33,60-72` |
| `FS_ReadFile` calls `Sys_VFS_ReadFile` first and returns early. This skips `fs_loadStack++`, so `FS_FreeFile` drives the counter negative and `Hunk_ClearTempMemory` never runs again. It also bypasses pure-server and pak checks. `FS_WriteFile` writes twice. | `code/qcommon/files.c:1500-1502,1554,1592,1625,1630,1643` |
| The VFS mounts the literal relative string `"baseq3"` against the process working directory before `Com_StartupVariable` parses `fs_basepath`. | `code/sys/sys_api.cpp:31`, `code/qcommon/common.c:2367-2390` |
| `VirtualFileSystem::write_binary` has no path containment. | `code/sys/fs/vfs.cpp:108-124` |
| The frame loop spins until the frame budget is spent. There is no `Sys_Sleep`, `usleep`, or `SDL_Delay` anywhere in `code/`. `Sys_Milliseconds` uses `gettimeofday`. | `code/qcommon/common.c:2700-2713`, `code/unix/unix_shared.c:62-77` |
| `Sys_DefaultHomePath` creates the directory with mode `0777` and calls `Sys_Error` on failure. `Sys_DefaultInstallPath` falls back to the working directory. | `code/unix/unix_shared.c:371-404` |
| The only missing-pak message is `Couldn't load default.cfg`. | `code/qcommon/files.c:3250-3280` |
| The CD key prompt is live on first menu entry. The validator, key file writes, and menu are all present. | `code/q3_ui/ui_menu.c:276-282`, `code/q3_ui/ui_cdkey.c`, `code/client/cl_main.c:3293-3345`, `code/qcommon/common.c:2242-2330,2552-2559` |
| The client sends an authorize request to a dead host on connect, and the server waits for the dead host before answering a challenge. | `code/client/cl_main.c` `CL_RequestAuthorization`, `code/server/sv_client.c:60-140`, `code/qcommon/qcommon.h:240` |
| The first run plays `intro.RoQ`. | `code/qcommon/common.c:2489-2492` |

## Steps

### Phase B1: 64-bit VM ABI end to end

Use the ioquake3 model. Modules export `intptr_t vmMain(int command, int arg0, ..., int arg11)`
and `void dllEntry(intptr_t (QDECL *syscallptr)(intptr_t, ...))`. Engine syscall handlers are
`intptr_t handler(intptr_t *args)`.

- [x] **B1.1 Change the VM types.** Done on 2 September 2026. In `code/qcommon/vm_local.h:130` change `systemCall` to
  `intptr_t (*systemCall)(intptr_t *parms)`. At `:138` change `entryPoint` to
  `intptr_t (QDECL *entryPoint)(int callNum, ...)`. Keep `VM_CallInterpreted` and
  `VM_CallCompiled` at `:171,174` returning `int` with `int *args`.
- [x] **B1.2 Change the public prototypes.** Done on 2 September 2026. In `code/qcommon/qcommon.h:318-331` change
  `VM_Create` to take `intptr_t (*systemCalls)(intptr_t *)`, and `VM_Call` to return
  `intptr_t`. At `:944` change `Sys_LoadDll` to
  `void *Sys_LoadDll(const char *name, char *fqpath, intptr_t (QDECL **entryPoint)(int, ...),
  intptr_t (QDECL *systemcalls)(intptr_t, ...))`.
- [x] **B1.3 Fix `vm.c`.** Done on 2 September 2026. At `code/qcommon/vm.c:333-346` make `VM_DllSyscall` return
  `intptr_t`, take `intptr_t arg, ...`, read the varargs as `intptr_t`, and call
  `currentVM->systemCall(args)` without the `(int*)` cast. At `:673-700` declare `intptr_t r;
  int args[12];`, read 12 `int` varargs, and call `vm->entryPoint(callnum, args[0], ...,
  args[11])`. Today the call passes 16 arguments to a 12-parameter function. At `:485` match
  the new `Sys_LoadDll` signature. Leave the interpreter pointer arithmetic at `:824` as it is.
- [x] **B1.4 Widen the interpreter's syscall arguments.** Done on 2 September 2026. At `code/qcommon/vm_interpreted.c:521`
  copy the VM's 32-bit argument block into `intptr_t args[16]` before calling
  `vm->systemCall(args)`. ioquake3 does this under a word-size check; do it unconditionally.
- [x] **B1.5 Change the three engine handlers.** Done on 2 September 2026. `code/server/sv_game.c:312-313` becomes
  `intptr_t SV_GameSystemCalls(intptr_t *args)` with the cast line removed. Same at
  `code/client/cl_cgame.c:417` and `code/client/cl_ui.c:772`. Change the `VMA(x)`, `VMI(x)`,
  and `VMF(x)` macros to take `intptr_t`. `cl_ui.c:1047` `return (intptr_t)strncpy(...)` is now
  correct. Audit every handler that returns a pointer and confirm it returns `intptr_t`.
- [x] **B1.6 Change the modules.** Done on 2 September 2026. `code/game/g_main.c:203`, `code/cgame/cg_main.c:46`,
  `code/q3_ui/ui_main.c:43`, and `code/ui/ui_main.c:168` become
  `Q_EXPORT intptr_t vmMain(int command, int arg0, ..., int arg11)`. In
  `code/game/g_syscalls.c:31-35`, `code/cgame/cg_syscalls.c:31-35`, and
  `code/ui/ui_syscalls.c:31-34` change the static pointer to
  `static intptr_t (QDECL *syscall)(intptr_t arg, ...)` and `dllEntry` to
  `Q_EXPORT void dllEntry(intptr_t (QDECL *syscallptr)(intptr_t arg, ...))`. Trap wrappers that
  return `int` are unchanged; values fit, and the one pointer return (`trap_strncpy`) is
  discarded by its callers.
- [x] **B1.7 Update `sys_dll.cpp` and the test stubs.** Done on 2 September 2026. Match the new `Sys_LoadDll` signature in
  `code/sys/sys_dll.cpp` (checklist 01 A4.3) and in `tests/test_platform_stubs.cpp`. Update the
  VM ABI paragraph in `docs/architecture.md:14-17`.
  - **Tests (for all of B1):** `tests/test_vm.cpp` (binary `quake3_tests`) plus
    `tests/vm_testmodule/tm_main.c`, built as `add_library(testmodule SHARED)` with
    `OUTPUT_NAME "testmodule${Q3_ARCH}"`, `SUFFIX ${Q3_DLL_EXT}`, `PREFIX ""`, into
    `${CMAKE_BINARY_DIR}/tests/baseq3`. The module exports `dllEntry` and `vmMain`; command `0`
    returns `arg0 + arg1`; command `1` calls `syscall(1, (intptr_t)ptr)` and returns the
    syscall's result. Cases: `VmAbi.HeapPointerSurvivesRoundTrip` sets `fs_basepath` to
    `<build>/tests` and `fs_game` to `baseq3`, calls `VM_Create("testmodule", TestSyscalls,
    VMI_NATIVE)`, allocates `void *p = malloc(1)` (heap addresses exceed 32 bits on macOS
    arm64), asserts `VM_Call(vm, 1, ...)` returns `p` intact, and `VM_Free`s;
    `VmAbi.AddCommandReturnsSum` checks command `0`; `VmAbi.MissingModuleReturnsNull` asserts
    `VM_Create` of a nonexistent module returns `NULL` without calling `Com_Error`. Checklist
    03 C6 owns the harness details.
  - **Verify:** `make build` with `-Werror=int-conversion` succeeds;
    `./build/quake3_modern +set sv_pure 0 +map q3dm17 +addbot sarge +quit` under `xvfb-run`
    runs bots without a crash; `ctest --preset dev -R VmAbi` passes in the container and on the
    macOS leg.

### Phase B2: prototype fallout

- [x] **B2.1 Fix every implicit declaration.** Done on 2 September 2026. With `-Werror=implicit-function-declaration`
  (checklist 01 A3.6) active, build and fix each error. Expected sites: add
  `#include "../sys/sys_api.h"` at `code/server/sv_init.c:362` (`Sys_ScriptEvent`) and
  `code/client/cl_main.c:1379` (`Sys_SanitizeDownloadFilename`); remove hand-written externs at
  `tests/test_cvar_cmd.cpp:7-8` now that `qcommon.h` declares them; add `FS_BuildOSPath` to
  `qcommon.h` if checklist 01 A2.3 has not; check `Sys_ShowIP` and `Sys_BeginProfiling` in
  `cl_main.c`. Fix the missing `return` at `code/game/q_shared.h:159` (`BigLong` under a Mac
  `#if`) with `-Werror=return-type`.
  - **Tests:** none, because the compiler is the test.
  - **Verify:** `make build 2>&1 | grep -E 'implicit|int-conversion|return-type' | wc -l`
    prints `0` on gcc and clang, and the macOS and Windows legs are green.

### Phase B3: crash handling and `Sys_Error`

- [ ] **B3.1 Install real signal handlers on Unix.** In `code/sys/sys_unix.cpp` implement
  `Sys_InitSignals` with `sigaction` and `SA_RESETHAND | SA_NODEFER | SA_SIGINFO` for `SIGSEGV`,
  `SIGBUS`, `SIGILL`, `SIGFPE`, `SIGABRT`, and `SIGTRAP`. The handler writes a fixed banner and
  the signal number with `write(2, ...)` (never `printf`), calls `backtrace(buf, 64)` and
  `backtrace_symbols_fd(buf, n, 2)` from `<execinfo.h>` (available on glibc and macOS), calls
  `Sys_ReleaseDisplay()` behind a `static volatile sig_atomic_t` re-entry guard, then
  `raise(sig)`. `SA_RESETHAND` restored the default action, so the process dumps core or hands
  off to the crash reporter. For `SIGINT`, `SIGTERM`, and `SIGHUP` the handler only sets
  `volatile sig_atomic_t sys_quitRequested = 1`. Add a check at the top of `Com_Frame` in
  `code/qcommon/common.c` that calls `Com_Quit_f()` when the flag is set. Delete
  `floating_point_exception_handler` (`code/unix/unix_main.c:457-460`) and its `signal(SIGFPE,
  ...)` call. Delete `code/unix/linux_signals.c` if checklist 01 A4.10 has not already.
- [ ] **B3.2 Install a Windows handler.** In `code/sys/sys_win32.cpp` `Sys_InitSignals` calls
  `SetUnhandledExceptionFilter` with a filter that calls `Sys_ReleaseDisplay()`, shows
  `MessageBoxA` with the exception code and address, writes an optional minidump through
  `MiniDumpWriteDump` when `Q3_MINIDUMP` is defined (CMake option, default `OFF`), and returns
  `EXCEPTION_EXECUTE_HANDLER`. `SetConsoleCtrlHandler` sets `sys_quitRequested`.
- [ ] **B3.3 Add `Sys_ReleaseDisplay`.** In `code/sys/sys_sdl.cpp` implement
  `void Sys_ReleaseDisplay(void)`: `SDL_SetRelativeMouseMode(SDL_FALSE)`,
  `SDL_SetWindowFullscreen(window, 0)`, `SDL_ShowCursor(SDL_ENABLE)`, and
  `SDL_SetWindowGrab(window, SDL_FALSE)`, each guarded by a null check on the window. The
  dedicated stub lives in `code/null/null_client.c` (checklist 01 A5.1).
- [ ] **B3.4 Rewrite `Sys_Error`.** In `code/sys/sys_main.cpp`: format with `vsnprintf` into a
  4096-byte buffer, call `Sys_ReleaseDisplay()`, call `CL_Shutdown()` only when
  `!Sys_IsDedicatedBuild()`, print to stderr, and when `SDL_WasInit(SDL_INIT_VIDEO)` is nonzero
  show `SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Quake III Arena", string, NULL)`. Then
  `Sys_Exit(1)`. Replace the `vsprintf` in `Sys_Warn` the same way.
- [ ] **B3.5 Make `Sys_Exit` clean.** `Sys_ConsoleInputShutdown(); SDL_Quit(); exit(ex);` in all
  build types. Delete the `assert(ex == 0)`.
  - **Tests (for all of B3):** none, because signal delivery and message boxes are not unit
    testable in the container. The gates are in Verify. Checklist 05 T5 adds thread names to
    the handler output later.
  - **Verify:** in the container under `xvfb-run`, `./build/quake3_modern +set r_fullscreen 1 +crash`
    prints a backtrace on stderr and exits by the default `SIGSEGV` action (exit code 139).
    `+error` shows a message box under Xvfb (check the SDL log line) and exits `1`. Start the
    client, then `kill -TERM <pid>`: it exits `0` and writes `q3config.cfg` in `fs_homepath`.
    `grep -rn 'signal(' code/sys` prints nothing.

### Phase B4: logger

- [ ] **B4.1 Rewrite `code/sys/logger/logger.hpp`.** Keep it header-only. Add
  `std::atomic<Level> min_level_` with `set_level()` and `level()`; `log()` returns before any
  formatting when `level < min_level_`. Guard output and the sink with a `std::mutex`. Strip
  `__FILE__` to its basename with a `constexpr std::string_view basename(std::string_view)`
  applied in the macro so the strip happens at compile time. Format into a `thread_local
  std::string` scratch instead of a fresh `ostringstream`. Record `main_thread_id_` in
  `Sys_SubsystemInit`; when `std::this_thread::get_id() != main_thread_id_` push the formatted
  line into a bounded `std::vector<std::string> queued_` (drop the oldest above 1024) instead
  of calling the sink, and add `flush_queued()` that `Sys_SubsystemFrame` calls. Checklist 05
  T1 replaces this queue with `MainThreadQueue`; keep the interface identical so the swap is
  one line. Macros: `LOG_WARN` and `LOG_ERROR` are always compiled; `LOG_DEBUG` and `LOG_INFO`
  compile to `((void)0)` only when `Q3_LOG_STRIP_VERBOSE` is defined, never on `NDEBUG`.
- [ ] **B4.2 Add the `com_logLevel` cvar.** Register it in `Sys_SubsystemInit`: `0` debug, `1`
  info, `2` warn, `3` error; default `"1"` in debug builds and `"2"` otherwise, `CVAR_ARCHIVE`.
  `Sys_SubsystemFrame` polls `->modified` and calls `set_level`. When `developer` is `1`, force
  debug.
  - **Tests:** rewrite `tests/test_modern_logger.cpp` (binary `q3sys_tests`). Cases:
    `Logger.CapturingSinkReceivesFormattedLine` installs a sink that appends to a string;
    `Logger.LevelFilterDropsInfoBelowWarning` sets `Warning` and asserts an `INFO` line is not
    delivered; `Logger.LineCarriesBasenameNotAbsolutePath` asserts the line contains
    `test_modern_logger.cpp:` and does not contain `/Users/` or `/src/`;
    `Logger.OffMainLogIsQueuedUntilFlush` logs from a `std::thread`, asserts the sink is empty,
    calls `flush_queued()`, asserts delivery; `Logger.ErrorCompiledUnderNDEBUG` is compiled
    with `-DNDEBUG` in a dedicated CI configuration (the `release` preset) and asserts an error
    line is delivered. Remove every assertion on `stdout` or `stderr` capture.
  - **Verify:** `ctest --preset dev -R Logger` and `ctest --preset release -R Logger` pass in the
    container. Start the client with `+set com_logLevel 0` and observe lines of the form
    `[DEBUG] [sys_sdl.cpp:99] ...` with no absolute path.

### Phase B5: `sys_api` hardening

- [ ] **B5.1 Own the script engine.** In `code/sys/sys_api.cpp` replace the raw pointer at `:9`
  with `static std::unique_ptr<q3::scripting::ScriptEngine> g_scriptEngine;` created inside
  `try { ... } catch (const std::exception &e) { Com_Printf("^1Scripting disabled: %s\n",
  e.what()); }` in `Sys_SubsystemInit`.
- [ ] **B5.2 Add `Sys_SubsystemShutdown`.** Declare it in `code/sys/sys_api.h`; it cancels the
  downloader, resets the script engine, and flushes the logger queue. Call it from
  `Com_Shutdown` in `code/qcommon/common.c`. Checklist 05 T5 extends the order.
- [ ] **B5.3 Add the exception boundary.** In `code/sys/sys_api.h` define:
  ```c
  #define Q3_NOEXCEPT_BOUNDARY(body) \
      try { body } catch (const std::exception &e) { Com_Printf("^1%s: %s\n", __func__, e.what()); } \
      catch (...) { Com_Printf("^1%s: unhandled C++ exception\n", __func__); }
  ```
  Wrap the body of every `extern "C"` function in `sys_api.cpp` that can allocate or call into
  C++ code. `Sys_ScriptExecute` returns `qboolean` (`qfalse` on error). Checklist 04 C-P2 step 0
  changes the catch to rethrow as `Com_Error(ERR_DROP)` once that becomes an exception.
- [ ] **B5.4 Remove the dead API.** Delete `Sys_VFS_ReadFile`, `Sys_VFS_WriteFile`, the
  `Modern_*` alias block in `sys_api.h`, and the `mount_search_path("baseq3")` call at
  `sys_api.cpp:31`. Step B6 removes the callers.
  - **Tests:** `tests/test_sys_api_boundary.cpp` (binary `quake3_tests`, because it links
    `Com_Printf`). Cases: `SysApiBoundary.ThrowingScriptDoesNotTerminate` calls
    `Sys_ScriptExecute` with invalid Lua and asserts the process is alive and the return is
    `qfalse`; `SysApiBoundary.ShutdownIsIdempotent` calls `Sys_SubsystemShutdown` twice;
    `SysApiBoundary.InitWithoutLuaStillReturns` is skipped unless a build flag disables Lua.
  - **Verify:** `ctest --preset dev -R SysApiBoundary` passes. `grep -rn 'Modern_\|Sys_VFS_'
    code tests` prints nothing.

### Phase B6: remove the VFS hooks

Rationale: `files.c` already is the virtual file system. The hook in `FS_ReadFile` bypasses
`fs_searchpaths`, pure-server checks, journaling, `fs_loadStack`, pak precedence, and
`fs_game`, and it resolves against the working directory before `fs_basepath` is known. Fixing
the ordering and adding containment would still leave two file systems with different
precedence rules. The C++ class stays as a helper for tests and future code.

- [ ] **B6.1 Delete the hooks.** Remove the two lines at `code/qcommon/files.c:1500-1502` and the
  `Sys_VFS_WriteFile` call at `:1643`. Remove the `#include "../sys/sys_api.h"` from `files.c` if
  nothing else uses it.
- [ ] **B6.2 Add containment to `vfs.cpp`.** At `code/sys/fs/vfs.cpp:108-124` reject absolute
  `relative_path` values and any `..` segment, and require
  `std::filesystem::weakly_canonical(full_path)` to start with
  `weakly_canonical(target_base)`. Make the constructor public so tests own an instance; keep
  `instance()` for compatibility.
  - **Tests:** `tests/test_files.cpp` (binary `quake3_tests`, harness from checklist 03 C2). Cases:
    `Files.ReadFreeLeavesLoadStackAtZero` pairs `FS_ReadFile` and `FS_FreeFile` three times and
    asserts `FS_LoadStack() == 0`; `Files.WriteFileLandsOnlyInHomePath` writes a file and
    asserts it exists under `fs_homepath/baseq3` and not under the working directory. In
    `tests/test_modern_fs.cpp` (binary `q3sys_tests`) construct a local `VirtualFileSystem` and
    add `Vfs.WriteRejectsParentTraversal` (`"../x"` returns `false` and creates nothing),
    `Vfs.WriteRejectsAbsolutePath`, and `Vfs.WriteInsideMountSucceeds`. Remove the
    `instance().unmount_all()` call at `tests/test_modern_fs.cpp:30`.
  - **Verify:** `ctest --preset dev -R 'Files|Vfs'` passes. Start the client from an empty working
    directory with `+set fs_basepath /paks` and confirm `path` in the console lists only
    `fs_basepath` and `fs_homepath` entries.

### Phase B7: frame pacing and monotonic clock

- [ ] **B7.1 Replace the spin loop.** At `code/qcommon/common.c:2700-2713` use:
  ```c
  do {
      com_frameTime = Com_EventLoop();
      if ( lastTime > com_frameTime ) {
          lastTime = com_frameTime;
      }
      msec = com_frameTime - lastTime;
      if ( msec < minMsec ) {
          int remaining = minMsec - msec;
          if ( com_busyWait->integer || remaining <= 2 ) {
              NET_Sleep( 0 );
          } else {
              NET_Sleep( remaining - 1 );
          }
      }
  } while ( msec < minMsec );
  ```
  Register `com_busyWait` (`"0"`, `CVAR_ARCHIVE`) next to `com_maxfps`.
- [ ] **B7.2 Make `NET_Sleep` work for the client.** In `code/sys/net/sys_net.cpp` remove the
  `!com_dedicated->integer` early return. When `ip_socket` is open, `select()` on it (plus
  `stdin` when dedicated and `stdin_active`) with the timeout; otherwise call `Sys_Sleep(msec)`.
  On Windows `select` works on sockets; skip `stdin` and use `Sys_Sleep` when no socket is
  open. `NET_Sleep(0)` on the client falls through to `Sys_Sleep(0)`, which yields.
  `SV_Frame`'s dedicated sleep at `code/server/sv_main.c:784` is unchanged and now uses the
  same path.
- [ ] **B7.3 Confirm the monotonic clock.** `Sys_Milliseconds` from checklist 01 A4.2 uses
  `SDL_GetPerformanceCounter`. Confirm no caller depends on wall-clock alignment (grep
  `Sys_Milliseconds` in `code/`); none should.
  - **Tests:** `tests/test_sys_time.cpp` (binary `quake3_tests`). Cases:
    `SysTime.MillisecondsNeverDecreases` calls `Sys_Milliseconds` 1000 times and asserts each
    value is greater than or equal to the previous; `SysTime.SleepAdvancesClock` records the
    time, calls `Sys_Sleep(5)`, and asserts at least 4 ms passed; `SysTime.NetSleepZeroReturns`
    asserts `NET_Sleep(0)` returns within 5 ms with no socket open.
  - **Verify:** in the container, start the client at the main menu with `com_maxfps 125` under
    `xvfb-run` and run `top -b -n 1 -p <pid>`; CPU is below 20% of one core (today about 100%).
    `q3ded` idle is below 2%. `timedemo 1; demo four` frames per second is unchanged with
    `com_busyWait 1`.

### Phase B8: first-run diagnostics

- [ ] **B8.1 Name the pak and the searched paths.** Replace the error at
  `code/qcommon/files.c:3273-3276` with:
  ```c
  if ( FS_ReadFile( "default.cfg", NULL ) <= 0 ) {
      Com_Error( ERR_FATAL,
          "Couldn't find pak0.pk3 (default.cfg missing).\n"
          "Searched:\n  fs_basepath: %s/%s\n  fs_homepath: %s/%s\n  fs_cdpath: %s/%s\n"
          "Copy pak0.pk3 to pak8.pk3 from your Quake III Arena install into one of these directories.",
          fs_basepath->string, BASEGAME, fs_homepath->string, BASEGAME, fs_cdpath->string, BASEGAME );
  }
  ```
  Apply the same text at `files.c:3322` (`FS_ConditionalRestart`). `Sys_Error` (B3.4) shows it
  in a message box on the client. Leave the search path print in `FS_Startup` at `files.c:2399`.
  - **Tests:** `tests/test_files.cpp` case `Files.MissingDefaultCfgNamesPak0AndPaths` runs
    `FS_InitFilesystem` against an empty `TempDir` with the throwing `Sys_Error` stub and
    asserts the message contains `pak0.pk3` and all three directory paths.
  - **Verify:** `make shell -c 'cd /tmp/empty && /src/build/quake3_modern'` under
    `xvfb-run` exits `1` with the message on stderr naming the three directories.

### Phase B9: CD key, authorize server, intro

- [ ] **B9.1 Remove the CD key gate.** Delete the block at `code/q3_ui/ui_menu.c:276-282`. Remove
  `code/q3_ui/ui_cdkey.c` from `UI_SOURCES` and every `UI_CDKeyMenu` and `UI_CDKeyMenu_Cache`
  reference (grep `ui_menu.c`, `ui_atoms.c`, `ui_main.c`, `ui_setup.c:127,231-239,296`, and
  `ui_local.h`). Remove the `UIMENU_NEED_CD` handling at `ui_atoms.c:805-823`.
- [ ] **B9.2 Stop touching the key file.** In `code/qcommon/common.c:2242-2330` make
  `Com_ReadCDKey` and `Com_AppendCDKey` fill blanks without disk access and `Com_WriteCDKey` a
  no-op. Remove the `Com_WriteCDKey` call in `Com_WriteConfiguration` at `:2552-2559`. Keep
  `cl_cdkey` storage because the `UI_GET_CDKEY` trap still reads it.
- [ ] **B9.3 Remove the authorize handshake.** In `code/client/cl_main.c` make
  `CL_RequestAuthorization` a no-op (no packet, no DNS lookup). In
  `code/server/sv_client.c:60-140` delete the authorize branch at `:92-133` so
  `SV_GetChallenge` always sends `challengeResponse` at once. Remove `SV_AuthorizeIpPacket` and
  its dispatch in `code/server/sv_main.c` `SV_ConnectionlessPacket`. Drop
  `AUTHORIZE_SERVER_NAME` and `PORT_AUTHORIZE` from `code/qcommon/qcommon.h:240`.
- [ ] **B9.4 Skip the intro.** Add `com_skipIntro` (`CVAR_ARCHIVE`, default `"1"`) and, when set,
  skip both `idlogo.RoQ` and the `nextmap "cinematic intro.RoQ"` at `common.c:2489-2492`.
  - **Tests:** `tests/test_server_challenge.cpp` (binary `quake3_tests`) if `SV_GetChallenge` can be
    called with a stubbed `svs.challenges` and a captured `NET_OutOfBandPrint`; case
    `ServerChallenge.AnswersImmediatelyWithoutAuthorize` asserts one `challengeResponse` is sent
    on the first call. If the server harness needs `SV_Init`, write `Tests: none, because the
    server state is not unit testable yet` and rely on Verify.
  - **Verify:** with a fresh `fs_homepath`, the client opens on the main menu with no CD key prompt
    and no `q3key` file is created. `q3ded +map q3dm1` in the container and a client connect
    over loopback show no `Resolving authorize.quake3arena.com` line, and the connect completes
    in under 100 ms of the challenge.

### Phase B10: master server cvars

- [ ] **B10.1 Pointer.** The dead master hostnames (`code/qcommon/qcommon.h:237`,
  `code/client/cl_main.c:2903-2909`) are fixed in checklist `08-renderer-ui.md` step U1.7
  together with the server browser. Do not duplicate the work here.

## Test map

| Test file | Binary | Cases | Added by |
|---|---|---|---|
| `tests/test_vm.cpp` with `tests/vm_testmodule/tm_main.c` | `quake3_tests` | `HeapPointerSurvivesRoundTrip`, `AddCommandReturnsSum`, `MissingModuleReturnsNull` | B1 (harness in checklist 03 C6) |
| `tests/test_modern_logger.cpp` (rewrite) | `q3sys_tests` | `CapturingSinkReceivesFormattedLine`, `LevelFilterDropsInfoBelowWarning`, `LineCarriesBasenameNotAbsolutePath`, `OffMainLogIsQueuedUntilFlush`, `ErrorCompiledUnderNDEBUG` | B4 |
| `tests/test_sys_api_boundary.cpp` | `quake3_tests` | `ThrowingScriptDoesNotTerminate`, `ShutdownIsIdempotent` | B5 |
| `tests/test_files.cpp` | `quake3_tests` | `ReadFreeLeavesLoadStackAtZero`, `WriteFileLandsOnlyInHomePath`, `MissingDefaultCfgNamesPak0AndPaths` | B6, B8 (harness in checklist 03 C2) |
| `tests/test_modern_fs.cpp` (rewrite) | `q3sys_tests` | `WriteRejectsParentTraversal`, `WriteRejectsAbsolutePath`, `WriteInsideMountSucceeds` | B6 |
| `tests/test_sys_time.cpp` | `quake3_tests` | `MillisecondsNeverDecreases`, `SleepAdvancesClock`, `NetSleepZeroReturns` | B7 |
| `tests/test_server_challenge.cpp` | `quake3_tests` | `AnswersImmediatelyWithoutAuthorize` (optional, see B9) | B9 |

## Out of scope

- The thread-safe main-thread queue and thread-affinity asserts. Checklist 05 T1 owns them and
  replaces the logger's interim queue.
- The HTTP downloader and its worker thread. Checklist 06 replaces it.
- `Com_Error` as a C++ exception. Checklist 04 C-P2 step 0 owns it.
- Master server cvars (checklist 08 U1.7).

## Follow-ons

- `com_maxfpsUnfocused` and `com_maxfpsMinimized` once checklist 08 R1.5 delivers focus events.
- A `--help` text that lists common `+set` cvars (checklist 01 A4.2 adds the flag; checklist 10
  documents the cvars).

## Done criteria

- `make build` is clean with `-Werror=implicit-function-declaration
  -Werror=int-conversion -Werror=return-type` on gcc and clang; the macOS and Windows legs are
  green.
- `crash` in fullscreen releases the display and prints a backtrace; `error` shows a message
  box; `kill -TERM` writes `q3config.cfg`.
- Idle main menu CPU is below 20% of one core in the container and `q3ded` idle is below 2%.
- A start from an empty directory names `pak0.pk3` and the three searched paths.
- A fresh profile reaches the main menu with no intro and no CD key prompt; a loopback connect
  completes without an authorize lookup.
- `path` shows no working-directory entries; `FS_LoadStack()` returns to `0` after paired
  reads.
- Every row of the test map exists and passes under `ctest --preset dev` and
  `ctest --preset asan`; the logger test also passes under `ctest --preset release`.

## Last step

- [ ] Delete this file and remove its row from `docs/plans/README.md`.
