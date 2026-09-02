# Checklist 10: documentation and hygiene

## Purpose

Make the repository documentation describe what the code does, give a new contributor a working
build path on three platforms, record third-party licences correctly, and remove stray files
and debug noise. After this checklist, no document claims a feature that has no call site, and
every new cvar has a reference entry.

**Status:** Not started

## Prerequisites

- Step 9 (stray files) has no prerequisite and lands first in the overall order (see
  `README.md`, **Dependency order**, row 1).
- Steps 1, 2, 5, 6, 7, and 8 need checklist `01-build-portability.md` complete, so that the
  build instructions you write are true.
- Steps 3 and 4 are continuous: rewrite each subsystem document when its checklist lands
  (`06-networking.md`, `07-scripting.md`, `08-renderer-ui.md`, `05-threading.md`).

Owner decisions this checklist relies on: item 3 (keep the tool trees, remove stray files only),
item 4 (Docker-first build documentation), item 5 (libcurl, LuaJIT), item 14 (stb for JPEG).

## Background

Documentation state at the audit:

- `README.txt` is the unmodified 2005 id Software release file. It describes Visual C++ 2003
  project files (`README.txt:171-173`), `cons` with gcc 2.95 (`README.txt:183-188`), and a
  `code/macosx/Quake3.pbproj` project (`README.txt:193`). None of these apply. The string
  `cmake` appears in no document.
- No `README.md`, `CONTRIBUTING`, `CHANGELOG`, or `LICENSE` file exists at the root. No document
  says where `pak0.pk3` goes, how to run tests, that CMake downloads GoogleTest from the network
  at configure time, or that LuaJIT is required.
- `docs/*.md` describe features that do not exist or do not behave as described:
  - `docs/networking.md:22` claims "non-blocking TCP sockets"; the sockets are blocking
    (`code/sys/net/http_downloader.cpp:100,135`).
  - `docs/networking.md:26` claims `FS_Restart` runs when a download completes;
    `Sys_GetHttpDownloadStatus` (`code/sys/sys_api.cpp:131`) has no callers.
  - `docs/networking.md:4` claims HTTPS; there is no TLS.
  - `docs/renderer.md:4` claims VBO streaming through `glBufferData`; `qglBufferData` is never
    called (`code/sys/sys_sdl.cpp:198-206` resolves it, nothing uses it).
  - `docs/renderer.md:6` names `SCR_AdjustFrom640`, which does not exist.
  - `docs/renderer.md:8-10` describes a Vulkan 1.3 backend; `code/renderer/vulkan/vk_backend.cpp`
    is 38 lines with no Vulkan header.
  - `docs/discord_rpc.md` documents a 29-line logging stub (`code/sys/rpc/discord_rpc.cpp`).
  - `docs/scripting.md:7` claims expression evaluation; `eval()` never calls Lua
    (`code/sys/scripting/script_engine.cpp:189-204`).
  - `docs/scripting.md:95-108` documents `set_gravity`, `enable_quad_damage`, and
    `scripts/match_rules.lua`; none exist and no script is loaded from disk.
  - `docs/architecture.md:12` says 47 unit tests; 49 `TEST(` macros exist.
- Sol2 v3.3.0 (about 30,000 vendored lines under `code/sys/scripting/sol/`, MIT licence) is not
  listed in the third-party inventory at `README.txt:17-152`.
- The 12 non-vendored headers in `code/sys/` contain 13 comment lines in 1,058 lines. No
  `Doxyfile`, `.clang-format`, `.clang-tidy`, or `.editorconfig` exists.
- Debug prints left by the "Add detailed logging" commits (`c1c2347`, `c393774`) fire on
  interactive paths: `code/q3_ui/ui_splevel.c:230,413,507`, `code/q3_ui/ui_sparena.c:49`,
  `code/q3_ui/ui_gameinfo.c:132` (a `Com_Printf` inside a UI module, where the surrounding code
  uses `trap_Print`), `code/server/sv_ccmds.c:147,151` (every `map` command prints a line).
- About 65 stray build files from the 2005 release are tracked (`.sln`, `.vcproj`, `.bat`,
  `.lnt`, `Conscript`, `.mak`), plus three Windows binaries in `lcc/bin/`, an accidental root
  screenshot added in commit `99ac738`, and the orphaned `code/macosx/` tree that the same
  commit's message claims to have removed.

## Steps

### Hygiene first

- [x] **9. Remove stray build files and tracked binaries.** Done on 2 September 2026.
  (Numbered 9 to match the approved plan; it lands first.) Run `git rm` on:

  ```text
  "Screenshot From 2026-09-01 09-17-54.png"
  code/quake3.sln code/quake3.vcproj code/*.lnt code/*.bat code/Construct code/Makefile
  code/botlib/botlib.vcproj code/botlib/*.mak
  code/bspc/Conscript code/bspc/*.sln code/bspc/*.vcproj code/bspc/*.mak
  code/cgame/Conscript code/cgame/*.bat code/cgame/cgame.vcproj
  code/game/Conscript code/game/*.bat code/game/game.vcproj
  code/q3_ui/Conscript code/q3_ui/*.bat code/q3_ui/q3_ui.vcproj
  code/ui/Conscript code/ui/*.bat code/ui/ui.vcproj
  code/renderer/renderer.vcproj code/splines/Splines.vcproj
  code/unix/Cons_gcc.pm code/unix/Conscript-* code/unix/Makefile code/unix/Makefile.Game
  code/unix/cons code/unix/pcons-2.3.1 code/unix/build_setup.sh code/unix/run-target.sh
  code/unix/extract_ver.pl code/unix/q3test.spec.sh code/unix/Quake3.kdelnk
  code/macosx
  lcc/bin/*.exe lcc/buildnt.bat
  libs/*/*.vcproj
  q3asm/*.sln q3asm/*.vcproj q3map/*.sln q3map/*.vcproj
  q3radiant/*.sln q3radiant/*.vcproj q3radiant/splines/Splines.vcproj
  ```

  Check each glob with `git ls-files <glob>` before removing, and keep any file a CMake target
  references (none of the above are referenced). Keep the tool trees themselves (`lcc/`, `q3map/`,
  `q3radiant/`, `code/bspc/`, `libs/`, `common/`, `q3asm/`). Do not remove `code/unix/*.c`
  here; checklist 01 step 4 replaces them. Add to `.gitignore`:

  ```text
  build*/
  .cache/
  compile_commands.json
  CMakeUserPresets.json
  ```

  **Tests:** none, because file removal; **Verify** covers it.
  **Verify:** `git ls-files | grep -Ei '\.(sln|vcproj|bat|lnt|mak|exe)$|Conscript|pcons|code/unix/cons$|code/macosx' | wc -l` prints `0`. `git ls-files | wc -l` drops by about 200. The
  container build (once checklist 01 lands) is unaffected.

- [ ] **8. Remove debug prints.**
  - `code/q3_ui/ui_splevel.c:230`, `:413`, `:507`: delete the three `trap_Print(va("UI_SPLevelMenu_..."))` traces.
  - `code/q3_ui/ui_sparena.c:49`: delete the `UI_SPArena_Start: Starting singleplayer arena level` trace.
  - `code/q3_ui/ui_gameinfo.c:132`: keep the skip of arenas whose BSP is missing, but delete the
    `Com_Printf`. A UI module must not call `Com_Printf`; if a message is wanted, use
    `trap_Print` behind a `uis.debug` check, if that field exists in `ui_local.h`.
  - `code/server/sv_ccmds.c:147`: restore the bare `return;` for an empty map name.
  - `code/server/sv_ccmds.c:151`: change `Com_Printf("SV_Map_f: Loading map '%s'")` to
    `Com_DPrintf` so it prints only with `developer 1`.
  **Tests:** none, because the change removes output; **Verify** covers it.
  **Verify:** start the client in the container with `+set developer 0`, open the single-player
  level menu, and confirm the console shows no `UI_SPLevelMenu` or `UI_ParseInfos` lines. Run
  `map q3dm1` and confirm no `SV_Map_f:` line appears.

### Root documents

- [ ] **1. Write the root `README.md` and move `README.txt`.**
  `git mv README.txt docs/legacy/README-id-2005.txt`. Write `README.md` with these sections:
  - **What this is.** The fork's scope: C11 and C++17 (C++17 only after checklist 04), SDL2,
    CMake, three platforms, compatibility with retail Quake III Arena assets.
  - **Requirements.** Docker as the primary path (link `docs/building.md`). Native package
    lists for people who prefer them: Linux `cmake ninja-build pkg-config libsdl2-dev libgl-dev
    libluajit-5.1-dev libcurl4-openssl-dev libgtest-dev`; macOS arm64 `brew install cmake ninja
    sdl2 luajit curl`; Windows Visual Studio 2022 plus vcpkg `sdl2 luajit curl gtest`.
  - **Build.** `make build`, or natively `cmake --preset dev && cmake --build --preset
    dev`. State that CMake downloads GoogleTest 1.14 with FetchContent at configure time when no
    system package exists, and that offline builds need a system GoogleTest or
    `-DFETCHCONTENT_FULLY_DISCONNECTED=ON` with a pre-populated source directory.
  - **Game data.** Copy `pak0.pk3` to `pak8.pk3` from a retail or Steam install into
    `build/baseq3/` next to the binary, or into the home path (`~/.q3a/baseq3`,
    `~/Library/Application Support/Quake3/baseq3`, `%APPDATA%\Quake3\baseq3`). The repository
    does not ship them.
  - **Run.** `./build/quake3_modern +set fs_basepath <dir>`; dedicated
    `./build/q3ded +map q3dm17`. Note the module naming `qagame<arch>.<ext>`.
  - **Tests.** `make test` or `ctest --preset dev --output-on-failure`.
  - **Feature status.** A table with one row per subsystem: Working, Partial, Planned, with a
    link to the relevant `docs/*.md`. Fill it from the **Subsystem reality check** table in
    `docs/plans/README.md` at the time you write it, and update it as checklists land.
  - **Contributing.** Pointer to `docs/plans/` while it exists, then to `CONTRIBUTING.md` if
    one is written; the style rules (Simplified Technical English, Google developer
    documentation style, `.clang-format`).
  - **Licence.** GNU General Public License version 2 (`COPYING.txt`) and a pointer to
    `THIRD_PARTY_LICENSES.md`.
  **Tests:** none, because documentation.
  **Verify:** the markdown link check (step 4) passes. A fresh clone on each CI platform,
  following only `README.md`, builds and passes `ctest`.

- [ ] **2. Write `THIRD_PARTY_LICENSES.md`.**
  Carry over the five entries from `docs/legacy/README-id-2005.txt` lines 19 to 140 with their
  notice text: zlib portions in `code/qcommon/unzip.c` (Gilles Vollant, Jean-loup Gailly, Mark
  Adler), RSA Data Security MD4 in `code/qcommon/md4.c`, the University of California standard
  C library replacement routines in `code/game/bg_lib.c`, the Stichting Mathematisch Centrum
  ADPCM coder in `code/client/snd_adpcm.c`, and the Independent JPEG Group library in
  `code/jpeg-6/` (remove this entry and the "based in part on the work of the Independent JPEG
  Group" notice when checklist 04 pull request 9 deletes `code/jpeg-6/`). Add:
  - Sol2 v3.3.0, MIT, `code/sys/scripting/sol/` (licence text from `sol.hpp:1-3` and
    `config.hpp:1-20`; version note from `config.hpp:22-25`).
  - SDL2, zlib licence, linked.
  - LuaJIT 2.1, MIT, linked (or built from source through FetchContent).
  - GoogleTest 1.14, BSD 3-Clause, fetched at build time, test binaries only.
  - libcurl, curl licence, linked (after checklist 06 step N1.4).
  - stb_image and stb_image_write, MIT or public domain dual licence, `code/third_party/stb/`
    (after checklist 04 pull request 9).
  Include the full licence text or a verbatim pointer for each.
  **Tests:** none, because documentation.
  **Verify:** every dependency named in `CMakeLists.txt` (`find_package`, `pkg_check_modules`,
  `FetchContent_Declare`) and every vendored directory has an entry. Check with
  `grep -E 'find_package|pkg_check_modules|FetchContent_Declare' CMakeLists.txt`.

- [ ] **5. Write `CHANGELOG.md`.**
  Use the Keep a Changelog format with an `[Unreleased]` section and `Added`, `Changed`, `Fixed`,
  `Security`, and `Removed` headings. Seed it from `git log` with honest wording for the existing
  commits: "added SDL2 platform layer", "added GLSL function pointer resolution (no shader
  programs yet)", "added HTTP downloader prototype (HTTP only, blocking)", "added Discord RPC
  stub", "added Vulkan backend stub", "integrated Sol2", "FastDL path (breaks UDP downloads,
  fixed in checklist 06)". Every later checklist step that changes user-visible behaviour adds
  a line here in the same commit.
  **Tests:** none, because documentation.
  **Verify:** each entry names a commit or a checklist step; no entry claims a feature the
  **Feature status** table marks as Planned.

### Subsystem documents

- [ ] **3. Rewrite `docs/*.md` to describe built behaviour.**
  Do this per subsystem when its checklist lands. Mark planned work with a **Planned** heading,
  never in the present tense.
  - `docs/architecture.md`: replace the test count with "run `ctest -N` to list the tests";
    remove the VBO/VAO and Vulkan claims (`docs/architecture.md:10-11`) and point to
    `docs/renderer.md`; describe the `q3sys` to C engine boundary (`code/sys/sys_api.h`), the
    native module then QVM fallback in `code/qcommon/vm.c:482-491`, and the main-thread rule
    (link `docs/threading.md` from checklist 05).
  - `docs/networking.md`: replace with the design from checklist 06 as built: the
    `cl_allowDownload` bitmask, `sv_dlURL` and `sv_allowDownload`, the libcurl HTTP path, the
    UDP fallback, file placement (`fs_homepath/<fs_game>/<name>.tmp` then rename), the allowlist
    rules, the manual test script, the bitstream facade and netchan loopback test, and a "Not
    supported" list (no resume, no parallel downloads).
  - `docs/discord_rpc.md`: the IPC protocol summary, the cvars, how to create a Discord
    application and upload the `logo` asset, a privacy note (off by default; what is sent: map,
    game type, host name, player counts), and troubleshooting with `discord_status`.
  - `docs/scripting.md`: the sandbox, script location, the event table with argument shapes,
    the `q3` API reference, console commands, a real example `scripts/example_frag_log.lua`, and
    an explicit "Not available" list (client-side scripting, entity mutation, `set_gravity`,
    `enable_quad_damage`).
  - New `docs/local_multiplayer.md`: `SessionManager` as built, controller slots, and
    "Split-screen rendering: planned" with the estimate from checklist 06 step N3.3.
  - `docs/renderer.md`: owned by checklist 08; this checklist removes the false
    `SCR_AdjustFrom640` sentence (`docs/renderer.md:6`) if checklist 08 has not.
  - `docs/threading.md`: written by checklist 05 phase T1; this checklist links it.
  **Tests:** none, because documentation.
  **Verify:** for every function, cvar, or command a document names, `grep -rn` finds a
  definition in `code/`. The link check (step 4) passes.

- [ ] **4. Write `docs/cvars.md` and the doc-sync checks.**
  A hand-kept table with columns Name, Default, Flags, Subsystem, Description. Initial rows:

  | Name | Default | Flags | Subsystem |
  |---|---|---|---|
  | `cl_allowDownload` | `1` | archive | downloads (bitmask: 1 enable, 2 no HTTP, 4 no UDP) |
  | `sv_dlURL` | empty | systeminfo, archive | downloads (server) |
  | `sv_allowDownload` | `1` | archive | downloads (server) |
  | `cl_dlMaxSize` | `512` | archive | downloads (MB) |
  | `cl_dlTimeout` | `30` | archive | downloads (seconds) |
  | `cl_dlFallbackURL` | empty | archive | downloads (third-party fallback, opt in) |
  | `cl_discordRichPresence` | `0` | archive | Discord |
  | `cl_discordClientId` | empty | archive | Discord |
  | `cl_splitScreenSlots` | `1` | archive | local multiplayer |
  | `sv_scriptEnable` | `1` | archive, latch | scripting |
  | `sv_scriptMaxInstructions` | `10000000` | none | scripting |
  | `com_logLevel` | `1` debug builds, `2` release | archive | logger |
  | `com_jobThreads` | `0` (auto) | archive, latch | threading |
  | `com_busyWait` | `0` | archive | frame pacing |
  | `com_skipIntro` | `1` | archive | startup |
  | `r_swapInterval` | `1` | archive | renderer |
  | `r_glCoreProfile` | `0` then `1` | archive, latch | renderer |
  | `r_vbo`, `r_glsl`, `r_fbo` | `1` | latch | renderer |
  | `r_ext_multisample`, `r_ext_texture_filter_anisotropic`, `r_ext_max_anisotropy` | `0`, `1`, `2` | latch, latch, archive | renderer |
  | `r_allowHighDPI`, `r_fullscreenExclusive`, `r_availableModes` | `1`, `0`, read-only | latch, archive, ROM | renderer |
  | `r_smp` | `0` then `1` | archive, latch | renderer |
  | `con_scale` | `1` | archive | console |
  | `cg_wideScreenHUD`, `cg_horplus` | `1` | archive | cgame |
  | `in_joystick`, `in_joystickThreshold`, `j_*` | see checklist 08 | archive | input |
  | `sv_master1` to `sv_master5` | see owner decision 10 | archive | server browser |

  Add `tests/check_cvar_docs.cmake`, registered with `add_test`, that greps
  `Cvar_Get *\( *"([A-Za-z0-9_]+)"` in `code/sys/`, `code/client/cl_main.c`, and
  `code/server/sv_init.c`, and fails when a name is absent from `docs/cvars.md` and from an
  allowlist file `tests/cvar_docs_allowlist.txt` of legacy names not yet documented. Add
  `ci/check_links.sh` that extracts `](...)` targets from every `.md` file outside
  `docs/legacy/` and fails when a relative target does not exist.
  **Tests:** `tests/check_cvar_docs.cmake` (ctest `Docs.CvarsDocumented`); `ci/check_links.sh`
  (CI job `docs`). Negative case: add a fake `Cvar_Get("zz_undocumented", ...)` locally and
  confirm the ctest fails, then remove it.
  **Verify:** `ctest -R Docs` passes; `ci/check_links.sh` prints `0 broken links`.

### Source hygiene

- [ ] **6. Add doc comments to `code/sys/**/*.hpp` and `sys_api.h`.**
  Doxygen-style `///` on every public class and method in `cvar_manager.hpp`, `vfs.hpp`,
  `logger.hpp`, `vec3.hpp`, `bitstream.hpp`, `transport.hpp`, `http_downloader.hpp`,
  `discord_rpc.hpp`, `discord_ipc.hpp`, `script_engine.hpp`, `session.hpp`, and the threading
  headers; C comments in `sys_api.h`. Each comment states the thread affinity ("Main thread
  only" or "Thread-safe") because that is the property the audit found violated. Fix the
  stutter at `script_engine.hpp:52`. Explain why, not what. No `Doxyfile` is required.
  **Tests:** none, because comments.
  **Verify:** `grep -rLE '^\s*///' code/sys --include=*.hpp | grep -v sol/` prints nothing.

- [ ] **7. Add formatting and lint configuration.**
  - Root `/.clang-format` for the legacy C tree. It exists so edits match; do not run it over
    legacy files wholesale, because that destroys `git blame`:

    ```yaml
    Language: Cpp
    BasedOnStyle: LLVM
    UseTab: Always
    IndentWidth: 4
    TabWidth: 4
    ColumnLimit: 0
    BreakBeforeBraces: Attach
    SpacesInParentheses: true
    SpaceBeforeParens: ControlStatementsExceptControlMacros
    AllowShortIfStatementsOnASingleLine: false
    SortIncludes: false
    PointerAlignment: Right
    ```

  - `code/sys/.clang-format` and `tests/.clang-format` for modern C++; run once over `code/sys`
    (excluding `sol/`) and `tests`:

    ```yaml
    BasedOnStyle: Google
    IndentWidth: 4
    UseTab: Never
    ColumnLimit: 120
    NamespaceIndentation: None
    AccessModifierOffset: -4
    DerivePointerAlignment: false
    PointerAlignment: Left
    SortIncludes: false
    AllowShortFunctionsOnASingleLine: Inline
    ```

  - `code/sys/scripting/sol/.clang-format` containing `DisableFormat: true`.
  - `/.editorconfig`:

    ```ini
    root = true
    [*]
    charset = utf-8
    end_of_line = lf
    insert_final_newline = true
    [*.{c,h}]
    indent_style = tab
    tab_width = 4
    [code/sys/**.{cpp,hpp}]
    indent_style = space
    indent_size = 4
    [tests/**.cpp]
    indent_style = space
    indent_size = 4
    [*.{md,txt}]
    trim_trailing_whitespace = false
    [CMakeLists.txt]
    indent_style = space
    indent_size = 4
    [*.lua]
    indent_style = space
    indent_size = 2
    ```

  - `/.clang-tidy` with `HeaderFilterRegex: 'code/sys/.*'` and checks `bugprone-*,
    cppcoreguidelines-*, performance-*, readability-*, concurrency-*,
    -readability-magic-numbers`. Wiring into CI belongs to checklist 01 step 7.
  **Tests:** none, because configuration; **Verify** covers it.
  **Verify:** `clang-format --dry-run --Werror $(git ls-files 'code/sys/**/*.cpp'
  'code/sys/**/*.hpp' 'tests/*.cpp' | grep -v sol/)` exits 0 in the container. Legacy files are
  unchanged (`git diff --stat code/qcommon` is empty).

## Test map

| File | Binary or runner | Cases | Added by step |
|---|---|---|---|
| `tests/check_cvar_docs.cmake`, `tests/cvar_docs_allowlist.txt` | ctest `Docs.CvarsDocumented` | every `Cvar_Get` name in `code/sys`, `cl_main.c`, `sv_init.c` is in `docs/cvars.md` or the allowlist; a fake undocumented cvar fails the test | 4 |
| `ci/check_links.sh` | CI job `docs` | every relative markdown link target exists | 4 |
| `clang-format --dry-run --Werror` | CI job `docs` | modern C++ files are formatted | 7 |

## Out of scope

- Rewriting `docs/renderer.md` content (checklist 08) and writing `docs/threading.md`
  (checklist 05); this checklist links and corrects them.
- Removing `code/unix/*.c` and `code/null/{null_glimp,null_main,null_net,mac_net}.c`
  (checklist 01 step 4).
- A `CONTRIBUTING.md`; write one only if the owner asks.

## Follow-ons

- A `Doxyfile` and published API documentation once `code/sys` stabilises.
- Enforcing `.clang-tidy` in CI on converted files (after checklist 04 phase P1).

## Done criteria

- `git ls-files` lists no `.sln`, `.vcproj`, `.bat`, `.lnt`, `.mak`, `Conscript`, or `.exe`
  file, and no `code/macosx/` or root screenshot.
- A fresh clone on Linux (container), macOS, and Windows builds and passes `ctest` by following
  only `README.md`.
- `THIRD_PARTY_LICENSES.md` covers every dependency in `CMakeLists.txt` and every vendored
  directory.
- No sentence in `docs/*.md` describes a feature without a call site; planned work sits under a
  **Planned** heading.
- `ctest -R Docs` and `ci/check_links.sh` pass in CI.
- Every public declaration in `code/sys/**/*.hpp` has a doc comment stating thread affinity.
- No debug print from commits `c1c2347` and `c393774` remains outside `developer 1`.

## Last step

- [ ] Delete this file and remove its row from `docs/plans/README.md`.
