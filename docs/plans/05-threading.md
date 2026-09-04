# Checklist 05: threading model

## Purpose

Give the engine an explicit threading model: the main thread owns all engine state, a
main-thread queue carries hand-offs from workers, debug builds assert thread affinity, a job
system runs background work, and a dedicated render backend thread executes the renderer command
lists. The render thread revives the id `r_smp` design correctly and maps onto the Vulkan backend
planned in checklist 09.

**Status:** In progress. Phases T1, T2a.1, and T2a.2 complete on 4 September 2026, with
deviations recorded in place on T1.3, T1.6, and T2a.1 by the audit of 4 September 2026: the
`q3ded` worker clamp from decision T-a is missing, `JobState::exception` is unsynchronised, the
lossy lane is mutex-guarded rather than lock-free, and `docs/threading.md` has no job-system
section. **Step T6.1 was meant to open with T1 and has not: there is no ThreadSanitizer leg, so
every "TSan build clean" verify line in this checklist is unverified.** Next: T6.1 (tracked as
`00-environment.md` step 9), then T2a.3 (asynchronous screenshot encode).

## Prerequisites

- Checklist 01 is complete: the platform layer lives under `code/sys/`, `code/unix` is gone, and
  the CMake OBJECT libraries exist.
- Checklist 02 step B4 (logger rewrite) is complete or lands together with T1. T1 provides the
  queue that the logger's console sink uses off-main.
- Checklist 04 C-P1 PRs 1 to 4 are complete, so qcommon, server, renderer, and client are C++
  before this checklist edits them.
- Checklist 08 R1 (extension detection and the SDL-based GL loader) is complete before T3.
- Checklist 06 N1 replaces the HTTP worker thread with main-thread `curl_multi` polling. Until
  it lands, T1 routes the worker's cvar updates through the queue.

### Owner decisions already taken

| # | Decision | Default the plan uses |
|---|---|---|
| T-a | Job thread count | Cvar `com_jobThreads` (`CVAR_ARCHIVE \| CVAR_LATCH`). `0` means auto: `clamp(hardware_concurrency() - 2, 1, 8)` on the client (reserve main and render), `clamp(hardware_concurrency() - 1, 1, 4)` on `q3ded`. The pool starts in `Sys_SubsystemInit` with the auto value because `q3config.cfg` is not executed yet; a non-zero latched value is applied at the end of `Com_Init` through `JobSystem::resize()`. |
| T-b | Queue policy | Two lanes in one `MainThreadQueue`. `post()` is the reliable lane (mutex plus `std::vector` swap, unbounded, high-water warning at 4096). `post_lossy()` is a fixed 1024-slot multi-producer single-consumer ring for log lines and progress mirrors; on full it increments an atomic drop counter and the drain prints one coalesced `[threading] N messages dropped` line. No lane blocks the poster, and no lane blocks the main thread beyond `drain(max_ms)`. |
| T-c | `r_smp` default | Land T3 with default `"0"` (the effective behaviour today). Flip to `"1"` on two or more cores in a separate commit once the T3 gates are green in Docker. Remove the `defined(SMP)` guard and replace `Sys_ProcessorCount` with `std::thread::hardware_concurrency()` through `q3::threading`. |

## Background

Verified against commit `ad3705e`. Re-verify anchors before you edit; checklists 01 and 04 move
files (`code/unix/unix_main.c` becomes `code/sys/sys_main.cpp`, `.c` becomes `.cpp`).

| Finding | Anchor |
|---|---|
| Single loop `while (1) Com_Frame()`. `Sys_SubsystemFrame(msec)` is already called right after the first `Com_EventLoop` in `Com_Frame`. | `code/unix/unix_main.c:1275-1281`, `code/qcommon/common.c:2683-2685` |
| `Sys_SubsystemInit` runs after `Cvar_Init` and before `FS_InitFilesystem`, so a job pool started there is available for pk3 indexing. Cvars exist, but `q3config.cfg` has not run. | `code/qcommon/common.c:2354-2392` |
| The id SMP scaffolding is intact but stubbed: `GLimp_SpawnRenderThread` returns `qfalse`, and `GLimp_FrontEndSleep`, `GLimp_WakeRenderer`, `GLimp_RendererSleep` are empty. | `code/sys/sys_sdl.cpp:248-254`, `code/sys/sys_sdl.hpp:21-24` |
| `r_smp` default is effectively `"0"`: the `Sys_ProcessorCount() > 1` branch sits behind `defined(SMP)`, which CMake never defines. `Sys_ProcessorCount` exists only under `__linux__`. | `code/renderer/tr_init.c:900-904`, `code/unix/unix_shared.c:428-434` |
| `R_InitCommandBuffers()` (the thread spawn) is called from `InitOpenGL`, before `R_InitImages`, `R_InitShaders`, and `R_InitFreeType` upload textures on the main thread. | `code/renderer/tr_init.c:229`, `tr_init.c:1089-1099` |
| The backend calls back into the client: `ri.CIN_RunCinematic` and `ri.CIN_UploadCinematic` from `R_BindAnimatedImage`. Under a real render thread this runs RoQ decode, `FS_Read`, `S_RawSamples`, `S_Update`, and `Com_DPrintf` on the render thread. | `code/renderer/tr_shade.c:220-223`, `code/client/cl_cin.c:1077-1200` |
| The backend uses the hunk temp stack, which is not thread-safe: `RB_TakeScreenshot*` and the `RB_SwapBuffers` overdraw readback. | `code/renderer/tr_init.c:383`, `:419`, `code/renderer/tr_backend.c:1042-1050` |
| The backend calls `ri.Error(ERR_DROP)` at 13 sites (a `longjmp`, illegal off the main thread), `ri.Printf` at 8 sites, and `ri.Milliseconds` at 9 sites. `ri.Milliseconds` is `CL_ScaledMilliseconds`, which reads `com_timescale->value` and writes the global `curtime`. | grep `ri\.Error\|ri\.Printf\|ri\.Milliseconds` in `tr_backend.c`, `tr_shade.c`, `tr_shade_calc.c`, `tr_surface.c`, `tr_sky.c`, `tr_shadows.c`, `tr_flares.c`; `code/unix/unix_shared.c:61-77`; `code/client/cl_main.c:2203` |
| The backend reads 29 distinct cvars directly: `r_clear`, `r_showtris`, `r_shownormals`, `r_fastsky`, `r_finish`, `r_measureOverdraw`, `r_drawBuffer`, `r_logFile`, `r_flares`, `r_flareSize`, `r_flareFade`, `r_shadows`, `r_speeds`, `r_lightmap`, `r_offsetFactor`, `r_offsetUnits`, `r_showsky`, `r_znear`, `r_debugSurface`, `r_debugSort`, `r_nobind`, `r_singleShader`, `r_primitives`, `r_dynamiclight`, `r_dlightBacks`, `r_ignoreGLErrors`, `r_smp`, `r_showSmp`, `r_skipBackEnd`. | grep `r_[a-zA-Z]*->` in the backend files |
| The backend reads `tr.frameCount` (mutated by `RE_BeginFrame` on main) and `tr.scratchImage[]->width/height` (mutated by `RE_UploadCinematic`). | `code/renderer/tr_flares.c`, `code/renderer/tr_backend.c:788-812` |
| Front-end GL calls outside the backend files: `RE_BeginFrame` overdraw, texture mode, gamma, and `glGetError`; `R_DebugPolygon` and `R_DebugGraphics`; `GL_TextureMode`; `Upload32` and `R_CreateImage`; `R_CreateFogImage`; `R_DeleteTextures`; `InitOpenGL`; `GL_CheckErrors`; `GL_SetDefaultState`; `RB_TakeScreenshot*`; `R_LevelShot`; `R_Init` `glGetError`; `RE_StretchRaw`; `RE_UploadCinematic`. | `code/renderer/tr_cmds.c:335-380`, `tr_main.c:1390-1437`, `tr_image.c:103-140`, `:502-716`, `:727-790`, `:2042`, `:2224-2245`, `tr_init.c:192-236`, `:243-280`, `:375-450`, `:532-590`, `:712-751`, `:1102`, `tr_backend.c:725-812` |
| `SurfIsOffscreen` uses the backend `tess` from the front end. id's FIXME returns `qfalse` under SMP, so portal culling differs between `r_smp 0` and `r_smp 1`. | `code/renderer/tr_main.c:816` |
| Sound: `AudioCallback` reads `dma.buffer` and the non-atomic `s_dmaPos` with no synchronisation. `SNDDMA_BeginPainting` and `SNDDMA_Submit` are empty. `dma.samples = 512*8*2 = 8192` is a power of two, which the `& ((dma.samples>>1)-1)` masks in `S_TransferStereo16` require. `s_dmaPos` is in mono-sample units, matching `dma.samples`, so `S_GetSoundtime`'s `samplepos/dma.channels` is consistent. | `code/sys/sys_sdl.cpp:273-359`, `code/client/snd_mix.c:113-140`, `code/client/snd_dma.c:1189-1230` |
| The HTTP worker mutates cvars from its thread (`Cvar_SetValue` in the progress lambda), and `LOG_*` from workers reaches `CL_ConsolePrint` through the logger sink. | `code/sys/sys_api.cpp:12-16`, `:119-129`, `code/sys/net/http_downloader.cpp:37` |
| Shutdown: `Com_Shutdown` only closes two files; `Sys_Quit` calls `CL_Shutdown` then `Sys_Exit` (`_exit`); `Sys_Error` also calls `CL_Shutdown`. Signals use `signal()` and call `GLimp_Shutdown` from the handler. | `code/qcommon/common.c` `Com_Shutdown`, `code/unix/unix_main.c:321-343`, `:393-416`, `code/unix/linux_signals.c:34-61` |
| `MAX_RENDER_COMMANDS = 0x40000` (256 KB) is too small to copy cinematic frames into the command stream. Pass pointers, not copies. | `code/renderer/tr_local.h:1485` |
| Tests link `q3sys q3renderer q3server qcommon botlib` with platform stubs in `tests/test_platform_stubs.cpp`. | `tests/CMakeLists.txt`, `tests/test_platform_stubs.cpp` |

## Steps

### T1 Ownership rules, main-thread queue, logger hook (3 to 4 days)

New files: `code/sys/threading/thread_affinity.hpp`, `thread_affinity.cpp`,
`main_thread_queue.hpp`, `main_thread_queue.cpp`, `threading_api.h` (C shim), `docs/threading.md`,
`tests/test_threading_queue.cpp`. Modified: `code/sys/sys_api.cpp`, `code/sys/sys_api.h`,
`code/sys/logger/logger.hpp`, `code/qcommon/cvar.cpp`, `cmd.cpp`, `common.cpp`, `files.cpp`,
`code/client/cl_console.cpp`, `code/server/sv_main.cpp`, `code/client/cl_main.cpp`,
`CMakeLists.txt`, `tests/CMakeLists.txt`.

- [x] **T1.1 Thread identity.** Done on 3 September 2026. `q3::threading::mark_main_thread()` captures `std::thread::id`
  and is the first call in `Sys_SubsystemInit`. Provide `main_thread_id()`, `is_main_thread()`,
  and `set_current_thread_name(const char*)` (`pthread_setname_np` with the Linux and macOS
  variants, `SetThreadDescription` on Windows). Store the name in a `thread_local const char*`
  for the crash handler (T5).
  **Tests:** `tests/test_threading_queue.cpp` (q3sys_tests) case `Affinity.MainThreadIsMarked`:
  `mark_main_thread()` then `is_main_thread()` is true on the caller and false inside a
  `std::thread`.
  **Verify:** `ctest -R Affinity`.

- [x] **T1.2 `Q3_ASSERT_MAIN_THREAD()`.** Done on 3 September 2026. Macro in `threading_api.h`, C-callable. Compiled when
  `!defined(NDEBUG) || defined(Q3_SANITIZE)`. Expands to `Sys_AssertMainThread(__FILE__,
  __LINE__)`, which prints the thread name and a backtrace hint with `write(2)` and calls
  `abort()`. Never call `Com_Error` from it (that is a `longjmp`). Place one assert at the entry
  of: `Cvar_Set2` (`cvar.c:287`), `Cvar_Get` (`cvar.c:188`), `Cmd_ExecuteString` (`cmd.c:621`),
  `Cbuf_AddText` (`cmd.c:88`), `Cbuf_Execute` (`cmd.c:168`), `Com_Printf` (`common.c:144`),
  `CL_ConsolePrint` (`cl_console.c:370`), `Z_Malloc` (`common.c:1001`) and `Z_Free`, `Hunk_Alloc`
  (`common.c:1663`), `Hunk_AllocateTempMemory` (`common.c:1736`), `FS_FOpenFileWrite`
  (`files.c:850`), `FS_FOpenFileRead` (`files.c:989`), `FS_Write` (`files.c:1308`), `FS_ReadFile`
  (`files.c:1500`), `FS_WriteFile` (`files.c:1642`), `SV_Frame` (`sv_main.c:751`), `CL_Frame`
  (`cl_main.c:2025`), `Sys_QueEvent` (`unix_main.c:1041`, now in `code/sys/sys_main.cpp`).
  **Tests:** `tests/test_threading_queue.cpp` case `Affinity.AssertFiresOffMain` with
  `EXPECT_DEATH` calling `Sys_AssertMainThread` from a `std::thread`.
  **Verify:** a debug run `make smoke` (which runs `+map` and `+quit`) fires no assert,
  because every existing path is on main.

- [x] **T1.3 `MainThreadQueue`.** Done on 3 September 2026. Singleton in `main_thread_queue.hpp/.cpp`.
  `post(std::function<void()>)` is the reliable lane. `post_lossy(FixedTask)` takes a 64-byte
  plain-old-data struct (function pointer plus 48 bytes of inline arguments) so the logger path
  never allocates. `drain(std::chrono::milliseconds budget)` runs from `Sys_SubsystemFrame`
  before `update_timers`. `drain_all()` runs at shutdown. C shim in `threading_api.h`:
  `Sys_PostToMainThread(void (*fn)(void*), void* ctx)` and `Sys_MainThreadQueueDrain(int max_ms)`.
  Lane semantics follow decision T-b.
  **Tests:** `tests/test_threading_queue.cpp` cases: `Queue.EightProducersTenThousandEach`
  (post from 8 threads, 10k items each, drain on the test thread, count is 80k and each
  producer's items arrive in FIFO order); `Queue.LossyDropsAbove1024` (post 2048 lossy items
  without draining, drop counter is 1024, one coalesced message on drain);
  `Queue.BudgetedDrainReturnsEarly` (post 10k slow items, `drain(1ms)` returns with items
  remaining); `Queue.CShimRoundTrip` (`Sys_PostToMainThread` runs the callback with its context
  on drain).
  **Verify:** `ctest -R Queue`; TSan build of `q3sys_tests` clean.

  **Deviation from decision T-b, recorded 4 September 2026.** The lossy lane is a
  mutex-guarded ring (`main_thread_queue.cpp:31`, `lossy_mutex_`), not the lock-free
  multi-producer single-consumer ring the decision specifies. It still never waits for space,
  which is the property the logger needs, so nothing here is broken; a producer can contend with
  the drain, which the decision was written to avoid. Reconcile the decision and the code, and
  fix `docs/threading.md`, which repeats the lock-free claim: see step T1.6. The verify line's
  ThreadSanitizer build has never run, because there is no such leg; see T6.1.

- [x] **T1.4 Logger hook.** Done on 3 September 2026. In `Logger::log`, keep the stdout and stderr writes under a small
  mutex to avoid interleaving, call the console sink directly when `is_main_thread()`, otherwise
  `post_lossy` the formatted line so `ConsolePrintSink` runs on main. This is the hook that
  checklist 02 B4's logger rewrite plugs into.
  **Tests:** `tests/test_modern_logger.cpp` (q3sys_tests) case `Logger.OffMainLineIsQueued`:
  install a capturing sink, log from a `std::thread`, assert the sink was not called until
  `drain()`.
  **Verify:** `ctest -R Logger`.

- [x] **T1.5 Interim HTTP worker fix.** Done on 3 September 2026. In `Sys_StartHttpDownload` (`sys_api.cpp:119-129`) the
  progress lambda posts the two `Cvar_SetValue` calls through `Sys_PostToMainThread`. Checklist
  06 N1 removes the worker entirely.
  **Tests:** none, because checklist 06 replaces the code; the TSan build is the check.
  **Verify:** TSan build has no report from `http_downloader.cpp`.

- [x] **T1.6 `docs/threading.md`.** Done on 3 September 2026. State the ownership rule: the main thread owns cvars, cmds,
  the console, zone and hunk memory, file handles, VMs, client and server state, and every
  `refimport_t` callback. List what is legal off main: pure functions on caller-owned buffers,
  `q_shared` string and math helpers, `Com_BlockChecksum` and MD4, the JPEG codec with its own
  memory manager, unzip on a privately opened `unzFile`, OS-level `fopen`/`read` on paths handed
  over by main, `LOG_*`, `Sys_PostToMainThread`, `std::` containers owned by the job. List what is
  forbidden: anything in the T1.2 assert list, `va()`, `FS_BuildOSPath` (static buffers),
  `Com_Error`, `ri.*`, `qgl*`, SDL video and audio calls. Document the two queue lanes, the job
  system (T2a), and the render thread contract (T3) as they land.
  **Tests:** none, because documentation.
  **Verify:** the file exists and checklist 10's link check passes.

  **Two corrections owed, recorded 4 September 2026.** The file has no job-system section
  although T2a.1 and T2a.2 have landed, and this step says to document the job system "as they
  land". And it describes the lossy lane as a lock-free multi-producer single-consumer ring,
  which overstates the code: `code/sys/threading/main_thread_queue.cpp:31` guards the ring with
  `lossy_mutex_`. It never waits for space, which is the property the design needs, but it is
  not lock-free, so a logging worker can contend with the drain. Either implement decision T-b
  as written or describe what the code does; describing it is the smaller honest change, and it
  matters because the logger's off-main path is the main producer.

- [x] **T1.7 Sanitizer option (with T6).** Done on 3 September 2026. Add `Q3_SANITIZE` to `CMakeLists.txt` if checklist 01
  has not: values `thread`, `address`, `undefined`; adds `-fsanitize=<value> -fno-omit-frame-pointer
  -g` to all targets and defines `Q3_SANITIZE` so the affinity asserts compile in sanitizer
  builds. Add the `asan` and `tsan` presets to `CMakePresets.json`.
  **Tests:** none, because build configuration.
  **Verify:** `make configure -DQ3_SANITIZE=thread && make build &&
  make test` is clean.

### T2a Job system and self-contained consumers (1.5 to 2 weeks)

New files: `code/sys/threading/job_system.hpp`, `job_system.cpp`, `tests/test_job_system.cpp`,
`tests/fixtures/paks/` (four tiny pk3 files generated by `tests/zip_writer.hpp` from checklist
03). Modified: `code/qcommon/files.cpp` (`FS_LoadZipFile`, `FS_AddGameDirectory`),
`code/renderer/tr_init.cpp` (screenshot commands), `code/renderer/tr_image.cpp` (`SaveJPG` split),
`code/renderer/tr_local.h` (`screenshotCommand_t`), `code/client/snd_mem.cpp` (optional),
`code/sys/sys_api.cpp` (`Sys_Job*` shims), `code/qcommon/common.cpp` (`com_jobThreads`).

- [x] **T2a.1 `JobSystem`.** Done on 4 September 2026. N `std::thread` workers named `q3-job-N`. One shared `std::deque`
  per priority (`High`, `Normal`, `Background`) under a mutex and condition variable. Work
  stealing is unnecessary at this granularity; keep the interface so it can be swapped.
  `Job { std::function<void()> body; std::function<void()> on_main_complete;
  std::shared_ptr<CancelToken> cancel; }`. `JobHandle { wait(); is_done(); cancel(); }`.
  Exceptions inside `body` are caught, stored as `std::exception_ptr` in the handle, and reported
  through the completion; they never propagate out of a worker and never call `Com_Error`.
  `parallel_for(begin, end, grain, fn)` returns a handle and is implemented as chunked jobs plus a
  counting latch; it is reserved for later front-end work. Completions always go through
  `MainThreadQueue::post`, so `wait()` on the main thread spins on `is_done()` while calling
  `drain(1ms)` to avoid deadlock. C shim: `Sys_JobSubmit(void (*fn)(void*), void* ctx,
  void (*done)(void*), int priority)` returning an `int` handle, `Sys_JobWait(int)`,
  `Sys_JobCancel(int)`. Cvar `com_jobThreads` per decision T-a; `JobSystem::resize()` at the end
  of `Com_Init`.
  **Tests:** `tests/test_job_system.cpp` (q3sys_tests) cases: `Jobs.CompleteExactlyOnce` (N
  jobs, counter is N); `Jobs.CompletionRunsOnDrainingThread` (record `std::this_thread::get_id()`
  in the completion, equals the draining thread); `Jobs.CancelBeforeStartSkipsBody`;
  `Jobs.CancelMidRunObservedViaToken`; `Jobs.ExceptionCapturedWorkerSurvives` (a throwing body is
  reported through the handle and completion, and a subsequent job still runs);
  `Jobs.ParallelForSumsOneMillion` (deterministic sum); `Jobs.WaitOnMainDoesNotDeadlock`
  (completion queued while `wait()` is called on the draining thread); `Jobs.ShutdownJoinsWithin2s`
  (pending jobs, `shutdown()` returns within 2 s).
  **Verify:** `ctest -R Jobs`; TSan build clean.

  **Two deviations found by the audit on 4 September 2026. Both are small and both need fixing;
  the step stays ticked because everything else it names is present and tested.**

  1. **The `q3ded` worker clamp is missing.** `auto_worker_count()`
     (`code/sys/threading/job_system.cpp:36-44`) implements only the client half of decision
     T-a, `clamp(hardware_concurrency() - 2, 1, 8)`. The dedicated-server half,
     `clamp(hardware_concurrency() - 1, 1, 4)`, is absent, and there is no `DEDICATED` compile
     definition to branch on because checklist 01 made it a run-time property. Branch on
     `com_dedicated`. Until then `com_jobThreads 0` on `q3ded` reserves a render thread that
     does not exist.
  2. **`JobState::exception` is a data race.** The worker writes it at `job_system.cpp:211`
     outside `state->mutex`, while `JobHandle::has_exception()` and `get_exception()`
     (`code/sys/threading/job_system.hpp:66-72`) read it with no synchronisation. Write and read
     it under the mutex. This is exactly what the step's own "TSan build clean" verify line
     would have caught, and that line has never run, because there is no ThreadSanitizer leg:
     see step T6.1 and `00-environment.md` step 9.

  A third defect, found on 4 September 2026 when the macOS leg failed
  `JobsFixture.WaitOnMainDoesNotDeadlock` under `--schedule-random`, and **fixed the same day**:
  `JobHandle::wait()` on the main thread could return before the completion it was waiting for
  had run. The worker posts the completion and only then sets `done`, so the last loop iteration
  observed `done`, exited, and left the completion in the queue. It passed on Linux and failed
  intermittently on macOS, which is what an order-randomised suite is for. `wait()` now drains
  once more after the loop; the release store makes the post visible, so one unbudgeted drain is
  enough. The redundant `is_done()` early return went with it, because a job that is already
  done can still have a queued completion.

  A fourth, smaller point, recorded rather than fixed: the completion always runs, even when the
  job was cancelled (`job_system.cpp:207-217` skips `body` but still posts
  `on_main_complete`), so the C shim's `done` callback fires for a cancelled job and
  `Sys_JobCancel` leaves its entry in `s_c_handles`. Decide whether a cancelled job should
  report completion before checklist 06 relies on the shim.
  **Tests (for the two fixes):** `Jobs.DedicatedAutoCountReservesOneCore` and a case that reads
  `has_exception()` from another thread while a throwing job completes, which must be clean
  under ThreadSanitizer.

- [x] **T2a.2 Parallel pk3 indexing.** Done on 4 September 2026. Split `FS_LoadZipFile` into a pure
  `FS_ScanZipFile(const char* ospath, zipIndex_t* out)` that opens its own `unzFile`, walks the
  central directory, collects `{name, pos, crc, uncompressed_size}` and the total name length, and
  keeps the `unzFile` open in `out`; and a main-only `FS_BuildPackFromIndex(zipIndex_t*,
  const char* basename)` that does the `Z_Malloc` calls, the hash table, `fs_packFiles +=`,
  `Com_BlockChecksum` of the keys, and adopts the `unzFile`. `FS_AddGameDirectory`
  (`files.c:2467`) keeps its sorted loop but dispatches `FS_ScanZipFile` jobs for all `sorted[i]`
  first, then walks `i` in the original order calling `wait()` and `FS_BuildPackFromIndex`. The
  `fs_searchpaths` order is load-bearing for pure checksums and `FS_ReorderPurePaks` and must not
  change. Copy path strings into the job before dispatch, because `FS_BuildOSPath` uses static
  buffers.
  **Tests:** `tests/test_files.cpp` (quake3_tests) case `Files.PakOrderAndChecksumsStable`:
  `FS_Startup` against `tests/fixtures/paks/` with four tiny pk3 files, with `com_jobThreads 1`
  and with `com_jobThreads 4`; `fs_debug 1` `path` output and `FS_LoadedPakChecksums()` are
  identical.
  **Verify:** connect to a `sv_pure 1` server in the container without an "Unpure client" kick;
  `Com_Printf("FS_Startup: %i msec")` around `FS_Startup` shows the change with `com_jobThreads
  1` versus auto on `map q3dm17`.

- [ ] **T2a.3 Asynchronous screenshot encode.** `RB_TakeScreenshotCmd` only does `qglReadPixels`
  into a `malloc` buffer (replace `ri.Hunk_AllocateTempMemory` at `tr_init.c:383`, `:419`),
  snapshots `s_gammatable` and `tr.overbrightBits` into the job, then submits a job that does the
  BGR swap, gamma correction, and TGA header or JPEG encode into memory. Refactor `SaveJPG`
  (`tr_image.c:1700`) into `R_EncodeJPGToMemory(...)` returning a buffer; the existing `jpegDest`
  already writes to memory (checklist 04 P1.9 replaces the codec but keeps this shape). The
  completion on main calls `ri.FS_WriteFile` and prints `Wrote %s`. `screenshotCommand_t` gets an
  inline `char fileName[MAX_OSPATH]`, which also fixes the two-screenshots-per-frame static in
  `R_TakeScreenshot` (`tr_init.c:456`). `R_LevelShot` follows the same split.
  **Tests:** none, because the encode needs pixels from a GL context; the byte-compare below is
  the gate.
  **Verify:** `screenshot` and `screenshotJPEG` outputs byte-compare (`cmp`, and ImageMagick
  `compare -metric AE` for JPEG) against a `com_jobThreads 1` synchronous run.

- [ ] **T2a.4 Sound decode (gated, measure first).** Behind a new cvar `s_asyncLoad` (default
  `0`). `S_LoadSound` (`snd_mem.c:329`) keeps `FS_ReadFile` on main, submits `GetWavinfo` plus
  `ResampleSfxRaw` (flat `short*` output, `snd_mem.c:290`) or ADPCM encode to a job, and the
  completion copies into `sndBuffer` chunks through `SND_malloc` on main. Defer
  `sfx->soundLength` and `soundData` until completion. Enable by default only after the
  `CL_InitCGame` timing line shows a win.
  **Tests:** `tests/test_sound.cpp` (checklist 03) case `Sound.AsyncLoadMatchesSync`: decode a
  fixture WAV both ways and compare the PCM buffers.
  **Verify:** `CL_InitCGame: %5.2f seconds` (`cl_cgame.c:755`) with `s_asyncLoad 0` versus `1`.

- [ ] **T2a.5 Hand-offs for other workers.** The download (curl) and Discord designs in
  checklist 06 deliver completions and progress mirrors through `MainThreadQueue`. Confirm
  nothing in those workers calls an asserted function.
  **Tests:** covered by checklist 06.
  **Verify:** TSan build clean with a download and Discord enabled.

### T3 Render backend thread (3 to 4 weeks)

New files: `code/sys/threading/render_thread.hpp`, `render_thread.cpp`,
`code/renderer/tr_backend_cvars.h`, `tests/test_render_thread.cpp`, `ci/smp_pixel_gate.sh`.
Modified: `code/sys/sys_sdl.cpp` and `.hpp` (replace the `GLimp_*` SMP stubs with
`GLimp_StartRenderThread`, `GLimp_StopRenderThread`, `GLimp_ReleaseContext`,
`GLimp_AcquireContext`, `GLimp_SetSwapInterval`), `code/renderer/tr_cmds.cpp`, `tr_backend.cpp`,
`tr_local.h`, `tr_init.cpp`, `tr_image.cpp`, `tr_shade.cpp`, `tr_main.cpp`, `tr_scene.cpp`,
`tr_flares.cpp`, `tr_bsp.cpp`, `tr_shader.cpp`, `tr_model.cpp`, `tr_font.cpp`,
`code/client/cl_cin.cpp`, `cl_main.cpp`.

- [ ] **T3.1 `RenderThread` mailbox.** The class owns a `std::thread`, a mutex, two condition
  variables, `const void* pending` (one slot), `bool busy`, `std::function<void()> sync_call`,
  and a sticky `std::string error`. API: `start(on_thread_begin, on_thread_end)` where the hooks
  run `SDL_GL_MakeCurrent(window, ctx)` and `SDL_GL_MakeCurrent(window, nullptr)`;
  `submit(const void* cmds)` waits until `!busy && !pending`, sets `pending`, and notifies;
  `wait_idle()`; `call_sync(fn)` (`wait_idle` then run `fn` on the render thread and wait);
  `stop()` (`wait_idle`, post quit, `join`); `take_error()`. This keeps id's one-frame-in-flight
  semantics (`backEndData[2]` is enough) and removes id's per-call `MakeCurrent` ping-pong
  (`linux_glimp.c:1638-1690`). The context lives on the render thread for the thread's lifetime;
  main holds it only when the thread is stopped.
  **Tests:** `tests/test_render_thread.cpp` (q3sys_tests) with a fake executor instead of GL:
  `RenderThread.SubmitBlocksWhileBusy`, `RenderThread.WaitIdleReturnsAfterExecute`,
  `RenderThread.CallSyncRunsOnThread` (record thread id), `RenderThread.TakeErrorAfterSimulatedError`
  (executor sets the sticky error), `RenderThread.StopJoins`.
  **Verify:** `ctest -R RenderThread`; TSan clean.

- [ ] **T3.2 Lifetime.** Start the thread at the end of `R_Init`, after builtin images, shaders,
  and fonts are uploaded on main (today `R_InitCommandBuffers` runs from `InitOpenGL`,
  `tr_init.c:229`). `RE_Shutdown` already calls `R_SyncRenderThread()` then
  `R_ShutdownCommandBuffers()`; make `R_ShutdownCommandBuffers` call `stop()` and re-acquire the
  context on main so `R_DeleteTextures` and `GLimp_Shutdown` stay unchanged. `vid_restart`
  destroys the window (`re.Shutdown(qtrue)`), so the thread is recreated on each restart. With
  `r_smp 0` the thread is not started and `R_IssueRenderCommands` executes inline as today. Every
  new mechanism below has an `if (!glConfig.smpActive) { run inline; }` branch so both paths
  execute identical GL sequences.
  **Tests:** none, because it needs a GL context; the `vid_restart` loop is the gate.
  **Verify:** `vid_restart` × 20 through a cfg loop; thread count before and after (`Sys_Print`
  of `ls /proc/self/task | wc -l` in `GLimp_StopRenderThread`) is stable; no deadlock.

- [ ] **T3.3 Move every front-end GL call.** Mechanisms: (a) a new render command, (b)
  `R_BackendCall(void (*fn)(void*), void*)` = `call_sync` when active, direct otherwise, (c)
  init or shutdown only, when the thread is not running.

  | Site | Mechanism |
  |---|---|
  | `RE_BeginFrame` overdraw stencil setup and `qglDisable(GL_STENCIL_TEST)` (`tr_cmds.c:335-352`) | (a) `RC_SET_OVERDRAW_MEASURE {bool on}` |
  | `RE_BeginFrame` `GL_TextureMode` on `r_textureMode->modified` (`tr_cmds.c:357-361`) | (a) `RC_TEXTURE_MODE {char mode[32]}`; the backend loops `tr.images`, which is read-only during a frame |
  | `RE_BeginFrame` gamma `R_SetColorMappings` (`tr_cmds.c:366-371`) | Stays on main. It rebuilds tables and calls `GLimp_SetGamma`, an SDL window call that must run on main. The tables are consumed by `Upload32`, which now runs through (b) |
  | `RE_BeginFrame` `qglGetError` (`tr_cmds.c:374-381`) | (a) `RC_CHECK_ERRORS`; the backend stores the error into the frame snapshot; main raises `ri.Error(ERR_FATAL)` from `R_IssueRenderCommands` if set |
  | `r_swapInterval` (today only in `GLimp_Init`) | (a) `RC_SET_SWAP_INTERVAL` when `r_swapInterval->modified` (checklist 08 R1.3 adds `GLimp_SetSwapInterval`) |
  | `RE_StretchRaw` (`tr_backend.c:725-780`) | (a) `RC_STRETCH_RAW {x, y, w, h, cols, rows, const byte* data, client, dirty}`. The data pointer is safe because RoQ alternates halves of `cin.linbuf` and the mailbox guarantees at most one frame in flight; add a comment and a debug assert in `RoQInterrupt` that the previous upload was consumed |
  | `RE_UploadCinematic` (`tr_backend.c:788`) and the `R_BindAnimatedImage` callbacks (`tr_shade.c:220-223`) | Move `ri.CIN_RunCinematic` and `ri.CIN_UploadCinematic` to the front end: in `R_AddDrawSurf` and `RE_StretchPic`, if `shader->hasVideoMap` (new flag set in `tr_shader.cpp` when `videoMap` is parsed) and the handle was not yet run this frame (`tr.videoMapsRunThisFrame[]`), call `ri.CIN_RunCinematic` then emit `RC_UPLOAD_CINEMATIC {handle, buf, cols, rows, dirty}` before the draw command. `R_BindAnimatedImage` in the backend becomes `GL_Bind(tr.scratchImage[handle])`. The `tr.scratchImage[]->width/height` mutation moves inside the command |
  | `R_DebugPolygon` and `R_DebugGraphics` (`tr_main.c:1390-1437`, cheat only) | (b) wrap `ri.CM_DrawDebugSurface(R_DebugPolygon)` in `R_BackendCall`; it already calls `R_SyncRenderThread` |
  | `R_CreateImage`, `Upload32`, `GL_Bind`, `GL_SelectTexture` (`tr_image.c:727-790`) | (b) `R_CreateImage` keeps the hunk allocation and hash insertion on main and wraps the GL block in `R_BackendCall`. All callers are already preceded by `R_SyncRenderThread` (`tr_bsp.c:150`, `tr_shader.c:2443`, `:2574`, `tr_model.c:125`, `:535`, `tr_font.c:352`, `tr_image.c:2411`); keep them. The `if (r_smp->integer)` guards in `tr_shader.c` must read `glConfig.smpActive`. T2b later batches uploads into `RC_UPLOAD_IMAGE` |
  | `GL_TextureMode` from `GL_SetDefaultState` and the console `r_textureMode` | (c) at init; (a) at run time through `RC_TEXTURE_MODE` |
  | `R_CreateFogImage` `qglTexParameterfv` (`tr_image.c:2042`) | (c) runs inside `R_InitImages` before the thread starts; add `Q3_ASSERT(!glConfig.smpActive)` |
  | `R_DeleteTextures` (`tr_image.c:2224`) | (c) after `stop()` in `RE_Shutdown` |
  | `InitOpenGL`, `GL_SetDefaultState`, `GfxInfo_f` GL string reads, `R_Init` `qglGetError` (`tr_init.c:192-236`, `:712-751`, `:1102`) | (c) before thread start. At run time `GfxInfo_f` reads only `glConfig` strings and pointer non-nullness, no GL calls, so it is fine on main |
  | `GL_CheckErrors` (`tr_init.c:243`) | Stays where it runs (`Upload32` through (b), and the backend). Its `ri.Error(ERR_FATAL)` goes through `RB_Error` |
  | `RB_TakeScreenshot*` and `R_LevelShot` (`tr_init.c:375-450`, `:532-590`) | Already commands (`RC_SCREENSHOT`). `R_LevelShot` is called from `R_ScreenShot_f` on main; convert it to a screenshot command with a `levelshot` flag. Hunk temp becomes `malloc` (T2a.3) |
  | `RE_EndRegistration` → `RB_ShowImages` (`tr_init.c:1153`) | (b) `R_BackendCall` |
  | `RB_SwapBuffers` overdraw `ri.Hunk_AllocateTempMemory` (`tr_backend.c:1042`) | Backend-owned `std::vector<uint8_t>` scratch through `RB_Scratch(size)` |

  **Tests:** none, because GL; the pixel gate is the test. Add a static check: a ctest that greps
  `qgl` in the non-backend renderer files and fails on any hit outside the `(c)` functions listed
  above (`ci/check_frontend_gl.sh`).
  **Verify:** `ci/check_frontend_gl.sh` passes; the pixel gate (T3.6) passes.

- [ ] **T3.4 The backend never touches engine state.**
  - `backEndCvars_t` in `tr_backend_cvars.h` with 29 fields (integers, floats, and
    `char drawBuffer[16]`): `clear`, `showtris`, `shownormals`, `fastsky`, `finish`,
    `measureOverdraw`, `drawBuffer`, `logFile`, `flares`, `flareSize`, `flareFade`, `shadows`,
    `speeds`, `lightmap`, `offsetFactor`, `offsetUnits`, `showsky`, `znear`, `debugSurface`,
    `debugSort`, `nobind`, `singleShader`, `primitives`, `dynamiclight`, `dlightBacks`,
    `ignoreGLErrors`, `smp`, `showSmp`, `skipBackEnd`. `R_IssueRenderCommands` fills it into
    `backEndData[tr.smpFrame]->cvars`. `RB_ExecuteRenderCommands` takes `backEndData_t*` and sets
    `backEnd.cvars = &data->cvars`. Mechanically replace `r_xxx->integer` with `backEnd.cvars.xxx`
    in the seven backend files. `r_smp->integer` at `tr_backend.c:1080` becomes a comparison
    against the two `backEndData` pointers.
  - Frame header additions to `backEndData_t`: `int frameCount` (for `tr_flares.cpp`),
    `int frameTimeMsec` (for `backEnd.refdef.time` in `RB_SetGL2D`, replaces `ri.Milliseconds`),
    `backEndCounters_t pcOut` written by the render thread at the end of the list.
    `R_PerformanceCounters` and `RE_EndFrame` read `backEndData[tr.smpFrame ^ 1]->pcOut` after the
    wait, which removes the current read-during-render race on `backEnd.pc.msec`.
  - `ri.Milliseconds` in the backend for `pc.msec`, `RB_ShowImages`, and `RE_StretchRaw` timing
    becomes `RB_Milliseconds()` over `std::chrono::steady_clock` with no globals.
  - `ri.Printf` becomes `RB_Printf(level, fmt, ...)`: format into a fixed 1 KB buffer and
    `Sys_PostToMainThread` (lossy lane) when off main, direct otherwise.
  - `ri.Error` becomes `RB_Error(code, fmt, ...)`. On the render thread it records the message in
    `RenderThread::error`, then `longjmp`s to a `setjmp` at the top of `RB_ExecuteRenderCommands`
    (same thread, legal), which abandons the rest of the list and marks the frame done. Main checks
    `take_error()` right after the wait in `R_IssueRenderCommands` and raises `ri.Error(code, ...)`
    there, so `ERR_DROP` semantics survive and `Com_Error` only ever runs on main. The inline path
    calls `ri.Error` directly. Once checklist 04 P2.0 lands, this becomes a caught exception on
    the render thread instead of `longjmp`.
  - `SurfIsOffscreen` (`tr_main.c:816`): replace the `tess`-based tessellation with a front-end
    bounds test (face `points`, `srfGridMesh_t::meshBounds`, `srfTriangles_t::bounds` projected
    through `R_TransformModelToClip`) so both `r_smp` paths take the same branch. Without this the
    pixel gate fails on portal and mirror maps (q3dm0, q3tourney2). Do this before the gate run.
  - `tr.images[]` growth happens only in `R_CreateImage` on main under `R_SyncRenderThread`; add
    `Q3_ASSERT(render thread idle)` inside `R_CreateImage`.
  - Already safe, no change: `dlightBits[SMP_FRAMES]`, `drawSurfs`, `entities`, `polys`,
    `polyVerts`, and `dlights` are per-`backEndData`; `RE_AddRefEntityToScene` copies the
    `refEntity_t` (`tr_scene.c:219`); `RE_AddPolyToScene` copies the verts (`tr_scene.c:151`);
    `RE_RenderScene` copies `refdef` into `tr.refdef`, which is copied into `drawSurfsCommand_t`.
  **Tests:** none for the cvar snapshot, because it is mechanical; `ci/check_frontend_gl.sh`
  gains a second grep that fails on `r_[a-zA-Z]*->` or `ri\.Error\|ri\.Printf\|ri\.Milliseconds`
  in the backend files.
  **Verify:** the static check passes; the pixel gate passes on q3dm0.

- [ ] **T3.5 Frame pacing and platform notes.** Main runs at most one frame ahead.
  `com_maxfps` pacing stays in `Com_Frame`. VSync blocks only the render thread inside
  `SDL_GL_SwapWindow`, which SDL2 allows off main on all three platforms; the window and the
  event pump stay on main, which macOS requires. `RE_EndFrame` returns the previous frame's
  back-end time; document that in `SCR_UpdateScreen`. Input-to-photon latency grows by up to one
  frame; `r_smp 0` stays the low-latency path. Keep `r_showSmp`. For Vulkan (checklist 09) the
  `RenderThread` only knows `submit`, `call_sync`, `stop`, and the two begin and end hooks; the
  `rb_backend_t` vtable is what `RB_ExecuteRenderCommands` dispatches through, and present happens
  on the render thread in both backends. Nothing in `code/sys/threading` changes for Vulkan.
  **Tests:** none, because documentation and timing.
  **Verify:** `com_speeds 1` shows front-end and back-end columns overlapping with `r_smp 1`
  (frame total is less than the sum) and equal to the sum with `r_smp 0`.

- [ ] **T3.6 Pixel gate and flip the default.** `ci/smp_pixel_gate.sh` runs the smoke demo
  twice in Docker under llvmpipe with `cl_avidemo 10` for deterministic per-frame TGAs:

  ```sh
  xvfb-run -a build/quake3_modern +set r_smp 0 +set com_jobThreads 1 +set cl_avidemo 10 +timedemo 1 +demo four +quit
  mv build/screenshots /tmp/smp0
  xvfb-run -a build/quake3_modern +set r_smp 1 +set com_jobThreads 1 +set cl_avidemo 10 +timedemo 1 +demo four +quit
  for f in /tmp/smp0/*.tga; do compare -metric AE "$f" "build/screenshots/$(basename "$f")" /dev/null || exit 1; done
  ```

  Every frame must compare with `0` differing pixels. Include a portal-map demo. Only after this
  gate, the TSan run, and the `vid_restart` loop are green, flip `r_smp` to `"1"` when
  `hardware_concurrency() >= 2` in a separate commit (decision T-c).
  **Tests:** none, because the script is the test.
  **Verify:** `ci/smp_pixel_gate.sh` exits 0; TSan (`Q3_SANITIZE=thread`) 60 s
  `+set r_smp 1 +timedemo 1 +demo four` prints zero reports with
  `TSAN_OPTIONS=halt_on_error=1`; `screenshot`, `screenshotJPEG`, `levelshot`, RoQ playback
  (`cinematic idlogo.RoQ`), an in-game `videoMap` shader, `map nonexistent`, and `error` all
  behave correctly with `r_smp 1`; `r_smp` toggles both ways through `vid_restart`.

### T2b Level-load image precache pipeline (about 1 week, after T3)

- [ ] **T2b.1 Decode in jobs, upload on the render thread.** `R_FindImageFile`
  (`tr_image.c:1856`) splits into: main reads the bytes with `ri.FS_ReadFile`; a job runs
  `LoadTGA` or `LoadJPG` into a `malloc` RGBA buffer plus the CPU side of `Upload32` (resample,
  `R_LightScaleTexture` with a snapshotted gamma and intensity table, mipmaps); the completion on
  main allocates `image_t` on the hunk and emits `RC_UPLOAD_IMAGE {image_t*, levels...}` executed
  on the render thread. `RE_EndRegistration` calls `R_SyncRenderThread` to flush. Replace
  `ri.Malloc` (`CL_RefMalloc` → `Z_TagMalloc`) inside the loaders with `malloc` on the job path.
  **Tests:** `tests/test_job_system.cpp` case `Jobs.ImageDecodePipelineOrdersCompletions`
  (fake decode jobs complete in submission order on drain); image parity is covered by the gate.
  **Verify:** the pixel gate passes; `imagelist` output is identical to a `com_jobThreads 1`
  run; the `CL_InitCGame` timing line improves on `map q3dm17`.

### T4 Sound handoff (2 to 3 days)

- [ ] **T4.1 Explicit handoff.** Keep the SDL callback and the DMA ring model; the mixer stays
  on main (parallelising it is a follow-on). `s_dmaPos` becomes `std::atomic<uint32_t>` with a
  release store in the callback and an acquire load in `SNDDMA_GetDMAPos`. `SNDDMA_BeginPainting`
  and `SNDDMA_Submit` become `SDL_LockAudioDevice` and `SDL_UnlockAudioDevice` around
  `S_PaintChannels`, so the callback never copies a region that is being written; the lock is
  held only for the paint duration. A lock-free ring is not needed while the ring stays the
  engine's own `dma.buffer`. Document in `SNDDMA_Init` that `dma.samples` must stay a power of two
  (`512*8*channels` today) because `S_TransferStereo16` masks with `(dma.samples>>1)-1`.
  **Tests:** none, because the SDL callback needs a device; the TSan timedemo is the gate.
  **Verify:** TSan timedemo with `s_initsound 1` is clean; `s_show 2` shows `painted` and
  `soundtime` monotonic; count callback zero-fills over five idle minutes at the menu and expect
  zero.

### T5 Shutdown, error, and crash paths (3 to 4 days)

- [ ] **T5.1 Shutdown order.** `Com_Shutdown`: `CL_ShutdownRef` (stops the render thread) →
  `JobSystem::shutdown(cancel_all, 2 s timeout, then detach and log)` →
  `MainThreadQueue::drain_all()` → Discord and curl stop (checklist 06) → the existing file
  closes. `Sys_Exit` calls `SDL_Quit` last.
  **Tests:** `tests/test_job_system.cpp` case `Jobs.ShutdownWithPendingJobsDetachesAfterTimeout`
  (a job that sleeps 5 s; `shutdown` returns within 2.5 s and logs once).
  **Verify:** `quit` from the menu and `kill -TERM` while in game exit cleanly under TSan and
  ASan with no leak report from the threading module.

- [ ] **T5.2 `Sys_Error` from a worker.** If `!is_main_thread()`, post the message on the
  reliable lane and park the worker in a sleep loop; main's drain raises `Sys_Error`. The same rule
  applies to a failed `Sys_AssertMainThread`, which calls `abort()`, not `Sys_Error`.
  **Tests:** `tests/test_job_system.cpp` case `Jobs.SysErrorFromJobIsPostedOnce` (a job calls a
  test double of `Sys_Error`; the main drain receives exactly one message).
  **Verify:** inject a `Sys_Error` from a test job in a debug run; the engine exits once with the
  message.

- [ ] **T5.3 Crash handler on any thread.** The `sigaction` handler from checklist 02 B3 uses
  `SA_SIGINFO | SA_ONSTACK`, prints the signal, `si_addr`, `pthread_self`, and the thread name
  from T1.1's `thread_local` with `write(2)` only, then `_exit(1)`. Drop any `GLimp_Shutdown()`
  call from the handler; it is not async-signal-safe. Windows: `SetUnhandledExceptionFilter` and
  `SetThreadDescription`.
  **Tests:** none, because signal handling; manual crash is the check.
  **Verify:** `crash` while `r_smp 1` and `com_jobThreads 4` prints the crashing thread's name.

### T6 Sanitizer CI (2 to 3 days, opened with T1)

- [ ] **T6.1 TSan leg.** A Docker CI job builds `-DQ3_SANITIZE=thread`, runs `q3sys_tests` and
  `quake3_tests`, then `quake3_modern +set r_smp 1 +set com_jobThreads 4 +timedemo 1 +demo four
  +quit` under `xvfb-run` with `TSAN_OPTIONS=halt_on_error=1 suppressions=ci/tsan.supp`. The
  suppressions file lists Mesa and llvmpipe internals only, by module. macOS arm64 leg: TSan with
  Apple clang. Windows leg: no TSan; run ASan and the pixel gate only.
  **Tests:** none, because CI configuration.
  **Verify:** the CI matrix shows the `tsan` job green.

- [ ] **T6.2 Pixel gate job.** Wire `ci/smp_pixel_gate.sh` as a separate CI job that runs on
  every pull request touching `code/renderer` or `code/sys/threading`.
  **Tests:** none, because CI configuration.
  **Verify:** the job runs and passes on a no-op PR.

## Test map

| Test file | Binary | Cases | Added by |
|---|---|---|---|
| `tests/test_threading_queue.cpp` | q3sys_tests | `Affinity.MainThreadIsMarked`, `Affinity.AssertFiresOffMain`, `Queue.EightProducersTenThousandEach`, `Queue.LossyDropsAbove1024`, `Queue.BudgetedDrainReturnsEarly`, `Queue.CShimRoundTrip` | T1.1, T1.2, T1.3 |
| `tests/test_modern_logger.cpp` | q3sys_tests | `Logger.OffMainLineIsQueued` | T1.4 |
| `tests/test_job_system.cpp` | q3sys_tests | `Jobs.CompleteExactlyOnce`, `Jobs.CompletionRunsOnDrainingThread`, `Jobs.CancelBeforeStartSkipsBody`, `Jobs.CancelMidRunObservedViaToken`, `Jobs.ExceptionCapturedWorkerSurvives`, `Jobs.ParallelForSumsOneMillion`, `Jobs.WaitOnMainDoesNotDeadlock`, `Jobs.ShutdownJoinsWithin2s`, `Jobs.ImageDecodePipelineOrdersCompletions`, `Jobs.ShutdownWithPendingJobsDetachesAfterTimeout`, `Jobs.SysErrorFromJobIsPostedOnce` | T2a.1, T2b.1, T5.1, T5.2 |
| `tests/test_files.cpp` | quake3_tests | `Files.PakOrderAndChecksumsStable` | T2a.2 |
| `tests/test_sound.cpp` | quake3_tests | `Sound.AsyncLoadMatchesSync` | T2a.4 |
| `tests/test_render_thread.cpp` | q3sys_tests | `RenderThread.SubmitBlocksWhileBusy`, `RenderThread.WaitIdleReturnsAfterExecute`, `RenderThread.CallSyncRunsOnThread`, `RenderThread.TakeErrorAfterSimulatedError`, `RenderThread.StopJoins` | T3.1 |
| `ci/check_frontend_gl.sh` | script (ctest) | no `qgl` outside backend files except init-only functions; no `r_*->`, `ri.Error`, `ri.Printf`, `ri.Milliseconds` in backend files | T3.3, T3.4 |
| `ci/smp_pixel_gate.sh` | script (CI job) | `r_smp 0` and `r_smp 1` frames identical, including a portal map | T3.6, T6.2 |
| TSan CI job | CI | tests plus 60 s timedemo with `r_smp 1` and four job threads | T6.1 |

## Out of scope

- Parallelising the renderer front end (`R_AddWorldSurfaces`, entity culling).
- The sound mixer on its own thread.
- RoQ decode one frame ahead.

## Follow-ons

- `parallel_for` over `R_AddWorldSurfaces` and entity culling once the front end is C++ and the
  render thread is stable.
- A mixer thread fed by a single-producer single-consumer ring.
- RoQ decode-ahead with a third `linbuf` and audio chunks kept on main.

## Done criteria

- `docs/threading.md` states the ownership rule and every subsystem follows it; the TSan CI job
  is green on the tests and on the 60 s timedemo with `r_smp 1` and four job threads.
- `ci/smp_pixel_gate.sh` passes, including the portal map.
- `com_speeds` shows front-end and back-end overlap with `r_smp 1`.
- `vid_restart` × 20 leaves the thread count unchanged and never deadlocks.
- `FS_LoadedPakChecksums()` is unchanged with parallel pk3 indexing; a pure-server connect works.
- Screenshots byte-match a `com_jobThreads 1` run.
- Sound is TSan clean and shows no zero-fill underruns over five idle minutes.
- A `Sys_Error` from a job exits once with the message; `quit` and `kill -TERM` are clean under
  TSan and ASan.
- Every row of the test map exists and passes under `ctest --preset dev`, `ctest --preset asan`,
  and `ctest --preset tsan`.

## Last step

- [ ] Delete this file and remove its row from `docs/plans/README.md`.
