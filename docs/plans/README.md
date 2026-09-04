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
- **Commit messages.** One line. Say what the commit does, in the imperative. No body, and no
  attribution trailer of any kind. The reasoning belongs on the checklist step, which is the
  record that survives; a commit body duplicates it and then drifts from it.
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
| `00-environment.md` | Docker image, the Makefile targets, gate G1 harness, continuous integration skeleton | In progress (Linux and MinGW images, compose, Makefile, smoke scripts, pixel gate, CI workflow, building doc, native targets done; open: adapt the smoke harness to the OpenArena data set, make gate G1 a dispatch-only job with Workload Identity Federation, the golden image, the ThreadSanitizer leg, the sanitizer option conflict) |
| `01-build-portability.md` | Stray files, platform macros, CMake object libraries, platform layer under `code/sys/`, `DEDICATED` at runtime, LuaJIT, CI legs, `docs/building.md` | Complete (A1-A8, and A6.3 verified on run 33 where the MinGW cross leg is green) |
| `02-stability.md` | 64-bit VM ABI, prototypes, crash handling, logger, `sys_api` hardening, VFS hook removal, frame pacing, first-run diagnostics, CD key and authorize removal | Complete (B1-B10; B5.2 was found half done on 4 September 2026, because nothing called `Sys_SubsystemShutdown`, and finished the same day) |
| `03-tests.md` | Test binary split, fixtures, `files.c`, netchan, collision, sound, VM bridge, marking of vacuous tests, sanitizer CI | In progress (C1-C8 landed, 127 cases green on all five legs; C6.2, C7.5, and C8.2 unticked on 4 September 2026; C7.6 added and done) |
| `04-cxx-migration.md` | Compile every directory as C++17, JPEG library swap, header close-out, idiomatic rewrites with `Com_Error` as an exception first | In progress (P1.1-P1.8 complete, and phase P1.W closed the Windows fallout on run 33, so all five legs are green; none of P1.1-P1.8 was checked against gate G1, because no golden exists; P0.7 rewritten; P1.W.5 `/permissive-` open) |
| `05-threading.md` | Main-thread ownership, main-thread queue, job system, render backend thread, image precache, sound handoff, shutdown, ThreadSanitizer CI | In progress (T1, T2a.1, T2a.2 complete, with deviations recorded on T1.3, T1.6, and T2a.1; no ThreadSanitizer leg yet, so every TSan verify line is unverified) |
| `06-networking.md` | UDP download restore, `sv_dlURL`, download policy, libcurl downloader, allowlist, client state machine, Discord IPC, bitstream facade, netchan loopback test, session slots | In progress (N1.1 UDP download restored) |
| `07-scripting.md` | Lua sandbox, `q3` API, events and game syscall, script loading, console commands | Not started, and **an unsandboxed interpreter ships today**: `code/sys/scripting/script_engine.cpp:15` opens `sol::lib::package`, so a script gets `require` and `package.loadlib`. There is no `q3` table at all; the registered surface is `print`, `add`, `multiply` |
| `08-renderer-ui.md` | Extension detection, gamma, VSync, mode table, resize and high-DPI, MSAA and anisotropy, VBO ring, GLSL programs, immediate-mode removal, core profile, FBO post-pass, HUD, pillarbox, console scale, video menu, mouse and controller, master servers, Hor+ FOV | Not started, but scaffolding has landed with no functionality: the VBO, VAO, and GLSL entry points are declared (`code/renderer/qgl.h:164-221`) and resolved (`code/sys/sys_sdl.cpp:200-221`) and **never called**, no `r_vbo` or `r_glsl` cvar exists, `glConfig.extensions_string` is **never assigned** though three sites print it, and `glConfig.deviceSupportsGamma` is hardcoded `qtrue` (`code/sys/sys_sdl.cpp:189`) |
| `09-vulkan.md` | Backend vtable seams, milestones M0 to M4, MoltenVK, honest cost | Not started. The stub **reports false success**: `code/renderer/vulkan/vk_backend.cpp` is 38 lines with no Vulkan header, `init()` fills a struct with fabricated values, returns `true`, and logs "Successfully initialized Vulkan 1.3 context". `tests/test_vulkan_backend.cpp:14` asserts those values, so it passes with no Vulkan present. Step V0.1 deletes both |
| `10-docs-hygiene.md` | Root README, third-party licences, docs rewrite, cvar reference, changelog, doc comments, formatting configuration, debug prints, stray files | In progress (step 9 done and verified: zero tracked `.sln`, `.vcproj`, `.bat`, `.lnt`, `Conscript`, `.mak`, or `.exe` files, and `code/unix`, `code/macosx`, and `code/win32` are gone. The root document is still id's 2005 `README.txt`) |

## Current state, 4 September 2026

**All five continuous integration legs are green**, on run 33 (commit `1304fd7a`), which is the
first time the MinGW cross leg has ever passed:

| Leg | Result |
|---|---|
| Linux (Ubuntu 24.04) | 127 of 127 |
| Linux ASan/UBSan | 127 of 127 |
| macOS arm64 (macOS 15) | 127 of 127 |
| Windows x64 (MSVC, vcpkg) | 126 of 126 |
| Windows Cross MinGW, under Wine | 126 of 126 |

The two Windows legs run one case fewer, because the home-path test is compiled off Windows
where `SDL_GetPrefPath` reads no variable a test can redirect.

`gh` resolves to the upstream `id-Software` repository from a clone of this fork, so
`gh run list` silently returns nothing. Pass the fork explicitly:
`gh run list -R ishmaelaqsar/Quake-III-Arena`.

### How it got there, 4 September 2026

The tip did not build on Windows. Seventeen commits had landed between `015c3d5` and `a2a5bba`
in one push (server, renderer, client, botlib, game, cgame, and ui compiled as C++17; checklist
03; threading T1, T2a.1, T2a.2), one continuous integration run covered all of them, and it was
red on MSVC while the nightly MinGW leg was red too. So every one of those steps had been ticked
without a green Windows leg, which the **Build with both compilers** convention exists to
prevent.

MSVC stops at the first target that fails, so the four classes visible at the time were hiding
others. Three iterations, each exposing what the previous one concealed:

| Run | Result |
|---|---|
| 29 | The first four classes fixed. MinGW built for the first time. MSVC reached the link and produced three more failures, all catalogue row 25 (`botimport`, `botlib_export`, `glConfig`), plus two lambda-capture errors in the tests. MinGW then failed at test discovery. |
| 31 | MinGW green. MSVC built and failed one test that escaped its own `catch` clause, because of the `/EHsc` the previous iteration had added. |
| 33 | All five legs green. |

Seven classes in total, now catalogue rows 26 to 28 in `04-cxx-migration.md` plus three more
sites of row 25. Three of them are worth remembering beyond Windows:

- **`abs()` on a float** truncates its argument before taking the magnitude, so
  `abs(DotProduct(a, b)) < 0.1` has been true for every input but exactly ±1 since 1999. Twelve
  sites, six of them live in bot navigation. GCC and Clang compile it silently; MSVC rejects it,
  which is the only reason it was found. The MSVC leg is now the regression guard for the class.
- **`/EHsc` is incompatible with this engine.** The `c` lets the compiler assume an `extern "C"`
  function never throws, and `Sys_Error` throws through `Com_Error` and `FS_ReadFile`, all
  `extern "C"`. Step P2.0 makes `Com_Error` itself an exception across those boundaries, so
  `/EHs` is a prerequisite for phase 2, not a local fix.
- **`JobHandle::wait()` could return before the completion it waited for had run.** The macOS
  leg caught it under `--schedule-random` while the Windows work was in flight. It was a real
  race, not a flake: the worker posts the completion and only then sets `done`.

**Gate G1 still has never run.** `ci/smoke/golden/` holds only a `README.md`, and the smoke step
skipped itself whenever the paks secret was absent, which it always was. So the whole of phase P1
landed with no pixel baseline, which `00-environment.md` step 3b explicitly forbade in writing,
and that baseline cannot be recovered from this tip. Step 3b and step P0.7 are rewritten to say
so and to re-base the oracle on the current tree.

**The game data is OpenArena**, not retail, in the private bucket
`ci-testing-q3-open-arena-assets`. The smoke harness cannot run on it as written: it plays
`demo four`, which is id retail content, and the engine loads only `demos/<name>.dm_68` while
OpenArena records protocol 71, so the gate needs a demo recorded by this engine. Steps 3c to 3e
in `00-environment.md` cover the harness, the dispatch-only job with Workload Identity
Federation, and the architecture question behind owner decision 18.

Fixed on 3 September beyond the checklist steps, because the platform legs exposed them:

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

### What the audit of 4 September 2026 found

The checklists are self-reported, so an audit read every ticked step against the tree. The
record-keeping discipline is real: every code commit carries its checklist edit. Four ticks were
wrong and are now unticked, with the reason recorded on the step:

| Step | Finding |
|---|---|
| `02` B5.2 | `Sys_SubsystemShutdown` exists but **the engine never calls it**. Its only caller is a test, so the test passes while the product path is dead: at exit the download is not cancelled, the script engine is not reset, and the `JobSystem` never shuts down. |
| `03` C6.2 | `VmAbi.SyscallReceivesSixteenIntptrArgs` was never written, so nothing guards the 16-slot syscall widening that phase B1 landed. |
| `03` C7.5 | Never written. Reassigned to `06-networking.md` step N1.4, which deletes the code the tests would have covered. |
| `03` C8.2 | `--gtest_repeat=3` is nowhere in the repository. |

About a dozen more ticks are defensible but no longer match the tree; each now carries a
recorded deviation. The ones worth knowing about: no ThreadSanitizer leg exists although
`05` T6.1 was meant to open with T1; `tests/CMakeLists.txt` and the workflow disagree about
`ASAN_OPTIONS=detect_leaks`, and the CMake side silently wins; the tests still stub
`Sys_Milliseconds`, so the monotonic-clock cases validate `std::chrono` rather than the engine;
and `-Werror=return-type` and `-Werror=write-strings` were never added, so two enforcement
promises do not exist.

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
| 5a | ~~`04-cxx-migration.md` phase P1.W (Windows fallout)~~ Done on 4 September 2026, run 33. `P1.W.5` (`/permissive-` per directory) is the remainder and is not blocking | 04 P1 pull requests 1 to 8 |
| 5b | `00-environment.md` steps 3c, 3d, 3e (OpenArena smoke harness, dispatch-only gate G1 job, the architecture question), then 3b and `04` P0.7 (produce the golden) | 05a |
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
| Network | ~~The FastDL rewrite dropped the legacy `download` command: a client that needs a file stalls at connect~~ **Fixed by checklist 06 step N1.1 on 2 September 2026.** The UDP path is restored at `code/client/cl_main.cpp:1337-1361` | `code/client/cl_main.c:1378-1426` |
| Network | HTTP downloader: still no TLS (an `https://` URL sets port 443 and then speaks cleartext), blocking sockets with no connect timeout, no status or redirect handling, `stoi` and `stoull` on server input inside a thread with no `catch` (remote `std::terminate`). **Nothing in the shipping client calls it** — only tests do — so the vulnerable code is dead until checklist 06 N1.4 rewrites it. The `Cvar_SetValue` from the worker was fixed by checklist 05 step T1.5 | `code/sys/net/http_downloader.cpp:50,87,113,171`, `code/sys/sys_api.cpp:257-263` |
| Network | `sv_dlURL` **no longer exists in the tree at all**, which is the safe state: N1.1 deleted the client-side cvar and N1.2, which adds it back server-side, is unstarted. The risk is latent, not live, so do not "fix" it before N1.2 and N1.3 land the policy cvars together. The blacklist is gone: `Sys_SanitizeDownloadFilename` (`code/sys/sys_api.cpp:199-243`) is a real allowlist with a recorded rationale | `code/client/cl_main.c:1403-1417`, `code/sys/sys_api.cpp:82-117` |
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
| Tests | Was 49 tests, now **127** across two binaries, with coverage for `files.cpp`, `net_chan.cpp`, `cm_*`, `snd_*`, and the VM bridge, plus an AddressSanitizer leg. Still open: the Vulkan and Discord tautologies survive by design (checklist 03 step C7.2 only marked them), and there is no ThreadSanitizer leg | `tests/` |
| Hygiene | ~~65 stray build files, three tracked `.exe` files, a tracked screenshot, orphaned `code/macosx/`~~ **Fixed by checklist 10 step 9 on 2 September 2026**, verified at zero. Debug prints in UI and server code are still open (step 8), and its line references are stale because checklist 04 renamed those files to `.cpp` | `lcc/bin/*.exe`, `code/q3_ui/ui_gameinfo.c:132`, `code/server/sv_ccmds.c:151` |

### Subsystem reality check

| Documented | Reality | Wired into the engine |
|---|---|---|
| SDL2 platform layer | Real | Yes |
| 64-bit VM ABI (`intptr_t`) | Half done | Yes |
| VFS | `std::filesystem` resolver that shadows `files.c` | Yes, harmful |
| `CvarManager` | Thin wrapper; `find()` creates cvars as a side effect | Only `notify_change` |
| Logger | Real, unsafe | Yes |
| FastDL and HTTP | UDP restored and working; the HTTP downloader is unreachable from the client | UDP yes, HTTP no |
| `ScriptEngine` (Lua) | Runs Lua **with `sol::lib::package` open**, so a script can load native libraries; hooks, sandbox, script loading, and the documented API are all absent | One `game_init` dispatch, no subscriber |
| Discord RPC | Logging stub | Tests only |
| Bitstream and transport | Wire-incompatible reimplementation of `msg.c` | Tests only |
| `SessionManager` | Slot array with viewport maths | One `reset()` call |
| VBO/VAO, GLSL | Do not exist | No |
| Vulkan | Stub that logs a successful Vulkan 1.3 initialisation that never happened | Tests only |

## Lifecycle of this directory

Each checklist deletes itself as its last step and removes its row from the **Checklist files**
table in this file. When the table is empty, delete this README and the `docs/plans/` directory.
The history of the work stays in git and in `CHANGELOG.md`.
