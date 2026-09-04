# Checklist 03: tests

## Purpose

Turn the test suite from 49 mostly shallow cases into infrastructure that every other
checklist builds on: two test binaries with a clear link contract, shared fixtures, coverage
of the subsystems that have none today (`files.c`, `net_chan.c`, `cm_*`, `snd_*`, the VM
bridge), marking of the tests that cannot fail (see C7.2: only `test_legacy_vm_syscalls.cpp` was actually removed), and sanitizer runs in continuous
integration (CI).

**Status:** In progress, reopened on 4 September 2026. Phases C1 through C8 were ticked on
4 September 2026 and the audit that day found three ticks wrong: C6.2 is missing the 16-argument
syscall case that guards the 64-bit application binary interface, C7.5 was never written at all
(and is now reassigned to checklist 06 step N1.4, which deletes the code it would cover), and
C8.2 is missing `--gtest_repeat=3`. C7.6 is added for a no-assertion case that phase B5 left
behind. C1.1, C1.2, C1.4, C3.1, C7.2, and C8.1 stay ticked with deviations recorded in place.
The suite is 127 cases, 56 in `q3sys_tests` and 71 in `quake3_tests`, green on Linux, the
sanitizer leg, and macOS. Do not delete this file until C6.2, C7.6, and C8.2 close.

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

- [x] **C1.1 Split the test binaries.** Done on 4 September 2026, with the link contract
  diluted; recorded 4 September 2026. Both targets exist and each case gets `TIMEOUT 60`
  (`tests/CMakeLists.txt:140-148`). But `q3sys_tests` is not "pure C++ tests for `code/sys`":
  beyond the specified dependencies it also links `q3server` and `botlib` and compiles
  `test_platform_stubs.cpp`, `TEST_PLATFORM_SOURCES`, and
  `code/renderer/vulkan/vk_backend.cpp` (`:32-33,47,65-76`), so the two binaries have
  near-identical dependency sets. The split delivered process separation, which is what the
  timeouts and the random schedule need; it did not deliver the link discipline that was the
  other half of the point. Also, `quake3_tests` does not link the `code/null/*` stubs this step
  names: the `CL_*` and `GLimp_*` stubs in `tests/test_platform_stubs.cpp:109-132` cover it
  instead. Both are defensible choices, and neither is what the step says. In `tests/CMakeLists.txt` define two executables:
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
- [x] **C1.2 Shrink `test_platform_stubs.cpp`.** Done on 4 September 2026, with two misses
  recorded 4 September 2026. **`Sys_Milliseconds` was not removed** (`:18-22`), though this step
  names it, so `SysTime.MillisecondsNeverDecreases` and `SysTime.SleepAdvancesClock` test
  `std::chrono` rather than the engine's SDL clock; see checklist 02 step B7.3 for the fix, which
  is to extract the clock from `sys_main.cpp` into its own translation unit. And `Sys_Error`
  (`:39`) is not declared `[[noreturn]]` as required, so control flow after a `Com_Error` in a
  test differs from the engine's. Because the real `Sys_ListFiles`, `Sys_LoadDll`,
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
- [x] **C1.3 Add `tests/engine_fixture.hpp`.** Done on 4 September 2026. Provide:
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
- [x] **C1.4 Add `tests/fixtures/`.** Done on 4 September 2026, and currently unused; recorded
  4 September 2026. The directory, its `README.md`, `gen.py`, and `sounds/sine440.wav` are
  tracked and the `Q3_TEST_FIXTURES` definition is set on both targets, but **no test reads
  either**: `test_sound.cpp` generates its sine in memory per C5.1. Keep it for checklist 05 step
  T2a.2 and checklist 06, or delete it if nothing claims it. Directory for small committed inputs: `sounds/sine440.wav`
  (generated by a script in `tests/fixtures/gen.py`, 0.1 s, 22050 Hz, 16 bit; commit the
  output, keep it under 10 KB), `demos/` stays empty because `four.dm_68` is retail content,
  and `maps/` stays empty until C4's one-brush BSP exists. Add a `README.md` that says what
  each file is and how it was made.
  - **Tests:** none.
  - **Verify:** `git ls-files tests/fixtures | wc -l` is greater than `0`, and no file exceeds
    100 KB.

### Phase C2: `files.c` and `unzip.c`

- [x] **C2.1 Add `tests/zip_writer.hpp`.** Done on 4 September 2026. A header-only stored-entry ZIP writer (about 70
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
- [x] **C2.2 Write `tests/test_files.cpp`.** Done on 4 September 2026. Uses `FsFixture`. In `SetUp` write
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
    and asserts `FS_FOpenFileRead("loose.txt")` fails.
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

- [x] **C3.1 Write `tests/test_netchan.cpp`.** Done on 4 September 2026, by a third route that
  this step did not offer; recorded so it is a decision and not an accident. Rather than a weak
  symbol or a link-time swap, a **production test seam** was added:
  `Sys_SetSendPacketOverride` (`code/sys/net/sys_net.cpp:134`), declared in
  `code/qcommon/qcommon.h:1011` and installed at `tests/test_netchan.cpp:37`. It is cleaner than
  a weak symbol and it works on MSVC, at the cost of a test-only function pointer in the
  shipping network layer. Accepted. If checklist 06 N3.2 introduces a transport seam, fold this
  into it. Uses `EngineFixture`. Replace `Sys_SendPacket` in
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

- [x] **C4.1 Write `tests/test_collision.cpp`.** Done on 4 September 2026. Uses `EngineFixture`. Cases:
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

- [x] **C5.1 Write `tests/test_sound.cpp`.** Done on 4 September 2026. Compiles `snd_adpcm.c` and `snd_wavelet.c` into
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

- [x] **C6.1 Build the test module.** Done on 3 September 2026 (during checklist 02 B1). Add `tests/vm_testmodule/tm_main.c` as
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
  **Unticked on 4 September 2026: the one new case this step asked for does not exist.** The
  three cases from checklist 02 B1 are there, plus `VmAbi.MissingModuleReturnsNull` and a real
  `VmAbi.HighAddressSurvivesRoundTrip`. But there is no `SyscallReceivesSixteenIntptrArgs`:
  `tests/test_vm.cpp:33-43` `TestSyscalls` reads `args[0]` and `args[1]` only, so **nothing
  verifies the 16-slot widening at `code/qcommon/vm_interpreted.cpp:520-528` or the
  twelve-argument entry-point call at `code/qcommon/vm.cpp:702-704`**, which is the guard on the
  whole 64-bit application binary interface phase. Also, the file uses a hand-rolled
  `VmAbiFixture` (`tests/test_vm.cpp:45`) instead of `FsFixture`, so `Cvar_Set("fs_basepath",
  ...)` at `:52` is never undone and `FS_Shutdown` never runs. That is benign while
  `gtest_discover_tests` gives each case its own process, and fragile if that ever changes.
  Nothing yet drives the interpreted path, so `VM_CallInterpreted` stays uncovered.
  - **Tests:** this step is the content.
  - **Verify:** `ctest --preset dev -R VmAbi` passes in the container and on the macOS CI leg,
    where heap addresses exceed 32 bits.

### Phase C7: replace tests that cannot fail

- [x] **C7.1 Delete `tests/test_legacy_vm_syscalls.cpp`.** Done on 4 September 2026. C6 supersedes it. Remove it from the
  source list.
- [x] **C7.2 Mark the two stub tests.** Done on 4 September 2026, correctly as written; the
  wider claim around it is what was wrong. Both TODO markers are present. **Both tests still
  assert nothing that can fail:** `tests/test_vulkan_backend.cpp:14` asserts
  `api_version == 130` against the literal hardcoded in `code/renderer/vulkan/vk_backend.cpp`,
  so it passes on a machine with no Vulkan at all, and `tests/test_discord_rpc.cpp:9-25` writes
  a struct through a setter and reads it back. That is by design here, since this step says "do
  not spend time improving them"; the Purpose line of this checklist has been corrected to stop
  claiming the tautologies were replaced. `tests/test_vulkan_backend.cpp` is rewritten by checklist
  `09-vulkan.md` and `tests/test_discord_rpc.cpp` by checklist `06-networking.md` N2. Add a
  one-line `// TODO(docs/plans/09-vulkan.md): replace with a skipped-without-ICD test.` and
  `// TODO(docs/plans/06-networking.md): replace with the fake IPC server test.` comment at the
  top of each, and move both into `q3sys_tests`. Do not spend time improving them here.
- [x] **C7.3 Rewrite `tests/test_modern_logger.cpp`** Done on 3 September 2026. per checklist 02 B4.2 (capturing sink,
  level filter, basename, cross-thread queue, `LOG_ERROR` under `NDEBUG`). If checklist 02 has
  already done it, skip.
- [x] **C7.4 Rewrite `tests/test_modern_fs.cpp`** Done on 3 September 2026. per checklist 02 B6.2 (local instance,
  containment negatives). If checklist 02 has already done it, skip.
- [ ] **C7.5 Extend `tests/test_http_downloader.cpp`** only if checklist 06 N1.4 has not replaced
  the downloader yet: add an in-process server thread that binds `127.0.0.1:0`, accepts one
  connection, and replies `HTTP/1.0 200` with a small body; assert bytes, monotonic progress,
  and `Completed`; add a connection-refused case that asserts `Failed` with a non-empty error;
  add a `cancel()` mid-transfer case that returns within 2 s. Remove the mock at `:5-11`.

  **Unticked on 4 September 2026, and deliberately deferred.** The tick was wrong: the step's
  own condition holds, because `06-networking.md:175` step N1.4 is unchecked, so all of this
  work was required and none of it exists. `tests/test_http_downloader.cpp` is still 13 lines
  with one case, `ParseAndDownloadMock`, asserting that a freshly constructed object is `Idle`
  and its error string empty, which is exactly the kind of test phase C7 exists to remove. There
  is no `tests/test_http_server.hpp`.

  **The decision is not to write it here.** Checklist 06 step N1.4 rewrites the downloader on
  `curl_multi` and deletes the socket code these tests would cover, so an in-process HTTP server
  written now is thrown away with it. This step is therefore reassigned: **06 N1.4 owns the
  downloader tests**, and its Tests line carries them. Note while it waits that the current
  downloader speaks cleartext to port 443 for an `https://` URL and calls `std::stoi`
  (`code/sys/net/http_downloader.cpp:87`) and `std::stoull` (`:171`) on server-controlled input
  on a thread with no `catch`, which is `std::terminate`. Nothing in the shipping client reaches
  it, only tests, so the risk is latent rather than live.
  - **Tests (for C7):** the rewritten files are the content.
  - **Verify:** `ctest --preset dev` lists no test named `LegacyVmSyscalls`; `grep -rn TODO
    tests/test_vulkan_backend.cpp tests/test_discord_rpc.cpp` shows the two pointers.

- [ ] **C7.6 Remove the vacuous case phase B5 added (added 4 September 2026).**
  `SysApiBoundary.InitWithoutLuaStillReturns` (`tests/test_sys_api_boundary.cpp:24-28`) calls
  `Sys_SubsystemInit()` and returns with **no assertion at all**. Checklist 02 step B5 says the
  case "is skipped unless a build flag disables Lua", and no such flag exists, so it is an
  unconditional no-op test written by the phase that was meant to remove no-op tests. Either
  gate it on a real flag and assert that start-up survives a missing interpreter, or delete it.
  Note also that the sibling case at `:17` is the only caller of `Sys_SubsystemShutdown` in the
  tree, which is what hid checklist 02 step B5.2.
  **Tests:** this step is the content.
  **Verify:** every case in `tests/test_sys_api_boundary.cpp` has at least one assertion.

### Phase C8: sanitizers in CI

- [x] **C8.1 Wire the sanitizer environment.** Done on 4 September 2026. In `tests/CMakeLists.txt`, when `Q3_SANITIZE` is
  set, add to both test targets
  `set_tests_properties(... PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=1:strict_string_checks=1:abort_on_error=1;UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1")`
  through the `gtest_discover_tests` `PROPERTIES` argument. Leak detection is unavailable on
  macOS arm64; set `detect_leaks=0` on Apple. Add `-fno-sanitize=alignment` for the engine
  binary because the network code misaligns by design.

  **Conflict recorded 4 September 2026.** This step sets `detect_leaks=1` off Apple, and
  `.github/workflows/ci.yml:171-173` passes `-e ASAN_OPTIONS=detect_leaks=0` with a comment
  explaining that the zone and hunk allocators deliberately never free at exit, so leak
  detection reports every engine allocation. A CTest `ENVIRONMENT` property sets the variable in
  the child process, so **this file wins and the workflow's documented rationale is dead**. The
  comment is the correct judgement. `00-environment.md` step 10 resolves it by setting
  `detect_leaks=0` here and deleting the `-e`, so there is one source of truth.
- [ ] **C8.2 Add the CI leg.** The Linux `asan` preset from checklist 01 A7 runs both binaries
  with `--gtest_shuffle --gtest_repeat=3`. Checklist 05 T6 adds the `tsan` leg.

  **Unticked on 4 September 2026.** The leg itself exists and is green
  (`.github/workflows/ci.yml:126-185`, `-DQ3_SANITIZE=address,undefined`), but
  **`--gtest_repeat=3` is nowhere in the repository.** `ctest --schedule-random` is not a
  substitute: it randomises the order in which cases run as separate processes, whereas
  repeating a case is what shakes out the order and timing dependence the new threading tests
  introduce. `--gtest_shuffle` is passed (`tests/CMakeLists.txt:147-148`) but does nothing,
  because `gtest_discover_tests` gives each case its own process. Closed by
  `00-environment.md` step 10, which also drops the dead `--gtest_shuffle` and resolves the
  `ASAN_OPTIONS` conflict below.
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
