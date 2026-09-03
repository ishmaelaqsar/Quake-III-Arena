# Checklist 03: tests

## Purpose

Turn the test suite from 49 mostly shallow cases into infrastructure that every other
checklist builds on: two test binaries with a clear link contract, shared fixtures, coverage
of the subsystems that have none today (`files.c`, `net_chan.c`, `cm_*`, `snd_*`, the VM
bridge), replacement of the tests that cannot fail, and sanitizer runs in continuous
integration (CI).

**Status:** Not started

## Prerequisites

- Checklist `01-build-portability.md` is complete. The libraries are OBJECT libraries and the
  test link no longer uses `--start-group`.
- Checklist `02-stability.md` step B6 (VFS hook removal) is complete before you write the
  `files.c` tests in C2, because the hook changes `FS_ReadFile` behaviour.
- Checklist `00-environment.md` provides the container. Every `ctest` command below runs
  through `make test` or `make shell`.
- Test files named in other checklists are written by those checklists. This file owns the
  infrastructure, the coverage gaps listed in Background, and the test map of the whole repo.

## Background

| Finding | Anchor |
|---|---|
| 49 `TEST(` and `TEST_F(` macros across 18 files in one binary `quake3_tests`. `docs/architecture.md:12` says 47. | `tests/*.cpp`, `tests/CMakeLists.txt:1-19` |
| No tests for `files.c` and `unzip.c` (pk3 handling), `net_chan.c` and its client and server wrappers, `cm_load.c`, `cm_patch.c`, `cm_polylib.c`, `cm_test.c`, `cm_trace.c`, `snd_dma.c`, `snd_mix.c`, `snd_mem.c`, `snd_wavelet.c`, `snd_adpcm.c`, `vm.c`, `vm_interpreted.c`, `common.c` zone and hunk, or the FastDL path. | `tests/` |
| `test_vulkan_backend.cpp:13` asserts `api_version == 130`, the literal hardcoded in `vk_backend.cpp:13`. It cannot fail. | `tests/test_vulkan_backend.cpp:6-13` |
| `test_legacy_vm_syscalls.cpp` tests `intptr_t` indexing and a `memcpy` on local arrays. It never calls engine code. | `tests/test_legacy_vm_syscalls.cpp:19-33` |
| `test_discord_rpc.cpp` writes a struct through a setter and reads it back. | `tests/test_discord_rpc.cpp:19-23` |
| `test_modern_logger.cpp` asserts on captured `stdout` and `stderr`, and every `LOG_*` macro is a no-op under `NDEBUG`. | `tests/test_modern_logger.cpp:15-41`, `code/sys/logger/logger.hpp:67-72` |
| `test_modern_fs.cpp:30` calls `VirtualFileSystem::instance().unmount_all()` on the process-wide singleton that `sys_api.cpp:31` also mutates. The suite is order dependent. | `tests/test_modern_fs.cpp:30` |
| No sanitizer option exists in either CMake file. | `CMakeLists.txt`, `tests/CMakeLists.txt` |
| The test link uses `-Wl,--start-group`, which Apple ld rejects. | `tests/CMakeLists.txt:36-50` |
| `test_platform_stubs.cpp` stubs `Sys_SendPacket`, `CL_ConsolePrint`, `Sys_Milliseconds`, and others, and `test_cvar_cmd.cpp:7-8` hand-declares `Com_InitZoneMemory` and `Com_InitSmallZoneMemory`. | `tests/test_platform_stubs.cpp`, `tests/test_cvar_cmd.cpp:7-8` |

## Steps

### Phase C1: infrastructure

- [ ] **C1.1 Split the test binaries.** In `tests/CMakeLists.txt` define two executables:
  - `q3sys_tests`: pure C++ tests for `code/sys`. Sources: `test_modern_logger.cpp`,
    `test_modern_fs.cpp`, `test_modern_net.cpp`, `test_modern_scripting.cpp`,
    `test_modern_cvar.cpp`, `test_modern_multiplayer.cpp`, `test_http_downloader.cpp`,
    `test_discord_rpc.cpp`, `test_modern_math.cpp`, and later `test_threading_queue.cpp`,
    `test_job_system.cpp`, `test_json_writer.cpp`, `test_download_policy.cpp`. Links `q3sys`
    objects, `qcommon` objects (for `Cvar_*` used by the cvar wrapper), `luajit::luajit`,
    `SDL2::SDL2`, `Threads::Threads`, and GoogleTest.
  - `quake3_tests`: engine tests. Sources: `test_math.cpp`, `test_strings.cpp`, `test_msg.cpp`,
    `test_huffman.cpp`, `test_md4.cpp`, `test_cvar_cmd.cpp`, `test_files.cpp`,
    `test_netchan.cpp`, `test_collision.cpp`, `test_sound.cpp`, `test_vm.cpp`,
    `test_module_symbols.cpp`, `test_sys_paths.cpp`, `test_sys_net.cpp`, `test_sys_time.cpp`,
    `test_sys_api_boundary.cpp`, `test_platform_stubs.cpp`. Links `qcommon`, `botlib`,
    `q3server`, `q3sys` objects, the real `code/sys/sys_dll.cpp`, `code/sys/sys_files_unix.cpp`
    or `sys_files_win32.cpp`, `code/sys/net/sys_net.cpp`, `code/null/null_client.c`,
    `null_input.c`, `null_snddma.c`, and `code/client/snd_adpcm.c` and `snd_wavelet.c` compiled
    directly for C5. Do not link `q3renderer`; nothing engine-side needs it and it pulls GL.
  - Both use `gtest_discover_tests(<target> PROPERTIES TIMEOUT 60 EXTRA_ARGS --gtest_shuffle
    DISCOVERY_MODE PRE_TEST)`. Pass `Q3_TEST_BUILD_DIR="${CMAKE_BINARY_DIR}"` and
    `Q3_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"` as compile definitions.
  - **Tests:** this step is the infrastructure. The gate is that every existing test still passes
    after the move.
  - **Verify:** `make test` runs both binaries and reports the same pass count as before
    plus zero failures; `ctest -N` lists tests from both.
- [ ] **C1.2 Shrink `test_platform_stubs.cpp`.** Because the real `Sys_ListFiles`, `Sys_LoadDll`,
  `Sys_Mkdir`, `Sys_Milliseconds`, and `Sys_SendPacket` now link, remove their stubs. Keep:
  `Sys_Error` (throws a C++ exception `q3::test::SysErrorException{message}` so tests can
  assert on `Com_Error(ERR_FATAL)`; declare it `[[noreturn]]`), `Sys_Print` (appends to a
  global `std::string` the fixture can read and clear), `Sys_Quit`, `Sys_GetEvent`,
  `Sys_ShowConsole`, `Sys_Init`, `Sys_SnapVector`, `Sys_IsDedicatedBuild` (returns `qtrue`),
  `Sys_ReleaseDisplay`, `Sys_Sleep` if not linked, `Sys_SendKeyEvents`, and the `IN_*` stubs.
  Remove the hand-written externs at `tests/test_cvar_cmd.cpp:7-8`; `qcommon.h` declares them
  after checklist 01 A2.3.
  - **Tests:** none, because this is the stub layer.
  - **Verify:** `quake3_tests` links with no duplicate or undefined `Sys_*` symbols.
- [ ] **C1.3 Add `tests/engine_fixture.hpp`.** Provide:
  - `q3::test::EngineFixture`, a `::testing::Test` subclass whose `SetUpTestSuite` calls, once
    per process, `Com_InitSmallZoneMemory(); Cvar_Init(); Cmd_Init(); Com_InitZoneMemory();
    Com_InitHunkMemory();` and whose `SetUp` clears the captured `Sys_Print` buffer.
  - `q3::test::TempDir`, an RAII directory under
    `std::filesystem::temp_directory_path() / ("q3t-" + pid + "-" + counter)` removed in the
    destructor, with `path()` and `write(relative, bytes)` helpers.
  - `q3::test::FsFixture : EngineFixture` whose `SetUp` creates a `TempDir base`, sets
    `fs_basepath` and `fs_homepath` to it with `Cvar_Set`, and whose `TearDown` calls
    `FS_Shutdown(qtrue)`.
  - **Tests:** none, because this is the fixture. It is exercised by every C2 to C6 test.
  - **Verify:** one trivial test that instantiates `FsFixture` passes and leaves no directory behind
    (`ls /tmp | grep q3t-` is empty after the run).
- [ ] **C1.4 Add `tests/fixtures/`.** Directory for small committed inputs: `sounds/sine440.wav`
  (generated by a script in `tests/fixtures/gen.py`, 0.1 s, 22050 Hz, 16 bit; commit the
  output, keep it under 10 KB), `demos/` stays empty because `four.dm_68` is retail content,
  and `maps/` stays empty until C4's one-brush BSP exists. Add a `README.md` that says what
  each file is and how it was made.
  - **Tests:** none.
  - **Verify:** `git ls-files tests/fixtures | wc -l` is greater than `0`, and no file exceeds
    100 KB.

### Phase C2: `files.c` and `unzip.c`

- [ ] **C2.1 Add `tests/zip_writer.hpp`.** A header-only stored-entry ZIP writer (about 70
  lines): `struct Entry { std::string name; std::string data; }`,
  `std::string write_zip(const std::vector<Entry>&)`. For each entry write a local file header
  (signature `0x04034b50`, version `20`, flags `0`, method `0` stored, CRC-32, sizes, name),
  then the data. Then a central directory entry per file (signature `0x02014b50`, local header
  offset), then the end-of-central-directory record (signature `0x06054b50`, entry count,
  directory size and offset). Use a static 256-entry CRC-32 table computed at first use.
  Deterministic output, no external tool.
  - **Tests:** `tests/test_zip_writer.cpp` (binary `quake3_tests`) case
    `ZipWriter.OutputOpensWithUnzip` writes a two-entry archive to a `TempDir` and reads both
    entries back through `unzOpen`, `unzLocateFile`, and `unzReadCurrentFile`.
  - **Verify:** `ctest --preset dev -R ZipWriter` passes.
- [ ] **C2.2 Write `tests/test_files.cpp`.** Uses `FsFixture`. In `SetUp` write
  `base/baseq3/pak0.pk3` with entries `default.cfg`, `maps/a.bsp`, `scripts/x.shader`, and
  `base/baseq3/loose.cfg`, then call `FS_InitFilesystem()`. Cases:
  - `Files.ReadFromPakReturnsContent` compares `FS_ReadFile("scripts/x.shader")` bytes.
  - `Files.LooseFileIsFoundWhenNotInPak` reads `loose.cfg`.
  - `Files.PakShadowsLooseFileOfSameName` writes a loose `default.cfg` and asserts the pak
    version wins under the default search order.
  - `Files.MissingFileReturnsMinusOne` for `FS_FOpenFileRead` on an absent name.
  - `Files.ReadFreeLeavesLoadStackAtZero` (checklist 02 B6).
  - `Files.WriteFileLandsOnlyInHomePath` (checklist 02 B6).
  - `Files.ListFilesFiltersByExtension` for `FS_ListFiles("scripts", ".shader", &n)`.
  - `Files.PureServerRefusesLooseFile` calls `FS_PureServerSetLoadedPaks` with the pak checksum
    and asserts `FS_FOpenFileRead("loose.cfg")` fails.
  - `Files.BuildOSPathUsesPlatformSeparator` checks `FS_BuildOSPath` output.
  - `Files.RestartKeepsChecksums` compares `FS_LoadedPakChecksums()` before and after
    `FS_Restart(checksumFeed)`.
  - `Files.MissingDefaultCfgNamesPak0AndPaths` (checklist 02 B8) against an empty `TempDir`,
    catching `SysErrorException`.
  - `Files.PakOrderIsDeterministic` writes `pak0.pk3`, `pak1.pk3`, `zzz.pk3`, and `aaa.pk3`
    and asserts `FS_LoadedPakNames()` order matches the sorted order `files.c` documents.
    Checklist 05 T2a reuses this case for parallel indexing.
  - **Tests:** this step is the content.
  - **Verify:** `ctest --preset dev -R Files` and `ctest --preset asan -R Files` pass.

### Phase C3: `net_chan.c`

- [ ] **C3.1 Write `tests/test_netchan.cpp`.** Uses `EngineFixture`. Replace `Sys_SendPacket` in
  this binary with a stub that records `(length, data, to)` into a vector the test owns (guard
  with a `#define` in `test_platform_stubs.cpp` or a weak symbol so the real `sys_net.cpp` one
  is not linked twice; if linking both is impossible, compile `sys_net.cpp` out of this binary
  and add the loopback cases to checklist 06's `test_netchan_loopback.cpp` instead). Cases:
  - `Netchan.SmallMessageRoundTrips`: `Netchan_Init(qport)`, `Netchan_Setup(NS_CLIENT, &a,
    adr, qport)`, `Netchan_Setup(NS_SERVER, &b, adr, qport)`, transmit 100 bytes from `a`, feed
    the captured packet to `Netchan_Process(&b, ...)`, compare payload.
  - `Netchan.LargeMessageFragmentsAndReassembles`: transmit `3 * FRAGMENT_SIZE` bytes, assert
    more than one packet was captured, feed all in order, compare payload, assert
    `incomingSequence` incremented by one.
  - `Netchan.DuplicateSequenceIsRejected`: feed the same packet twice and assert the second
    `Netchan_Process` returns `qfalse`.
  - `Netchan.OutOfOrderIsDroppedAndCounted`: feed packet 3 before 2 and check the `showdrop`
    counter path (enable `showdrop` and read the captured `Sys_Print`).
  - **Tests:** this step is the content.
  - **Verify:** `ctest --preset dev -R Netchan` passes.

### Phase C4: collision model

- [ ] **C4.1 Write `tests/test_collision.cpp`.** Uses `EngineFixture`. Cases:
  - `Collision.EmptyMapBuildsBoxHull`: `CM_LoadMap("", qfalse, &checksum)` (the empty-name path
    at `code/qcommon/cm_load.c:596`) succeeds.
  - `Collision.BoxTraceFromOutsideHits`: `CM_TempBoxModel(mins, maxs, qfalse)` then
    `CM_BoxTrace` from outside toward the box; assert `fraction < 1` and `plane.normal` is
    axis aligned.
  - `Collision.TraceStartingInsideIsStartSolid`.
  - `Collision.PointContentsInsideBox` returns `CONTENTS_SOLID` inside and `0` outside.
  - `Collision.TransformedTraceRespectsRotation` with `CM_TransformedBoxTrace` and a 90-degree
    yaw.
  - Stretch, behind a `GTEST_SKIP` until the fixture exists: generate a one-brush BSP with the
    ZIP writer fixture and exercise `CM_LoadMap("maps/box.bsp")`, `CM_InlineModel`, and
    `CM_LeafCluster`.
  - **Tests:** this step is the content.
  - **Verify:** `ctest --preset dev -R Collision` passes.

### Phase C5: sound

- [ ] **C5.1 Write `tests/test_sound.cpp`.** Compiles `snd_adpcm.c` and `snd_wavelet.c` into
  `quake3_tests`; if `snd_mem.c` is needed for `SND_malloc`, compile it too with stubs for its
  `S_*` dependencies. Cases:
  - `Sound.AdpcmRoundTripStaysWithinRmsBound`: encode and decode a 440 Hz sine (generated in
    the test, 0.1 s at 22050 Hz) with `S_AdpcmEncode` and `S_AdpcmDecode`; assert root mean
    square error below a bound you calibrate once (record the value in the test comment).
  - `Sound.WaveletRoundTripStaysWithinRmsBound` through `encodeWavelet` and the decoder using
    an `sfx_t` chunk chain.
  - `Sound.GetWavinfoParsesGeneratedRiff` builds a 44-byte RIFF header in memory and asserts
    rate, width, and sample count.
  - `Sound.GetWavinfoRejectsTruncatedHeader`.
  - **Tests:** this step is the content.
  - **Verify:** `ctest --preset dev -R Sound` passes.

### Phase C6: VM syscall bridge

- [ ] **C6.1 Build the test module.** Add `tests/vm_testmodule/tm_main.c` as
  `add_library(testmodule SHARED)` with `PREFIX ""`, `OUTPUT_NAME "testmodule${Q3_ARCH}"`,
  `SUFFIX "${Q3_DLL_EXT}"`, `LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests/baseq3`, and
  `RUNTIME_OUTPUT_DIRECTORY` the same. It exports `dllEntry` and `vmMain` with the signatures
  from checklist 02 B1. Command `0` returns `arg0 + arg1`. Command `1` calls
  `syscall(1, (intptr_t)ptr_from_arg0_arg1)` and returns the result. Make `quake3_tests` depend
  on `testmodule`.
- [ ] **C6.2 Write `tests/test_vm.cpp`.** Uses `FsFixture` with `fs_basepath` set to
  `Q3_TEST_BUILD_DIR "/tests"` and `fs_game` to `baseq3`. Cases are listed in checklist 02 B1:
  `VmAbi.HeapPointerSurvivesRoundTrip`, `VmAbi.AddCommandReturnsSum`,
  `VmAbi.MissingModuleReturnsNull`. Add `VmAbi.SyscallReceivesSixteenIntptrArgs`, which passes
  16 distinct values through `VM_Call` to a recording `TestSyscalls(intptr_t *args)` and asserts
  each slot.
  - **Tests:** this step is the content.
  - **Verify:** `ctest --preset dev -R VmAbi` passes in the container and on the macOS CI leg,
    where heap addresses exceed 32 bits.

### Phase C7: replace tests that cannot fail

- [ ] **C7.1 Delete `tests/test_legacy_vm_syscalls.cpp`.** C6 supersedes it. Remove it from the
  source list.
- [ ] **C7.2 Mark the two stub tests.** `tests/test_vulkan_backend.cpp` is rewritten by checklist
  `09-vulkan.md` and `tests/test_discord_rpc.cpp` by checklist `06-networking.md` N2. Add a
  one-line `// TODO(docs/plans/09-vulkan.md): replace with a skipped-without-ICD test.` and
  `// TODO(docs/plans/06-networking.md): replace with the fake IPC server test.` comment at the
  top of each, and move both into `q3sys_tests`. Do not spend time improving them here.
- [ ] **C7.3 Rewrite `tests/test_modern_logger.cpp`** per checklist 02 B4.2 (capturing sink,
  level filter, basename, cross-thread queue, `LOG_ERROR` under `NDEBUG`). If checklist 02 has
  already done it, skip.
- [ ] **C7.4 Rewrite `tests/test_modern_fs.cpp`** per checklist 02 B6.2 (local instance,
  containment negatives). If checklist 02 has already done it, skip.
- [ ] **C7.5 Extend `tests/test_http_downloader.cpp`** only if checklist 06 N1.4 has not replaced
  the downloader yet: add an in-process server thread that binds `127.0.0.1:0`, accepts one
  connection, and replies `HTTP/1.0 200` with a small body; assert bytes, monotonic progress,
  and `Completed`; add a connection-refused case that asserts `Failed` with a non-empty error;
  add a `cancel()` mid-transfer case that returns within 2 s. Remove the mock at `:5-11`.
  - **Tests (for C7):** the rewritten files are the content.
  - **Verify:** `ctest --preset dev` lists no test named `LegacyVmSyscalls`; `grep -rn TODO
    tests/test_vulkan_backend.cpp tests/test_discord_rpc.cpp` shows the two pointers.

### Phase C8: sanitizers in CI

- [ ] **C8.1 Wire the sanitizer environment.** In `tests/CMakeLists.txt`, when `Q3_SANITIZE` is
  set, add to both test targets
  `set_tests_properties(... PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=1:strict_string_checks=1:abort_on_error=1;UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1")`
  through the `gtest_discover_tests` `PROPERTIES` argument. Leak detection is unavailable on
  macOS arm64; set `detect_leaks=0` on Apple. Add `-fno-sanitize=alignment` for the engine
  binary because the network code misaligns by design.
- [ ] **C8.2 Add the CI leg.** The Linux `asan` preset from checklist 01 A7 runs both binaries
  with `--gtest_shuffle --gtest_repeat=3`. Checklist 05 T6 adds the `tsan` leg.
  - **Tests:** none, because this is CI wiring.
  - **Verify:** `ctest --preset asan` is green in the container and on the Linux CI leg.

## Already seeded

`tests/engine_init.hpp` exists as of 3 September 2026 (added by checklist 02 step B1.8). It
provides an idempotent `EnsureEngineInitialised()` that calls `Com_InitSmallZoneMemory`,
`Cmd_Init`, `Cvar_Init`, and `Com_InitZoneMemory` once per process, and
`tests/test_cvar_cmd.cpp` and `tests/test_vm.cpp` both use it. Step C1 should absorb it into the
fuller `tests/engine_fixture.hpp` rather than adding a second initialisation path, and keep the
idempotence, because the suite runs with `--gtest_shuffle`.

`tests/test_vm.cpp` and `tests/vm_testmodule/tm_main.c` also exist already, so step C6 has only
to confirm the harness and extend the cases, not create them.

## Test map

Every test file in the repo after this checklist, with the binary and the checklist that owns
its content.

| Test file | Binary | Owner |
|---|---|---|
| `tests/engine_fixture.hpp`, `tests/zip_writer.hpp`, `tests/test_platform_stubs.cpp`, `tests/fixtures/` | both | 03 C1, C2 |
| `tests/test_zip_writer.cpp` | `quake3_tests` | 03 C2 |
| `tests/test_files.cpp` | `quake3_tests` | 03 C2, 02 B6, 02 B8, 05 T2a |
| `tests/test_netchan.cpp` | `quake3_tests` | 03 C3 |
| `tests/test_collision.cpp` | `quake3_tests` | 03 C4 |
| `tests/test_sound.cpp` | `quake3_tests` | 03 C5 |
| `tests/test_vm.cpp`, `tests/vm_testmodule/tm_main.c` | `quake3_tests` | 03 C6, 02 B1 |
| `tests/test_math.cpp`, `test_strings.cpp`, `test_msg.cpp`, `test_huffman.cpp`, `test_md4.cpp`, `test_cvar_cmd.cpp` | `quake3_tests` | existing, 01 A5.2 adds one case |
| `tests/test_module_symbols.cpp`, `test_sys_paths.cpp`, `test_sys_net.cpp`, `test_sys_time.cpp`, `test_sys_api_boundary.cpp`, `test_server_challenge.cpp` | `quake3_tests` | 01, 02 |
| `tests/test_modern_logger.cpp`, `test_modern_fs.cpp`, `test_modern_cvar.cpp`, `test_modern_math.cpp` | `q3sys_tests` | 02 B4, 02 B6, existing |
| `tests/test_modern_net.cpp`, `test_netchan_loopback.cpp`, `test_http_downloader.cpp`, `test_download_policy.cpp`, `test_discord_rpc.cpp`, `test_json_writer.cpp`, `test_modern_multiplayer.cpp` | `q3sys_tests` (loopback test in `quake3_tests`) | 06 |
| `tests/test_modern_scripting.cpp` | `q3sys_tests` | 07 |
| `tests/test_threading_queue.cpp`, `test_job_system.cpp`, `test_render_thread.cpp` | `q3sys_tests` | 05 |
| `tests/test_mode_table.cpp`, `test_fov.cpp`, `test_adjust640.cpp`, `test_glsl_headers.cpp` | `quake3_tests` | 08 |
| `tests/test_vulkan_backend.cpp` (rewrite) | `q3sys_tests` | 09 |
| `tests/test_jpeg_parity.cpp`, `test_error_unwind.cpp`, and phase-2 files `test_cvar.cpp`, `test_cmd.cpp`, `test_memory.cpp`, `test_snapshot.cpp`, `test_shader_parser.cpp` | `quake3_tests` | 04 |
| `tests/check_cvar_docs.cmake` | ctest script | 10 |
| Deleted: `tests/test_legacy_vm_syscalls.cpp` | | 03 C7 |

## Out of scope

- Integration checks that render, run demos, or run bot matches. They live under `ci/` as
  scripts (checklist 00) and are referenced by other checklists.
- The content of tests owned by other checklists. This file only reserves their place in the
  map.

## Follow-ons

- Code coverage (`--coverage` plus `gcovr`) as an optional CI report once the suite is stable.
- A fuzz target for `MSG_Read*` and `Info_ValueForKey` with libFuzzer under the `asan` preset.

## Done criteria

- `make test` runs `q3sys_tests` and `quake3_tests`; `ctest -N` lists both.
- `ctest --preset dev` and `ctest --preset asan` are green with `--gtest_shuffle` and
  `--gtest_repeat=3`.
- `test_legacy_vm_syscalls.cpp` is gone; `test_files.cpp`, `test_netchan.cpp`,
  `test_collision.cpp`, `test_sound.cpp`, and `test_vm.cpp` exist with the cases listed here.
- `tests/test_platform_stubs.cpp` contains no stub for a function that the real platform
  sources provide.
- No test mutates a process-wide singleton without owning it.
- Every row of the test map that this checklist owns exists and passes.

## Last step

- [ ] Delete this file and remove its row from `docs/plans/README.md`.
