# Improvement plan checklists

This directory holds the work plan for the modernised Quake III Arena fork as a set of
checklists. Each checklist covers one track of work. Any agent or person can open a checklist
in any later session and continue the work without access to the conversation that produced it.

The plan came from a read-only audit of the repository at commit `ad3705e` on 1 September 2026.
The audit found that the modernisation layer (about 45 commits between 30 August and
1 September 2026) does not build on the owner's machine, that several documented features are
stubs, and that the FastDL rewrite prevents clients from joining servers that need a download.
The **Audit summary** section at the end of this file records the findings with `file:line`
anchors so the reason for each step survives.

## How to work a checklist

1. Open the checklist file. Read **Purpose**, **Prerequisites**, and **Background** before you
   change any code.
2. Check that every prerequisite checklist is at the required state. The **Dependency order**
   table in this file shows the order.
3. Pick the first unchecked step. Re-verify each `file:line` anchor before you edit, because the
   code might have moved since the audit.
4. Do the step as one commit or one pull request. Keep the change surgical: every changed line
   must trace to the step.
5. Write the tests that the step's **Tests** line names. Run them in the container. Run the
   check that the **Verify** line names.
6. Tick the step (`- [x]`) only when both the tests and the verification pass. Update the
   **Status** line at the top of the checklist. Commit the checklist change in the same commit
   as the code change.
7. When every step is ticked and the **Done criteria** hold, do the last step: delete the
   checklist file and remove its row from the table in this file.

If a step turns out to be wrong or impossible, do not skip it silently. Edit the step to record
what you found, and add a replacement step. The checklist is the record of decisions.

## Shared conventions

- **Surgical commits.** Touch only what the step needs. Do not reformat or refactor adjacent
  code. Match the existing style of the file you edit: the legacy C tree uses tabs and id
  Software style, `code/sys/` uses four spaces and `snake_case` in namespace `q3::`.
- **Rename-then-fix pull requests.** In the C++ migration (checklist `04-cxx-migration.md`),
  every directory lands as one pull request with two commits: commit A renames files and updates
  CMake paths only, commit B contains the code fixes. Never squash A into B.
- **Documentation style.** Write every document, comment, commit message, and pull request
  description in Simplified Technical English (ASD-STE100) and follow the Google developer
  documentation style guide. Use the present tense and the second person. Use sentence-case
  headings. Do not write "simply", "just", "easy", or "obvious". Write "might" for possibility
  and keep "may" for permission. Spell out an abbreviation the first time you use it. Use the
  serial comma. Do not use exclamation marks. Code comments explain why, not what.
- **Commit messages.** When an agent commits, the message ends with the line
  `Co-Authored-By: Claude <noreply@anthropic.com>`.
- **Owner decisions are final.** The **Owner decisions** section records every decision the
  owner took during planning. Do not re-ask them. When a step needs a decision that is not
  recorded, the step names the default that the plan proceeds on.

## Logging

`docs/logging.md` is the policy. In short: `LOG_WARN` and `LOG_ERROR` are compiled in every build
type, `LOG_DEBUG` and `LOG_INFO` are compiled out of `Release` only, and `com_logLevel` filters at
run time. `INFO` is one line per lifecycle event; anything that can repeat is `DEBUG`. Nothing goes
in per-frame or per-field code. The logger is a C++ header, so instrument each directory as
checklist 04 converts it to C++ rather than adding a second idiom for the C files.

## Testing conventions

Every checklist step that changes behaviour names its unit tests on a **Tests** line and its
manual or integration check on a **Verify** line. A step is not done until its tests exist and
pass in the container. These rules apply to every checklist.

- **Two test binaries.** `q3sys_tests` holds pure C++ tests and links only the `q3sys`
  objects, LuaJIT, and SDL2. `quake3_tests` holds engine tests and links the `qcommon`,
  `botlib`, `q3server`, and `q3sys` objects, the real platform files (`code/sys/sys_dll.c`,
  `code/sys/sys_files_*.c`, `code/sys/net/sys_net.c`), the null client stubs, and the
  reduced stubs in `tests/test_platform_stubs.cpp`. Checklist `03-tests.md` creates this split.
- **Discovery and timeouts.** There is one binary today, `quake3_tests`; checklist 03 step C1
  splits it. `gtest_discover_tests` registers each case separately, so each runs in its own
  process and there is no intra-binary order to shuffle: continuous integration uses
  `ctest --schedule-random`, which is the property that matters. No per-test timeout is set in
  CMake yet, so only the CI `--timeout 120` bounds a hung test; add `PROPERTIES TIMEOUT` in C1.
- **Locations.** One test file per subsystem, named `tests/test_<subsystem>.cpp`. Shared
  helpers live in `tests/*.hpp`; today that is `engine_init.hpp` alone. The fuller set
  (`engine_fixture.hpp`, `zip_writer.hpp`, `fake_ipc_server.hpp`, `test_http_server.hpp`) and
  `tests/fixtures/` arrive with checklists 03 and 06, so do not cite them as existing.
- **Naming.** Test case names read `Subsystem.BehaviourUnderCondition`, for example
  `Files.LoadStackReturnsToZeroAfterPairedReadAndFree`.
- **Build with both compilers.** GCC and Clang disagree about what is an error, not just a
  warning: Clang rejects the `register` storage class in C++17 and treats discarded `const`
  qualifiers as an error under the flags this build sets. A GCC-clean change can still fail the
  macOS leg. Before ticking any step that renames or const-corrects code, configure a second
  build directory with `-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++` and build it
  with `-- -k 0`.
- **Regression rule.** Every bug fix adds a regression case that fails before the fix and
  passes after it. Name the case after the behaviour, not the bug number.
- **Integration checks live in `ci/`.** Screenshot gates, timedemos, bot matches, sanitizer
  runs, and pixel comparisons are shell scripts under `ci/`, not GoogleTest cases. Checklists
  reference them by the gate numbers below.
- **Negative cases.** Every security or validation rule has at least one test that proves the
  rule rejects bad input.

### Gate G1: golden screenshot

G1 proves that a change does not alter rendered output. `ci/smoke/run_smoke.sh` runs the client
headless in the container under Mesa `llvmpipe` with a fixed configuration (640x480, `r_picmip 1`,
`r_texturebits 32`, `r_ext_compressed_textures 0`, sound off, native modules), plays
`timedemo 1; demo four`, takes a screenshot at a fixed frame, and compares it with the committed
image in `ci/smoke/golden/` using ImageMagick `compare -metric AE`. The gate passes when the
number of differing pixels is 0. The golden images are produced once from the all-C tree, on the Linux x86_64 machine, after
the C++ preparation pull request (checklist `04-cxx-migration.md`, step P0) and committed. A
change that alters output by design (for example the JPEG library swap) regenerates the golden
image in its own commit and says so.

### Gate G2: perceptual tolerance

G2 replaces G1 for a change that alters pixels by design but must stay visually identical. The
same smoke run compares with `compare -metric PSNR`. The gate passes when the peak signal-to-noise
ratio is at least 45 dB. The JPEG swap also runs a decoder parity test that decodes every JPEG in
`pak0.pk3` with both decoders and asserts a maximum per-channel delta of 2.

## Checklist files

| File | Covers | Status |
|---|---|---|
| `00-environment.md` | Docker image, the Makefile targets, gate G1 harness, continuous integration skeleton | In progress (Linux image, compose, Makefile, smoke scripts, pixel gate, CI skeleton, building doc, native targets done; MinGW image verification and golden image open) |
| `01-build-portability.md` | Stray files, platform macros, CMake object libraries, platform layer under `code/sys/`, `DEDICATED` at runtime, LuaJIT, CI legs, `docs/building.md` | In progress (Phases A1-A8 complete: platform layer, CMake restructure, CI legs, build docs; MinGW cross-check open) |
| `02-stability.md` | 64-bit VM ABI, prototypes, crash handling, logger, `sys_api` hardening, VFS hook removal, frame pacing, first-run diagnostics, CD key and authorize removal | Complete (Phases B1-B10 complete) |
| `03-tests.md` | Test binary split, fixtures, `files.c`, netchan, collision, sound, VM bridge, replacement of vacuous tests, sanitizer CI | Not started; `engine_init.hpp` and the VM test already seeded |
| `04-cxx-migration.md` | Compile every directory as C++17, JPEG library swap, header close-out, idiomatic rewrites with `Com_Error` as an exception first | In progress (Phase P0 complete, P1.1-P1.6 complete) |
| `05-threading.md` | Main-thread ownership, main-thread queue, job system, render backend thread, image precache, sound handoff, shutdown, ThreadSanitizer CI | In progress (Phase T1 complete) |
| `06-networking.md` | UDP download restore, `sv_dlURL`, download policy, libcurl downloader, allowlist, client state machine, Discord IPC, bitstream facade, netchan loopback test, session slots | In progress (N1.1 UDP download restored) |
| `07-scripting.md` | Lua sandbox, `q3` API, events and game syscall, script loading, console commands | Not started |
| `08-renderer-ui.md` | Extension detection, gamma, VSync, mode table, resize and high-DPI, MSAA and anisotropy, VBO ring, GLSL programs, immediate-mode removal, core profile, FBO post-pass, HUD, pillarbox, console scale, video menu, mouse and controller, master servers, Hor+ FOV | Not started |
| `09-vulkan.md` | Backend vtable seams, milestones M0 to M4, MoltenVK, honest cost | Not started |
| `10-docs-hygiene.md` | Root README, third-party licences, docs rewrite, cvar reference, changelog, doc comments, formatting configuration, debug prints, stray files | Not started |

## Current state, 3 September 2026

The tree builds and links on Linux and macOS, and **all 77 tests pass** locally, under
AddressSanitizer too. The Linux continuous integration leg is green.

Fixed today beyond the checklist steps, because the platform legs exposed them:

- `Q_rsqrt` read eight bytes from a four-byte float and returned a sign-flipped result on
  x86_64, which is wrong arithmetic in `VectorNormalizeFast` and the renderer.
- The module entry point was called through a variadic pointer, so every module call on Apple
  arm64 received garbage arguments.
- `Snd_Memset` was defined off Linux, where it is a macro for `Com_Memset`, giving macOS a
  duplicate symbol.
- `ctime` received an `unsigned long` where Windows wants a 64-bit `time_t`.
- `sol::nil` does not exist on macOS, where `nil` is an Objective-C macro.
- Gate G1 could pass on a crash, and would silently test the bytecode in `pak0.pk3` when the
  built modules were absent.
- The download filename check was a blacklist with four live bypasses and no test.
- LuaJIT could not be resolved on Windows, and the fallback that was meant to cover it was
  itself broken.

All are recorded in `00-environment.md`, and the two C++ migration classes are catalogue rows 23
and 24 in `04-cxx-migration.md`.

Continuous integration state: **all four platform legs green**, confirmed on run for commit
`015c3d5`. Linux, Linux with sanitizers, and macOS pass 77 of 77; Windows passes 76 of 76, the
difference being one home-path test that is compiled off Windows because `SDL_GetPrefPath` reads
no variable a test can redirect. The MinGW cross leg runs nightly.

One blocker remains: gate G1 has no golden image, because no machine used so far has game data.
Produce it with `make smoke-update-golden` on the Linux machine before the next rename.

## Dependency order

Work the checklists in this order. Rows that share a number can run in parallel. A row can
start when the rows it depends on are at the state the **Depends on** column names.

| Order | Checklist and steps | Depends on |
|---|---|---|
| 0 | `00-environment.md` all steps | Nothing |
| 1 | `10-docs-hygiene.md` step 9 (stray files); `01-build-portability.md` steps 2 to 6 in one pull request | 00 complete |
| 2 | `06-networking.md` step N1.1 (restore UDP download) | 01 builds |
| 3 | `02-stability.md` steps 1 and 2 (VM ABI in C, prototypes); `01-build-portability.md` step 7 (CI legs) | 01 steps 2 to 6 |
| 4 | `04-cxx-migration.md` phase P0 (preparation pull request, golden images) | 02 steps 1 and 2 |
| 5 | `04-cxx-migration.md` phase P1 pull requests 1 to 4 (qcommon, server, renderer, client) | 04 P0 |
| 6 | `05-threading.md` phase T1; `02-stability.md` steps 3 to 9 | 04 P1 pull request 4 |
| 7 | `04-cxx-migration.md` phase P1 pull requests 5 to 8 (botlib, game, cgame, q3_ui) | 06 row |
| 8 | `03-tests.md` all steps; `10-docs-hygiene.md` steps 1, 2, 5, 6, 7, 8; `05-threading.md` phases T2a and T4 | 05 T1 |
| 9 | `08-renderer-ui.md` phase R1 with U1 steps 1, 2, 3, 7, 8 in parallel, then U1 steps 4, 5, 6 | 04 P1 pull request 3 |
| 10 | `05-threading.md` phases T3, T2b, T5 | 08 R1 |
| 11 | `06-networking.md` N1.2 to N3; `07-scripting.md` all steps | 05 T1, libcurl approval, LuaJIT `REQUIRED` |
| 12 | `04-cxx-migration.md` P1 pull requests 9 and 10 (JPEG swap, header close-out); P2 step 0 (`Com_Error` as exception) | 01 complete (`code/unix` gone) |
| 13 | `08-renderer-ui.md` phase R2 | 05 T3 |
| 14 | `04-cxx-migration.md` P2 steps 1 to 5 | 04 P2 step 0 |
| 15 | `09-vulkan.md` | 08 R2 step 4 (core profile default) |
| Continuous | `10-docs-hygiene.md` steps 3 and 4 (per-subsystem docs, cvar reference, changelog) | Land with each phase |

Rough effort: environment 3 days; build portability plus stability 3 weeks; tests 1 week; C++
preparation 4 days and compile-as-C++ 5 weeks (3 with two people); threading T1 4 days, T2a
2 weeks, T3 4 weeks, T2b 1 week, T4 3 days, T5 4 days, T6 3 days; networking N1 1 week, N2
3 days, N3 3 days; scripting 1 week; docs 3 days plus upkeep; renderer R1 plus UI 3 weeks; R2
4 to 6 weeks; idiomatic C++ 14 to 17 weeks interleaved; Vulkan 3 to 5 months.

## Owner decisions

The owner took these decisions during planning. The checklists rely on them. Do not re-ask.

1. **Platforms:** Linux x86_64, macOS arm64, and Windows x64. Windows uses MSVC plus vcpkg in
   CI; clang-cl works with the same preset.
2. **Stub features:** implement for real, do not remove. This covers VBO/VAO, GLSL, Vulkan,
   Discord Rich Presence, bitstream/transport, the split-screen `SessionManager`, and Lua hooks.
3. **Legacy tree:** keep the tool trees (`lcc/`, `q3map/`, `q3radiant/`, `code/bspc/`, `libs/`,
   `common/`). Remove only stray build files and tracked binaries.
4. **Dev machine:** install nothing. Linux builds and tests run in Docker; macOS and Windows
   are CI legs.
5. **Dependencies approved:** libcurl for FastDL (Linux package, macOS system or brew, Windows
   vcpkg); LuaJIT becomes `REQUIRED` in CMake with a `luajit-cmake` FetchContent fallback
   (LuaJIT 2.1 for arm64). Not added: nlohmann/json, the Discord GameSDK, mbedTLS.
6. **Home paths:** ioquake3-compatible (`~/.q3a`, `~/Library/Application Support/Quake3`,
   `%APPDATA%\Quake3`).
7. **VFS hooks** in `FS_ReadFile` and `FS_WriteFile`: remove them. `VirtualFileSystem` stays as a
   C++ helper only.
8. **First run:** `com_skipIntro` default `1`; the CD-key gate and the authorize server
   handshake are removed. Windows crash handling shows a message box only, no minidumps unless
   asked.
9. **Graphics:** two-stage GL profile (compatibility 2.1 for the A/B period, then 3.3 core as
   the default and minimum); Vulkan as an in-tree backend behind an `rb_backend_t` vtable
   selected by `cl_renderer`; MoltenVK on macOS. Video defaults: `r_mode -2` (desktop native),
   `r_fullscreen 1` (borderless), `r_swapInterval 1`, `r_allowHighDPI 1`. HUD and field of view:
   `cg_wideScreenHUD 1` and `cg_horplus 1` with opt-out. sRGB stays off.
10. **Master servers:** `sv_master1 master.ioquake3.org`, `sv_master2 master.maverickservers.com`.
11. **Discord:** the owner creates a Discord application and sets `cl_discordClientId`. The
    feature is off by default (`cl_discordRichPresence 0`).
12. **Split-screen rendering:** deferred. Register controller slots now and document rendering
    as planned.
13. **Scripting:** server-side sandboxed Lua with a `q3.*` API. Drop the legacy domain-specific
    language and the entity-property store. Keep `eval`. `sv_scriptEnable` default `1`.
    `cl_cURL_URL` is replaced by `cl_dlFallbackURL`, default empty.
14. **C++ migration:** `stb_image` and `stb_image_write` for JPEG (gate G2); add
    `-fno-strict-aliasing` now and remove it per subsystem later; sweep string literals to
    `const char*`; `qboolean` becomes `typedef int qboolean; enum { qfalse, qtrue };`; exceptions
    and run-time type information stay on everywhere; `Com_Error` becomes a C++ exception as the
    first idiomatic step; `unzip.c` and `md4.c` stay C; renames use `git mv` per directory.
15. **Threading:** `com_jobThreads 0` means auto (`cores - 2` clamped to 1 to 8 on the client,
    `cores - 1` clamped to 1 to 4 on `q3ded`); the main-thread queue has a reliable lane and a
    lossy lane and never blocks the poster; `r_smp` lands with default `0` and flips to `1` on
    two or more cores in a separate commit once the gates pass; the render thread lands before
    the VBO/GLSL rewrite.

16. **macOS leg:** primary check is the `macos-15` CI runner. Local option is the native build
    with `Q3_FETCH_DEPS=ON` (`make native-test`), which installs nothing. No macOS virtual
    machine.
17. **Windows leg:** local check is the MinGW-w64 cross build in the `win` container with tests
    under Wine (`make win-test`). MSVC with vcpkg stays a CI leg. No clang-cl plus xwin.
18. **Primary machine:** Linux (assumed x86_64). Gate G1 golden images are produced there;
    llvmpipe output on the arm64 laptop is advisory until it is shown identical.

## Audit summary

Three read-only audits ran on 1 September 2026 over build, stability, and tests; renderer, UI,
and UX; and documentation and subsystems. No build directory and no CI existed, so nothing in
the tree was build-verified. The project did not compile on the owner's macOS machine.

### Headline findings

| Area | Finding | Anchor |
|---|---|---|
| Build | GNU-only `-Wl,--start-group`; Apple ld rejects it | `CMakeLists.txt:207,325`, `tests/CMakeLists.txt:36-50` |
| Build | `unix_glw.h` has a hard `#error` off Linux; core GL comes from `dlopen("libGL.so.1")`, extensions from SDL | `code/unix/unix_glw.h:22`, `code/sys/sys_sdl.cpp:190` |
| Build | `q_shared.h` has no `__APPLE__` branch; `common.c` includes `<winsock.h>` off Linux | `code/game/q_shared.h:133-310`, `code/qcommon/common.c:28-36` |
| Build | Modules hardcoded to `qagamex86_64` and `.so`; no arm64, no `.dylib` or `.dll` | `CMakeLists.txt:349,383,437`, `code/unix/unix_main.c:725-739` |
| Build | `DEDICATED` leaks into the client through `PUBLIC` on `q3server`; `q3ded` defaults `dedicated` to 0 | `CMakeLists.txt:199`, `code/qcommon/common.c:2409` |
| Build | LuaJIT silently optional in CMake but required by `sol.hpp`; missing on the dev machine | `CMakeLists.txt:35-38,180-182` |
| Build | No `CMAKE_BUILD_TYPE` default; `NDEBUG` never set; GoogleTest fetch has no hash; `install(baseq3/)` targets a missing directory | `CMakeLists.txt:445-473` |
| Stability | The VFS front-runs `FS_ReadFile`: bypasses pure-server checks, breaks `fs_loadStack` so temp hunk memory is never freed again, mounts cwd-relative `baseq3` before `fs_basepath` is parsed; `FS_WriteFile` writes twice | `code/qcommon/files.c:1500-1502,1643`, `code/sys/sys_api.cpp:31` |
| Stability | 64-bit VM ABI half-fixed: `VM_Call` arguments and syscall returns are still `int` | `code/qcommon/vm.c:333,673,694`, `code/client/cl_ui.c:1047` |
| Stability | The client has no working crash handler; the handler is not async-signal-safe; no backtrace; `Sys_Error` leaves fullscreen and mouse grab | `code/unix/linux_signals.c:34-60`, `code/unix/unix_main.c:393-416` |
| Stability | The main loop busy-waits; no sleep anywhere; `gettimeofday` timer | `code/qcommon/common.c:2708-2713`, `code/unix/unix_shared.c:62-77` |
| Stability | Logger: no mutex, every `LOG_*` including errors vanishes under `NDEBUG`, absolute `__FILE__` paths, console sink called from a worker thread | `code/sys/logger/logger.hpp:35-72` |
| Network | The FastDL rewrite dropped the legacy `download` command: a client that needs a file stalls at connect | `code/client/cl_main.c:1378-1426` |
| Network | HTTP downloader: no TLS, blocking sockets, no status or redirect handling, `stoi` and `stoull` on server input inside a thread (remote `std::terminate`), cwd-relative final filename, `Cvar_SetValue` from the worker | `code/sys/net/http_downloader.cpp`, `code/sys/sys_api.cpp:119-129` |
| Network | `sv_dlURL` is server-controlled with no host or scheme check (server-side request forgery); the `ws.q3df.org` fallback has no opt-in; the sanitizer is a case-sensitive blacklist | `code/client/cl_main.c:1403-1417`, `code/sys/sys_api.cpp:82-117` |
| Network | Master and authorize servers are dead hostnames with no cvar override | `code/qcommon/qcommon.h:237,240`, `code/client/cl_main.c:2903-2909` |
| Graphics | No VBO/VAO or GLSL code path exists; pointers are resolved and never called | `code/renderer/tr_shade.c:170,261`, `code/sys/sys_sdl.cpp:198-221` |
| Graphics | Extension detection was deleted with `linux_glimp.c`: `glConfig` strings empty, `isFullscreen` never set so overbright is dead, gamma hardcoded | `code/sys/sys_sdl.cpp:187`, `code/renderer/tr_image.c:2137-2141` |
| Graphics | VSync default defeated by cvar registration order; two divergent mode tables; first run is 640x480; no resize or high-DPI handling | `code/renderer/tr_init.c:933`, `code/sys/sys_sdl.cpp:111-137,405-479` |
| Graphics | The Vulkan "backend" is 78 lines with no Vulkan header | `code/renderer/vulkan/vk_backend.cpp` |
| UI | HUD and scoreboard are stretched on widescreen; `cgs.screenXBias` is never assigned; Team Arena UI bias is commented out | `code/cgame/cg_drawtools.c:33-45`, `code/ui/ui_shared.c:3554-3560` |
| UI | The pillarbox is never cleared in menus (stale frames at the edges) | `code/client/cl_scrn.c:440-442` |
| UI | Mouse grabbed permanently, wheel events not handled, no focus handling; controller has five hardcoded buttons and no axes | `code/sys/sys_sdl.cpp:222,369,421-473` |
| UI | Console fixed at 78 columns and raw 8x16 pixels; video menu lists 1999 modes and dead cvars | `code/client/cl_console.c:253`, `code/q3_ui/ui_video.c:762-777` |
| UX | CD-key gate live in a GPL build; intro cinematic on first run; missing-pak error names only `default.cfg`; no `--help` | `code/q3_ui/ui_menu.c:276-282`, `code/qcommon/files.c:3250-3280` |
| Docs | `README.txt` is the 2005 id file; no build docs; `docs/*.md` describe VBO, GLSL, Vulkan, HTTPS, Discord, and a Lua mod API that do not exist; Sol2 missing from the licence inventory | `docs/*.md`, `README.txt` |
| Tests | 49 tests; none for `files.c`, `net_chan.c`, `cm_*`, `snd_*`, the VM bridge, or FastDL. Vulkan, Discord, and VM-syscall tests are tautologies. The logger test fails under `NDEBUG`. No sanitizers | `tests/` |
| Hygiene | 65 stray `.sln`, `.vcproj`, `.bat`, `.lnt`, `Conscript`, and `.mak` files, three tracked `.exe` files, a tracked screenshot, orphaned `code/macosx/`; debug prints left in UI and server code | `lcc/bin/*.exe`, `code/q3_ui/ui_gameinfo.c:132`, `code/server/sv_ccmds.c:151` |

### Subsystem reality check

| Documented | Reality | Wired into the engine |
|---|---|---|
| SDL2 platform layer | Real | Yes |
| 64-bit VM ABI (`intptr_t`) | Half done | Yes |
| VFS | `std::filesystem` resolver that shadows `files.c` | Yes, harmful |
| `CvarManager` | Thin wrapper; `find()` creates cvars as a side effect | Only `notify_change` |
| Logger | Real, unsafe | Yes |
| FastDL and HTTP | Broken end to end | Yes |
| `ScriptEngine` (Lua) | Runs Lua; hooks, sandbox, script loading, and the documented API are absent | One `game_init` dispatch, no subscriber |
| Discord RPC | Logging stub | Tests only |
| Bitstream and transport | Wire-incompatible reimplementation of `msg.c` | Tests only |
| `SessionManager` | Slot array with viewport maths | One `reset()` call |
| VBO/VAO, GLSL | Do not exist | No |
| Vulkan | Stub | Tests only |

## Lifecycle of this directory

Each checklist deletes itself as its last step and removes its row from the **Checklist files**
table in this file. When the table is empty, delete this README and the `docs/plans/` directory.
The history of the work stays in git and in `CHANGELOG.md`.
