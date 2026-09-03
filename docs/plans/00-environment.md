# Checklist 00: development environment

## Purpose

Create the container image, the compose file and Makefile, the smoke gates, and the continuous integration (CI) skeleton that
every other checklist uses. After this checklist, an agent builds, tests, and renders the game
headless on the owner's machine without installing anything on the host, and the same image runs
the Linux legs in CI.

**Status:** In progress. Steps 1, 2, 3, 4, 5, and 6 done on 2 September 2026 (image, compose file, Makefile, smoke scripts, pixel gate, CI workflow, building doc). Steps 7 and 8 (MinGW cross image, native macOS targets) added on 2 September 2026; 8 done, 7 needs verification on the Linux machine with Docker. Open: 3b golden image, 7.

## Prerequisites

None. This is the first checklist.

Owner decisions this checklist relies on (see `README.md`, **Owner decisions**): item 4 (install
nothing on the dev machine), item 1 (platforms), item 5 (approved dependencies: libcurl, LuaJIT).

## Background

- No build directory, `CMakeCache.txt`, object file, or shared library exists anywhere in the
  tree. No `.github/`, `.gitlab-ci.yml`, or `CMakePresets.json` exists. Nothing in the tree has
  been build-verified on any platform.
- The owner's machine is macOS arm64 (Darwin 25.5). `pkg-config --exists luajit` fails, SDL2 is
  provided by `sdl2-compat` (SDL3 underneath), and GoogleTest is not installed. The owner installs
  nothing locally.
- The current build does not compile on macOS (`CMakeLists.txt:207,325` use `-Wl,--start-group`,
  `code/unix/unix_glw.h:22` is a hard `#error` off Linux). Checklist `01-build-portability.md`
  fixes that; this checklist provides the Linux container where the fixes are proven first.
- Mesa `llvmpipe` provides software OpenGL 4.5 and `lavapipe` provides software Vulkan inside
  the container, so renderer screenshot gates run headless and are reproducible.
- Verified on 2 September 2026 inside the container (Linux arm64, gcc 13, Mesa 25.2.8): the tree
  at `ad3705e` configures and builds all 281 targets with `make build`, and `make test` passes
  46 of 49 tests. The three failures are `ModernLoggerTest.*`, because `RelWithDebInfo` defines
  `NDEBUG` and `code/sys/logger/logger.hpp:62-72` compiles every `LOG_*` call out; checklist
  `02-stability.md` step B4 fixes the logger and checklist 03 rewrites the test. The modules
  build as `qagamex86_64.so` on an arm64 container, so `Sys_LoadDll` cannot find them until
  checklist 01 step A3.1 derives the architecture string; the engine falls back to the QVMs in
  `pak0.pk3`, which is enough for the smoke gates.
- The owner does most of the work on a Linux machine (assumed x86_64) and sometimes on the
  macOS arm64 laptop. Produce gate G1 golden images on the Linux x86_64 machine. Native
  modules load there because the build names them `*x86_64.so`; on the arm64 laptop the engine
  falls back to the QVMs until checklist 01 derives the architecture string.
- macOS cannot run in a container and Windows containers only run on a Windows host. The
  Windows leg cross-compiles with MinGW-w64 in the `win` container and runs tests under Wine;
  the macOS leg is the CI runner plus a native no-install build (owner decisions 16 to 18).
- Game data (`pak0.pk3` to `pak8.pk3`) is not in the repository (`.gitignore` excludes `*.pk3`).
  The container mounts the owner's pak directory read-only. Never copy paks into the image.

## Steps

### Container

- [x] **1. Write `docker/Dockerfile`.** Done on 2 September 2026.
  Base image `ubuntu:24.04`. Packages: `build-essential clang gdb ccache cmake ninja-build
  pkg-config git ca-certificates clang-format clang-tidy python3 file libsdl2-dev libgl-dev
  libx11-dev libxext-dev libluajit-5.1-dev libcurl4-openssl-dev libgtest-dev libvulkan-dev
  vulkan-tools mesa-utils libgl1-mesa-dri mesa-vulkan-drivers xvfb apitrace imagemagick`. The
  image creates a world-writable `/tmp/q3home` (with a `.gitconfig` that marks `/src` safe) and
  `/ccache`, because the compose file runs the container as the host user, who has no passwd
  entry. Environment defaults: `LIBGL_ALWAYS_SOFTWARE=1`, `GALLIUM_DRIVER=llvmpipe`,
  `SDL_AUDIODRIVER=dummy`.
  **Tests:** none, because this is infrastructure; gate G1 (step 3) is the test.
  **Verify:** `make image` succeeds. `make shell` then `cmake --version && pkg-config
  --modversion luajit && glxinfo -B` prints versions and `llvmpipe` as the renderer.

- [x] **1b. Pin the base image digest and record the Mesa version.** Done on 2 September 2026.
  Run `docker pull ubuntu:24.04` and `docker inspect --format '{{index .RepoDigests 0}}'
  ubuntu:24.04`, then change the `FROM` line to `ubuntu:24.04@sha256:...`. Run `make shell` and
  `glxinfo -B`, and record the Mesa version in a comment next to the `FROM` line. Gate G1's
  golden image depends on that version, so redo this step (and regenerate the golden image in
  the same commit) whenever you refresh the pin.
  **Tests:** none, because this is infrastructure.
  **Verify:** `make image-rebuild` succeeds from the pinned digest; `docker/Dockerfile` names
  the Mesa version.

- [x] **2. Write `compose.yaml` and the root `Makefile`.** Done on 2 September 2026. These
  replace the `docker/run.sh` script that the original plan named.
  `compose.yaml` defines one service `dev` built from `docker/`, run as `${Q3_UID}:${Q3_GID}`,
  with the repository at `/src`, `${Q3_PAKS:-./docker/paks}` read-only at `/paks`, a named
  volume `q3-ccache` at `/ccache`, and `SYS_PTRACE` plus `seccomp=unconfined` for sanitizers
  and gdb. `docker/paks/README.md` explains where the paks go; `.gitignore` already excludes
  `*.pk3`.
  The `Makefile` exports `Q3_UID`, `Q3_GID`, and `Q3_PAKS` and wraps `docker compose run --rm
  dev`. Targets: `image`, `image-rebuild`, `configure`, `build`, `test`, `asan`, `tsan`, `smoke`,
  `smoke-update-golden`, `apitrace`, `bot-match`, `shell`, `clean`, `distclean`, `help`.
  Variables: `BUILD_DIR`, `BUILD_TYPE`, `CMAKE_ARGS`, `CTEST_ARGS`, `Q3_PAKS`, `CI` (drops the
  TTY). The `asan` and `tsan` targets pass sanitizer flags through `CMAKE_<LANG>_FLAGS` and the
  linker flag variables until checklist 01 step A3.7 adds `Q3_SANITIZE`; switch them to
  `-DQ3_SANITIZE=...` in that step. The configure step enables `ccache` launchers and
  `CMAKE_EXPORT_COMPILE_COMMANDS`; switch it to `cmake --preset dev` when checklist 01 step
  A3.10 adds `CMakePresets.json`.
  **Tests:** none, because this is infrastructure; the `test` target runs the test suite.
  **Verify:** `make shell` opens a prompt with `/src` mounted and `id -u` equal to the host
  user. `make build` runs the configure and build steps inside the container; until checklist
  01 lands it might fail inside the tree itself, and the last line shows the exit code. After
  checklist 01, `make test` prints `100% tests passed`.

### Gates

- [x] **3. Write gate G1: `ci/smoke/run_smoke.sh`, `ci/smoke/smoke.cfg`, `ci/smoke/golden/`.**
  Scripts written on 2 September 2026; the golden image is still pending (see below).
  The script takes an optional `--update-golden` or `--apitrace` flag and the build directory.
  It needs `Q3_PAKS` (default `/paks` in the container). It builds a throwaway game directory in
  `mktemp -d` with symlinks to the paks and to the built modules, copies `smoke.cfg` into a
  throwaway `fs_homepath`, and runs:

  ```sh
  xvfb-run -a -s "-screen 0 1024x768x24" \
    env LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe SDL_VIDEODRIVER=x11 SDL_AUDIODRIVER=dummy \
    timeout 300 "$BUILD_DIR/quake3_modern" \
      +set fs_basepath "$BASEPATH" +set fs_homepath "$HOMEPATH" \
      +set r_fullscreen 0 +set r_mode -1 +set r_customwidth 640 +set r_customheight 480 \
      +set r_picmip 1 +set r_texturebits 32 +set r_ext_compressed_textures 0 \
      +set r_swapInterval 0 +set r_gamma 1 +set r_overBrightBits 1 \
      +set s_initsound 0 +set com_introplayed 1 +set com_maxfps 0 \
      +set timedemo 1 +set nextdemo quit \
      +exec smoke.cfg
  ```

  `smoke.cfg` is `demo four`, `wait 200`, `screenshot smoke`, `wait 5`, `quit`. The frame is
  selected by the `wait` count, not by the end of the demo: after the demo ends the client
  disconnects and shows the menu, so a screenshot from `nextdemo` would not show gameplay. In
  timedemo mode the client consumes demo messages per rendered frame independent of wall-clock
  time (`code/client/cl_cgame.c`, `CL_SetCGameTime` reads demo messages until `cl.serverTime`
  catches up, and `cl_main.c:398` fixes the frame time under `timedemo`), so frame 200 is the
  same demo frame on every run. `+set nextdemo quit` ends the run even if `smoke.cfg` fails.
  The screenshot lands in `$HOMEPATH/baseq3/screenshots/smoke.tga` (the engine's
  `screenshot <name>` form, `code/renderer/tr_init.c` `R_ScreenShot_f`). The script copies it to
  `ci/smoke/out/smoke.tga` and `.png`, prints the timedemo frames-per-second line, and compares
  with `compare -metric AE ci/smoke/golden/smoke.tga ci/smoke/out/smoke.tga ci/smoke/out/smoke-diff.png`.
  Exit codes: 0 zero differing pixels, 1 difference or no golden yet, 2 setup error, 3 no
  screenshot produced. `--update-golden` copies the output into `ci/smoke/golden/` (commit it
  with the reason). `--apitrace` wraps the binary in `apitrace trace --api gl` and prints counts
  for `glBegin`, `glMatrixMode`, `glTexEnvf`, `glAlphaFunc`, `glVertexPointer`,
  `glDrawElements`, `glBufferSubData`, and `glUseProgram`, so later checklists can assert them.
  `ci/smoke/run_bot_match.sh` (`make bot-match`) is the dedicated-server gate: 60 seconds of
  `q3ded` with four bots on `q3dm7`, exit 124 from `timeout` expected, no `Sys_Error` or
  `ERROR` in the log, at least four `entered the game` lines; the second argument selects
  `vm_game 0` (native module) or `1` (id QVM).
  The `wait 200` frame choice is the initial approach. If the first golden runs show the frame
  drifting between runs, switch to `+set cl_avidemo 10` and compare a fixed frame file instead;
  record the change here.
  **Tests:** none, because this is the integration gate itself.
  **Verify (pending checklist 01):** `make smoke` exits 1 with `no golden image yet` and saves
  `ci/smoke/out/smoke.png`; inspect it; `make smoke-update-golden` then `make smoke` prints
  `differing pixels: 0`. Change `r_picmip` to `2` in the script, rerun, and see a non-zero
  count and exit 1 (proves the gate detects change); restore the script. `make bot-match`
  prints `pass`.

- [ ] **3b. Produce the first golden image.** Still open on 3 September 2026, now blocked only
  on game data. Checklist 01 is complete and the tree builds and links, but the machine used so
  far has no `pak0.pk3`, so `make smoke` stops with `no pak0.pk3 in /paks` (exit code 2). Run
  this on the Linux machine: put the retail paks in `docker/paks/` or set `Q3_PAKS`, run
  `make smoke-update-golden`, inspect `ci/smoke/out/smoke.png`, then commit
  `ci/smoke/golden/smoke.tga` and `.png` with a message that says which build produced them.
  Until this lands, no rename in checklist 04 has a pixel baseline, so do it before phase P1
  step P1.2. Original note: blocked on checklist 01 (the tree does not build
  yet) and owned by checklist `04-cxx-migration.md` phase P0, which produces it from the all-C
  tree. Until then `make smoke` exits 1.
  **Tests:** none.
  **Verify:** `ci/smoke/golden/smoke.tga` and `smoke.png` are committed and `make smoke`
  passes.

- [x] **4. Write `ci/smp_pixel_gate.sh` (skeleton).** Done on 2 September 2026.
  Two runs of the same smoke command differ by one cvar pair given as arguments (for example
  `r_smp 0` and `r_smp 1`, or `r_vbo 0` and `r_vbo 1`), with `+set cl_avidemo 10` so every
  tenth frame is written as a TGA. The script compares every frame pair with `compare -metric
  AE` and fails on the first non-zero result, printing the frame number. Accept `--demo <name>`
  so checklist 05 can add a portal map demo. The script is complete when it runs two passes and
  compares; the cvars it is called with come from later checklists.
  **Tests:** none, because this is the integration gate itself.
  **Verify:** `ci/smp_pixel_gate.sh build r_picmip 1 r_picmip 1` passes (identical runs);
  `ci/smp_pixel_gate.sh build r_picmip 1 r_picmip 2` fails and names the first frame.

### Continuous integration

- [x] **5. Write `.github/workflows/ci.yml` (skeleton).** Done on 2 September 2026.
  Jobs:
  - `linux-dev`, `linux-asan`, `linux-smoke`: run on `ubuntu-24.04` inside the image built from
    `docker/Dockerfile` (use `docker build` in the job, cache with `actions/cache` keyed on the
    Dockerfile hash). `linux-smoke` needs paks; read them from a private release asset or a
    secret-protected URL into `$Q3_PAKS`, and skip with a clear message when the secret is
    absent (forks). Pass `-DQ3_WERROR=ON` once checklist 01 adds it.
  - `macos-arm64` on `macos-15` and `windows-x64` on `windows-2022`: present in the file but
    guarded by `if: false` with the comment `enabled by checklist 01 step 7`.
  - Upload `build/quake3_modern`, `build/q3ded`, and `build/baseq3/*` as artifacts from every
    leg that builds, so the owner can run a real-GPU check from a download without a local
    install.
  - `concurrency: { group: ci-${{ github.ref }}, cancel-in-progress: true }`.
  - `ctest` runs with `--output-on-failure --timeout 120` and `--gtest_shuffle` through
    `EXTRA_ARGS` (checklist 03 wires `gtest_discover_tests` for it).
  **Tests:** none, because this is infrastructure.
  **Verify:** `act -l` or a push to a branch lists the jobs; the Linux jobs reach the build
  step and fail only where checklist 01 has not landed yet.

- [x] **6. Write `docs/building.md` (stub).** Done on 2 September 2026.
  Two sections: "Build in the container" with the `make` targets and the `Q3_PAKS`
  variable, and "Native builds" with the sentence "Checklist `01-build-portability.md` completes
  this section." Link it from the root `README.md` once checklist 10 creates that file.
  **Tests:** none, because documentation.
  **Verify:** a reader who has only this file and Docker can run `make test`.

### Windows and macOS legs

- [ ] **7. Write `docker/Dockerfile.mingw`, `cmake/toolchain-mingw-w64.cmake`, the `win`
  compose service, and the `win-*` Make targets.** Files written on 2 September 2026. Not yet
  verified: on the arm64 laptop the image build runs under amd64 emulation and `make image-win`
  stopped in the SDL2 step with `x86_64-w64-mingw32-gcc-posix: internal compiler error:
  Segmentation fault` (an emulation fault; the same file compiled for the shared target minutes
  earlier). Run `make image-win` on the Linux x86_64 machine, fix anything real, and tick this
  step there. The image is
  `linux/amd64` (Wine runs x86_64 Windows binaries natively there; on Apple Silicon it runs
  under emulation) with `gcc-mingw-w64-x86-64-posix`, `g++-mingw-w64-x86-64-posix`, Wine,
  CMake, and Ninja. It builds SDL2 `release-2.32.8` (shared and static), curl `curl-8_19_0`
  with Schannel, and LuaJIT v2.1 at commit `1ee778a4e37122d8ca7d5733c590a47dafd6b15c` (static,
  with a hand-written `luajit.pc`) into `/opt/mingw-deps`, sets `PKG_CONFIG_LIBDIR` to that
  prefix, and sets `WINEPATH` so Wine finds `SDL2.dll`, `libcurl-4.dll`, and the MinGW runtime
  DLLs. The toolchain file uses the POSIX-thread compiler variants, restricts `find_*` to the
  cross prefix and sysroot, and sets `CMAKE_CROSSCOMPILING_EMULATOR wine` so `ctest` runs the
  `.exe` files. Targets: `make image-win`, `win-configure`, `win-build`, `win-test`, `win-shell`;
  build directory `build-win64`. The tree itself does not compile for Windows until checklist
  01 phase A4 adds `code/sys/sys_win32.cpp`; until then `make win-build` fails in the platform
  files, which is expected.
  **Tests:** none, because this is infrastructure; `make win-test` runs the suite under Wine
  once checklist 01 lands.
  **Verify:** `make image-win` builds. `make win-shell` then
  `x86_64-w64-mingw32-g++-posix --version`, `pkg-config --modversion luajit`, `ls
  /opt/mingw-deps/lib/cmake/SDL2`, and `wine --version` print versions. Cross-compile and run
  a hello-world: `echo 'int main(){return 0;}' > /tmp/t.c && x86_64-w64-mingw32-gcc-posix
  /tmp/t.c -o /tmp/t.exe && wine /tmp/t.exe && echo ok`. After checklist 01 A6.3,
  `make win-test` passes.

- [x] **8. Add the native host targets for macOS.** Done on 2 September 2026: `make
  native-configure`, `native-build`, `native-test` build in `build-native` with the host
  compiler and `-DQ3_FETCH_DEPS=ON`. They work once checklist 01 step A6.2 adds that CMake
  option and phase A2 makes `q_shared.h` compile on macOS. Nothing is installed on the host;
  the Mac already has Xcode, CMake 4.4, and Ninja.
  **Tests:** none, because the target is a wrapper; `make native-test` runs the suite.
  **Verify:** after checklist 01, `make native-test` on the Mac configures (with network for the
  fetch), builds, and passes `ctest` with no Homebrew library installed.

## Continuous integration defects found on 3 September 2026

Every run of the workflow has failed so far. The Linux and macOS legs failed at their `Build`
step on the link errors that checklist 04 step P1.1 fixed on 3 September 2026. The remaining
failures are defects in `.github/workflows/ci.yml` itself, and each one needs a fix before that
leg can pass:

- [x] **`-DQ3_WERROR=ON` makes the Linux legs unpassable.** Fixed on 3 September 2026 by removing the flag from both legs, with a comment at the top of the workflow that says why and when to reinstate it per target. Both the `Linux Dev` and
  `Linux ASan/UBSan` legs configure with it (`.github/workflows/ci.yml:43` and `:102`), but the
  legacy tree is not warning-clean: a local build with `CMAKE_ARGS='-DQ3_WERROR=ON'` produces
  23 errors, all from original id code, mostly `-Werror=maybe-uninitialized` in the renderer and
  botlib and `-Werror=stringop-truncation`. Checklist 01 step A3.6 set the option's default to
  `OFF` and intended `-Werror` to apply per directory as it converts, which is also what
  checklist 04 assumes. The workflow instead applies it to the whole tree. Pick one: scope the
  flag to the converted targets, which matches the plan, or fix the 23 warnings in the legacy
  code first. Do not simply drop the flag, or nothing enforces warning cleanliness on the
  directories that have converted.
- [x] **`--gtest_shuffle` is passed to `ctest`** Fixed on 3 September 2026: replaced with `ctest --schedule-random` on all four legs, which is the real equivalent because `gtest_discover_tests` runs each case in its own process. at `.github/workflows/ci.yml:278` (macOS) and
  `:313` (Windows). It is a GoogleTest flag, not a `ctest` flag, so `ctest` exits with an
  unknown-argument error even when every test would pass. Use `ctest --schedule-random`, or put
  `EXTRA_ARGS --gtest_shuffle` on `gtest_discover_tests` in `tests/CMakeLists.txt` and drop the
  flag here.
- [x] **Backslash line continuations inside a PowerShell step** Fixed on 3 September 2026 with `shell: bash` on that step and `$VCPKG_INSTALLATION_ROOT` in shell syntax. at `:304-305`. The
  `windows-2022` runner defaults to `pwsh`, which continues lines with a backtick, so the
  `cmake` configure command is malformed and the step fails. Either add `shell: bash` to that
  step or use backticks.
- [x] **The vcpkg triplet applies to one port only** Fixed on 3 September 2026: `vcpkg install --triplet x64-windows sdl2 luajit gtest curl`. at `:301`:
  `vcpkg install sdl2 luajit gtest curl:x64-windows` gives only `curl` the `x64-windows`
  triplet. Pass `--triplet x64-windows` for all of them.
- [x] **`EXTRA_ARGS` is inert** Fixed on 3 September 2026 by deleting it; the shuffle intent now lives in `--schedule-random`. at `:59` and `:120`. The variable is exported into the
  container, but nothing reads it: the `Makefile` uses `CTEST_ARGS`, and these steps call
  `docker run` directly rather than going through the `Makefile`. The shuffle silently never
  happens on the Linux legs. Prefer calling the `Makefile` targets so that local and
  continuous integration runs stay identical.
- [x] **The MinGW leg** fails at `Configure and build Windows binaries`. Partly fixed on 3 September 2026: the image itself builds on an x86_64 runner, and the first real error was `code/botlib/l_precomp.c:708`, which held `unsigned long t` where `ctime` wants a `time_t *`. On Windows `time_t` is 64-bit and `long` is 32-bit, so the pointer types genuinely differ; Linux hid it because `time_t` is `long` there and the mismatch was only a signedness warning. The declaration now uses `time_t`. Expect further Windows-only findings on the next run.

## Workflow shape, restructured 3 September 2026

The matrix was six jobs, all on every push. It is now five, four of which run on a push:

| Job | Runs on | Why |
|---|---|---|
| `linux` | push, pull request, nightly | Builds once, then runs the unit tests and gate G1. Replaces the former `Linux Dev` and `Linux Smoke Gate`, which compiled the same configuration twice. Merged rather than chained with `needs:`, because artifact download does not preserve the executable bit and the smoke gate has to run the binary. |
| `linux-asan` | push, pull request, nightly | Different compiler flags, so it cannot share the build. |
| `macos-arm64` | push, pull request, nightly | Earns its place: it caught four defect families that GCC on Linux accepted, listed in the section below. |
| `windows-x64` | push, pull request, nightly | Microsoft Visual C++ is what Windows users build with, so this is the Windows gate. |
| `linux-mingw` | nightly and manual only | Gated with `if: github.event_name == 'schedule' \|\| github.event_name == 'workflow_dispatch'`. It protects the local `make win-test` path, needs an image that builds SDL2, curl, and LuaJIT from source, and is the slowest and most fragile leg. Not a push gate. |

Also changed: `paths-ignore` for `docs/**`, `**.md`, and `COPYING.txt`, so a checklist edit no
longer spends the whole matrix; a nightly `schedule` at 03:17 UTC plus `workflow_dispatch`; and
the concurrency group is now keyed on the event as well as the ref, so the nightly run and a
push to the same branch do not cancel each other.

Still open on the shape: no ThreadSanitizer leg. Checklist 05 step T6 adds it when the threading
work starts.

## Cross-platform build defects the continuous integration legs exposed

Each of these built cleanly with GCC on Linux and failed on another platform. Together they are
the argument for keeping the macOS and Windows legs on every push.

- **macOS, duplicate symbol `Com_Memset`.** `code/game/q_shared.h:334-339` declares
  `Snd_Memset` as a real function only on Linux, where it works around an old glibc memset bug,
  and defines it as a macro for `Com_Memset` everywhere else. `code/sys/sys_sdl.cpp` defined it
  unconditionally, so off Linux the definition expanded to a second `Com_Memset` and Apple's
  linker rejected it. The definition is now guarded with `#ifdef __linux__`. Fixed 3 September
  2026.
- **Windows, `time_t` against `long`.** See the MinGW entry above.
- **macOS, `sol::nil`.** Sol2 defines the short alias only when the platform has not taken the
  name, and `nil` is an Objective-C macro, so `code/sys/scripting/script_engine.cpp` did not
  compile there. It now uses `sol::lua_nil`. Fixed 3 September 2026. This one predates the
  current work: the script engine had never been compiled on macOS.
- **macOS, `register` and discarded `const`.** Recorded as catalogue row 24 in
  `docs/plans/04-cxx-migration.md`. Fixed 3 September 2026.

## Remaining continuous integration blockers, 3 September 2026

- [ ] **The logger tests fail on every leg.** `Linux`, `Linux ASan/UBSan`, and `macOS arm64` all
  build and then fail at their test step on `ModernLoggerTest.MacroFormattingAndLevels`,
  `ErrorLevelToStderr`, and `CApiLoggingWrappers`. A `RelWithDebInfo` build defines `NDEBUG`, and
  `code/sys/logger/logger.hpp:62-72` compiles every `LOG_*` call out under it, errors included,
  so the tests capture nothing. This is the single thing between those three legs and green.
  Checklist 02 step B4 rewrites the logger and checklist 03 rewrites the tests; the part that
  unblocks CI is keeping `LOG_WARN` and `LOG_ERROR` compiled in release builds.
- [ ] **The Windows leg cannot resolve LuaJIT.** After the PowerShell fix the configure step now
  reaches dependency resolution and fails there. `CMakeLists.txt` looks for LuaJIT with
  `pkg_check_modules`, which needs pkg-config; the `windows-2022` runner has none, so
  `LUAJIT_FOUND` is false even though vcpkg installed the port, and the build falls through to
  the `Q3_FETCH_LUAJIT` path. That wrapper then fails with
  `LuaJIT.cmake:892 file COPY cannot find "/LuaJIT/src/jit"`. Fix in checklist 01 step A6.1: try
  `find_path(LUAJIT_INCLUDE_DIR luajit.h)` and `find_library(LUAJIT_LIBRARY NAMES luajit-5.1
  lua51)` before falling back to FetchContent, because the vcpkg toolchain makes those searches
  work without pkg-config. Not attempted here, because it cannot be verified without a Windows
  machine.

## Test map

| File | Binary or runner | Cases | Added by step |
|---|---|---|---|
| `ci/smoke/run_smoke.sh`, `ci/smoke/smoke.cfg` | shell, gate G1 | frame 200 of `demo four` equals `ci/smoke/golden/smoke.tga`; `--apitrace` call counts | 3 |
| `ci/smoke/run_bot_match.sh` | shell, dedicated-server gate | 60 s bot match with native module and with `vm_game 1` exits by timeout with no error and four bots joined | 3 |
| `ci/smoke/golden/smoke.tga` | fixture | produced by checklist 04 phase P0 | 3b |
| `ci/smp_pixel_gate.sh` | shell | every `cl_avidemo` frame identical between two cvar settings | 4 |
| `.github/workflows/ci.yml` | GitHub Actions | dev, asan, smoke legs in the image; macOS and Windows legs disabled | 5 |
| `docker/Dockerfile.mingw`, `cmake/toolchain-mingw-w64.cmake` | container `win`, Wine | `make win-test` runs the unit tests as Windows binaries | 7 |
| `Makefile` `native-*` targets | host (macOS) | `make native-test` runs the unit tests natively with fetched dependencies | 8 |

## Out of scope

- Fixing the build itself (checklist 01).
- The `Q3_SANITIZE` and `Q3_WERROR` CMake options (checklist 01 step 3).
- The ThreadSanitizer suppressions file `ci/tsan.supp` (checklist 05 phase T6).
- Producing the golden image (checklist 04 phase P0).

## Follow-ons

- A `docker/Dockerfile.windows-cross` with MinGW is possible if the MSVC leg proves slow; not
  planned.

## Done criteria

- `make build && make test` succeed on the owner's machine with nothing installed on the
  host (after checklist 01 makes the tree build).
- `make smoke` and `make bot-match` exit 0 with `Q3_PAKS` mounted and the golden image present.
- The CI workflow file exists, the Linux jobs run inside the image, and the macOS and Windows
  jobs are present but disabled.
- `docs/building.md` explains the container workflow.
- `make image-win` builds and the MinGW toolchain check in step 7 passes; after checklist 01,
  `make win-test` passes under Wine and `make native-test` passes on the Mac.

## Last step

- [ ] Delete this file and remove its row from `docs/plans/README.md`.
